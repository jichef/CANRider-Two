#include "gps.h"

HardwareSerial SerialGPS(2);
TinyGPSPlus tinyGps;

void gps_setup() {
    SerialGPS.begin(9600, SERIAL_8N1, BOARD_GPS_RX_PIN, BOARD_GPS_TX_PIN);
    Serial.println("[GPS] Inicializado");
}

void gps_update() {
    while (SerialGPS.available()) {
        tinyGps.encode(SerialGPS.read());
    }
}

double gps_get_lat() {
    return tinyGps.location.lat();
}

double gps_get_lon() {
    return tinyGps.location.lng();
}

float gps_get_speed() {
    return tinyGps.speed.kmph();
}

uint32_t gps_get_satellites() {
    return tinyGps.satellites.value();
}

bool gps_has_fix() {
    return tinyGps.location.isValid() && tinyGps.location.age() < 2000;
}

String gps_get_time() {
    if (!tinyGps.date.isValid() || !tinyGps.time.isValid()) {
        return "";
    }
    
    char sz[32];
    // Formato ISO 8601 para Supabase: YYYY-MM-DDTHH:MM:SSZ
    sprintf(sz, "%04d-%02d-%02dT%02d:%02d:%02dZ",
            tinyGps.date.year(), tinyGps.date.month(), tinyGps.date.day(),
            tinyGps.time.hour(), tinyGps.time.minute(), tinyGps.time.second());
    return String(sz);
}
