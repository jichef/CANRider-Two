#pragma once
#include <Arduino.h>

struct TelemetryData {
    float lat;
    float lng;
    float speed;
    int soc;
    float voltage;
    float current;
};

void initTelemetry();
bool sendTelemetry(const TelemetryData& data);
