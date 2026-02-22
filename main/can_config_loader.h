#ifndef CAN_CONFIG_LOADER_H
#define CAN_CONFIG_LOADER_H

#include <Arduino.h>
#include "can_config.h"

extern CANConfig manualConfig;
extern String getTimestamp();
extern bool checkNetwork();
extern void sendAT(const char *cmd, uint32_t timeout);

int getJsonIntValue(const String &json, const char *key, int defaultVal = 0) {
  String searchKey = "\"" + String(key) + "\":";
  int index = json.indexOf(searchKey);
  if (index == -1) return defaultVal;
  
  int valueStart = json.indexOf(":", index) + 1;
  int valueEnd = json.indexOf(",", valueStart);
  if (valueEnd == -1) valueEnd = json.indexOf("}", valueStart);
  
  String value = json.substring(valueStart, valueEnd);
  value.trim();
  return value.toInt();
}

float getJsonFloatValue(const String &json, const char *key, float defaultVal = 0.0f) {
  String searchKey = "\"" + String(key) + "\":";
  int index = json.indexOf(searchKey);
  if (index == -1) return defaultVal;
  
  int valueStart = json.indexOf(":", index) + 1;
  int valueEnd = json.indexOf(",", valueStart);
  if (valueEnd == -1) valueEnd = json.indexOf("}", valueStart);
  
  String value = json.substring(valueStart, valueEnd);
  value.trim();
  return value.toFloat();
}

bool getJsonBoolValue(const String &json, const char *key, bool defaultVal = false) {
  String searchKey = "\"" + String(key) + "\":";
  int index = json.indexOf(searchKey);
  if (index == -1) return defaultVal;
  
  int valueStart = json.indexOf(":", index) + 1;
  int valueEnd = json.indexOf(",", valueStart);
  if (valueEnd == -1) valueEnd = json.indexOf("}", valueStart);
  
  String value = json.substring(valueStart, valueEnd);
  value.trim();
  return (value == "true");
}

void loadCanConfigFromSupabase(const char* motorcycle_id) {
  if (!checkNetwork()) {
    Serial.println(getTimestamp() + " [CAN_CONFIG] No network available");
    return;
  }

  delay(2000);
  Serial.println(getTimestamp() + " [CAN_CONFIG] Fetching configuration from Supabase...");
  
  SerialAT.println("AT+HTTPTERM");
  delay(500);
  while (SerialAT.available()) SerialAT.read();
  
  sendAT("AT+HTTPINIT", 3000);
  
#ifdef LILYGO_SIM7000G
  sendAT("AT+HTTPPARA=\"CID\",1", 3000);
  sendAT("AT+HTTPSSL=1", 3000);
#else
  sendAT("AT+HTTPPARA=\"SSLCFG\",0", 3000);
#endif

  String url = String(SUPABASE_URL);
  url.replace("/telemetry", "/can_configurations");
  url += "?motorcycle_id=eq." + String(motorcycle_id) + "&apikey=" + String(SUPABASE_KEY);
  
  String urlCmd = "AT+HTTPPARA=\"URL\",\"" + url + "\"";
  sendAT(urlCmd.c_str(), 3000);
  
  sendAT("AT+HTTPACTION=0", 15000);
  
  uint32_t t = millis();
  String response = "";
  bool foundAction = false;
  
  while (millis() - t < 40000) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
      Serial.write(c);
    }
    if (response.indexOf("+HTTPACTION:") != -1) {
      foundAction = true;
      delay(500);
      break;
    }
  }
  
  if (!foundAction) {
    Serial.println("\n[CAN_CONFIG] No HTTP response received!");
    return;
  }
  
  Serial.println("\n[CAN_CONFIG] Response received: " + response.substring(0, 150));

  if (response.indexOf(",200,") != -1) {
    int httpIdx = response.indexOf("+HTTPACTION:");
    int firstComma = response.indexOf(",", httpIdx);
    int secondComma = response.indexOf(",", firstComma + 1);
    String lenStr = response.substring(firstComma + 1, secondComma);
    lenStr.trim();
    int len = lenStr.toInt();
    
    Serial.printf("[CAN_CONFIG] Parsed length: %d bytes\n", len);

    if (len > 0 && len < 8000) {
      String readCmd = "AT+HTTPREAD=0," + String(len);
      Serial.println("[CAN_CONFIG] Reading " + String(len) + " bytes...");
      SerialAT.println(readCmd);
      
      delay(500);
      String body = "";
      t = millis();
      while (millis() - t < 10000) {
        while (SerialAT.available()) {
          char c = SerialAT.read();
          body += c;
        }
        if (body.indexOf("}") != -1 && body.indexOf("[") != -1) break;
        delay(50);
      }
      
      Serial.println("\n[CAN_CONFIG] Body: " + body.substring(0, 200));

      if (body.length() > 0) {
        Serial.println(getTimestamp() + " [CAN_CONFIG] Got response: " + body.substring(0, 200));
        
        int dataStart = body.indexOf("[");
        int dataEnd = body.lastIndexOf("}");
        if (dataStart != -1 && dataEnd != -1) {
          String jsonStr = body.substring(dataStart + 1, dataEnd + 1);
          
          manualConfig.batA.voltage.id = getJsonIntValue(jsonStr, "v_id", 0x504);
          manualConfig.batA.voltage.start_byte = getJsonIntValue(jsonStr, "v_start", 2);
          manualConfig.batA.voltage.length = getJsonIntValue(jsonStr, "v_len", 2);
          manualConfig.batA.voltage.factor = getJsonFloatValue(jsonStr, "v_factor", 0.01f);
          manualConfig.batA.voltage.big_endian = getJsonBoolValue(jsonStr, "v_be", true);
          
          manualConfig.batA.current.id = getJsonIntValue(jsonStr, "c_id", 0x504);
          manualConfig.batA.current.start_byte = getJsonIntValue(jsonStr, "c_start", 4);
          manualConfig.batA.current.length = getJsonIntValue(jsonStr, "c_len", 2);
          manualConfig.batA.current.factor = getJsonFloatValue(jsonStr, "c_factor", 0.1f);
          manualConfig.batA.current.big_endian = getJsonBoolValue(jsonStr, "c_be", true);
          manualConfig.batA.current.is_signed = getJsonBoolValue(jsonStr, "c_signed", true);
          
          manualConfig.batA.soc.id = getJsonIntValue(jsonStr, "s_id", 0x540);
          manualConfig.batA.soc.start_byte = getJsonIntValue(jsonStr, "s_start", 0);
          manualConfig.batA.soc.length = getJsonIntValue(jsonStr, "s_len", 1);
          manualConfig.batA.soc.factor = getJsonFloatValue(jsonStr, "s_factor", 1.0f);
          manualConfig.batA.soc.big_endian = getJsonBoolValue(jsonStr, "s_be", true);
          
          manualConfig.batA.temp.id = getJsonIntValue(jsonStr, "t_id", 0x540);
          manualConfig.batA.temp.start_byte = getJsonIntValue(jsonStr, "t_start", 3);
          manualConfig.batA.temp.length = getJsonIntValue(jsonStr, "t_len", 1);
          manualConfig.batA.temp.factor = getJsonFloatValue(jsonStr, "t_factor", 1.0f);
          manualConfig.batA.temp.big_endian = getJsonBoolValue(jsonStr, "t_be", true);
          
          manualConfig.batB.voltage.id = getJsonIntValue(jsonStr, "vb_id", 0x505);
          manualConfig.batB.voltage.start_byte = getJsonIntValue(jsonStr, "vb_start", 2);
          manualConfig.batB.voltage.length = getJsonIntValue(jsonStr, "vb_len", 2);
          manualConfig.batB.voltage.factor = getJsonFloatValue(jsonStr, "vb_factor", 0.01f);
          manualConfig.batB.voltage.big_endian = getJsonBoolValue(jsonStr, "vb_be", true);
          
          manualConfig.batB.current.id = getJsonIntValue(jsonStr, "cb_id", 0x505);
          manualConfig.batB.current.start_byte = getJsonIntValue(jsonStr, "cb_start", 4);
          manualConfig.batB.current.length = getJsonIntValue(jsonStr, "cb_len", 2);
          manualConfig.batB.current.factor = getJsonFloatValue(jsonStr, "cb_factor", 0.1f);
          manualConfig.batB.current.big_endian = getJsonBoolValue(jsonStr, "cb_be", true);
          manualConfig.batB.current.is_signed = getJsonBoolValue(jsonStr, "cb_signed", true);
          
          manualConfig.batB.soc.id = getJsonIntValue(jsonStr, "sb_id", 0x541);
          manualConfig.batB.soc.start_byte = getJsonIntValue(jsonStr, "sb_start", 0);
          manualConfig.batB.soc.length = getJsonIntValue(jsonStr, "sb_len", 1);
          manualConfig.batB.soc.factor = getJsonFloatValue(jsonStr, "sb_factor", 1.0f);
          manualConfig.batB.soc.big_endian = getJsonBoolValue(jsonStr, "sb_be", true);
          
          manualConfig.batB.temp.id = getJsonIntValue(jsonStr, "tb_id", 0x541);
          manualConfig.batB.temp.start_byte = getJsonIntValue(jsonStr, "tb_start", 3);
          manualConfig.batB.temp.length = getJsonIntValue(jsonStr, "tb_len", 1);
          manualConfig.batB.temp.factor = getJsonFloatValue(jsonStr, "tb_factor", 1.0f);
          manualConfig.batB.temp.big_endian = getJsonBoolValue(jsonStr, "tb_be", true);
          
          manualConfig.time_tx_id = getJsonIntValue(jsonStr, "time_tx_id", 0x510);
          manualConfig.time_hour_byte = getJsonIntValue(jsonStr, "time_hour_byte", 5);
          manualConfig.time_min_byte = getJsonIntValue(jsonStr, "time_min_byte", 6);
          
          Serial.println(getTimestamp() + " [CAN_CONFIG] ========== CONFIGURACIÓN CARGADA ==========");
          Serial.printf("[CAN_CONFIG] BATERÍA A:\n");
          Serial.printf("[CAN_CONFIG]   Voltaje:   ID=0x%03X start=%d len=%d factor=%.4f BE=%d\n",
            manualConfig.batA.voltage.id, manualConfig.batA.voltage.start_byte, 
            manualConfig.batA.voltage.length, manualConfig.batA.voltage.factor,
            manualConfig.batA.voltage.big_endian);
          Serial.printf("[CAN_CONFIG]   Corriente: ID=0x%03X start=%d len=%d factor=%.4f BE=%d SIGN=%d\n",
            manualConfig.batA.current.id, manualConfig.batA.current.start_byte,
            manualConfig.batA.current.length, manualConfig.batA.current.factor,
            manualConfig.batA.current.big_endian, manualConfig.batA.current.is_signed);
          Serial.printf("[CAN_CONFIG]   SOC:       ID=0x%03X start=%d len=%d factor=%.4f BE=%d\n",
            manualConfig.batA.soc.id, manualConfig.batA.soc.start_byte,
            manualConfig.batA.soc.length, manualConfig.batA.soc.factor,
            manualConfig.batA.soc.big_endian);
          Serial.printf("[CAN_CONFIG]   Temp:      ID=0x%03X start=%d len=%d factor=%.4f BE=%d\n",
            manualConfig.batA.temp.id, manualConfig.batA.temp.start_byte,
            manualConfig.batA.temp.length, manualConfig.batA.temp.factor,
            manualConfig.batA.temp.big_endian);
          
          Serial.printf("[CAN_CONFIG] BATERÍA B:\n");
          Serial.printf("[CAN_CONFIG]   Voltaje:   ID=0x%03X start=%d len=%d factor=%.4f BE=%d\n",
            manualConfig.batB.voltage.id, manualConfig.batB.voltage.start_byte,
            manualConfig.batB.voltage.length, manualConfig.batB.voltage.factor,
            manualConfig.batB.voltage.big_endian);
          Serial.printf("[CAN_CONFIG]   Corriente: ID=0x%03X start=%d len=%d factor=%.4f BE=%d SIGN=%d\n",
            manualConfig.batB.current.id, manualConfig.batB.current.start_byte,
            manualConfig.batB.current.length, manualConfig.batB.current.factor,
            manualConfig.batB.current.big_endian, manualConfig.batB.current.is_signed);
          Serial.printf("[CAN_CONFIG]   SOC:       ID=0x%03X start=%d len=%d factor=%.4f BE=%d\n",
            manualConfig.batB.soc.id, manualConfig.batB.soc.start_byte,
            manualConfig.batB.soc.length, manualConfig.batB.soc.factor,
            manualConfig.batB.soc.big_endian);
          Serial.printf("[CAN_CONFIG]   Temp:      ID=0x%03X start=%d len=%d factor=%.4f BE=%d\n",
            manualConfig.batB.temp.id, manualConfig.batB.temp.start_byte,
            manualConfig.batB.temp.length, manualConfig.batB.temp.factor,
            manualConfig.batB.temp.big_endian);
          
          Serial.printf("[CAN_CONFIG] SISTEMA:\n");
          Serial.printf("[CAN_CONFIG]   Time TX ID: 0x%03X byte_hora=%d byte_min=%d\n",
            manualConfig.time_tx_id, manualConfig.time_hour_byte, manualConfig.time_min_byte);
          Serial.println("[CAN_CONFIG] =========================================");
        }
      }
    }
  } else {
    Serial.println(getTimestamp() + " [CAN_CONFIG] HTTP request failed: " + response.substring(0, 100));
  }
  
  sendAT("AT+HTTPTERM", 3000);
}

#endif
