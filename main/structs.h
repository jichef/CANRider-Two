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

// Cuerpo de telemetría ya construido + los datos que necesita updateTrip()
// después de mandarlo — definido aquí (no inline en main.ino) porque el
// generador automático de prototipos de Arduino inserta el prototipo de
// buildTelemetrySnapshot() antes de que el struct exista si se define en
// el propio .ino.
struct TelemetrySnapshot {
    String  body;
    bool    busAlive;
    float   currentSoc;
    TimeRef t;
};

// Puntos de traza guardados durante el viaje (lat/lon/velocidad) para poder
// dibujar la ruta real en el mapa, coloreada por velocidad, en vez de solo
// una línea recta entre inicio y fin. Un punto por ciclo de POST
// (POST_INTERVAL_MS) mientras hay fix GPS — a 15s/punto, 300 puntos cubren
// 75 minutos de viaje, de sobra para un trayecto urbano típico. Si un
// viaje dura más, se dejan de añadir puntos nuevos (se pierde el tramo
// final, no el inicio) en vez de crecer sin límite.
#define MAX_TRIP_POINTS 300

struct TripState {
    bool     active      = false;
    uint32_t startMs     = 0;
    float    startSoc    = 0;
    float    distanceKm  = 0;
    float    maxSpeed    = 0;
    float    lastLat     = 0, lastLon = 0;
    bool     hasLastPos  = false;
    int      sy, sm, sd, sh, smin, ss;
    float    trackLat[MAX_TRIP_POINTS];
    float    trackLon[MAX_TRIP_POINTS];
    float    trackSpeed[MAX_TRIP_POINTS];
    int      trackCount  = 0;
};
