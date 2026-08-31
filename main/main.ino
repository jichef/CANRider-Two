#include "config.h"        // primero: define LILYGO_T_A7670 o LILYGO_SIM7000G
#include "AT/utilities.h"  // usa el define de placa de config.h
#include "driver/twai.h"
#include "structs.h"

#if defined(MODEM_SIM7000G)
// El motor AT+SH de esta revisión de firmware SIM7000G es poco fiable
// (HEADERLEN limitado a 350 y AT+SHCONN falla igualmente). Se usa
// TinyGsmClientSecure (AT+CAOPEN/CASEND/CARECV) en su lugar, como hace
// el ejemplo oficial de LilyGo HttpsBuiltlnPostSupabase.ino.
#include <TinyGsmClient.h>
#include "StreamDebugger.h"  // PRUEBA TEMPORAL: ver los AT reales de TinyGsm
StreamDebugger debugger(SerialAT, Serial);
TinyGsm modem(debugger);
#endif

// ── Timing ────────────────────────────────────────────────────────────────────
#define POST_INTERVAL_MS  15000UL
#define RETRY_WAIT_MS     10000UL
#define HTTP_FAIL_MAX     3
// bytes máximos de respuesta GET (cabeceras HTTP + cuerpo JSON juntos en el
// buffer de sim7000Request). Con 16 señales configuradas ya se llega a
// ~4700 bytes (3585 de JSON + ~1100 de cabeceras de Supabase) — por encima
// de los 4096 que había antes, lo que truncaba el JSON y perdía las últimas
// señales de la lista en silencio (incluida clock_minutes → hora mal en la
// pantalla de la moto). Con margen para crecer más señales sin repetir esto.
#define MAX_HTTP_BODY     12288

static TimeRef          lastTime = {};
static SemaphoreHandle_t timeMux = NULL;

static TimeRef snapshotTime() {
    TimeRef t = {};
    if (xSemaphoreTake(timeMux, pdMS_TO_TICKS(5)) == pdTRUE) {
        t = lastTime;
        xSemaphoreGive(timeMux);
    }
    return t;
}
static void storeTime(const TimeRef& t) {
    if (xSemaphoreTake(timeMux, portMAX_DELAY) == pdTRUE) {
        lastTime = t;
        xSemaphoreGive(timeMux);
    }
}

// ── Señales CAN ────────────────────────────────────────────────────────────────
// Definidas en setupCANSignals() más abajo (hardcodeadas, ver el porqué ahí).
static CANSignal         canSignals[MAX_SIGNALS];
static int               canSignalCount = 0;
static SemaphoreHandle_t canMux         = NULL;

// ── Tramas TX dinámicas (agrupadas por frame_id) ──────────────────────────────

static TxFrame txFrames[MAX_TX_FRAMES];
static int     txFrameCount = 0;

// ── Estado ────────────────────────────────────────────────────────────────────
enum State { MODEM_BOOT, NET_SETUP, HTTP_SETUP, RUNNING, ERROR_WAIT };
static State    state     = MODEM_BOOT;
static uint32_t stateAt   = 0;
static uint32_t nextPost  = 0;
static int      httpFails = 0;

// ── Helpers AT ────────────────────────────────────────────────────────────────
bool sendAT(const char* cmd, const char* expect = "OK", uint32_t timeout = 5000) {
    Serial.print(">> "); Serial.println(cmd);
    SerialAT.println(cmd);
    String buf;
    uint32_t t = millis();
    while (millis() - t < timeout) {
        while (SerialAT.available()) {
            char c = SerialAT.read(); Serial.write(c); buf += c;
        }
        if (buf.indexOf(expect)  >= 0) return true;
        if (buf.indexOf("ERROR") >= 0) { Serial.println("[FAIL]"); return false; }
    }
    Serial.println("[TIMEOUT]"); return false;
}

String queryAT(const char* cmd, const char* prefix, uint32_t timeout = 5000) {
    Serial.print(">> "); Serial.println(cmd);
    SerialAT.println(cmd);
    String buf;
    uint32_t t = millis();
    while (millis() - t < timeout) {
        while (SerialAT.available()) {
            char c = SerialAT.read(); Serial.write(c); buf += c;
        }
        int idx = buf.indexOf(prefix);
        if (idx >= 0) {
            int end = buf.indexOf('\n', idx);
            return buf.substring(idx, end >= 0 ? end : buf.length());
        }
    }
    return "";
}

// ── HTTP genérico (usado por el envío de telemetría/viajes) ───────────────────
#if defined(MODEM_SIM7000G)
// AT+SH poco fiable en esta revisión de firmware (HEADERLEN limitado a 350,
// AT+SHCONN falla igualmente). Se usa TinyGsmClientSecure (AT+CAOPEN/CASEND/
// CARECV) en su lugar, como hace el ejemplo oficial de LilyGo
// HttpsBuiltlnPostSupabase.ino.

// Host sin esquema, para pasar a connect(). SUPABASE_URL nunca lleva barra final.
static String sim7000Host() {
    String host = String(SUPABASE_URL);
    if (host.startsWith("https://")) host.remove(0, 8);
    if (host.startsWith("http://"))  host.remove(0, 7);
    return host;
}

// Cloudflare (delante de Supabase) añade una cookie __cf_bm de ~250 bytes
// con Set-Cookie en cada respuesta SIN cookie previa. Con las cabeceras que
// ya de por sí manda Cloudflare, eso deja la respuesta total pegada al
// límite (~1369 bytes) donde el SIM7000G corta la conexión en roaming — con
// una petición de una sola señal se comprobó que sin esa cookie el total
// baja de ~1400 a ~1080 bytes. Reenviándola en peticiones sucesivas,
// Cloudflare no la vuelve a mandar y libera ese margen.
static String g_cfCookie;

static void captureCfCookie(const String& raw) {
    // Cabecera insensible a mayúsculas (Cloudflare la manda en minúsculas,
    // "set-cookie:") — comparar en una copia en minúsculas conserva los
    // índices porque toLowerCase() no cambia la longitud de la cadena.
    String lower = raw; lower.toLowerCase();
    int idx = lower.indexOf("set-cookie: __cf_bm=");
    if (idx < 0) return;
    int start = idx + 12;  // longitud de "set-cookie: "
    int end   = raw.indexOf(';', start);
    if (end < 0) return;
    g_cfCookie = raw.substring(start, end);
}

// Petición HTTPS genérica (GET sin body, o POST con body). Devuelve el código
// HTTP en httpStatus y el cuerpo de la respuesta en respBody.
static bool sim7000Request(const String& method, const String& path, const String& body,
                            int& httpStatus, String& respBody) {
    httpStatus = 0; respBody = "";
    String host = sim7000Host();

    // connect(host,port) sin más usa el timeout por defecto de TinyGsm: 75s
    // (macro TINY_GSM_CLIENT_CONNECT_OVERRIDES). Cuando AT+CAOPEN no da una
    // respuesta clara en roaming, eso deja el intento colgado 75s enteros
    // antes de poder ni siquiera reintentar — medido con timestamps: saltos
    // de ~75.5s entre páginas fallidas. 15s de margen es de sobra para el
    // caso bueno (CAOPEN normalmente responde en 1-2s cuando funciona).
    TinyGsmClientSecure sslClient(modem);
    if (!sslClient.connect(host.c_str(), 443, 15)) {
        Serial.println("[HTTPS] connect() falló");
        return false;
    }

    sslClient.print(method); sslClient.print(" "); sslClient.print(path); sslClient.print(" HTTP/1.1\r\n");
    sslClient.print("Host: "); sslClient.print(host); sslClient.print("\r\n");
    sslClient.print("apikey: "); sslClient.print(SUPABASE_KEY); sslClient.print("\r\n");
    sslClient.print("Authorization: Bearer "); sslClient.print(SUPABASE_KEY); sslClient.print("\r\n");
    if (g_cfCookie.length() > 0) {
        sslClient.print("Cookie: "); sslClient.print(g_cfCookie); sslClient.print("\r\n");
    }
    if (body.length() > 0) {
        sslClient.print("Content-Type: application/json\r\n");
        // Sin "Prefer: return=minimal" a propósito: con cuerpo vacío en la
        // respuesta, la conexión se cierra antes de que dé tiempo a leer
        // nada y el POST se cuenta como fallo aunque se procesara bien.
        // Que devuelva el registro creado da contenido real que detectar.
        sslClient.print("Content-Length: "); sslClient.print(body.length()); sslClient.print("\r\n");
    }
    sslClient.print("Connection: close\r\n\r\n");
    if (body.length() > 0) sslClient.print(body);

    // En roaming, una conexión que se va a cortar sin datos puede quedarse
    // "abierta" muchos segundos sin dar señales de vida antes de que el
    // módem la dé por muerta. Las respuestas que sí llegan lo hacen en
    // pocos segundos, así que un techo corto no penaliza el caso bueno y
    // evita que uno malo bloquee el resto (p.ej. el envío de telemetría).
    String raw; raw.reserve(2048);
    uint32_t rStart = millis(), lastByte = millis();
    while (millis() - rStart < 10000) {
        while (sslClient.available()) {
            // Con la conexión cortada a medias, available() puede seguir
            // devolviendo un conteo obsoleto (>0) mientras read() ya
            // devuelve -1 (AT+CARECV falla porque el socket ya se cerró).
            // Casteábamos ese -1 a char y lo añadíamos como si fuera un
            // byte real, lo que refrescaba lastByte en cada vuelta y dejaba
            // el bucle girando en vacío el timeout entero sin avanzar ni
            // salir por el corte de inactividad.
            int c = sslClient.read();
            if (c < 0) break;
            raw += (char)c;
            lastByte = millis();
            if (raw.length() >= MAX_HTTP_BODY) break;
        }
        if (!sslClient.connected() && !sslClient.available()) break;
        // Sin el "raw.length() > 0" de antes: si la conexión nunca entrega
        // ni un solo byte (available() sigue en true de forma obsoleta,
        // read() siempre falla), lastByte se queda clavado en rStart y este
        // corte actúa igual como un tope de ~4s en vez de agotar los 10s
        // enteros machacando AT+CARECV contra un socket ya muerto.
        if (millis() - lastByte > 4000) break;
        delay(10);
    }
    // sslClient.stop() sin argumento (TinyGsmClientSIM7000SSL.h) equivale a
    // stop(15000): antes de cerrar, vacía el "sock_available" del módem
    // llamando a AT+CARECV en un while sin ningún delay. Si la conexión ya
    // murió con datos fantasma pendientes (nuestro caso: el módem se queda
    // creyendo que hay bytes por leer que en realidad nunca llegarán),
    // sock_available nunca baja a 0 y ese bucle interno de la librería
    // machaca AT+CARECV durante los 15 segundos completos en CADA cierre de
    // conexión — encima de nuestro propio timeout. Con un maxWaitMs bajo
    // seguimos dándole ocasión de vaciar un búfer real (caso bueno), pero
    // sin pagar 15s enteros por cada conexión muerta.
    sslClient.stop(500);
    captureCfCookie(raw);  // las cabeceras llegan enteras aunque el cuerpo se corte

    int httpPos = raw.indexOf("HTTP/");
    if (httpPos >= 0) {
        int sp = raw.indexOf(' ', httpPos + 5);
        if (sp >= 0) httpStatus = raw.substring(sp + 1, sp + 4).toInt();
    }
    int sepIdx = raw.indexOf("\r\n\r\n");
    if (sepIdx >= 0) {
        String headers = raw.substring(0, sepIdx);
        String bodyRaw = raw.substring(sepIdx + 4);
        respBody = (headers.indexOf("chunked") >= 0) ? dechunkBody(bodyRaw) : bodyRaw;
    }
    return httpStatus > 0;
}

// Supabase (vía Cloudflare) siempre responde con Transfer-Encoding: chunked,
// incluso en HTTP/1.1 con Connection: close. Sin decodificar esto, cada
// fragmento deja un prefijo hexadecimal (tamaño+\r\n) incrustado en mitad
// del JSON — normalmente cae en un hueco entre objetos y pasa desapercibido,
// pero si cae dentro de un string o número corrompe el parseo silenciosamente.
static String dechunkBody(const String& raw) {
    String out; out.reserve(raw.length());
    int pos = 0;
    while (pos < (int)raw.length()) {
        int lineEnd = raw.indexOf("\r\n", pos);
        if (lineEnd < 0) break;
        String sizeHex = raw.substring(pos, lineEnd);
        int semi = sizeHex.indexOf(';');
        if (semi >= 0) sizeHex = sizeHex.substring(0, semi);
        sizeHex.trim();
        long chunkSize = strtol(sizeHex.c_str(), nullptr, 16);
        if (chunkSize <= 0) break;  // chunk final (0\r\n\r\n)
        int dataStart = lineEnd + 2;
        int dataEnd   = dataStart + chunkSize;
        if (dataEnd > (int)raw.length()) {
            // Conexión cortada a mitad de un chunk (frecuente en roaming):
            // devolvemos lo recibido hasta ahí, incompleto.
            out += raw.substring(dataStart);
            break;
        }
        out += raw.substring(dataStart, dataEnd);
        pos = dataEnd + 2;  // saltar el \r\n que cierra el chunk
    }
    return out;
}

#endif

// ── Señales CAN ────────────────────────────────────────────────────────────────
// Antes se descargaban de la tabla can_signals en Supabase, para no publicar
// en el repo abierto la trama de Vmoto que cada usuario reverse-engineered
// para su moto. Se pasa a hardcodeada por decisión consciente: en roaming
// inestable la descarga fallaba a menudo (páginas incompletas, arranques con
// la hora sin cargar) y además su tráfico HTTPS interfería con las consultas
// de GPS al competir por el mismo canal AT — se prioriza que reloj y
// baterías funcionen siempre sobre esa privacidad del protocolo. El TX
// sigue limitado a la hora, como siempre.
//
// El frame_id/byte de cada señal vive en config.h (CAN_CLOCK_FRAME,
// CAN_BATTERY_A_FRAME, etc.) para que adaptar esto a otra moto sea
// cuestión de editar ese único archivo. Añadir señales nuevas — sobre
// todo cualquier trama TX — sigue requiriendo tocar este .ino
// directamente: a propósito, para que nunca se pueda ampliar lo que se
// transmite por CAN solo con un archivo de configuración.
static void addCANSignal(uint32_t frameId, char direction, uint8_t byteStart, const char* name,
                          uint8_t bitMask = 0, bool dualMode = false) {
    CANSignal& s = canSignals[canSignalCount++];
    s.frameId      = frameId;
    s.direction    = direction;
    s.txIntervalMs = 200;
    s.dualMode     = dualMode;
    strncpy(s.name, name, sizeof(s.name) - 1);
    s.name[sizeof(s.name) - 1] = '\0';
    s.byteStart = byteStart;
    s.byteLen   = 1;
    s.bitMask   = bitMask;
    s.bigEndian = true;
    s.isSigned  = false;
    s.scale     = 1;
    s.offsetVal = 0;
    s.value     = 0.0f;
    s.updated   = false;
}

static void setupCANSignals() {
    if (xSemaphoreTake(canMux, portMAX_DELAY) == pdTRUE) {
        canSignalCount = 0;

        // Cada señal solo se registra si sus #define existen en config.h.
        // Si dejaste el bloque 5/5 de config.h sin rellenar (recomendado
        // hasta tener los valores reales de tu moto), esa señal se omite
        // por completo — para la hora eso significa que el ESP32 NO
        // transmite ninguna trama por CAN, en vez de mandar un frame_id
        // inventado con datos basura al bus real.
#if defined(CAN_CLOCK_FRAME) && defined(CAN_CLOCK_HOUR_BYTE) && defined(CAN_CLOCK_MIN_BYTE)
        addCANSignal(CAN_CLOCK_FRAME,     't', CAN_CLOCK_HOUR_BYTE, "clock_hours");
        addCANSignal(CAN_CLOCK_FRAME,     't', CAN_CLOCK_MIN_BYTE,  "clock_minutes");
#endif
#if defined(CAN_BATTERY_A_FRAME) && defined(CAN_BATTERY_A_BYTE)
        addCANSignal(CAN_BATTERY_A_FRAME, 'r', CAN_BATTERY_A_BYTE,  "moto_battery");
#endif
#if defined(CAN_BATTERY_B_FRAME) && defined(CAN_BATTERY_B_BYTE)
        addCANSignal(CAN_BATTERY_B_FRAME, 'r', CAN_BATTERY_B_BYTE,  "moto_battery_b");
#endif
#if defined(CAN_CHARGING_FRAME) && defined(CAN_CHARGING_BYTE) && defined(CAN_CHARGING_BITMASK)
        addCANSignal(CAN_CHARGING_FRAME,  'r', CAN_CHARGING_BYTE,  "bms_charging",
                     CAN_CHARGING_BITMASK, true);
#endif

        // Reconstruir lista de tramas TX únicas (agrupadas por frame_id)
        txFrameCount = 0;
        for (int i = 0; i < canSignalCount && txFrameCount < MAX_TX_FRAMES; i++) {
            if (canSignals[i].direction != 't') continue;
            bool found = false;
            for (int f = 0; f < txFrameCount; f++) {
                if (txFrames[f].frameId == canSignals[i].frameId) { found = true; break; }
            }
            if (!found) {
                txFrames[txFrameCount].frameId    = canSignals[i].frameId;
                txFrames[txFrameCount].intervalMs = canSignals[i].txIntervalMs;
                txFrames[txFrameCount].lastSentMs = 0;
                txFrameCount++;
            }
        }
        xSemaphoreGive(canMux);
    }
    Serial.printf("[CAN] %d señales (%d frames TX)\n", canSignalCount, txFrameCount);
#if !(defined(CAN_CLOCK_FRAME) && defined(CAN_CLOCK_HOUR_BYTE) && defined(CAN_CLOCK_MIN_BYTE))
    Serial.println("[CAN] Reloj CAN sin configurar en config.h — no se transmite ninguna trama");
#endif
}

// ── Valor para un byte de una trama TX ───────────────────────────────────────
// Llamada desde canTask con canMux tomado y time snapshot previo.
// Señales de reloj: usan la hora NITZ ajustada con el tiempo transcurrido.
// Cualquier otro nombre: byte fijo = (uint8_t)offsetVal.
static uint8_t txSignalByte(const char* name, float offsetVal, const TimeRef& t, uint32_t now) {
    if (!t.valid) return (uint8_t)offsetVal;
    uint32_t elapsed  = now - t.capturedAt;
    uint32_t totalSec = (uint32_t)t.hour * 3600u + (uint32_t)t.min * 60u
                      + t.sec + elapsed / 1000u;
    // t.hour/min/sec están en UTC (ver readNetworkTime); el panel de la
    // moto espera hora local — se aplica aquí el offset de la red (incluye
    // DST) solo para el byte que sale por CAN, sin tocar el UTC interno
    // que sí usan trips/telemetría hacia Supabase.
    int32_t localSec = (int32_t)(totalSec % 86400u) + t.utcOffsetMin * 60;
    localSec = ((localSec % 86400) + 86400) % 86400;
    if (strcmp(name, "clock_hours")   == 0) return (uint8_t)(localSec / 3600);
    if (strcmp(name, "clock_minutes") == 0) return (uint8_t)((localSec / 60) % 60);
    if (strcmp(name, "clock_seconds") == 0) return (uint8_t)(localSec % 60);
    if (strcmp(name, "clock_day")     == 0) return t.day;
    if (strcmp(name, "clock_month")   == 0) return t.month;
    if (strcmp(name, "clock_year_hi") == 0) return (uint8_t)(t.year >> 8);
    if (strcmp(name, "clock_year_lo") == 0) return (uint8_t)(t.year & 0xFFu);
    return (uint8_t)offsetVal;
}

// ── Decodificación de tramas CAN ──────────────────────────────────────────────
// Llamada desde el task CAN con canMux tomado.
// Soporta dual_mode (modo A = frameId, modo B = frameId+1) y bit_mask.
void parseCANFrame(const twai_message_t& msg) {
    for (int i = 0; i < canSignalCount; i++) {
        CANSignal& s = canSignals[i];
        if (s.direction == 't') continue;  // las TX no se decodifican al recibirlas
        bool match = (s.frameId == msg.identifier) ||
                     (s.dualMode && (s.frameId + 1) == msg.identifier);
        if (!match) continue;
        if (s.byteStart >= msg.data_length_code) continue;

        if (s.bitMask != 0) {
            // Extracción de bit individual: 1.0 si el bit está activo, 0.0 si no
            s.value   = (msg.data[s.byteStart] & s.bitMask) ? 1.0f : 0.0f;
            s.updated = true;
            continue;
        }

        if (s.byteStart + s.byteLen > msg.data_length_code) continue;

        int32_t raw = 0;
        if (s.bigEndian) {
            for (int b = 0; b < s.byteLen; b++)
                raw = (raw << 8) | msg.data[s.byteStart + b];
        } else {
            for (int b = s.byteLen - 1; b >= 0; b--)
                raw = (raw << 8) | msg.data[s.byteStart + b];
        }
        if (s.isSigned && s.byteLen < 4) {
            int bits = s.byteLen * 8;
            if (raw & (1 << (bits - 1))) raw |= (int32_t)(~0u << bits);
        }
        s.value   = raw * s.scale + s.offsetVal;
        s.updated = true;
    }
}

// ── Tiempo de red (NITZ/CCLK) ─────────────────────────────────────────────────
bool readNetworkTime() {
    String resp = queryAT("AT+CCLK?", "+CCLK:", 3000);
    int q1 = resp.indexOf('"'), q2 = resp.lastIndexOf('"');
    if (q1 < 0 || q2 - q1 < 18) return false;
    String dt = resp.substring(q1 + 1, q2);   // "26/06/18,14:34:53+08"

    TimeRef t  = snapshotTime();
    t.year  = 2000 + dt.substring(0, 2).toInt();
    t.month =        dt.substring(3, 5).toInt();
    t.day   =        dt.substring(6, 8).toInt();
    t.hour  =        dt.substring(9, 11).toInt();
    t.min   =        dt.substring(12, 14).toInt();
    t.sec   =        dt.substring(15, 17).toInt();

    // Convertir a UTC para uso interno (trips/telemetría en Supabase usan
    // UTC), pero guardando el offset ("+08" = +2h) para poder volver a
    // hora local al construir la trama del reloj — el panel de la moto
    // espera hora local (con DST incluido, que la red ya resuelve sola),
    // no UTC.
    int signIdx = dt.indexOf('+', 16), sign = 1;
    if (signIdx < 0) { signIdx = dt.indexOf('-', 16); sign = -1; }
    if (signIdx >= 0) {
        int offsetMin = sign * dt.substring(signIdx + 1).toInt() * 15;
        int totalMin  = (int)t.hour * 60 + t.min - offsetMin;
        t.hour = ((totalMin / 60) % 24 + 24) % 24;
        t.min  = ((totalMin % 60)      + 60) % 60;
        t.utcOffsetMin = offsetMin;
    }
    t.capturedAt = millis();
    t.valid      = true;
    storeTime(t);
    Serial.printf("[TIME] UTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                  t.year, t.month, t.day, t.hour, t.min, t.sec);
    return true;
}

// ── GPS ───────────────────────────────────────────────────────────────────────
static float nmeaToDeg(float nmea, char hemi) {
    int   deg    = (int)(nmea / 100);
    float result = deg + (nmea - deg * 100.0f) / 60.0f;
    return (hemi == 'S' || hemi == 'W') ? -result : result;
}

// ── GPS ───────────────────────────────────────────────────────────────────────
#if defined(MODEM_A7670G)
// A7670G: AT+CGPSINFO → "+CGPSINFO: ddmm.mmmm,N,dddmm.mmmm,E,ddmmyy,hhmmss.s,alt,speed,course"
void readGPS() {
    String resp = queryAT("AT+CGPSINFO", "+CGPSINFO:", 3000);
    int colon = resp.indexOf(':');
    if (colon < 0) return;
    String d = resp.substring(colon + 2); d.trim();
    if (d.length() < 10 || d[0] == ',') return;

    String f[9]; int fi = 0, prev = 0;
    for (int i = 0; i <= (int)d.length() && fi < 9; i++) {
        if (i == (int)d.length() || d[i] == ',') { f[fi++] = d.substring(prev, i); prev = i + 1; }
    }
    if (fi < 6 || f[0].length() == 0) return;

    TimeRef t   = snapshotTime();
    t.hasPos    = true;
    t.posSource = 'g';
    t.lat       = nmeaToDeg(f[0].toFloat(), f[1].length() ? f[1][0] : 'N');
    t.lon       = nmeaToDeg(f[2].toFloat(), f[3].length() ? f[3][0] : 'E');
    t.speed_kmh = (fi >= 8) ? f[7].toFloat() : 0.0f;
    if (f[4].length() >= 6) {
        t.day = f[4].substring(0,2).toInt(); t.month = f[4].substring(2,4).toInt();
        t.year = 2000 + f[4].substring(4,6).toInt();
    }
    if (f[5].length() >= 6) {
        t.hour = f[5].substring(0,2).toInt(); t.min = f[5].substring(2,4).toInt();
        t.sec  = f[5].substring(4,6).toInt();
    }
    t.capturedAt = millis(); t.valid = true;
    storeTime(t);
}
#else // SIM7000G

// Respaldo cuando no hay fix GPS: posición aproximada por triangulación de
// celda (LBS), resuelta por el propio operador a través del módem. Mucha
// menos precisión que el GPS (de cientos de metros a varios km según la
// zona), pero mejor que no tener nada mientras se consigue un fix real —
// típicamente los primeros minutos tras arrancar, o en interior/garaje.
static void readLBS() {
    static uint32_t lastTry = 0;
    if (millis() - lastTry < 60000) return;
    lastTry = millis();

    String resp = queryAT("AT+CLBS=1,1", "+CLBS:", 10000);
    int colon = resp.indexOf(':');
    if (colon < 0) return;
    String d = resp.substring(colon + 2); d.trim();

    String f[4]; int fi = 0, prev = 0;
    for (int i = 0; i <= (int)d.length() && fi < 4; i++) {
        if (i == (int)d.length() || d[i] == ',') { f[fi++] = d.substring(prev, i); prev = i + 1; }
    }
    // f[0]=código de resultado (0=OK) f[1]=lat f[2]=lon
    if (fi < 3 || f[0] != "0") return;

    // Ojo: no se toca t.capturedAt aquí — solo posición, no hora. Si se
    // pisara con millis() actual sin también avanzar hour/min/sec, el
    // cálculo de segundos transcurridos de txSignalByte() se resetearía a
    // 0 con una hora ya vieja, y el reloj de la moto saltaría hacia atrás.
    TimeRef t   = snapshotTime();
    t.hasPos    = true;
    t.posSource = 'l';
    t.lat       = f[1].toFloat();
    t.lon       = f[2].toFloat();
    t.speed_kmh = 0;  // LBS no da velocidad; no arrastrar la última del GPS
    storeTime(t);
}

// AT+CGNSINF → "+CGNSINF: run,fix,YYYYMMDDHHmmSS.sss,lat,lon,alt,speed,course,..."
// lat/lon ya en grados decimales (positivo=N/E, negativo=S/W)
void readGPS() {
    String resp = queryAT("AT+CGNSINF", "+CGNSINF:", 3000);
    int colon = resp.indexOf(':');
    if (colon < 0) return;
    String d = resp.substring(colon + 2); d.trim();

    String f[10]; int fi = 0, prev = 0;
    for (int i = 0; i <= (int)d.length() && fi < 10; i++) {
        if (i == (int)d.length() || d[i] == ',') { f[fi++] = d.substring(prev, i); prev = i + 1; }
    }
    // f[0]=run_status f[1]=fix_status f[2]=datetime f[3]=lat f[4]=lon f[6]=speed
    // Visto en campo: el motor GNSS puede aparecer como apagado
    // (run_status=0) sin que nosotros lo hayamos apagado ni haya habido una
    // reconexión de por medio — probablemente algo del propio módulo. Sin
    // rearmarlo aquí, se queda apagado hasta el siguiente HTTP_SETUP
    // (puede tardar minutos con la red inestable), y el GPS nunca llega a
    // tener el tiempo continuo que necesita para adquirir posición.
    if (fi >= 1 && f[0] != "1") {
        static uint32_t lastRearm = 0;
        if (millis() - lastRearm > 30000) {
            lastRearm = millis();
            modem.enableGPS(MODEM_GPS_ENABLE_GPIO, MODEM_GPS_ENABLE_LEVEL);
        }
    }
    if (fi < 7 || f[1] != "1" || f[2].length() < 14) { readLBS(); return; }

    TimeRef t   = snapshotTime();
    t.hasPos    = true;
    t.posSource = 'g';
    t.lat       = f[3].toFloat();
    t.lon       = f[4].toFloat();
    t.speed_kmh = f[6].toFloat();
    // datetime: YYYYMMDDHHMMSS
    t.year  = f[2].substring(0, 4).toInt();
    t.month = f[2].substring(4, 6).toInt();
    t.day   = f[2].substring(6, 8).toInt();
    t.hour  = f[2].substring(8, 10).toInt();
    t.min   = f[2].substring(10, 12).toInt();
    t.sec   = f[2].substring(12, 14).toInt();
    t.capturedAt = millis(); t.valid = true;
    storeTime(t);
}
#endif

// ── Batería ───────────────────────────────────────────────────────────────────
static BatReading readBattery() {
    BatReading b = {};
    String resp = queryAT("AT+CBC", "+CBC:", 3000);
    int colon = resp.indexOf(':'); if (colon < 0) return b;
    String d = resp.substring(colon + 2);
    int c1 = d.indexOf(','), c2 = d.indexOf(',', c1 + 1);
    if (c1 < 0 || c2 < 0) return b;
    b.charging = (d.substring(0, c1).toInt() == 1);
    b.pct      = d.substring(c1 + 1, c2).toInt();
    b.volts    = d.substring(c2 + 1).toFloat() / 1000.0f;
    b.valid    = true; return b;
}

// AT+CSQ → RSSI 0-31 (99=desconocido) → dBm = –113 + 2×rssi
// Devuelve INT16_MIN si el modem no responde o rssi==99.
int16_t readSignalStrength() {
    String resp = queryAT("AT+CSQ", "+CSQ:", 3000);
    int colon = resp.indexOf(':'); if (colon < 0) return INT16_MIN;
    int rssi = resp.substring(colon + 2).toInt();
    if (rssi == 99 || rssi < 0 || rssi > 31) return INT16_MIN;
    return (int16_t)(-113 + 2 * rssi);
}

// ── CAN task (Core 0) ─────────────────────────────────────────────────────────
// • Emite las tramas TX definidas en Supabase (direction='tx').
//   Ej: 0x510 con clock_hours/clock_minutes → pantalla muestra hora NITZ.
//   NO emite ninguna trama que el usuario no haya configurado explícitamente.
// • Recibe tramas del bus y las decodifica según la config de Supabase.

void setupCAN() {
    twai_general_config_t gcfg = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t  tcfg = CAN_SPEED;
    twai_filter_config_t  fcfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&gcfg, &tcfg, &fcfg) != ESP_OK || twai_start() != ESP_OK)
        Serial.println("[CAN] Error TWAI");
    else
        Serial.println("[CAN] TWAI listo");
}

// Marca de tiempo de la última trama CAN recibida, sea cual sea (conocida o
// no) — se usa como señal de "moto encendida": el bus solo tiene tráfico
// cuando el propio sistema eléctrico de la moto está arrancado. volatile
// porque la escribe canTask() (core 0) y la lee loop() (core 1); un solo
// uint32_t alineado se lee/escribe de forma atómica en el ESP32, no hace
// falta mutex para esto.
static volatile uint32_t lastCanFrameMs = 0;

void canTask(void*) {
    for (;;) {
        uint32_t now = millis();
        TimeRef  t   = snapshotTime();

        // — Recuperación de bus-off —
        // Visto en campo con la moto real: tras transmitir bien un rato, el
        // contador de errores de TX llega a 128 (p.ej. si deja de haber
        // ACK un momento) y el periférico se autodesconecta del bus
        // (bus-off), tal y como exige el propio estándar CAN. Sin
        // recuperarlo, el reloj dejaría de emitirse para siempre hasta un
        // reinicio manual del ESP32. Esto es manejo de errores del driver
        // TWAI, no tráfico nuevo: sigue sin emitirse nada salvo la misma
        // trama del reloj ya configurada.
        static bool recovering = false;
        twai_status_info_t twaiSt;
        twai_get_status_info(&twaiSt);
        if (twaiSt.state == TWAI_STATE_BUS_OFF && !recovering) {
            Serial.println("[CAN] Bus-off detectado, iniciando recuperación...");
            twai_initiate_recovery();
            recovering = true;
        }
        if (recovering) {
            if (twaiSt.state == TWAI_STATE_STOPPED) {
                twai_start();
                Serial.println("[CAN] Bus recuperado, reanudando");
                recovering = false;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // — Emisión de tramas TX configuradas —
        // Solo emite frames explícitamente marcados direction='t' para
        // evitar enviar tramas no autorizadas al bus.
        if (xSemaphoreTake(canMux, pdMS_TO_TICKS(5)) == pdTRUE) {
            for (int f = 0; f < txFrameCount; f++) {
                TxFrame& tf = txFrames[f];
                if (now - tf.lastSentMs < tf.intervalMs) continue;
                tf.lastSentMs = now;

                // Si esta trama lleva algún campo de reloj y aún no hay hora
                // de red válida (recién arrancado/reconectado, roaming
                // lento), no se emite en absoluto — antes se enviaba con
                // 00:00 (el valor por defecto de txSignalByte sin hora
                // válida), y esa hora implausible parece ser justo lo que
                // dispara el "error 102" en el panel. No es una trama
                // nueva ni un contenido añadido: es dejar de mandar la
                // MISMA trama del reloj cuando su contenido sería basura.
                bool hasClock = false;
                for (int i = 0; i < canSignalCount; i++) {
                    if (canSignals[i].direction == 't' && canSignals[i].frameId == tf.frameId &&
                        strncmp(canSignals[i].name, "clock_", 6) == 0) {
                        hasClock = true;
                        break;
                    }
                }
                if (hasClock && !t.valid) continue;

                twai_message_t msg = {};
                msg.identifier       = tf.frameId;
                msg.extd             = 0;
                msg.data_length_code = 8;

                for (int i = 0; i < canSignalCount; i++) {
                    CANSignal& s = canSignals[i];
                    if (s.direction != 't' || s.frameId != tf.frameId) continue;
                    if (s.byteStart < 8)
                        msg.data[s.byteStart] = txSignalByte(s.name, s.offsetVal, t, now);
                }
                twai_transmit(&msg, pdMS_TO_TICKS(5));
            }
            xSemaphoreGive(canMux);
        }

        // — Recepción (no bloqueante: lee todo lo que haya en la cola) —
        twai_message_t rx;
        while (twai_receive(&rx, 0) == ESP_OK) {
            lastCanFrameMs = millis();   // cualquier trama cuenta, conocida o no
            if (xSemaphoreTake(canMux, pdMS_TO_TICKS(5)) == pdTRUE) {
                parseCANFrame(rx);
                xSemaphoreGive(canMux);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ── HTTP POST ─────────────────────────────────────────────────────────────────
#if defined(MODEM_A7670G)

bool setupHTTP() {
    sendAT("AT+HTTPTERM");
    if (!sendAT("AT+HTTPINIT"))               return false;
    if (!sendAT("AT+HTTPPARA=\"SSLCFG\",0")) return false;
    String urlCmd = String("AT+HTTPPARA=\"URL\",\"")
                    + SUPABASE_URL + "/rest/v1/telemetry?apikey=" + SUPABASE_KEY + "\"";
    if (!sendAT(urlCmd.c_str())) return false;
    if (!sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"")) return false;
    String ud = String("AT+HTTPPARA=\"USERDATA\",\"Authorization: Bearer ") + SUPABASE_KEY + "\"";
    return sendAT(ud.c_str());
}

bool httpPost(const String& body) {
    String dcmd = "AT+HTTPDATA=" + String(body.length()) + ",5000";
    if (!sendAT(dcmd.c_str(), "DOWNLOAD", 6000)) return false;
    SerialAT.print(body); delay(200);
    return sendAT("AT+HTTPACTION=1", "+HTTPACTION:", 15000);
}

bool httpPostTo(const String& tablePath, const String& body) {
    bool ok = false;
    sendAT("AT+HTTPTERM");
    do {
        if (!sendAT("AT+HTTPINIT"))               break;
        if (!sendAT("AT+HTTPPARA=\"SSLCFG\",0")) break;
        String urlCmd = String("AT+HTTPPARA=\"URL\",\"")
                        + SUPABASE_URL + tablePath + "?apikey=" + SUPABASE_KEY + "\"";
        if (!sendAT(urlCmd.c_str()))               break;
        if (!sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"")) break;
        String ud = String("AT+HTTPPARA=\"USERDATA\",\"Authorization: Bearer ")
                    + SUPABASE_KEY + "\"";
        if (!sendAT(ud.c_str()))                   break;
        String dcmd = "AT+HTTPDATA=" + String(body.length()) + ",5000";
        if (!sendAT(dcmd.c_str(), "DOWNLOAD", 6000)) break;
        SerialAT.print(body); delay(200);
        ok = sendAT("AT+HTTPACTION=1", "+HTTPACTION:", 15000);
    } while (false);
    sendAT("AT+HTTPTERM");
    return ok;
}

#else // SIM7000G — TinyGsmClientSecure (AT+CAOPEN), ver sim7000Request() arriba

// setupHTTP: sin sesión persistente en SIM7000G, cada POST abre su propia conexión
bool setupHTTP() { return true; }

bool httpPost(const String& body) {
    int status; String respBody;
    if (!sim7000Request("POST", "/rest/v1/telemetry", body, status, respBody)) return false;
    return (status == 200 || status == 201);
}

bool httpPostTo(const String& tablePath, const String& body) {
    int status; String respBody;
    if (!sim7000Request("POST", tablePath, body, status, respBody)) return false;
    return (status == 200 || status == 201);
}

#endif

// ── Seguimiento de viajes ─────────────────────────────────────────────────────
// Un viaje empieza en cuanto se recibe la primera trama CAN (la moto se ha
// encendido de verdad) y termina cuando el bus lleva CAN_ALIVE_TIMEOUT_MS
// sin ninguna trama (la moto se ha apagado) — no cuando el GPS mide poca
// velocidad. Antes se usaba un umbral de velocidad GPS (arrancaba a partir
// de 5 km/h, cerraba tras 2 min parado sin subir de 2 km/h): eso se saltaba
// trayectos cortos que nunca llegaban a 5 km/h y tardaba 2 min de más en
// cerrar el viaje al llegar. El bus CAN es una señal directa de "encendida/
// apagada", así que no hace falta inferirlo por velocidad.
// Distancia y velocidad máxima se siguen calculando por GPS (Haversine
// entre lecturas), igual que antes.

#define CAN_ALIVE_TIMEOUT_MS 8000UL   // sin ninguna trama CAN durante esto = moto apagada

// GPS moviéndose de verdad (no ruido de posición en reposo) mientras el bus
// CAN está en silencio (moto apagada, ver canBusAlive()) no tiene una
// explicación normal: la moto no se mueve sola apagada. Es la firma de que
// la están transportando sin la llave — p.ej. cargada en una furgoneta.
#define THEFT_SPEED_KMH 5.0f

static TripState tripState;

static bool canBusAlive() {
    uint32_t last = lastCanFrameMs;   // volatile, lectura atómica
    if (last == 0) return false;      // nunca se ha visto ninguna trama desde el arranque
    return (millis() - last) < CAN_ALIVE_TIMEOUT_MS;
}

static float haversineKm(float lat1, float lon1, float lat2, float lon2) {
    const float R = 6371.0f;
    float dLat = (lat2 - lat1) * DEG_TO_RAD;
    float dLon = (lon2 - lon1) * DEG_TO_RAD;
    float a = sinf(dLat / 2) * sinf(dLat / 2)
            + cosf(lat1 * DEG_TO_RAD) * cosf(lat2 * DEG_TO_RAD)
            * sinf(dLon / 2) * sinf(dLon / 2);
    return R * 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
}

// Devuelve true si el viaje acaba de terminar (se usó httpPostTo → caller debe
// hacer state = HTTP_SETUP para restaurar la sesión de telemetría).
bool updateTrip(bool busAlive, float speed, float soc, float lat, float lon,
                bool hasPos, const TimeRef& t) {
    if (!tripState.active && busAlive) {
        tripState.active     = true;
        tripState.startMs    = millis();
        tripState.startSoc   = soc;
        tripState.distanceKm = 0;
        tripState.maxSpeed   = speed;
        tripState.hasLastPos = hasPos;
        tripState.lastLat    = lat;
        tripState.lastLon    = lon;
        tripState.sy = t.year; tripState.sm = t.month; tripState.sd = t.day;
        tripState.sh = t.hour; tripState.smin = t.min; tripState.ss = t.sec;
        Serial.println("[TRIP] Inicio de viaje (CAN activo)");
        return false;
    }

    if (!tripState.active) return false;

    if (speed > tripState.maxSpeed) tripState.maxSpeed = speed;

    if (hasPos && tripState.hasLastPos)
        tripState.distanceKm += haversineKm(tripState.lastLat, tripState.lastLon, lat, lon);
    if (hasPos) { tripState.lastLat = lat; tripState.lastLon = lon; tripState.hasLastPos = true; }

    if (!busAlive) {
        uint32_t durMin = (millis() - tripState.startMs) / 60000UL;

        char startISO[21], endISO[21], durStr[12];
        snprintf(startISO, sizeof(startISO), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                 tripState.sy, tripState.sm, tripState.sd,
                 tripState.sh, tripState.smin, tripState.ss);
        snprintf(endISO,   sizeof(endISO),   "%04d-%02d-%02dT%02d:%02d:%02dZ",
                 t.year, t.month, t.day, t.hour, t.min, t.sec);
        snprintf(durStr,   sizeof(durStr),   "%uh %02umin",
                 (unsigned)(durMin / 60), (unsigned)(durMin % 60));

        String body = "{\"motorcycle_id\":\"" VEHICLE_ID "\"";
        body += ",\"start_time\":\""         + String(startISO)                    + "\"";
        body += ",\"end_time\":\""            + String(endISO)                     + "\"";
        body += ",\"distance\":"              + String(tripState.distanceKm, 2);
        body += ",\"duration\":\""            + String(durStr)                     + "\"";
        body += ",\"max_speed\":"             + String(tripState.maxSpeed, 1);
        body += ",\"start_battery_level\":"   + String(tripState.startSoc, 1);
        body += ",\"end_battery_level\":"     + String(soc, 1);
        body += ",\"consumption\":"           + String(tripState.startSoc - soc, 1);
        body += "}";

        Serial.print("[TRIP] Fin (CAN en silencio): "); Serial.println(body);
        if (!httpPostTo("/rest/v1/trips", body))
            Serial.println("[TRIP] Error al guardar viaje");

        tripState.active = false;
        return true;   // sesión HTTP consumida → caller debe re-inicializar
    }
    return false;
}

// ── Red ───────────────────────────────────────────────────────────────────────
#if defined(MODEM_A7670G)
bool networkSetup() {
    if (!sendAT("ATE0"))              return false;
    if (!sendAT("AT+CPIN?", "READY")) return false;
    String apn = String("AT+CGDCONT=1,\"IP\",\"") + APN + "\"";
    if (!sendAT(apn.c_str()))                  return false;
    if (!sendAT("AT+CGACT=1,1", "OK", 10000))  return false;
    if (!sendAT("AT+NETOPEN",   "OK", 15000))  return false;
    delay(1000);
    sendAT("AT+CDNSCFG=\"8.8.8.8\",\"8.8.4.4\"");
    sendAT("AT+CTZU=1");
    if (!sendAT("AT+CSSLCFG=\"sslversion\",0,4")) return false;
    sendAT("AT+CSSLCFG=\"authmode\",0,0");
    if (!sendAT("AT+CSSLCFG=\"enableSNI\",0,1"))  return false;
    sendAT("AT+CSSLCFG=\"ignorelocaltime\",0,1");
    return true;
}
#else // SIM7000G
bool networkSetup() {
    if (!sendAT("ATE0"))              return false;

    // Ciclo de radio (CFUN=0/1) antes de pedir el contexto de datos: en
    // itinerancia (Digi Mobil funciona así en esta zona) el contexto de
    // "aplicación" (CNACT) se quedaba en +APP PDP: DEACTIVE de forma
    // consistente sin esto, sin importar señal ni reintentos de CNACT solo.
    // Verificado en banco: forzar un re-registro completo de radio antes de
    // CNACT lo soluciona de forma reproducible (probado 2/2 veces), incluso
    // con señal peor que en los intentos que fallaban antes.
    sendAT("AT+CFUN=0", "OK", 5000);
    delay(500);
    sendAT("AT+CFUN=1", "OK", 5000);
    delay(15000);  // tiempo para reregistrarse en la red tras CFUN=1

    if (!sendAT("AT+CPIN?", "READY")) return false;
    String apn = String("AT+CGDCONT=1,\"IP\",\"") + APN + "\"";
    if (!sendAT(apn.c_str()))                  return false;
    if (!sendAT("AT+CGACT=1,1", "OK", 30000))  return false;
    delay(1000);
    sendAT("AT+CTZU=1");   // NITZ

    // Contexto de "aplicación" (CNACT) para AT+CAOPEN — necesario aparte de
    // CGACT. Se reintenta por si acaso, pero con el ciclo CFUN de arriba
    // debería cuajar ya en el primer intento.
    bool appNetUp = false;
    for (int i = 0; i < 5 && !appNetUp; i++) {
        if (i > 0) { Serial.println("[NET] Reintentando activar datos..."); delay(3000); }
        appNetUp = modem.gprsConnect(APN, "", "");
    }
    if (!appNetUp) Serial.println("[NET] No se pudo activar el contexto de datos tras 5 intentos");
    // AT+SH gestiona SSL internamente; no se necesita CSSLCFG
    return true;
}
#endif

// ── Arranque ──────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200); delay(1000);
    Serial.println("[BOOT] CanRider v2");

    timeMux = xSemaphoreCreateMutex();
    canMux  = xSemaphoreCreateMutex();

#ifdef BOARD_POWERON_PIN
    pinMode(BOARD_POWERON_PIN, OUTPUT); digitalWrite(BOARD_POWERON_PIN, HIGH);
#endif
    // Reset de hardware del módem antes de encenderlo: sin esto, un módem que
    // quedó en estado raro (reflasheos repetidos, sesión previa colgada) no
    // responde a PWRKEY por sí solo. Secuencia verificada contra el ATdebug
    // oficial de LilyGo, que sí consigue "AT" -> "OK" donde PWRKEY solo, no.
#ifdef MODEM_RESET_PIN
    pinMode(MODEM_RESET_PIN, OUTPUT);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL); delay(100);
    digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL);  delay(2600);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
#endif
#ifdef MODEM_DTR_PIN
    pinMode(MODEM_DTR_PIN, OUTPUT);
    digitalWrite(MODEM_DTR_PIN, LOW);  // asegura que el módem no está en sleep
#endif
    pinMode(BOARD_PWRKEY_PIN,  OUTPUT);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);  delay(100);
    digitalWrite(BOARD_PWRKEY_PIN, HIGH); delay(MODEM_POWERON_PULSE_WIDTH_MS);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);

    SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    setupCAN();
    setupCANSignals();
    xTaskCreatePinnedToCore(canTask, "CAN", 3072, NULL, 1, NULL, 0);
    stateAt = millis();
}

// ── Loop principal (Core 1) ───────────────────────────────────────────────────
void loop() {
    switch (state) {

    case MODEM_BOOT:
        if (millis() - stateAt < 8000) break;
        for (int i = 0; i < 15; i++) {
            if (sendAT("AT", "OK", 1000)) { state = NET_SETUP; return; }
            delay(300);
        }
        Serial.println("[ERROR] Módem no responde");
        state = ERROR_WAIT; stateAt = millis();
        break;

    case NET_SETUP:
        Serial.println("[STATE] NET_SETUP...");
        if (networkSetup()) {
            if (!readNetworkTime()) Serial.println("[WARN] Hora de red no disponible");
            state = HTTP_SETUP;
        } else {
            state = ERROR_WAIT; stateAt = millis();
        }
        break;

    case HTTP_SETUP:
        Serial.println("[STATE] HTTP_SETUP...");
#if defined(MODEM_A7670G)
        sendAT("AT+CGPS=1",    "OK", 3000);
#else
        // AT+CGNSPWR=1 a secas solo enciende el receptor del chip; en esta
        // placa la antena GPS tiene su alimentación detrás de un GPIO del
        // propio módem (MODEM_GPS_ENABLE_GPIO=48, ver utilities.h) que hay
        // que activar con AT+CGPIO — si no, el receptor queda "encendido
        // pero sordo": run_status=1 en AT+CGNSINF pero cero satélites
        // detectados nunca, aunque se espere al aire libre. modem.enableGPS()
        // (TinyGsmGPS.tpp) manda ambas cosas en el orden correcto.
        modem.enableGPS(MODEM_GPS_ENABLE_GPIO, MODEM_GPS_ENABLE_LEVEL);
#endif
        if (!snapshotTime().valid) readNetworkTime();
        if (setupHTTP()) {
            Serial.println("[STATE] RUNNING");
            httpFails = 0; state = RUNNING; nextPost = millis();
        } else {
            state = ERROR_WAIT; stateAt = millis();
        }
        break;

    case RUNNING: {
        while (SerialAT.available()) Serial.write(SerialAT.read());
        while (Serial.available())   SerialAT.write(Serial.read());

        if (millis() < nextPost) break;
        nextPost = millis() + POST_INTERVAL_MS;

        readGPS();
        BatReading bat      = readBattery();
        int16_t    rssi     = readSignalStrength();
        TimeRef    t        = snapshotTime();
        bool       busAlive = canBusAlive();

        // GPS moviéndose sin CAN = posible sustracción, ver THEFT_SPEED_KMH.
        bool movingWithoutCan = t.hasPos && !busAlive && t.speed_kmh >= THEFT_SPEED_KMH;
        if (movingWithoutCan)
            Serial.println("[ALERTA] Movimiento GPS sin tramas CAN — posible sustracción");

        String body = "{\"motorcycle_id\":\"" VEHICLE_ID "\"";
        if (t.hasPos) {
            body += ",\"latitude\":"  + String(t.lat,       6);
            body += ",\"longitude\":" + String(t.lon,       6);
            body += ",\"speed\":"     + String(t.speed_kmh, 1);
            body += ",\"position_source\":\"";
            body += (t.posSource == 'l') ? "lbs" : "gps";
            body += "\"";
        } else {
            body += ",\"speed\":0";
        }
        body += ",\"moving_without_can\":" + String(movingWithoutCan ? "true" : "false");
        if (bat.valid) {
            body += ",\"battery_level\":"   + String(bat.pct);
            body += ",\"battery_voltage\":" + String(bat.volts, 3);
            body += ",\"is_charging\":"     + String(bat.charging ? "true" : "false");
        }
        if (rssi != INT16_MIN) {
            body += ",\"signal_strength\":" + String(rssi);
        }

        // Añadir señales CAN decodificadas (solo RX); capturar soc para viajes.
        // La CPX reporta el SoC por 0x540 (moto_battery) o 0x541
        // (moto_battery_b) según el "modo de batería" activo en el BMS —
        // en la práctica solo uno de los dos trae datos reales a la vez,
        // así que se usa el que no esté a cero.
        float socA = 0, socB = 0;
        if (xSemaphoreTake(canMux, pdMS_TO_TICKS(10)) == pdTRUE) {
            for (int i = 0; i < canSignalCount; i++) {
                if (canSignals[i].direction == 't') continue;  // TX no va a telemetría
                if (strcmp(canSignals[i].name, "moto_battery")   == 0) socA = canSignals[i].value;
                if (strcmp(canSignals[i].name, "moto_battery_b") == 0) socB = canSignals[i].value;
                if (canSignals[i].updated) {
                    body += ",\"" + String(canSignals[i].name) + "\":"
                          + String(canSignals[i].value, canSignals[i].byteLen == 1 ? 0 : 2);
                    canSignals[i].updated = false;
                }
            }
            xSemaphoreGive(canMux);
        }
        float currentSoc = (socA > 0) ? socA : socB;
        body += "}";

        Serial.print("[POST] "); Serial.println(body);
        if (httpPost(body)) {
            httpFails = 0;
            Serial.println("[OK] Telemetría enviada");
        } else {
            httpFails++;
            Serial.printf("[ERROR] Fallo POST %d/%d\n", httpFails, HTTP_FAIL_MAX);
            if (httpFails >= HTTP_FAIL_MAX) {
#if defined(MODEM_A7670G)
                sendAT("AT+HTTPTERM"); sendAT("AT+NETCLOSE");
#endif
                // SIM7000G: nada que cerrar aquí — TinyGsmClientSecure ya
                // cierra su socket (sslClient.stop()) dentro de
                // sim7000Request(). AT+SHDISC es de la implementación AT+SH
                // anterior; ya no hay sesión SH que cerrar y siempre daba
                // timeout, costando 5s extra en cada reconexión.
                state = NET_SETUP; break;
            } else {
                // Espera antes de reintentar: sin esto, un fallo persistente
                // reintenta CAOPEN/SH decenas de veces por segundo contra la
                // red del operador — puede agravar el propio problema.
                state = ERROR_WAIT; stateAt = millis(); break;
            }
        }

        // Actualizar viaje; si termina, re-inicializar sesión HTTP
        if (updateTrip(busAlive, t.speed_kmh, currentSoc, t.lat, t.lon, t.hasPos, t))
            state = HTTP_SETUP;
        break;
    }

    case ERROR_WAIT:
        while (SerialAT.available()) Serial.write(SerialAT.read());
        if (millis() - stateAt > RETRY_WAIT_MS) {
#if defined(MODEM_A7670G)
            sendAT("AT+HTTPTERM"); sendAT("AT+NETCLOSE");
#endif
            // SIM7000G: nada que cerrar (ver comentario equivalente más
            // arriba) — AT+SHDISC siempre daba timeout aquí (5s perdidos en
            // cada reconexión, justo cuando la red ya va mal).
            state = NET_SETUP;
        }
        break;
    }
}
