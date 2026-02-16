#ifndef CAN_CONFIG_H
#define CAN_CONFIG_H

#include <Arduino.h>

// Definición de una regla para extraer datos de una trama CAN
struct CANRule {
    uint32_t id;         // ID de la trama CAN
    uint8_t start_byte;  // Byte de inicio (0-7)
    uint8_t length;      // Longitud en bytes (1-2)
    float factor;        // Multiplicador (ej: 0.1 para voltios)
    bool is_signed;      // Si el valor tiene signo (para corriente)
    bool big_endian;     // Orden de los bytes
};

// Configuración manual para una batería completa
struct BatteryConfig {
    CANRule voltage;
    CANRule current;
    CANRule soc;
    CANRule temp;
};

// Configuración global del sistema
struct CANConfig {
    BatteryConfig batA;
    BatteryConfig batB;
    uint32_t time_tx_id = 0x510;  // ID configurable para inyección de hora
    uint8_t time_hour_byte = 5;    // Byte donde va la hora (0-7)
    uint8_t time_min_byte = 6;     // Byte donde van los minutos (0-7)
};

// Valores por defecto
CANConfig manualConfig = {
    .batA = {
        .voltage = { .id = 0x504, .start_byte = 2, .length = 2, .factor = 0.01f, .is_signed = false, .big_endian = true },
        .current = { .id = 0x504, .start_byte = 4, .length = 2, .factor = 0.1f,  .is_signed = true,  .big_endian = true },
        .soc     = { .id = 0x540, .start_byte = 0, .length = 1, .factor = 1.0f,  .is_signed = false, .big_endian = true },
        .temp    = { .id = 0x540, .start_byte = 3, .length = 1, .factor = 1.0f,  .is_signed = false, .big_endian = true }
    },
    .batB = {
        .voltage = { .id = 0x505, .start_byte = 2, .length = 2, .factor = 0.01f, .is_signed = false, .big_endian = true },
        .current = { .id = 0x505, .start_byte = 4, .length = 2, .factor = 0.1f,  .is_signed = true,  .big_endian = true },
        .soc     = { .id = 0x541, .start_byte = 0, .length = 1, .factor = 1.0f,  .is_signed = false, .big_endian = true },
        .temp    = { .id = 0x541, .start_byte = 3, .length = 1, .factor = 1.0f,  .is_signed = false, .big_endian = true }
    },
    .time_tx_id = 0x510,
    .time_hour_byte = 5,
    .time_min_byte = 6
};

#endif
