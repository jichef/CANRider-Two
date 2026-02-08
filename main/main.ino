#define LILYGO_T_A7670
#include "AT/utilities.h"
#include "config.h"

/*
config.h DEBE CONTENER:

#define APN           "internet.digimobil.es"
#define SUPABASE_URL  "https://jmisxaxqwtkudvkytkha.supabase.co/rest/v1/telemetry"
#define SUPABASE_KEY  "TU_API_KEY_AQUI"
#define VEHICLE_ID    "test01"
*/

void sendAT(const char *cmd, uint32_t timeout = 3000) {
  Serial.print(">> ");
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

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n[BOOT] A7670G + SUPABASE HTTPS");

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
  sendAT("AT+CGDCONT=1,\"IP\",\"" APN "\"");
  sendAT("AT+CGACT=1,1");
  sendAT("AT+NETOPEN", 5000);
  delay(2000);
  sendAT("AT+IPADDR");
  sendAT("AT+CDNSCFG=\"8.8.8.8\",\"8.8.4.4\"");

  // ---------- SSL ----------
  sendAT("AT+CSSLCFG=\"sslversion\",0,4");      // TLS 1.2
  sendAT("AT+CSSLCFG=\"authmode\",0,0");        // Sin CA
  sendAT("AT+CSSLCFG=\"enableSNI\",0,1");       // Obligatorio
  sendAT("AT+CSSLCFG=\"ignorelocaltime\",0,1"); // Sin RTC

  // ---------- HTTP ----------
  sendAT("AT+HTTPTERM");
  sendAT("AT+HTTPINIT");
  sendAT("AT+HTTPPARA=\"SSLCFG\",0");

  // 🔗 SOLUCIÓN: Pasamos apikey por URL y Authorization por cabecera
  String fullUrl = String(SUPABASE_URL) + "?apikey=" + String(SUPABASE_KEY);
  String urlCmd = "AT+HTTPPARA=\"URL\",\"" + fullUrl + "\"";
  sendAT(urlCmd.c_str());

  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"");

  // 🔑 CABECERA AUTHORIZATION (Necesaria para RLS)
  String ud = "AT+HTTPPARA=\"USERDATA\",\"Authorization: Bearer " + String(SUPABASE_KEY) + "\"";
  sendAT(ud.c_str());

  // ---------- BODY ----------
  String body = "{\"motorcycle_id\":\"" VEHICLE_ID "\",\"speed\":42.5,\"battery_level\":88}"; 

  String dcmd = "AT+HTTPDATA=" + String(body.length()) + ",5000";
  sendAT(dcmd.c_str()); 
  // Nota: sendAT ya espera y muestra la respuesta. Aquí debería salir "DOWNLOAD"
  SerialAT.print(body);
  delay(500);

  // ---------- POST ----------
  sendAT("AT+HTTPACTION=1", 15000);
  // No leemos respuesta (HTTPREAD) porque el código 201 no devuelve cuerpo
  sendAT("AT+HTTPTERM");

  Serial.println("\n[OK] FIN. Datos enviados a Supabase.");
}

void loop() {
  while (SerialAT.available()) Serial.write(SerialAT.read());
  while (Serial.available()) SerialAT.write(Serial.read());
}
