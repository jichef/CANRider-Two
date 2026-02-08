#include <Arduino.h>
#include <TinyGPS++.h>

#define SerialAT   Serial1
#define SerialGPS  Serial2

#define BOARD_MODEM_RX_PIN 27
#define BOARD_MODEM_TX_PIN 26
#define BOARD_GPS_RX_PIN   22
#define BOARD_GPS_TX_PIN   21
#define BOARD_POWER_ON_PIN 12
#define BOARD_MODEM_PWR_PIN 4

TinyGPSPlus gps;

void displayInfo();  // 👈 PROTOTIPO

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BOARD_POWER_ON_PIN, OUTPUT);
  digitalWrite(BOARD_POWER_ON_PIN, HIGH);

  pinMode(BOARD_MODEM_PWR_PIN, OUTPUT);
  digitalWrite(BOARD_MODEM_PWR_PIN, LOW);
  delay(100);
  digitalWrite(BOARD_MODEM_PWR_PIN, HIGH);
  delay(100);
  digitalWrite(BOARD_MODEM_PWR_PIN, LOW);

  SerialAT.begin(115200, SERIAL_8N1, BOARD_MODEM_RX_PIN, BOARD_MODEM_TX_PIN);
  SerialGPS.begin(9600, SERIAL_8N1, BOARD_GPS_RX_PIN, BOARD_GPS_TX_PIN);

  Serial.println("A7670G + GPS externo (debug NMEA)");
}

void loop() {
  while (SerialGPS.available()) {
    char c = SerialGPS.read();
    Serial.write(c);            // 👈 VER NMEA
    gps.encode(c);
  }

  if (gps.location.isUpdated()) {
    displayInfo();
  }
}

void displayInfo() {
  Serial.print("\nLAT: ");
  Serial.print(gps.location.lat(), 6);
  Serial.print(" LON: ");
  Serial.print(gps.location.lng(), 6);
  Serial.print(" SATS: ");
  Serial.println(gps.satellites.value());
}
