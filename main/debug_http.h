#ifndef DEBUG_HTTP_H
#define DEBUG_HTTP_H

void testHttpRead() {
  Serial.println("\n\n=== TEST HTTP READ ===");
  
  // 1. Inicializar HTTP
  Serial.println("[TEST] AT+HTTPTERM");
  SerialAT.println("AT+HTTPTERM");
  delay(1000);
  while (SerialAT.available()) SerialAT.read();
  
  Serial.println("[TEST] AT+HTTPINIT");
  SerialAT.println("AT+HTTPINIT");
  delay(1000);
  while (SerialAT.available()) SerialAT.read();
  
  Serial.println("[TEST] AT+HTTPPARA=\"SSLCFG\",0");
  SerialAT.println("AT+HTTPPARA=\"SSLCFG\",0");
  delay(1000);
  while (SerialAT.available()) SerialAT.read();
  
  // 2. URL simple de test (sin Supabase)
  String testUrl = "https://jmisxaxqwtkudvkytkha.supabase.co/rest/v1/can_configurations?motorcycle_id=eq.550e8400-e29b-41d4-a716-446655440000&apikey=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImptaXN4YXhxd3RrdWR2a3l0a2hhIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzA0MjAxMTAsImV4cCI6MjA4NTk5NjExMH0.uKlohYPeqVm8WRloritzMaDMSq_wT1NxOipfsicx75M";
  
  String urlCmd = "AT+HTTPPARA=\"URL\",\"" + testUrl + "\"";
  Serial.println("[TEST] Setting URL...");
  SerialAT.println(urlCmd);
  delay(1000);
  while (SerialAT.available()) SerialAT.read();
  
  // 3. HTTP GET
  Serial.println("[TEST] AT+HTTPACTION=0");
  SerialAT.println("AT+HTTPACTION=0");
  
  uint32_t t = millis();
  String actionResp = "";
  int statusCode = -1;
  int contentLen = -1;
  
  Serial.println("[TEST] Waiting for +HTTPACTION response...");
  while (millis() - t < 60000) {
    if (SerialAT.available()) {
      char c = SerialAT.read();
      actionResp += c;
      Serial.write(c);
      
      if (actionResp.indexOf("+HTTPACTION:") != -1) {
        // Parse: +HTTPACTION: 0,<status>,<len>
        int idx = actionResp.indexOf("+HTTPACTION:");
        int c1 = actionResp.indexOf(",", idx);
        int c2 = actionResp.indexOf(",", c1 + 1);
        int c3 = actionResp.indexOf("\n", c2);
        if (c1 != -1 && c2 != -1) {
          String statusStr = actionResp.substring(c1 + 1, c2);
          String lenStr = actionResp.substring(c2 + 1, c3 != -1 ? c3 : c2 + 10);
          statusStr.trim();
          lenStr.trim();
          statusCode = statusStr.toInt();
          contentLen = lenStr.toInt();
          Serial.printf("\n[TEST] Parsed: Status=%d, Len=%d\n", statusCode, contentLen);
          break;
        }
      }
    }
    delay(10);
  }
  
  if (statusCode != 200) {
    Serial.printf("[TEST] ERROR: Status code %d, expected 200\n", statusCode);
    return;
  }
  
  if (contentLen <= 0 || contentLen > 10000) {
    Serial.printf("[TEST] ERROR: Invalid content length %d\n", contentLen);
    return;
  }
  
  // 4. Read HTTP body
  delay(1000);
  while (SerialAT.available()) SerialAT.read();
  
  String readCmd = "AT+HTTPREAD=0," + String(contentLen);
  Serial.println("\n[TEST] " + readCmd);
  SerialAT.println(readCmd);
  
  delay(1000);
  String body = "";
  t = millis();
  uint32_t lastChar = millis();
  
  Serial.println("[TEST] Reading body...");
  while (millis() - t < 30000) {
    if (SerialAT.available()) {
      char c = SerialAT.read();
      body += c;
      Serial.write(c);
      lastChar = millis();
    } else {
      if (millis() - lastChar > 3000 && body.length() > 20) {
        break;
      }
    }
    delay(10);
  }
  
  Serial.printf("\n\n[TEST] Body received: %d bytes\n", body.length());
  if (body.length() > 0) {
    int printLen = min(300, body.length());
    Serial.println("[TEST] First " + String(printLen) + " bytes:");
    Serial.println(body.substring(0, printLen));
  }
  
  Serial.println("[TEST] === END TEST ===\n");
}

#endif
