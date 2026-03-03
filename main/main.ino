#define LILYGO_T_A7670
// #define LILYGO_SIM7000G
#include <time.h>
#include <sys/time.h>
#include <SPI.h>
#include <SD.h>
#include "AT/utilities.h"
#include "config.h"
#include "gps.h"
#include "can_decoder.h"
#include "can_config_loader.h"

// Definir pines CAN reconfigurados para no entrar en conflicto con la SD
#ifndef CAN_RX_PIN
#define CAN_RX_PIN 21
#endif
#ifndef CAN_TX_PIN
#define CAN_TX_PIN 22
#endif

#ifndef CAN_ACTIVITY_LED_PIN
#define CAN_ACTIVITY_LED_PIN 23
#endif

/*
config.h DEBE CONTENER:

#define APN           "internet.digimobil.es"
#define SUPABASE_URL  "https://jmisxaxqwtkudvkytkha.supabase.co/rest/v1/telemetry"
#define SUPABASE_KEY  "TU_API_KEY_AQUI"
#define VEHICLE_ID    "test01"
*/

// Variables globales de control
uint32_t lastTaskCanTimeSend = 0; // Control de envío de hora en Core 0
uint32_t lastCanActivityTime = 0; // Marca de tiempo de la última trama CAN recibida
bool isTripActive = false;        // Estado del trayecto actual
int networkFailures = 0;          // Contador de fallos de red consecutivos
const int MAX_NETWORK_FAILURES = 10; // Reiniciar tras 10 fallos
time_t tripStartTime = 0;         // Hora de inicio del trayecto
time_t tripEndTime = 0;           // Hora de fin del trayecto
uint32_t tripDuration = 0;        // Duración total en segundos

// Variables para seguimiento de trayectos (Movidas aquí para evitar errores de compilación)
float tripStartLat = 0, tripStartLon = 0;
int tripStartBatA = -1, tripStartBatB = -1;
float tripTotalSpeed = 0;
uint32_t tripPointsCount = 0;
float tripDistance = 0;
float lastTripLat = 0, lastTripLon = 0;
String tripPathJson = "";
uint32_t tripPointsStored = 0;
const uint32_t MAX_TRIP_POINTS = 200; // Límite en RAM para el resumen rápido
uint32_t lastTripPathPointTime = 0;
const uint32_t tripPathPointInterval = 30000; // Punto cada 30s en RAM
String currentTripFile = ""; // Archivo en SD para el recorrido completo

// Variables para detección de robo (Movidas aquí para evitar errores de compilación)
bool isTheftEventActive = false;
time_t theftStartTime = 0;
float theftStartLat = 0, theftStartLon = 0;
int theftStartBat = -1;
float theftMaxSpeed = 0;
float theftDistance = 0;
float lastTheftLat = 0, lastTheftLon = 0;
uint32_t lastMovementTime = 0;
const uint32_t movementTimeoutTheft = 120000; // 2 minutos sin movimiento = fin evento

// Filtros y recorrido modo robo
String theftPathJson = "";
uint32_t theftPointsCount = 0;
const uint32_t MAX_THEFT_POINTS = 100;
const float THEFT_SPEED_THRESHOLD = 5.0; // km/h
const float THEFT_MIN_DISTANCE_SEND = 0.05; // 50 metros para confirmar robo
uint32_t lastPathPointTime = 0;
const uint32_t pathPointInterval = 10000; // Guardar punto cada 10s
String currentTheftFile = ""; // Archivo SD para robo

// --- FUNCIONES BÁSICAS (Declarar antes de usar) ---
String getTimestamp() {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  
  if (timeinfo.tm_year > 120) { // Si el año es > 2020, asumimos sincronizado
    char buf[22];
    strftime(buf, sizeof(buf), "[%Y-%m-%d %H:%M:%S]", &timeinfo);
    return String(buf);
  }
  
  uint32_t seconds = millis() / 1000;
  uint32_t h = seconds / 3600;
  uint32_t m = (seconds % 3600) / 60;
  uint32_t s = seconds % 60;
  char buf[12];
  snprintf(buf, sizeof(buf), "[%02u:%02u:%02u]", h, m, s);
  return String(buf);
}

void sendAT(const char *cmd, uint32_t timeout = 3000) {
  Serial.print(getTimestamp() + " >> ");
  Serial.println(cmd);
  SerialAT.println(cmd);

  uint32_t t = millis();
  while (millis() - t < timeout) {
    while (SerialAT.available()) {
      Serial.write(SerialAT.read());
    }
  }
  Serial.println();
}
// --------------------------------------------------

int getBatteryLevel() {
  uint32_t mv = 0;
  for(int i=0; i<10; i++) mv += analogReadMilliVolts(35);
  mv /= 10;
  
  // Si no hay lectura en el 35, probar el 34
  if (mv < 500) {
    mv = 0;
    for(int i=0; i<10; i++) mv += analogReadMilliVolts(34);
    mv /= 10;
  }
  
  float voltage = (mv / 1000.0) * 2.0;
  int percentage = (voltage - 3.4) * 100 / (4.2 - 3.4);
  if (percentage > 100) percentage = 100;
  if (percentage < 0) percentage = 0;
  
  Serial.print(getTimestamp() + " [BAT] V:" + String(voltage) + "V (" + String(percentage) + "%)");
  return percentage;
}

int getRSSI() {
  SerialAT.println("AT+CSQ");
  String res = "";
  uint32_t t = millis();
  while (millis() - t < 500) {
    while (SerialAT.available()) res += (char)SerialAT.read();
  }
  int index = res.indexOf("+CSQ: ");
  if (index != -1) {
    int rssi = res.substring(index + 6, res.indexOf(",", index)).toInt();
    return rssi;
  }
  return 99; // 99 significa desconocido
}

String formatISO8601(time_t t) {
  if (t <= 0) return "";
  struct tm ti;
  gmtime_r(&t, &ti);
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &ti);
  return String(buf);
}

String getCurrentISO8601() {
  String gpsTime = gps_get_time();
  if (gpsTime != "") return gpsTime;

  time_t now;
  time(&now);
  return formatISO8601(now);
}

float lbsLat = 0, lbsLon = 0;

void updateLBS() {
  // AT+CLBS=1,1 -> Obtener lat/lon desde la red
  SerialAT.println("AT+CLBS=1,1");
  String res = "";
  uint32_t t = millis();
  while (millis() - t < 3000) {
    while (SerialAT.available()) res += (char)SerialAT.read();
  }
  
  // Formato esperado: +CLBS: <lat>,<lon>,<precision>,<date>,<time>
  int index = res.indexOf("+CLBS: ");
  if (index != -1) {
    int firstComma = res.indexOf(",", index);
    int secondComma = res.indexOf(",", firstComma + 1);
    int thirdComma = res.indexOf(",", secondComma + 1);
    if (secondComma != -1) {
      lbsLat = res.substring(firstComma + 1, secondComma).toFloat();
      lbsLon = res.substring(secondComma + 1, thirdComma).toFloat();
      Serial.printf("\n[LBS] Posición por red: %f, %f\n", lbsLat, lbsLon);
    }
  }
}

bool checkNetwork() {
#ifdef LILYGO_SIM7000G
  SerialAT.println("AT+CNACT?");
  String res = "";
  uint32_t t = millis();
  while (millis() - t < 500) {
    while (SerialAT.available()) res += (char)SerialAT.read();
  }
  
  if (res.indexOf("+CNACT: 1") != -1) {
    networkFailures = 0;
    return true;
  }
  
  Serial.println("\n" + getTimestamp() + " [NET] SIM7000 Red caída. Intentando abrir...");
  sendAT("AT+CNACT=1,\"" APN "\"", 5000);
#else
  SerialAT.println("AT+NETOPEN?");
  String res = "";
  uint32_t t = millis();
  while (millis() - t < 500) {
    while (SerialAT.available()) res += (char)SerialAT.read();
  }
  
  if (res.indexOf("+NETOPEN: 1") != -1) {
    networkFailures = 0; // Resetear contador si la red está abierta
    return true;
  }
  
  Serial.println("\n" + getTimestamp() + " [NET] Red caída o cerrada. Intentando abrir...");
  sendAT("AT+NETOPEN", 5000);
#endif
  delay(2000);
  
  // Verificar si se abrió tras el intento
#ifdef LILYGO_SIM7000G
  SerialAT.println("AT+CNACT?");
#else
  SerialAT.println("AT+NETOPEN?");
#endif
  res = "";
  t = millis();
  while (millis() - t < 500) {
    while (SerialAT.available()) res += (char)SerialAT.read();
  }
  
#ifdef LILYGO_SIM7000G
  if (res.indexOf("+CNACT: 1") != -1) {
#else
  if (res.indexOf("+NETOPEN: 1") != -1) {
#endif
    networkFailures = 0;
    return true;
  }

  networkFailures++;
  Serial.printf("[NET] Fallo de red #%d de %d\n", networkFailures, MAX_NETWORK_FAILURES);
  
  if (networkFailures >= MAX_NETWORK_FAILURES) {
    Serial.println("🚨 Demasiados fallos de red. Reiniciando ESP32...");
    delay(1000);
    esp_restart();
  }

  return false;
}

void syncTimeHTTP() {
  if (!checkNetwork()) return;

  Serial.println("[TIME] Obteniendo hora real vía HTTP API...");
  sendAT("AT+CDNSCFG=\"8.8.8.8\",\"8.8.4.4\""); // Asegurar DNS
  sendAT("AT+HTTPTERM");
  sendAT("AT+HTTPINIT");
  sendAT("AT+HTTPPARA=\"URL\",\"http://worldtimeapi.org/api/timezone/Etc/UTC\"");
  sendAT("AT+HTTPACTION=0"); // GET

  // Esperar respuesta (máximo 10s para red lenta)
  uint32_t t = millis();
  String res = "";
  while (millis() - t < 10000) {
    while (SerialAT.available()) res += (char)SerialAT.read();
    if (res.indexOf("+HTTPACTION:") != -1) break;
  }

  if (res.indexOf(",200,") != -1) {
    int lastComma = res.lastIndexOf(",");
    int len = res.substring(lastComma + 1).toInt();
    if (len > 0) {
      String readCmd = "AT+HTTPREAD=0," + String(len);
      SerialAT.println(readCmd);
      
      String body = "";
      t = millis();
      while (millis() - t < 5000) {
        while (SerialAT.available()) body += (char)SerialAT.read();
        if (body.indexOf("datetime") != -1 && body.indexOf("}") != -1) break;
      }

      int dateIdx = body.indexOf("\"datetime\":\"");
      if (dateIdx != -1) {
        String iso = body.substring(dateIdx + 12, dateIdx + 31); // "2026-02-10T20:30:00"
        struct tm tm_info;
        memset(&tm_info, 0, sizeof(struct tm));
        sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", 
               &tm_info.tm_year, &tm_info.tm_mon, &tm_info.tm_mday,
               &tm_info.tm_hour, &tm_info.tm_min, &tm_info.tm_sec);
        
        tm_info.tm_year -= 1900;
        tm_info.tm_mon -= 1;

        // Forzar siempre a UTC internamente para evitar desfases con Supabase/Vercel
        setenv("TZ", "UTC0", 1);
        tzset();
        time_t t_now = mktime(&tm_info);
        struct timeval tv = { .tv_sec = t_now };
        settimeofday(&tv, NULL);
        
        Serial.printf("[TIME] Sincronización HTTP Exitosa (UTC): %ld\n", t_now);
      }
    }
  } else {
    Serial.printf("[TIME] Error HTTP: %s\n", res.c_str());
  }
  sendAT("AT+HTTPTERM");
}

void syncTimeNTP() {
  if (!checkNetwork()) return;
  Serial.println("[TIME] Sincronizando vía NTP...");
  sendAT("AT+CNTP=\"pool.ntp.org\",0");
  sendAT("AT+CNTP");
  delay(2000);
}

void syncTimeCell() {
  if (!checkNetwork()) return;
  Serial.println("[TIME] Obteniendo hora de la red celular...");
  // AT+CLBS=1,1 devuelve lat,lon,precision,date,time
  SerialAT.println("AT+CLBS=1,1");
  String res = "";
  uint32_t t = millis();
  while (millis() - t < 5000) {
    while (SerialAT.available()) res += (char)SerialAT.read();
    if (res.indexOf("+CLBS:") != -1) break;
  }

  int lastComma = res.lastIndexOf(",");
  if (lastComma != -1) {
    // Formato: ...precision,YYYY/MM/DD,HH:MM:SS
    int dateComma = res.lastIndexOf(",", lastComma - 1);
    if (dateComma != -1) {
      String datePart = res.substring(dateComma + 1, lastComma);
      String timePart = res.substring(lastComma + 1);
      timePart.trim();
      
      struct tm tm_info;
      memset(&tm_info, 0, sizeof(struct tm));
      int yy, mm, dd, hh, min, ss;
      if (sscanf(datePart.c_str(), "%d/%d/%d", &yy, &mm, &dd) == 3 &&
          sscanf(timePart.c_str(), "%d:%d:%d", &hh, &min, &ss) == 3) {
        tm_info.tm_year = yy - 1900;
        tm_info.tm_mon = mm - 1;
        tm_info.tm_mday = dd;
        tm_info.tm_hour = hh;
        tm_info.tm_min = min;
        tm_info.tm_sec = ss;

        setenv("TZ", "UTC0", 1);
        tzset();
        time_t t_now = mktime(&tm_info);
        struct timeval tv = { .tv_sec = t_now };
        settimeofday(&tv, NULL);
        Serial.printf("[TIME] Sincronización Celular Exitosa: %02d:%02d:%02d\n", hh, min, ss);
      }
    }
  }
}

void syncNetworkTime() {
  // 1. Intentar GPS
  if (gps_get_lat() != 0) {
    String gpsTime = gps_get_time();
    if (gpsTime.length() > 18) {
      struct tm tm_info;
      memset(&tm_info, 0, sizeof(struct tm));
      int yy, mm, dd, hh, min, ss;
      if (sscanf(gpsTime.c_str(), "%d-%d-%dT%d:%d:%d", &yy, &mm, &dd, &hh, &min, &ss) == 6) {
        tm_info.tm_year = yy - 1900;
        tm_info.tm_mon = mm - 1;
        tm_info.tm_mday = dd;
        tm_info.tm_hour = hh;
        tm_info.tm_min = min;
        tm_info.tm_sec = ss;
        
        setenv("TZ", "UTC0", 1);
        tzset();
        time_t t_now = mktime(&tm_info);
        struct timeval tv = { .tv_sec = t_now };
        settimeofday(&tv, NULL);
        Serial.println("[TIME] Sincronizado mediante GPS.");
        return;
      }
    }
  }

  // 2. Intentar Cell Network (Más rápido que HTTP)
  syncTimeCell();

  // 3. Intentar HTTP API
  if (time(NULL) < 1700000000) syncTimeHTTP();

  // 4. NTP Backup
  if (time(NULL) < 1700000000) syncTimeNTP();

  // 5. Final Backup: Módem (AT+CCLK)
  if (time(NULL) < 1700000000) {
    // Limpiar buffer antes de pedir la hora (como en OLD)
    while (SerialAT.available()) SerialAT.read();
    
    SerialAT.println("AT+CCLK?");
    String res = "";
    uint32_t t = millis();
    while (millis() - t < 1500) {
      while (SerialAT.available()) res += (char)SerialAT.read();
      if (res.indexOf("OK") != -1) break;
    }
    
    int start = res.indexOf("\"");
    int end = res.lastIndexOf("\"");
    if (start != -1 && end != -1 && end > start) {
      String cclk = res.substring(start + 1, end); 
      struct tm tm_info;
      memset(&tm_info, 0, sizeof(struct tm));
      int yy, mm, dd, hh, min, ss;
      if (sscanf(cclk.c_str(), "%d/%d/%d,%d:%d:%d", &yy, &mm, &dd, &hh, &min, &ss) == 6) {
        // Validar que no sea 00:00:00 (error transitorio de OLD)
        if (hh == 0 && min == 0 && ss == 0 && yy == 0) {
           Serial.println("[TIME] CCLK devolvió 00:00:00, descartando.");
           return;
        }
        
        tm_info.tm_year = yy + 100; 
        tm_info.tm_mon = mm - 1;
        tm_info.tm_mday = dd;
        tm_info.tm_hour = hh;
        tm_info.tm_min = min;
        tm_info.tm_sec = ss;
        
        setenv("TZ", "UTC0", 1);
        tzset();
        time_t t_now = mktime(&tm_info);
        struct timeval tv = { .tv_sec = t_now };
        settimeofday(&tv, NULL);
        Serial.println("[TIME] Sincronización por Módem completa.");
      }
    }
  }
}

void sendTelemetry() {
  // Verificar red antes de intentar enviar
  if (!checkNetwork()) {
    Serial.println(getTimestamp() + " [NET] No se pudo restablecer la red.");
    return;
  }

  int rssi = getRSSI();
  int bat = getBatteryLevel();
  
  // ---------- HTTP CONFIG ----------
  sendAT("AT+HTTPTERM");
  sendAT("AT+HTTPINIT");
#ifdef LILYGO_SIM7000G
  sendAT("AT+HTTPPARA=\"CID\",1");
  sendAT("AT+HTTPSSL=1");
  // Intentamos añadir cabeceras individuales si el firmware lo permite
  String h1 = "AT+HTTPPARA=\"HEADER\",\"apikey: " + String(SUPABASE_KEY) + "\"";
  sendAT(h1.c_str());
  String h2 = "AT+HTTPPARA=\"HEADER\",\"Authorization: Bearer " + String(SUPABASE_KEY) + "\"";
  sendAT(h2.c_str());
#else
  sendAT("AT+HTTPPARA=\"SSLCFG\",0");
#endif

  String fullUrl = String(SUPABASE_URL) + "?apikey=" + String(SUPABASE_KEY);
  String urlCmd = "AT+HTTPPARA=\"URL\",\"" + fullUrl + "\"";
  sendAT(urlCmd.c_str());

  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"");

  // ---------- BODY ----------
  float finalLat = gps_get_lat();
  float finalLon = gps_get_lon();
  
  String locType = "gps";
  // Si no hay GPS, intentamos LBS
  if (finalLat == 0) {
    updateLBS();
    finalLat = lbsLat;
    finalLon = lbsLon;
    locType = "lbs";
  }
  
  String timestamp = getCurrentISO8601();
  
  // Calcular duración si el trayecto está activo
  if (isTripActive) {
    time_t now_unix;
    time(&now_unix);
    tripDuration = (uint32_t)(now_unix - tripStartTime);
  }

  String body = "{\"motorcycle_id\":\"" VEHICLE_ID "\",\"speed\":" + String(gps_get_speed()) + 
                ",\"battery_level\":" + String(bat) + ",\"latitude\":" + String(finalLat, 6) + 
                ",\"longitude\":" + String(finalLon, 6) + 
                ",\"signal_strength\":" + String(rssi) +
                ",\"location_type\":\"" + locType + "\"" +
                ",\"is_trip_active\":" + (isTripActive ? "true" : "false") +
                ",\"is_theft_active\":" + (isTheftEventActive ? "true" : "false") +
                ",\"trip_duration\":" + String(tripDuration) +
                ",\"duration\":" + String(tripDuration) +
                ",\"start_time\":" + (tripStartTime > 0 ? "\"" + formatISO8601(tripStartTime) + "\"" : "null") +
                ",\"trip_start\":" + (tripStartTime > 0 ? "\"" + formatISO8601(tripStartTime) + "\"" : "null") +
                ",\"end_time\":" + (tripEndTime > 0 ? "\"" + formatISO8601(tripEndTime) + "\"" : "null") +
                ",\"trip_end\":" + (tripEndTime > 0 ? "\"" + formatISO8601(tripEndTime) + "\"" : "null");
  
  if (timestamp != "") {
    body += ",\"timestamp\":\"" + timestamp + "\"";
    body += ",\"date\":\"" + timestamp.substring(0, 10) + "\"";
  }
  
  if (batA.soc != -1) {
    body += ",\"moto_battery\":" + String(batA.soc);
    body += ",\"bat_a_volts\":" + String(batA.voltage);
    body += ",\"bat_a_amps\":" + String(batA.current);
    body += ",\"bat_a_temp\":" + String(batA.temp);
    body += ",\"is_charging\":" + String(batA.is_charging ? "true" : "false");
  }
  if (batB.soc != -1) {
    body += ",\"moto_battery_b\":" + String(batB.soc);
    body += ",\"bat_b_volts\":" + String(batB.voltage);
    body += ",\"bat_b_amps\":" + String(batB.current);
    body += ",\"bat_b_temp\":" + String(batB.temp);
    body += ",\"is_charging_b\":" + String(batB.is_charging ? "true" : "false");
  }
  
  body += "}"; 

  Serial.println(getTimestamp() + " [SEND] " + body);

  String dcmd = "AT+HTTPDATA=" + String(body.length()) + ",5000";
  sendAT(dcmd.c_str()); 
  SerialAT.print(body);
  delay(500);

  // ---------- POST ----------
  Serial.println(getTimestamp() + " [HTTP] Iniciando POST...");
  SerialAT.println("AT+HTTPACTION=1");
  
  // Esperar activamente al código de respuesta (+HTTPACTION: 1,XXX,LEN)
  String actionRes = "";
  uint32_t tAction = millis();
  while (millis() - tAction < 15000) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      actionRes += c;
      Serial.write(c);
    }
    if (actionRes.indexOf("+HTTPACTION:") != -1 && actionRes.indexOf("\n", actionRes.indexOf("+HTTPACTION:")) != -1) break;
  }

  // Si hubo error 400, leer el cuerpo de la respuesta obligatoriamente
  if (actionRes.indexOf(",400,") != -1) {
    int firstComma = actionRes.indexOf(",400,");
    int secondComma = actionRes.indexOf(",", firstComma + 5);
    String lenStr = actionRes.substring(firstComma + 5, secondComma);
    lenStr.trim();
    
    Serial.printf("\n[DEBUG] Error 400 detectado (%s bytes). Motivo: ", lenStr.c_str());
    String readCmd = "AT+HTTPREAD=0," + lenStr;
    sendAT(readCmd.c_str(), 3000);
  }
  
  sendAT("AT+HTTPTERM");

  Serial.println(getTimestamp() + " [OK] Telemetría enviada.");
}

void canBusTask(void *pvParameters) {
  uint32_t lastHeartbeat = 0;
  for(;;) {
    can_update(); // Recibe y decodifica continuamente
    
    if (millis() - lastHeartbeat > 5000) {
      Serial.println("[TASK] CAN Core 0 OK - Escuchando bus...");
      lastHeartbeat = millis();
    }
    
    /* 
    // Envío de latido de hora (ID 0x510) cada 200ms (como en OLD)
    if (millis() - lastTaskCanTimeSend > 200) {
      time_t now;
      struct tm ti;
      time(&now);
      localtime_r(&now, &ti);
      
      if (ti.tm_year > 120) {
        can_send_time((uint8_t)ti.tm_hour, (uint8_t)ti.tm_min, (uint8_t)ti.tm_sec);
      }
      lastTaskCanTimeSend = millis();
    }
    */
    vTaskDelay(pdMS_TO_TICKS(10)); // Pequeña pausa para no saturar el core
  }
}

uint32_t lastCanConfigLoad = 0;
const uint32_t canConfigLoadInterval = 21600000;

void logPointToSD(String filename, float lat, float lon, float speed, int bat) {
  if (!SD.exists("/trips")) SD.mkdir("/trips");
  
  File file = SD.open(filename, FILE_APPEND);
  if (file) {
    file.printf("%.6f,%.6f,%.1f,%d\n", lat, lon, speed, bat);
    file.close();
  }
}

String readPathFromSD(String filename) {
  File file = SD.open(filename, FILE_READ);
  if (!file) return "";

  String path = "";
  int count = 0;
  // Para evitar saturar RAM, si el archivo es muy grande, saltamos puntos
  long fileSize = file.size();
  int skip = 1;
  if (fileSize > 5000) skip = 2; // Más de ~150 puntos, leemos la mitad
  if (fileSize > 10000) skip = 4; // Más de ~300 puntos, leemos 1/4

  while (file.available()) {
    String line = file.readStringUntil('\n');
    if (count % skip == 0) {
      int firstComma = line.indexOf(',');
      int secondComma = line.indexOf(',', firstComma + 1);
      if (firstComma != -1 && secondComma != -1) {
        String lat = line.substring(0, firstComma);
        String lon = line.substring(firstComma + 1, secondComma);
        if (path != "") path += ",";
        path += "[" + lat + "," + lon + "]";
      }
    }
    count++;
  }
  file.close();
  return path;
}

void checkPendingTrips() {
  if (!checkNetwork()) return;
  
  File root = SD.open("/trips");
  if (!root) return;
  
  File file = root.openNextFile();
  while (file) {
    String name = file.name();
    // Si es un archivo CSV y no es el que estamos usando ahora
    if (name.endsWith(".csv") && 
        "/trips/" + name != currentTripFile && 
        "/trips/" + name != currentTheftFile) {
      
      Serial.println("[SD] Detectado viaje pendiente: " + name);
      // Aquí se podría implementar una versión simplificada de sendTripSummary 
      // que lea el CSV y lo envíe. Por ahora, para no complicar el flujo
      // solo avisamos. En una fase posterior podemos automatizar la subida.
    }
    file = root.openNextFile();
  }
}

void initSD() {
  Serial.println("[SD] Inicializando SPI...");
  SPI.begin(BOARD_SCK_PIN, BOARD_MISO_PIN, BOARD_MOSI_PIN);
  if (!SD.begin(BOARD_SD_CS_PIN)) {
    Serial.println("[SD] Error: No se pudo montar la tarjeta.");
  } else {
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
      Serial.println("[SD] Error: No hay tarjeta insertada.");
      return;
    }
    Serial.printf("[SD] Tarjeta montada (Tipo: %d). Tamaño: %llu MB\n", cardType, SD.cardSize() / (1024 * 1024));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  initDefaultCanConfig();

  Serial.println("\n" + getTimestamp() + " [BOOT] CanRiderONE");

  // ---------- POWER MODEM ----------
#ifdef BOARD_POWERON_PIN
  pinMode(BOARD_POWERON_PIN, OUTPUT);
  digitalWrite(BOARD_POWERON_PIN, HIGH);
#endif
  
  pinMode(CAN_ACTIVITY_LED_PIN, OUTPUT);
  digitalWrite(CAN_ACTIVITY_LED_PIN, LOW); // LED apagado inicialmente

  initSD();

  pinMode(BOARD_PWRKEY_PIN, OUTPUT);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);
  delay(100);
  digitalWrite(BOARD_PWRKEY_PIN, HIGH);
  delay(2000);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);

#ifndef MODEM_BAUDRATE
#define MODEM_BAUDRATE 115200
#endif
  SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  gps_setup();
  
  if (can_setup(CAN_RX_PIN, CAN_TX_PIN)) {
    Serial.println(getTimestamp() + " [CAN] OK. Ejecutando en Loop principal...");
    // xTaskCreatePinnedToCore(canBusTask, "CAN_Task", 4096, NULL, 5, NULL, 0); // Core 0 desactivado para test
  } else {
    Serial.println(getTimestamp() + " [ERROR] Falló inicialización CAN");
  }

  delay(8000);

  // ---------- AT SYNC ----------
  for (int i = 0; i < 10; i++) {
    SerialAT.println("AT");
    delay(500);
    if (SerialAT.available()) break;
  }

  sendAT("ATE0");
  sendAT("AT+CPIN?");
#ifdef LILYGO_SIM7000G
  sendAT("AT+CGNSPWR=1"); // Encender GPS interno
#endif

  // ---------- NETWORK ----------
  sendAT("AT+CTZU=1"); // Automatic Time Zone Update
#ifdef LILYGO_SIM7000G
  sendAT("AT+CNACT=1,\"" APN "\"", 5000);
#else
  sendAT("AT+CGDCONT=1,\"IP\",\"" APN "\"");
  sendAT("AT+CGACT=1,1");
  sendAT("AT+NETOPEN", 5000);
#endif
  delay(2000);
#ifdef LILYGO_SIM7000G
  sendAT("AT+CNACT?");
#else
  sendAT("AT+IPADDR");
#endif
  sendAT("AT+CDNSCFG=\"8.8.8.8\",\"1.1.1.1\"");
  sendAT("AT+CLBSCFG=1,3");             
  
  syncNetworkTime();
  delay(2000);

#ifndef LILYGO_SIM7000G
  sendAT("AT+HTTPTERM", 3000);
#endif
#ifdef LILYGO_SIM7000G
  sendAT("AT+CSSLCFG=\"sslversion\",1,3");      // TLS 1.2, Perfil 1
  sendAT("AT+CSSLCFG=\"authmode\",1,0");        // Sin CA
  sendAT("AT+CSSLCFG=\"ignorertctime\",1,1");   // Sin RTC
  sendAT("AT+CSSLCFG=\"sni\",1,\"jmisxaxqwtkudvkytkha.supabase.co\"");
#else
  sendAT("AT+CSSLCFG=\"sslversion\",0,4");      // TLS 1.2
  sendAT("AT+CSSLCFG=\"authmode\",0,0");        // Sin CA
  sendAT("AT+CSSLCFG=\"enableSNI\",0,1");       // Obligatorio
  sendAT("AT+CSSLCFG=\"ignorelocaltime\",0,1"); // Sin RTC
  sendAT("AT+CSSLCFG=\"sni\",1,\"jmisxaxqwtkudvkytkha.supabase.co\"");
#endif

  loadCanConfigFromSupabase(VEHICLE_ID);
  lastCanConfigLoad = millis();

  Serial.println("\n" + getTimestamp() + " [READY] Sistema iniciado.");
}

uint32_t lastSend = 0;
const uint32_t sendInterval = 10000;
uint32_t lastCanTimeSend = 0;
const uint32_t canTimeInterval = 200;
uint32_t lastTimeSync = 0;
const uint32_t syncInterval = 3600000;
uint32_t lastGeneralActivity = 0;

void enterDeepSleep(const char* reason, uint32_t seconds = 0) {
  Serial.println("\n" + getTimestamp() + " [SLEEP] " + reason);
  
  // Apagar módem de forma controlada
  sendAT("AT+CPOWD=1"); 
  delay(2000);
#ifdef BOARD_POWERON_PIN
  digitalWrite(BOARD_POWERON_PIN, LOW);
#endif

  if (seconds > 0) {
    Serial.printf("Despertando en %u segundos para reporte de seguridad...\n", seconds);
    esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
  } else {
    Serial.println("Apagado total por batería crítica.");
  }
  
  esp_deep_sleep_start();
}

void sendTheftEvent() {
  Serial.printf("\n[THEFT_SEND] Enviando evento... Distancia: %.2f km\n", theftDistance);
  
  if (theftDistance < THEFT_MIN_DISTANCE_SEND) {
    Serial.println("[THEFT_SEND] Distancia insuficiente para confirmar robo, descartando");
    return;
  }
  
  if (!checkNetwork()) {
    Serial.println("[THEFT_SEND] Sin red, no se puede enviar");
    return;
  }

  // Asegurar que el último punto esté en el recorrido si ha cambiado
  if (lastTheftLat != 0 && (theftPointsCount == 0 || lastPathPointTime != millis())) {
    theftPathJson += ",[" + String(lastTheftLat, 6) + "," + String(lastTheftLon, 6) + "]";
    theftPointsCount++;
  }

  time_t now;
  time(&now);

  int endBat = (batA.soc != -1) ? batA.soc : ((batB.soc != -1) ? batB.soc : -1);
  Serial.printf("[THEFT_SEND] Vehículo: %s | Bat: %d→%d%% | Distancia: %.2fkm | Puntos: %d\n", 
    VEHICLE_ID, theftStartBat, endBat, theftDistance, theftPointsCount);
  
  sendAT("AT+HTTPTERM", 3000);
  sendAT("AT+HTTPINIT", 3000);
#ifdef LILYGO_SIM7000G
  sendAT("AT+HTTPPARA=\"CID\",1", 3000);
  sendAT("AT+HTTPSSL=1", 3000);
#else
  sendAT("AT+HTTPPARA=\"SSLCFG\",0", 3000);
#endif

  // Enviar a trips como recorrido normal marcado como robo
  String url = String(SUPABASE_URL);
  url.replace("telemetry", "trips");
  url += "?apikey=" + String(SUPABASE_KEY);
  String urlCmd = "AT+HTTPPARA=\"URL\",\"" + url + "\"";
  sendAT(urlCmd.c_str(), 3000);
  
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 3000);

  // Priorizar recorrido completo desde SD
  String fullPath = readPathFromSD(currentTheftFile);
  if (fullPath == "") fullPath = theftPathJson; // Fallback a RAM

  String tripBody = "{\"motorcycle_id\":\"" VEHICLE_ID "\",\"start_time\":\"" + formatISO8601(theftStartTime) + 
                    "\",\"end_time\":\"" + formatISO8601(now) + 
                    "\",\"distance\":" + String(theftDistance, 2) +
                    ",\"avg_speed\":" + String(theftMaxSpeed / 2, 2) +
                    ",\"max_speed\":" + String(theftMaxSpeed, 2) +
                    ",\"consumption\":" + String(max(0, theftStartBat - endBat)) +
                    ",\"start_battery_level\":" + String(theftStartBat) +
                    ",\"end_battery_level\":" + String(endBat) +
                    ",\"path\":[" + fullPath + "]" +
                    ",\"is_theft_detected\":true}";

  Serial.println(getTimestamp() + " [TRIP_INSERT] " + tripBody.substring(0, 150));

  String dcmd = "AT+HTTPDATA=" + String(tripBody.length()) + ",5000";
  sendAT(dcmd.c_str(), 3000);
  SerialAT.print(tripBody);
  delay(500);

  SerialAT.println("AT+HTTPACTION=1");
  uint32_t t = millis();
  String actionRes = "";
  while (millis() - t < 15000) {
    while (SerialAT.available()) {
      actionRes += (char)SerialAT.read();
    }
    if (actionRes.indexOf("+HTTPACTION:") != -1) break;
  }
  
  sendAT("AT+HTTPTERM", 3000);
  Serial.println(getTimestamp() + " [TRIP_INSERT] Trip guardado en Supabase");
}

void sendTripSummary() {
  if (tripPointsCount == 0) return;
  if (!checkNetwork()) return;

  time_t now;
  time(&now);
  uint32_t duration = difftime(now, tripStartTime);
  float avgSpeed = tripTotalSpeed / tripPointsCount;
  
  int endBatA = batA.soc;
  int endBatB = batB.soc;
  int consumptionA = (tripStartBatA != -1 && endBatA != -1) ? (tripStartBatA - endBatA) : 0;
  int consumptionB = (tripStartBatB != -1 && endBatB != -1) ? (tripStartBatB - endBatB) : 0;

  // Asegurar que el último punto esté en el recorrido
  if (lastTripLat != 0 && (tripPointsStored == 0 || lastTripPathPointTime != millis())) {
    tripPathJson += ",[" + String(lastTripLat, 6) + "," + String(lastTripLon, 6) + "]";
    tripPointsStored++;
  }

  // ---------- HTTP CONFIG ----------
  sendAT("AT+HTTPTERM");
  sendAT("AT+HTTPSSL=1");
sendAT("AT+HTTPPARA=\"CID\",1");
sendAT("AT+HTTPPARA=\"SSLCFG\",1");
  sendAT("AT+HTTPINIT");
#ifdef LILYGO_SIM7000G
  sendAT("AT+HTTPPARA=\"CID\",1");
  sendAT("AT+HTTPSSL=1");
#else
  sendAT("AT+HTTPPARA=\"SSLCFG\",0");
#endif

  String url = String(SUPABASE_URL);
  url.replace("telemetry", "trips");
  url += "?apikey=";
  url += SUPABASE_KEY;
  String urlCmd = "AT+HTTPPARA=\"URL\",\"" + url + "\"";
  sendAT(urlCmd.c_str());

  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"");
#ifdef LILYGO_SIM7000G
  String authHeader = "Authorization: Bearer " + String(SUPABASE_KEY);
  String ud = "AT+HTTPPARA=\"HEADER\",\"" + authHeader + "\"";
#else
  String ud = "AT+HTTPPARA=\"USERDATA\",\"Authorization: Bearer " + String(SUPABASE_KEY) + "\"";
#endif
  sendAT(ud.c_str());

  // Priorizar recorrido completo desde SD
  String fullPath = readPathFromSD(currentTripFile);
  if (fullPath == "") fullPath = tripPathJson; // Fallback a RAM

  String body = "{\"motorcycle_id\":\"" VEHICLE_ID "\",\"start_time\":" + (tripStartTime > 0 ? "\"" + formatISO8601(tripStartTime) + "\"" : "null") + 
                ",\"trip_start\":" + (tripStartTime > 0 ? "\"" + formatISO8601(tripStartTime) + "\"" : "null") + 
                ",\"end_time\":" + (now > 0 ? "\"" + formatISO8601(now) + "\"" : "null") + 
                ",\"trip_end\":" + (now > 0 ? "\"" + formatISO8601(now) + "\"" : "null") + 
                ",\"distance\":" + String(tripDistance, 2) + 
                ",\"avg_speed\":" + String(avgSpeed, 2) + 
                ",\"duration\":" + String(duration) + 
                ",\"trip_duration\":" + String(duration) + 
                ",\"consumption\":" + String(consumptionA + consumptionB) + 
                ",\"start_battery_level\":" + String(tripStartBatA) + 
                ",\"end_battery_level\":" + String(endBatA) + 
                ",\"path\":[" + fullPath + "]}";

  Serial.println(getTimestamp() + " [TRIP_END] " + body);

  String dcmd = "AT+HTTPDATA=" + String(body.length()) + ",5000";
  sendAT(dcmd.c_str()); 
  SerialAT.print(body);
  delay(500);

  sendAT("AT+HTTPACTION=1", 15000);
  sendAT("AT+HTTPTERM");
}

float calculateDistance(float lat1, float lon1, float lat2, float lon2) {
  if (lat1 == 0 || lat2 == 0) return 0;
  float dLat = (lat2 - lat1) * 0.0174532925f;
  float dLon = (lon2 - lon1) * 0.0174532925f;
  float a = sinf(dLat/2) * sinf(dLat/2) + cosf(lat1*0.0174532925f) * cosf(lat2*0.0174532925f) * sinf(dLon/2) * sinf(dLon/2);
  float c = 2 * atan2f(sqrtf(a), sqrtf(1-a));
  return 6371.0f * c; 
}

void loop() {
  can_update(); 
  gps_update(); 

  // Variable de batería para uso en toda la función loop
  int currentBat = (batA.soc != -1) ? batA.soc : ((batB.soc != -1) ? batB.soc : -1);

  // Asegurar que el LED se apague si no hay actividad CAN reciente
  if (millis() - lastCanActivityTime > 100) {
    digitalWrite(CAN_ACTIVITY_LED_PIN, LOW);
  }

  // Verificar actividad CAN (lastCanActivityTime se actualiza en can_decoder.h)
  bool hasCanData = (lastCanActivityTime > 0 && (millis() - lastCanActivityTime < 5000));

  if (hasCanData) {
    // INICIO DE TRAYECTO
    if (!isTripActive) {
      float startLat = gps_get_lat();
      float startLon = gps_get_lon();
      
      if (startLat == 0) {
        updateLBS();
        startLat = lbsLat;
        startLon = lbsLon;
      }

      isTripActive = true;
      time(&tripStartTime);
      tripEndTime = 0; // Resetear fin
      tripStartLat = startLat;
      tripStartLon = startLon;
      tripStartBatA = (batA.soc != -1) ? batA.soc : -1;
      tripStartBatB = (batB.soc != -1) ? batB.soc : -1;
      tripTotalSpeed = 0;
      tripPointsCount = 0;
      tripDistance = 0;
      lastTripLat = startLat;
      lastTripLon = startLon;
      
      // Iniciar JSON del recorrido
      tripPathJson = "[" + String(startLat, 6) + "," + String(startLon, 6) + "]";
      tripPointsStored = 1;
      lastTripPathPointTime = millis();
      
      // Crear archivo en SD
      currentTripFile = "/trips/T_" + String(tripStartTime) + ".csv";
      logPointToSD(currentTripFile, startLat, startLon, gps_get_speed(), tripStartBatA);

      Serial.println("\n" + getTimestamp() + " [TRIP] Trayecto INICIADO. SD: " + currentTripFile);
    }

    // ACUMULACIÓN DE DATOS
    float currentLat = gps_get_lat();
    float currentLon = gps_get_lon();
    float currentSpeed = gps_get_speed();
    currentBat = (batA.soc != -1) ? batA.soc : ((batB.soc != -1) ? batB.soc : -1);

    if (currentLat != 0) {
      if (lastTripLat != 0) {
        float dist = calculateDistance(lastTripLat, lastTripLon, currentLat, currentLon);
        if (dist > 0.001) { // 1 metro
          tripDistance += dist;
          lastTripLat = currentLat;
          lastTripLon = currentLon;
          
          // Guardar en SD siempre que haya movimiento relevante para tener máxima resolución
          static uint32_t lastSDWrite = 0;
          if (millis() - lastSDWrite > 10000) { // Máximo cada 10s en SD
            logPointToSD(currentTripFile, currentLat, currentLon, currentSpeed, currentBat);
            lastSDWrite = millis();
          }
        }
      } else {
        lastTripLat = currentLat;
        lastTripLon = currentLon;
      }

      // Almacenar punto de recorrido en RAM (resumen rápido)
      if (millis() - lastTripPathPointTime > tripPathPointInterval && tripPointsStored < MAX_TRIP_POINTS) {
        tripPathJson += ",[" + String(currentLat, 6) + "," + String(currentLon, 6) + "]";
        tripPointsStored++;
        lastTripPathPointTime = millis();
      }
    }
    
    tripTotalSpeed += currentSpeed;
    tripPointsCount++;
    lastGeneralActivity = millis(); // Hay actividad CAN
  }

  // FIN DE TRAYECTO (1 minuto sin CAN)
  if (isTripActive && (millis() - lastCanActivityTime > 60000)) {
    Serial.println("\n" + getTimestamp() + " [TRIP] Trayecto FINALIZADO por inactividad CAN.");
    time(&tripEndTime);
    tripDuration = (uint32_t)(tripEndTime - tripStartTime);
    sendTripSummary();
    isTripActive = false;
  }

  // PROTECCIÓN CRÍTICA (Batería muerta, apagado total)
  if ((batA.soc != -1 && batA.soc <= 10) || (batB.soc != -1 && batB.soc <= 10)) {
    enterDeepSleep("Batería CRÍTICA (<10%) - Apagado total");
  }
  
  // 2. DETECCIÓN DE MOVIMIENTO / ROBO
  bool isMoving = gps_get_speed() >= THEFT_SPEED_THRESHOLD;
  bool isCanActive = (lastCanActivityTime > 0 && (millis() - lastCanActivityTime < 5000));
  
  if (isMoving) {
    lastGeneralActivity = millis();
  }
  
  // ANTI-ROBO: Movimiento sin CAN activo
  if (isMoving && !isCanActive) {
    float currentLat = gps_get_lat();
    float currentLon = gps_get_lon();
    float currentSpeed = gps_get_speed();

    if (!isTheftEventActive) {
      isTheftEventActive = true;
      time(&theftStartTime);
      theftStartLat = currentLat;
      theftStartLon = currentLon;
      theftStartBat = (batA.soc != -1) ? batA.soc : ((batB.soc != -1) ? batB.soc : -1);
      theftMaxSpeed = currentSpeed;
      theftDistance = 0;
      lastTheftLat = currentLat;
      lastTheftLon = currentLon;
      lastMovementTime = millis();
      
      // Iniciar JSON del recorrido
      theftPathJson = "[" + String(currentLat, 6) + "," + String(currentLon, 6) + "]";
      theftPointsCount = 1;
      lastPathPointTime = millis();

      // Iniciar SD para robo
      currentTheftFile = "/trips/R_" + String(theftStartTime) + ".csv";
      logPointToSD(currentTheftFile, currentLat, currentLon, currentSpeed, theftStartBat);

      Serial.printf("\n[THEFT_DETECTED] Posible ROBO!!! GPS: %.6f, %.6f | SD: %s\n", 
        theftStartLat, theftStartLon, currentTheftFile.c_str());
    } else {
      // Actualizar datos del evento
      if (currentLat != 0 && lastTheftLat != 0) {
        float dist = calculateDistance(lastTheftLat, lastTheftLon, currentLat, currentLon);
        if (dist > 0.001) { // Solo si se ha movido algo relevante (1 metro)
          theftDistance += dist;
          lastTheftLat = currentLat;
          lastTheftLon = currentLon;
          lastMovementTime = millis();
          
          // Registro alta frecuencia en SD
          static uint32_t lastSDTheftWrite = 0;
          if (millis() - lastSDTheftWrite > 5000) { // Cada 5s en robo
             logPointToSD(currentTheftFile, currentLat, currentLon, currentSpeed, currentBat);
             lastSDTheftWrite = millis();
          }
        }
      }
      
      if (currentSpeed > theftMaxSpeed) {
        theftMaxSpeed = currentSpeed;
      }

      // Almacenar punto de recorrido cada X segundos
      if (millis() - lastPathPointTime > pathPointInterval && theftPointsCount < MAX_THEFT_POINTS) {
        theftPathJson += ",[" + String(currentLat, 6) + "," + String(currentLon, 6) + "]";
        theftPointsCount++;
        lastPathPointTime = millis();
      }
    }
  }
  
  // FIN DE EVENTO DE ROBO (2 min sin movimiento)
  if (isTheftEventActive && (millis() - lastMovementTime > movementTimeoutTheft)) {
    Serial.println("\n" + getTimestamp() + " [THEFT_END] Evento de robo finalizado");
    sendTheftEvent();
    isTheftEventActive = false;
  }

  // 3. REPOSO INTELIGENTE (Inactividad de 5 minutos)
  if (millis() > 300000 && (millis() - lastGeneralActivity > 300000)) {
    // Si la batería está sana (>15%), despertamos cada 30 min para seguridad
    uint32_t sleepTime = 1800; // 30 minutos
    enterDeepSleep("Inactividad detectada - Modo Vigilancia", sleepTime);
  }

  // Re-sincronizar con la red GSM cada hora
  if (millis() - lastTimeSync > syncInterval) {
    syncNetworkTime();
    lastTimeSync = millis();
  }

  // Recargar configuración CAN cada 6 horas
  if (millis() - lastCanConfigLoad > canConfigLoadInterval) {
    loadCanConfigFromSupabase(VEHICLE_ID);
    lastCanConfigLoad = millis();
  }

  // Enviar trama de hora cada 200ms
  if (millis() - lastCanTimeSend > canTimeInterval) {
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);
    
    if (ti.tm_year > 120) { // Solo si la hora está sincronizada
      can_send_time((uint8_t)ti.tm_hour, (uint8_t)ti.tm_min, (uint8_t)ti.tm_sec);
    }
    lastCanTimeSend = millis();
  }

  if (millis() - lastSend > sendInterval) {
    sendTelemetry();
    lastSend = millis();
  }

  // Debug bridge
  while (SerialAT.available()) Serial.write(SerialAT.read());
  while (Serial.available()) SerialAT.write(Serial.read());
}
