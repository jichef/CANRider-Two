#include <Arduino.h>
#include "config.h"
#include "config_user.h"
#include "can_bus.h"
#include "modem.h"
#include "gps.h"
#include "telemetry.h"
#include "logs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"

// ========= Variables globales =========
SemaphoreHandle_t modemMutex = nullptr;
extern uint8_t soc;
int localHour = 0;
int minute = 0;
bool hora_valida = false;
bool alerta_enviada = false;
bool red_activa = false;
bool modo_diagnostico = false;
extern bool gprsConnected;

// Datos de telemetría compartidos
TelemetryData globalData = {0, 0, 0, 0, 0, 0};
SemaphoreHandle_t dataMutex = nullptr;

// ========= Prototipos de tareas =========
void taskTelemetryLoop(void *pvParameters);
void taskGPSUpdate(void *pvParameters);

void setup() {
  Serial.begin(115200);
  delay(500);

  // --- WATCHDOG TIMER (30s) ---
  esp_task_wdt_init(30, true);
  esp_task_wdt_add(NULL);

  // --- LOGGER ---
  initLogger(Serial, LOG_DEBUG);
  logMsg(LOG_INFO, "SETUP", "Iniciando CanRider ONE (Portal Web Mode)");

  // === CAN Bus ===
  initCAN();
  esp_task_wdt_reset();
  xTaskCreatePinnedToCore(taskCANProcessing, "CANTask", 4096, NULL, 2, NULL, 0);

  // === Módem ===
  initModem();
  esp_task_wdt_reset();
  modemMutex = xSemaphoreCreateMutex();
  dataMutex = xSemaphoreCreateMutex();

  // Conexión GPRS
  if (connectGPRS()) {
    esp_task_wdt_reset();
    logMsg(LOG_INFO, "GPRS", "Conectado y listo");
    
    // Tareas principales
    xTaskCreatePinnedToCore(taskGPSUpdate, "GPSTask", 6144, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(taskTelemetryLoop, "TelemetryTask", 8192, NULL, 3, NULL, 1);
  } else {
    esp_task_wdt_reset();
    logMsg(LOG_ERROR, "GPRS", "Fallo de red inicial. El sistema intentará operar.");
    // Podríamos lanzar las tareas igual y que fallen hasta que haya red
    xTaskCreatePinnedToCore(taskGPSUpdate, "GPSTask", 6144, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(taskTelemetryLoop, "TelemetryTask", 8192, NULL, 3, NULL, 1);
  }
}

void loop() {
  esp_task_wdt_reset();
  
  // Mostrar estado básico por Serial cada 10s
  static uint32_t lastReport = 0;
  if (millis() - lastReport > 10000) {
    lastReport = millis();
    logMsg(LOG_INFO, "STATUS", "Batería: " + String(soc) + "% | GPRS: " + (gprsConnected ? "ON" : "OFF"));
  }
  
  delay(1000);
}

// ========= Tarea GPS =========
void taskGPSUpdate(void *pvParameters) {
  // Encender GPS
  gpsStartFor(3600000); // 1 hora
  
  for (;;) {
    if (gprsConnected && modemMutex) {
      float lat = 0, lng = 0, speed = 0, course = 0;
      if (gps_get_position(lat, lng, speed, course)) {
        if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          globalData.lat = lat;
          globalData.lng = lng;
          globalData.speed = speed;
          xSemaphoreGive(dataMutex);
        }
        logMsg(LOG_DEBUG, "GPS", "Fix: " + String(lat, 6) + "," + String(lng, 6));
      } else {
        logMsg(LOG_DEBUG, "GPS", "Buscando satélites...");
        if (!gpsActive()) gpsStartFor(3600000);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5000)); // Cada 5s
  }
}

// ========= Tarea Telemetría =========
void taskTelemetryLoop(void *pvParameters) {
  for (;;) {
    if (gprsConnected) {
      TelemetryData sendData;
      
      // Copia segura de datos
      if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        sendData = globalData;
        xSemaphoreGive(dataMutex);
      }
      
      sendData.soc = soc; // Soc se actualiza por CAN en otra tarea

      // Enviar telemetría (aunque no haya GPS, enviamos batería y estado)
      if (sendTelemetry(sendData)) {
        if (sendData.lat == 0) {
          logMsg(LOG_DEBUG, "TELEMETRY", "Enviado (sin GPS todavía)");
        } else {
          logMsg(LOG_DEBUG, "TELEMETRY", "Enviado OK");
        }
      } else {
        logMsg(LOG_WARN, "TELEMETRY", "Fallo al enviar");
      }
    } else {
        // Intentar reconectar si se pierde
        connectGPRS();
    }
    vTaskDelay(pdMS_TO_TICKS(15000)); // Cada 15s
  }
}
