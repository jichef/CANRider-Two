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
    struct timeval tv = { .tv_sec = t_now };
    settimeofday(&tv, NULL);
    
    Serial.println("\n[TIME] Reloj ESP32 sincronizado con red GSM.");
  }
}

String getTimestamp() {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  
  if (timeinfo.tm_year > 120) { // Si el año es > 2020, asumimos sincronizado
    char buf[12];
    strftime(buf, sizeof(buf), "[%H:%M:%S]", &timeinfo);
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
  localtime_r(&now, &ti);
  
  if (ti.tm_year > 120) {
    char buf[25];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &ti);
    return String(buf);
  }
  return "";
}

void sendTelemetry() {
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
  can_update();
  
  String timestamp = getCurrentISO8601();

  String body = "{\"motorcycle_id\":\"" VEHICLE_ID "\",\"speed\":" + String(gps_get_speed()) + 
                ",\"battery_level\":" + String(bat) + ",\"latitude\":" + String(gps_get_lat(), 6) + 
                ",\"longitude\":" + String(gps_get_lon(), 6) + 
                ",\"signal_strength\":" + String(rssi);
  
  if (timestamp != "") {
    body += ",\"timestamp\":\"" + timestamp + "\"";
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
  sendAT("AT+HTTPTERM");

  Serial.println(getTimestamp() + " [OK] Telemetría enviada.");
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
    Serial.println(getTimestamp() + " [CAN] Inicializado en pines RX:" + String(CAN_RX_PIN) + " TX:" + String(CAN_TX_PIN));
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
uint32_t lastCanTimeSend = 0;
const uint32_t canTimeInterval = 200; // Enviar trama CAN cada 200ms
uint32_t lastTimeSync = 0;
const uint32_t syncInterval = 3600000; // Re-sincronizar hora cada 1 hora

void enterDeepSleep(const char* reason) {
  Serial.println("\n" + getTimestamp() + " [CRITICAL] " + reason);
  Serial.println(getTimestamp() + " [SHUTDOWN] Entrando en modo ahorro para proteger batería moto...");
  
  // Apagar módem
  sendAT("AT+CPOWD=1"); 
  delay(2000);
  
  // Apagar alimentación módem si es posible
  digitalWrite(BOARD_POWERON_PIN, LOW);
  
  // Dormir profundamente (despertará por reset de alimentación)
  esp_deep_sleep_start();
}

void loop() {
  gps_update(); 
  can_update();

  // --- PROTECCIÓN DE BATERÍA ---
  if ((batA.soc != -1 && batA.soc <= 10) || (batB.soc != -1 && batB.soc <= 10)) {
    enterDeepSleep("Batería moto baja (<10%)");
  }

  // Re-sincronizar con la red GSM cada hora
  if (millis() - lastTimeSync > syncInterval) {
    syncNetworkTime();
    lastTimeSync = millis();
  }

  // Enviar trama CAN horaria cada 200ms
  if (millis() - lastCanTimeSend > canTimeInterval) {
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);
    
    // Solo enviamos si el reloj está sincronizado (año > 2020)
    if (ti.tm_year > 120) {
      can_send_time((uint8_t)ti.tm_hour, (uint8_t)ti.tm_min);
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
