#ifndef CAN_CONFIG_LOADER_H
#define CAN_CONFIG_LOADER_H

#include <Arduino.h>
#include "can_config.h"

extern CANConfig manualConfig;
extern String getTimestamp();
extern bool checkNetwork();
extern void sendAT(const char *cmd, uint32_t timeout = 3000);
extern String sendATReturn(const char *cmd, uint32_t timeout = 3000);

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

extern String getFromSupabase(String path);

void loadCanConfigFromSupabase(const char* motorcycle_id) {
  if (!checkNetwork()) {
    Serial.println(getTimestamp() + " [CAN_CONFIG] No network available");
    return;
  }

  delay(2000);
  Serial.println(getTimestamp() + " [CAN_CONFIG] Fetching configuration from Supabase...");
  
  String path = "/rest/v1/can_configurations?motorcycle_id=eq." + String(motorcycle_id);
  String body = getFromSupabase(path);

  if (body.length() > 0 && body.indexOf("[") != -1 && body.indexOf("}") != -1) {
    int dataStart = body.indexOf("[");
    int dataEnd = body.lastIndexOf("}");
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
    manualConfig.timezone_offset = (int8_t)getJsonIntValue(jsonStr, "timezone_offset", 0);
    manualConfig.dst_mode = getJsonBoolValue(jsonStr, "dst_mode", false);
    
    Serial.println(getTimestamp() + " [CAN_CONFIG] ========== CONFIGURACIÓN CARGADA ==========");
  }
}

#endif
