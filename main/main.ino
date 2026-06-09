#define LILYGO_T_A7670
#include "AT/utilities.h"
#include "config.h"
#include "driver/twai.h"

// ── Timing ────────────────────────────────────────────────────────────────────
#define POST_INTERVAL_MS  15000UL
#define RETRY_WAIT_MS     10000UL
#define HTTP_FAIL_MAX     3
#define MAX_HTTP_BODY     4096    // bytes máximos de respuesta GET

// ── Referencia de tiempo ──────────────────────────────────────────────────────
struct TimeRef {
    bool     valid;
    bool     hasPos;
    float    lat, lon, speed_kmh;
    uint8_t  hour, min, sec;
    uint8_t  day, month;
    uint16_t year;
    uint32_t capturedAt;
};

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

// ── Señales CAN (config descargada de Supabase) ───────────────────────────────
// Tabla esperada en Supabase: can_signals
//   frame_id    integer  -- ID del mensaje CAN (ej. 512 = 0x200)
//   signal_name text     -- nombre del campo en telemetría (ej. "rpm")
//   byte_start  integer  -- primer byte del payload (0-7)
//   byte_length integer  -- 1, 2 o 4
//   big_endian  boolean
//   is_signed   boolean
//   scale       float    -- valor_real = raw * scale + offset_val
//   offset_val  float

#define MAX_SIGNALS 32

struct CANSignal {
    uint32_t frameId;
    char     direction;   // 'r'=RX (leer del bus), 't'=TX (emitir al bus)
    uint16_t txIntervalMs;
    bool     dualMode;    // true → también escucha frameId+1 (solo RX)
    char     name[20];
    uint8_t  byteStart;
    uint8_t  byteLen;
    uint8_t  bitMask;     // 0=extracción de bytes; N>0 → result=(data[byteStart]&N)?1:0
    bool     bigEndian;
    bool     isSigned;
    float    scale;
    float    offsetVal;
    float    value;
    bool     updated;
};

static CANSignal         canSignals[MAX_SIGNALS];
static int               canSignalCount = 0;
static SemaphoreHandle_t canMux         = NULL;

// ── Tramas TX dinámicas (agrupadas por frame_id) ──────────────────────────────
#define MAX_TX_FRAMES 8

struct TxFrame {
    uint32_t frameId;
    uint32_t intervalMs;
    uint32_t lastSentMs;
};

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

// Lee exactamente `size` bytes del cuerpo HTTP tras el encabezado +HTTPREAD.
// Formato del módem: "+HTTPREAD: N\r\n<N bytes>\r\nOK\r\n"
String httpReadBody(int size) {
    if (size <= 0 || size > MAX_HTTP_BODY) return "";
    String cmd = "AT+HTTPREAD=0," + String(size);
    Serial.print(">> "); Serial.println(cmd);
    SerialAT.println(cmd);

    String buf;
    bool   bodyMode  = false;
    int    bodyBytes = 0;
    uint32_t t = millis();

    while (millis() - t < 10000) {
        while (SerialAT.available()) {
            char c = SerialAT.read();
            if (!bodyMode) {
                buf += c;
                // El encabezado termina en la primera '\n' que contiene "+HTTPREAD:"
                if (c == '\n' && buf.indexOf("+HTTPREAD:") >= 0) {
                    buf = ""; bodyMode = true;
                }
            } else {
                buf += c;
                if (++bodyBytes >= size) return buf.substring(0, size);
            }
        }
    }
    return buf;
}

// ── JSON helpers (sin dependencias externas) ──────────────────────────────────
static int32_t jsonInt(const String& j, const char* field) {
    String key = String('"') + field + "\":";
    int idx = j.indexOf(key);
    if (idx < 0) return 0;
    return j.substring(idx + key.length()).toInt();
}
static float jsonFloat(const String& j, const char* field) {
    String key = String('"') + field + "\":";
    int idx = j.indexOf(key);
    if (idx < 0) return 0.0f;
    return j.substring(idx + key.length()).toFloat();
}
static bool jsonBool(const String& j, const char* field) {
    String key = String('"') + field + "\":";
    int idx = j.indexOf(key);
    if (idx < 0) return false;
    int s = idx + key.length();
    return j.substring(s, s + 4) == "true";
}
static String jsonStr(const String& j, const char* field) {
    String key = String('"') + field + "\":\"";
    int idx = j.indexOf(key);
    if (idx < 0) return "";
    int s = idx + key.length();
    int e = j.indexOf('"', s);
    return (e < 0) ? "" : j.substring(s, e);
}
// Devuelve el siguiente objeto JSON {} a partir de `from`; actualiza `next`.
static String nextJsonObj(const String& j, int from, int& next) {
    int start = j.indexOf('{', from);
    if (start < 0) { next = -1; return ""; }
    int depth = 0;
    for (int i = start; i < (int)j.length(); i++) {
        if (j[i] == '{') depth++;
        else if (j[i] == '}' && --depth == 0) {
            next = i + 1;
            return j.substring(start, i + 1);
        }
    }
    next = -1; return "";
}

// ── HTTP GET genérico ─────────────────────────────────────────────────────────
// Gestiona su propio ciclo HTTPINIT/HTTPTERM.
// `path` ejemplo: "/rest/v1/can_signals?vehicle_id=eq.UUID&select=*"
String httpGet(const String& path) {
    sendAT("AT+HTTPTERM");
    if (!sendAT("AT+HTTPINIT"))               return "";
    if (!sendAT("AT+HTTPPARA=\"SSLCFG\",0")) return "";

    String url = String(SUPABASE_URL) + path + "&apikey=" + SUPABASE_KEY;
    String urlCmd = "AT+HTTPPARA=\"URL\",\"" + url + "\"";
    if (!sendAT(urlCmd.c_str())) { sendAT("AT+HTTPTERM"); return ""; }

    String auth = String("AT+HTTPPARA=\"USERDATA\",\"Authorization: Bearer ") + SUPABASE_KEY + "\"";
    if (!sendAT(auth.c_str()))   { sendAT("AT+HTTPTERM"); return ""; }

    // +HTTPACTION: 0,<status>,<size>
    String actionLine = queryAT("AT+HTTPACTION=0", "+HTTPACTION:", 15000);
    int c1 = actionLine.indexOf(','), c2 = actionLine.indexOf(',', c1 + 1);
    if (c1 < 0 || c2 < 0) { sendAT("AT+HTTPTERM"); return ""; }
    int status = actionLine.substring(c1 + 1, c2).toInt();
    int size   = actionLine.substring(c2 + 1).toInt();
    if (status != 200 || size <= 0) {
        Serial.printf("[GET] HTTP %d size=%d\n", status, size);
        sendAT("AT+HTTPTERM"); return "";
    }

    String body = httpReadBody(size);
    sendAT("AT+HTTPTERM");
    return body;
}

// ── Config CAN desde Supabase ─────────────────────────────────────────────────
bool fetchCANConfig() {
    Serial.println("[CAN] Descargando config desde Supabase...");
    String path = String("/rest/v1/can_signals?vehicle_id=eq.") + VEHICLE_ID
                + "&select=frame_id,direction,tx_interval_ms,dual_mode,signal_name,"
                  "byte_start,byte_length,bit_mask,big_endian,is_signed,scale,offset_val";
    String json = httpGet(path);
    if (json.length() == 0) {
        Serial.println("[CAN] Config no disponible");
        return false;
    }

    if (xSemaphoreTake(canMux, portMAX_DELAY) == pdTRUE) {
        canSignalCount = 0;
        int pos = 0;
        while (canSignalCount < MAX_SIGNALS) {
            int next;
            String obj = nextJsonObj(json, pos, next);
            if (next < 0 || obj.length() == 0) break;
            pos = next;

            CANSignal& s    = canSignals[canSignalCount];
            s.frameId       = (uint32_t)jsonInt(obj, "frame_id");
            String dir      = jsonStr(obj, "direction");
            s.direction     = (dir == "tx") ? 't' : 'r';
            s.txIntervalMs  = (uint16_t)jsonInt(obj, "tx_interval_ms");
            if (s.txIntervalMs == 0) s.txIntervalMs = 200;
            s.dualMode      = jsonBool(obj, "dual_mode");
            String nm       = jsonStr(obj, "signal_name");
            nm.toCharArray(s.name, sizeof(s.name));
            s.byteStart     = (uint8_t)jsonInt(obj, "byte_start");
            s.byteLen       = (uint8_t)jsonInt(obj, "byte_length");
            s.bitMask       = (uint8_t)jsonInt(obj, "bit_mask");
            s.bigEndian     = jsonBool(obj, "big_endian");
            s.isSigned      = jsonBool(obj, "is_signed");
            s.scale         = jsonFloat(obj, "scale");
            s.offsetVal     = jsonFloat(obj, "offset_val");
            // Evitar división por cero en decodificación RX (TX no usa scale)
            if (s.direction == 'r' && s.scale == 0.0f) s.scale = 1.0f;
            s.value   = 0.0f;
            s.updated = false;
            canSignalCount++;
            Serial.printf("[CAN] %s '%s' frame=0x%X%s byte=%d len=%d\n",
                          s.direction == 't' ? "TX" : "RX",
                          s.name, s.frameId,
                          s.dualMode ? "+1" : "",
                          s.byteStart, s.byteLen);
        }

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
    return true;
}

// ── Valor para un byte de una trama TX ───────────────────────────────────────
// Llamada desde canTask con canMux tomado y time snapshot previo.
// Señales de reloj: usan la hora NITZ ajustada con el tiempo transcurrido.
// Cualquier otro nombre: byte fijo = (uint8_t)offsetVal.
static uint8_t txSignalByte(const CANSignal& s, const TimeRef& t, uint32_t now) {
    if (!t.valid) return (uint8_t)s.offsetVal;
    uint32_t elapsed  = now - t.capturedAt;
    uint32_t totalSec = (uint32_t)t.hour * 3600u + (uint32_t)t.min * 60u
                      + t.sec + elapsed / 1000u;
    if (strcmp(s.name, "clock_hours")   == 0) return (uint8_t)((totalSec / 3600u) % 24u);
    if (strcmp(s.name, "clock_minutes") == 0) return (uint8_t)((totalSec / 60u)   % 60u);
    if (strcmp(s.name, "clock_seconds") == 0) return (uint8_t)( totalSec          % 60u);
    if (strcmp(s.name, "clock_day")     == 0) return t.day;
    if (strcmp(s.name, "clock_month")   == 0) return t.month;
    if (strcmp(s.name, "clock_year_hi") == 0) return (uint8_t)(t.year >> 8);
    if (strcmp(s.name, "clock_year_lo") == 0) return (uint8_t)(t.year & 0xFFu);
    // Byte fijo: el usuario define el valor con offset_val
    return (uint8_t)s.offsetVal;
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
            if (raw & (1 << (bits - 1))) raw |= (~0 << bits);
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

    // Convertir a UTC: offset en cuartos de hora ("+08" = +2h)
    int signIdx = dt.indexOf('+', 16), sign = 1;
    if (signIdx < 0) { signIdx = dt.indexOf('-', 16); sign = -1; }
    if (signIdx >= 0) {
        int offsetMin = sign * dt.substring(signIdx + 1).toInt() * 15;
        int totalMin  = (int)t.hour * 60 + t.min - offsetMin;
        t.hour = ((totalMin / 60) % 24 + 24) % 24;
        t.min  = ((totalMin % 60)      + 60) % 60;
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

    TimeRef t     = snapshotTime();
    t.hasPos      = true;
    t.lat         = nmeaToDeg(f[0].toFloat(), f[1].length() ? f[1][0] : 'N');
    t.lon         = nmeaToDeg(f[2].toFloat(), f[3].length() ? f[3][0] : 'E');
    t.speed_kmh   = (fi >= 8) ? f[7].toFloat() : 0.0f;
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

// ── Batería ───────────────────────────────────────────────────────────────────
struct BatReading { bool valid; int pct; float volts; bool charging; };

BatReading readBattery() {
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

void canTask(void*) {
    for (;;) {
        uint32_t now = millis();
        TimeRef  t   = snapshotTime();

        // — Emisión de tramas TX definidas en Supabase —
        // Solo emite frames que el usuario haya configurado explícitamente
        // con direction='tx' para evitar enviar tramas no autorizadas al bus.
        if (xSemaphoreTake(canMux, pdMS_TO_TICKS(5)) == pdTRUE) {
            for (int f = 0; f < txFrameCount; f++) {
                TxFrame& tf = txFrames[f];
                if (now - tf.lastSentMs < tf.intervalMs) continue;
                tf.lastSentMs = now;

                twai_message_t msg = {};
                msg.identifier       = tf.frameId;
                msg.extd             = 0;
                msg.data_length_code = 8;

                for (int i = 0; i < canSignalCount; i++) {
                    CANSignal& s = canSignals[i];
                    if (s.direction != 't' || s.frameId != tf.frameId) continue;
                    if (s.byteStart < 8)
                        msg.data[s.byteStart] = txSignalByte(s, t, now);
                }
                twai_transmit(&msg, pdMS_TO_TICKS(5));
            }
            xSemaphoreGive(canMux);
        }

        // — Recepción (no bloqueante: lee todo lo que haya en la cola) —
        twai_message_t rx;
        while (twai_receive(&rx, 0) == ESP_OK) {
            if (xSemaphoreTake(canMux, pdMS_TO_TICKS(5)) == pdTRUE) {
                parseCANFrame(rx);
                xSemaphoreGive(canMux);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ── HTTP POST ─────────────────────────────────────────────────────────────────
bool setupHTTP() {
    sendAT("AT+HTTPTERM");
    if (!sendAT("AT+HTTPINIT"))               return false;
    if (!sendAT("AT+HTTPPARA=\"SSLCFG\",0")) return false;
    // SUPABASE_URL es la URL base (sin path), construimos la URL completa aquí
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

// ── POST a tabla arbitraria (para viajes) ────────────────────────────────────
// Abre su propio ciclo HTTPINIT/HTTPTERM. Después de llamarlo, ir a HTTP_SETUP
// para restaurar la sesión de telemetría.
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

// ── Seguimiento de viajes ─────────────────────────────────────────────────────
// Un viaje comienza cuando speed >= TRIP_START_KMH y termina cuando
// permanece por debajo de TRIP_END_KMH durante TRIP_END_MS consecutivos.
// La distancia se acumula con la fórmula de Haversine entre lecturas GPS.

#define TRIP_START_KMH  5.0f
#define TRIP_END_KMH    2.0f
#define TRIP_END_MS     120000UL   // 2 min parado para cerrar el viaje

struct TripState {
    bool     active      = false;
    uint32_t startMs     = 0;
    float    startSoc    = 0;
    float    distanceKm  = 0;
    float    maxSpeed    = 0;
    float    lastLat     = 0, lastLon = 0;
    bool     hasLastPos  = false;
    uint32_t stopSince   = 0;
    int      sy, sm, sd, sh, smin, ss;   // fecha/hora de inicio
} tripState;

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
bool updateTrip(float speed, float soc, float lat, float lon,
                bool hasPos, const TimeRef& t) {
    bool moving = speed >= TRIP_START_KMH;

    if (!tripState.active && moving) {
        tripState.active     = true;
        tripState.startMs    = millis();
        tripState.startSoc   = soc;
        tripState.distanceKm = 0;
        tripState.maxSpeed   = speed;
        tripState.hasLastPos = hasPos;
        tripState.lastLat    = lat;
        tripState.lastLon    = lon;
        tripState.stopSince  = 0;
        tripState.sy = t.year; tripState.sm = t.month; tripState.sd = t.day;
        tripState.sh = t.hour; tripState.smin = t.min; tripState.ss = t.sec;
        Serial.println("[TRIP] Inicio de viaje");
        return false;
    }

    if (!tripState.active) return false;

    if (speed > tripState.maxSpeed) tripState.maxSpeed = speed;

    if (hasPos && tripState.hasLastPos)
        tripState.distanceKm += haversineKm(tripState.lastLat, tripState.lastLon, lat, lon);
    if (hasPos) { tripState.lastLat = lat; tripState.lastLon = lon; tripState.hasLastPos = true; }

    if (speed < TRIP_END_KMH) {
        if (tripState.stopSince == 0) tripState.stopSince = millis();
        else if (millis() - tripState.stopSince >= TRIP_END_MS) {
            uint32_t durMin = (millis() - tripState.startMs) / 60000UL;

            char startISO[21], endISO[21], durStr[12];
            snprintf(startISO, sizeof(startISO), "20%02d-%02d-%02dT%02d:%02d:%02dZ",
                     tripState.sy, tripState.sm, tripState.sd,
                     tripState.sh, tripState.smin, tripState.ss);
            snprintf(endISO,   sizeof(endISO),   "20%02d-%02d-%02dT%02d:%02d:%02dZ",
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

            Serial.print("[TRIP] Fin: "); Serial.println(body);
            if (!httpPostTo("/rest/v1/trips", body))
                Serial.println("[TRIP] Error al guardar viaje");

            tripState.active    = false;
            tripState.stopSince = 0;
            return true;   // sesión HTTP consumida → caller debe re-inicializar
        }
    } else {
        tripState.stopSince = 0;
    }
    return false;
}

// ── Red ───────────────────────────────────────────────────────────────────────
bool networkSetup() {
    if (!sendAT("ATE0"))              return false;
    if (!sendAT("AT+CPIN?", "READY")) return false;
    String apn = String("AT+CGDCONT=1,\"IP\",\"") + APN + "\"";
    if (!sendAT(apn.c_str()))                  return false;
    if (!sendAT("AT+CGACT=1,1", "OK", 10000))  return false;
    if (!sendAT("AT+NETOPEN",   "OK", 15000))  return false;
    delay(1000);
    sendAT("AT+CDNSCFG=\"8.8.8.8\",\"8.8.4.4\"");
    sendAT("AT+CTZU=1");   // sincronización automática de hora por red (NITZ)
    if (!sendAT("AT+CSSLCFG=\"sslversion\",0,4")) return false;
    sendAT("AT+CSSLCFG=\"authmode\",0,0");          // sin CA bundle en el módulo
    if (!sendAT("AT+CSSLCFG=\"enableSNI\",0,1"))  return false;
    sendAT("AT+CSSLCFG=\"ignorelocaltime\",0,1");
    return true;
}

// ── Arranque ──────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200); delay(1000);
    Serial.println("[BOOT] CanRider v2");

    timeMux = xSemaphoreCreateMutex();
    canMux  = xSemaphoreCreateMutex();

    pinMode(BOARD_POWERON_PIN, OUTPUT); digitalWrite(BOARD_POWERON_PIN, HIGH);
    pinMode(BOARD_PWRKEY_PIN,  OUTPUT);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);  delay(100);
    digitalWrite(BOARD_PWRKEY_PIN, HIGH); delay(MODEM_POWERON_PULSE_WIDTH_MS);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);

    SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    setupCAN();
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
        sendAT("AT+CGPS=1", "OK", 3000);
        if (!snapshotTime().valid) readNetworkTime();
        fetchCANConfig();   // descarga/actualiza señales desde Supabase
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
        BatReading bat  = readBattery();
        int16_t    rssi = readSignalStrength();
        TimeRef    t    = snapshotTime();

        String body = "{\"motorcycle_id\":\"" VEHICLE_ID "\"";
        if (t.hasPos) {
            body += ",\"latitude\":"  + String(t.lat,       6);
            body += ",\"longitude\":" + String(t.lon,       6);
            body += ",\"speed\":"     + String(t.speed_kmh, 1);
        } else {
            body += ",\"speed\":0";
        }
        if (bat.valid) {
            body += ",\"battery_level\":"   + String(bat.pct);
            body += ",\"battery_voltage\":" + String(bat.volts, 3);
            body += ",\"is_charging\":"     + String(bat.charging ? "true" : "false");
        }
        if (rssi != INT16_MIN) {
            body += ",\"signal_strength\":" + String(rssi);
        }

        // Añadir señales CAN decodificadas (solo RX); capturar soc para viajes
        float currentSoc = 0;
        if (xSemaphoreTake(canMux, pdMS_TO_TICKS(10)) == pdTRUE) {
            for (int i = 0; i < canSignalCount; i++) {
                if (canSignals[i].direction == 't') continue;  // TX no va a telemetría
                if (strcmp(canSignals[i].name, "soc") == 0) currentSoc = canSignals[i].value;
                if (canSignals[i].updated) {
                    body += ",\"" + String(canSignals[i].name) + "\":"
                          + String(canSignals[i].value, canSignals[i].byteLen == 1 ? 0 : 2);
                    canSignals[i].updated = false;
                }
            }
            xSemaphoreGive(canMux);
        }
        body += "}";

        Serial.print("[POST] "); Serial.println(body);
        if (httpPost(body)) {
            httpFails = 0;
            Serial.println("[OK] Telemetría enviada");
        } else {
            httpFails++;
            Serial.printf("[ERROR] Fallo POST %d/%d\n", httpFails, HTTP_FAIL_MAX);
            if (httpFails >= HTTP_FAIL_MAX) {
                sendAT("AT+HTTPTERM"); sendAT("AT+NETCLOSE"); state = NET_SETUP;
                break;
            } else { state = HTTP_SETUP; break; }
        }

        // Actualizar viaje; si termina, re-inicializar sesión HTTP
        if (updateTrip(t.speed_kmh, currentSoc, t.lat, t.lon, t.hasPos, t))
            state = HTTP_SETUP;
        break;
    }

    case ERROR_WAIT:
        while (SerialAT.available()) Serial.write(SerialAT.read());
        if (millis() - stateAt > RETRY_WAIT_MS) {
            sendAT("AT+HTTPTERM"); sendAT("AT+NETCLOSE"); state = NET_SETUP;
        }
        break;
    }
}
