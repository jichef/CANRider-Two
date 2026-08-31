#pragma once
#include <stdint.h>

#define MAX_SIGNALS   32
#define MAX_TX_FRAMES  8

struct TimeRef {
    bool     valid;
    bool     hasPos;
    char     posSource;  // 'g'=GPS real, 'l'=aproximada por LBS (celda)
    float    lat, lon, speed_kmh;
    uint8_t  hour, min, sec;   // siempre en UTC (hour/min/sec + utcOffsetMin = hora local)
    uint8_t  day, month;
    uint16_t year;
    uint32_t capturedAt;
    int      utcOffsetMin;     // offset local tomado de la red (AT+CCLK), incluye DST
};

struct CANSignal {
    uint32_t frameId;
    char     direction;    // 'r'=RX, 't'=TX
    uint16_t txIntervalMs;
    bool     dualMode;     // true → también escucha frameId+1 (solo RX)
    char     name[20];
    uint8_t  byteStart;
    uint8_t  byteLen;
    uint8_t  bitMask;      // 0=extracción de bytes; N>0 → (data[byteStart]&N)?1:0
    bool     bigEndian;
    bool     isSigned;
    float    scale;
    float    offsetVal;
    float    value;
    bool     updated;
};

struct TxFrame {
    uint32_t frameId;
    uint32_t intervalMs;
    uint32_t lastSentMs;
};

struct BatReading { bool valid; int pct; float volts; bool charging; };

struct TripState {
    bool     active      = false;
    uint32_t startMs     = 0;
    float    startSoc    = 0;
    float    distanceKm  = 0;
    float    maxSpeed    = 0;
    float    lastLat     = 0, lastLon = 0;
    bool     hasLastPos  = false;
    uint32_t stopSince   = 0;
    int      sy, sm, sd, sh, smin, ss;
};
