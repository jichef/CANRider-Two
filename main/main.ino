#define LILYGO_T_A7670
#include <time.h>
#include <sys/time.h>
#include "AT/utilities.h"
#include "config.h"
#include "gps.h"
#include "can_decoder.h"

// Definir pines CAN si no están definidos
#ifndef CAN_RX_PIN
#define CAN_RX_PIN 32
#endif
#ifndef CAN_TX_PIN
#define CAN_TX_PIN 33
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

void syncNetworkTime() {
  SerialAT.println("AT+CCLK?");
  String res = "";
  uint32_t t = millis();
  while (millis() - t < 1000) {
    while (SerialAT.available()) res += (char)SerialAT.read();
  }
  
  // Formato: +CCLK: "25/02/08,21:16:29+04"
  int start = res.indexOf("\"");
  int end = res.lastIndexOf("\"");
  if (start != -1 && end != -1 && end > start) {
    String cclk = res.substring(start + 1, end);
    
    struct tm tm;
    tm.tm_year = 100 + cclk.substring(0, 2).toInt(); // 2000 + yy - 1900
    tm.tm_mon  = cclk.substring(3, 5).toInt() - 1;   // 0-11
    tm.tm_mday = cclk.substring(6, 8).toInt();
    tm.tm_hour = cclk.substring(9, 11).toInt();
    tm.tm_min  = cclk.substring(12, 14).toInt();
    tm.tm_sec  = cclk.substring(15, 17).toInt();
    
    time_t t_now = mktime(&tm);
    
    // Aplicamos el ajuste de -8h detectado para sincronizar con la red local
    t_now -= (8 * 3600); 
    
    struct timeval tv = { .tv_sec = t_now };
    settimeofday(&tv, NULL);

    // Configurar Zona Horaria de España (CET/CEST)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    
    Serial.println("\n[TIME] Reloj ESP32 sincronizado (Horario España).");
  }
}

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

String getCurrentISO8601() {
  String gpsTime = gps_get_time();
  if (gpsTime != "") return gpsTime;

  time_t now;
  struct tm ti;
  time(&now);
  gmtime_r(&now, &ti);
  
  if (ti.tm_year > 120) {
    char buf[25];
    // Usamos gmtime_r y 'Z' para enviar UTC real. 
    // El navegador lo convertirá a la hora local del usuario automáticamente.
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &ti);
    return String(buf);
  }
  return "";
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
  SerialAT.println("AT+NETOPEN?");
  String res = "";
  uint32_t t = millis();
  while (millis() - t < 500) {
    while (SerialAT.available()) res += (char)SerialAT.read();
  }
  
  if (res.indexOf("+NETOPEN: 1") != -1) return true;
  
  Serial.println("\n" + getTimestamp() + " [NET] Red caída o cerrada. Intentando abrir...");
  sendAT("AT+NETOPEN", 5000);
  delay(2000);
  
  // Verificar si se abrió tras el intento
  SerialAT.println("AT+NETOPEN?");
  res = "";
  t = millis();
  while (millis() - t < 500) {
    while (SerialAT.available()) res += (char)SerialAT.read();
  }
  return (res.indexOf("+NETOPEN: 1") != -1);
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
  sendAT("AT+HTTPPARA=\"SSLCFG\",0");

  String fullUrl = String(SUPABASE_URL) + "?apikey=" + String(SUPABASE_KEY);
  String urlCmd = "AT+HTTPPARA=\"URL\",\"" + fullUrl + "\"";
  sendAT(urlCmd.c_str());

  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"");

  String ud = "AT+HTTPPARA=\"USERDATA\",\"Authorization: Bearer " + String(SUPABASE_KEY) + "\"";
  sendAT(ud.c_str());

  // ---------- BODY ----------
  gps_update(); 
  // can_update() ahora se gestiona exclusivamente en la tarea canBusTask (Core 0)
  
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

  String body = "{\"motorcycle_id\":\"" VEHICLE_ID "\",\"speed\":" + String(gps_get_speed()) + 
                ",\"battery_level\":" + String(bat) + ",\"latitude\":" + String(finalLat, 6) + 
                ",\"longitude\":" + String(finalLon, 6) + 
                ",\"signal_strength\":" + String(rssi) +
                ",\"location_type\":\"" + locType + "\"";
  
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
  sendAT("AT+HTTPACTION=1", 15000);
  
  // Leer respuesta en caso de error para diagnóstico
  SerialAT.println("AT+HTTPREAD");
  uint32_t t = millis();
  while (millis() - t < 2000) {
    while (SerialAT.available()) Serial.write(SerialAT.read());
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
    
    // Envío de hora desactivado por petición del usuario
    /*
    if (millis() - lastTaskCanTimeSend > 1000) {
      time_t now;
      struct tm ti;
      time(&now);
      localtime_r(&now, &ti);
      
      if (ti.tm_year > 120) {
        can_send_time((uint8_t)ti.tm_hour, (uint8_t)ti.tm_min);
      }
      lastTaskCanTimeSend = millis();
    }
    */
    vTaskDelay(pdMS_TO_TICKS(10)); // Pequeña pausa para no saturar el core
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Configuración ADC para batería
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  Serial.println("\n" + getTimestamp() + " [BOOT] CanRiderONE");

  // ---------- POWER MODEM ----------
  pinMode(BOARD_POWERON_PIN, OUTPUT);
  digitalWrite(BOARD_POWERON_PIN, HIGH);

  pinMode(BOARD_PWRKEY_PIN, OUTPUT);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);
  delay(100);
  digitalWrite(BOARD_PWRKEY_PIN, HIGH);
  delay(2000);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);

  SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  gps_setup();
  
  if (can_setup(CAN_RX_PIN, CAN_TX_PIN)) {
    Serial.println(getTimestamp() + " [CAN] OK. Iniciando Tarea Independiente...");
    xTaskCreatePinnedToCore(canBusTask, "CAN_Task", 4096, NULL, 5, NULL, 0); // Core 0
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

  // ---------- NETWORK ----------
  sendAT("AT+CTZU=1"); // Automatic Time Zone Update
  sendAT("AT+CGDCONT=1,\"IP\",\"" APN "\"");
  sendAT("AT+CGACT=1,1");
  sendAT("AT+NETOPEN", 5000);
  delay(2000);
  sendAT("AT+IPADDR");
  sendAT("AT+CNTP=\"pool.ntp.org\",0"); // Opcional: Sincronizar NTP también
  sendAT("AT+CLBSCFG=1,3");             // Configurar LBS para máxima precisión
  syncNetworkTime();
  sendAT("AT+CDNSCFG=\"8.8.8.8\",\"8.8.4.4\"");

  // ---------- SSL ----------
  sendAT("AT+CSSLCFG=\"sslversion\",0,4");      // TLS 1.2
  sendAT("AT+CSSLCFG=\"authmode\",0,0");        // Sin CA
  sendAT("AT+CSSLCFG=\"enableSNI\",0,1");       // Obligatorio
  sendAT("AT+CSSLCFG=\"ignorelocaltime\",0,1"); // Sin RTC

  Serial.println("\n" + getTimestamp() + " [READY] Sistema iniciado.");
}

uint32_t lastSend = 0;
const uint32_t sendInterval = 10000; // Enviar telemetría cada 10 segundos
uint32_t lastCanTimeSend = 0; // Para el loop principal (si se usa)
const uint32_t canTimeInterval = 200; // Enviar trama CAN cada 200ms
uint32_t lastTimeSync = 0;
const uint32_t syncInterval = 3600000; // Re-sincronizar hora cada 1 hora
uint32_t lastCanActivity = 0;

void enterDeepSleep(const char* reason, uint32_t seconds = 0) {
  Serial.println("\n" + getTimestamp() + " [SLEEP] " + reason);
  
  // Apagar módem de forma controlada
  sendAT("AT+CPOWD=1"); 
  delay(2000);
  digitalWrite(BOARD_POWERON_PIN, LOW);

  if (seconds > 0) {
    Serial.printf("Despertando en %u segundos para reporte de seguridad...\n", seconds);
    esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
  } else {
    Serial.println("Apagado total por batería crítica.");
  }
  
  esp_deep_sleep_start();
}

// Variables para seguimiento de trayectos
bool isTripActive = false;
time_t tripStartTime = 0;
float tripStartLat = 0, tripStartLon = 0;
int tripStartBatA = -1, tripStartBatB = -1;
float tripTotalSpeed = 0;
uint32_t tripPointsCount = 0;
float tripDistance = 0;
float lastTripLat = 0, lastTripLon = 0;

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

  // ---------- HTTP CONFIG ----------
  sendAT("AT+HTTPTERM");
  sendAT("AT+HTTPINIT");
  sendAT("AT+HTTPPARA=\"SSLCFG\",0");

  String url = String(SUPABASE_URL);
  url.replace("telemetry", "trips");
  url += "?apikey=";
  url += SUPABASE_KEY;
  String urlCmd = "AT+HTTPPARA=\"URL\",\"" + url + "\"";
  sendAT(urlCmd.c_str());

  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"");
  String ud = "AT+HTTPPARA=\"USERDATA\",\"Authorization: Bearer " + String(SUPABASE_KEY) + "\"";
  sendAT(ud.c_str());

  char startTimeStr[25];
  struct tm ti;
  localtime_r(&tripStartTime, &ti);
  strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%dT%H:%M:%S", &ti);

  char endTimeStr[25];
  localtime_r(&now, &ti);
  strftime(endTimeStr, sizeof(endTimeStr), "%Y-%m-%dT%H:%M:%S", &ti);

  String body = "{\"motorcycle_id\":\"" VEHICLE_ID "\",\"start_time\":\"" + String(startTimeStr) + 
                "\",\"end_time\":\"" + String(endTimeStr) + "\",\"distance\":" + String(tripDistance, 2) + 
                ",\"avg_speed\":" + String(avgSpeed, 2) + ",\"duration\":" + String(duration) + 
                ",\"consumption\":" + String(consumptionA + consumptionB) + 
                ",\"start_battery_level\":" + String(tripStartBatA) + 
                ",\"end_battery_level\":" + String(endBatA) + 
                ",\"path\":[[" + String(tripStartLat, 6) + "," + String(tripStartLon, 6) + "],[" + 
                String(lastTripLat, 6) + "," + String(lastTripLon, 6) + "]]}";

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
  gps_update(); 
  // can_update() ahora corre en su propia tarea (Core 0)

  bool hasCanData = (batA.soc != -1 || batB.soc != -1);

  if (hasCanData) {
    lastCanActivity = millis();
    
    // INICIO DE TRAYECTO
    if (!isTripActive) {
      float startLat = gps_get_lat();
      float startLon = gps_get_lon();
      
      // Si no hay GPS, intentamos LBS para no perder el inicio
      if (startLat == 0) {
        updateLBS();
        startLat = lbsLat;
        startLon = lbsLon;
      }

      isTripActive = true;
      time(&tripStartTime);
      tripStartLat = startLat;
      tripStartLon = startLon;
      tripStartBatA = batA.soc;
      tripStartBatB = batB.soc;
      tripTotalSpeed = 0;
      tripPointsCount = 0;
      tripDistance = 0;
      lastTripLat = startLat;
      lastTripLon = startLon;
      Serial.println("\n" + getTimestamp() + " [TRIP] Trayecto INICIADO.");
    }

    // ACUMULACIÓN DE DATOS
    float currentLat = gps_get_lat();
    float currentLon = gps_get_lon();
    float currentSpeed = gps_get_speed();

    if (currentLat != 0) {
      if (lastTripLat != 0) {
        tripDistance += calculateDistance(lastTripLat, lastTripLon, currentLat, currentLon);
      }
      lastTripLat = currentLat;
      lastTripLon = currentLon;
    }
    
    tripTotalSpeed += currentSpeed;
    tripPointsCount++;
  }

  // FIN DE TRAYECTO (1 minuto sin CAN)
  if (isTripActive && (millis() - lastCanActivity > 60000)) {
    Serial.println("\n" + getTimestamp() + " [TRIP] Trayecto FINALIZADO por inactividad CAN.");
    sendTripSummary();
    isTripActive = false;
  }

  // PROTECCIÓN CRÍTICA (Batería muerta, apagado total)
  if ((batA.soc != -1 && batA.soc <= 10) || (batB.soc != -1 && batB.soc <= 10)) {
    enterDeepSleep("Batería CRÍTICA (<10%) - Apagado total");
  }
  
  // 2. DETECCIÓN DE MOVIMIENTO / ROBO
  // Si la moto está apagada (no hay CAN) pero se está moviendo (GPS > 2km/h)
  bool isMoving = gps_get_speed() > 2.0;
  if (isMoving) {
    lastCanActivity = millis(); // Resetear inactividad si hay movimiento
  }

  // 3. REPOSO INTELIGENTE (Inactividad de 5 minutos)
  if (millis() > 300000 && (millis() - lastCanActivity > 300000)) {
    // Si la batería está sana (>15%), despertamos cada 30 min para seguridad
    uint32_t sleepTime = 1800; // 30 minutos
    enterDeepSleep("Inactividad detectada - Modo Vigilancia", sleepTime);
  }

  // Re-sincronizar con la red GSM cada hora
  if (millis() - lastTimeSync > syncInterval) {
    syncNetworkTime();
    lastTimeSync = millis();
  }

  // can_send_time() ahora corre en su propia tarea (Core 0)

  if (millis() - lastSend > sendInterval) {
    sendTelemetry();
    lastSend = millis();
  }

  // Debug bridge
  while (SerialAT.available()) Serial.write(SerialAT.read());
  while (Serial.available()) SerialAT.write(Serial.read());
}
