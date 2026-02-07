#ifndef CONFIG_USER_H
#define CONFIG_USER_H

#define MODEM_RX   26
#define MODEM_TX   27
#define PWR_PIN     4
#define UART_BAUD 115200

// Red móvil
#pragma once

#define APN       "internet.digimobil.es"
#define GPRS_USER ""
#define GPRS_PASS ""
#define SIM_PIN "6981"

// SMS (opcional)
#define TELEFONO_ADMIN     "+34600000000"
// Muteo de logs ruidosos
#define DEBUG_CAN_HOUR 0   // 0 = silenciado, 1 = visible
#define DEBUG_SOC_RX   0   // 0 = silenciado, 1 = visible


// Supabase / Web Portal
#define WEB_PORTAL_URL "https://canrider-two.vercel.app/api/telemetry"
#define TELEMETRY_SECRET "tu_clave_secreta_aqui" // Debe coincidir con el env en Vercel
#define VEHICLE_ID "ESP32_NODE_01"
#define GPS_TIMEOUT_MS        120000UL   // 120 s (cámbialo aquí)
#define GPS_PROGRESS_STEP_S   5          // progreso cada 5 s

// (opcional) satélites en progreso vía URCs; 0=off, 1=on
#define GNSS_USE_URC   
#endif
