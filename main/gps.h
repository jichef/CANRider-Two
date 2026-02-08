#ifndef GPS_H
#define GPS_H

#include <Arduino.h>
#include <TinyGPS++.h>

// Pines para LilyGO T-A7670G R2 con GPS externo
#define BOARD_GPS_RX_PIN   22
#define BOARD_GPS_TX_PIN   21

void gps_setup();
void gps_update();
double gps_get_lat();
double gps_get_lon();
float gps_get_speed();
uint32_t gps_get_satellites();
bool gps_has_fix();

#endif
