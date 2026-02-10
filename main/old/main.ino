#include <Arduino.h>
#include "config.h"
#include "config_user.h"

#include "can_bus.h"
#include "modem.h"
#include "gps.h"
#include "telegram.h"
#include "bateria.h"
#include "energia.h"
#include "logs.h"        // <<— tu logger (log.cpp)
#include "modo_diag.h"
#include "utils.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"  // Watchdog Timer

// ========= Variables globales =========
int localHour = 0;
int minute = 0;
time_t hora_actual;
bool hora_valida = false;
extern bool gprsConnected;
int telegramErrorCount = 0;

// Mutex para proteger el módem (AT/HTTPS/GPS/Telegram)
SemaphoreHandle_t modemMutex = nullptr;

// Centinela para "SoC desconocido" (soc es uint8_t en bateria.h)
static const uint8_t SOC_UNKNOWN = 0xFF;
static const uint8_t SOC_PLACEHOLDER = 55;  // Valor por defecto cuando no hay SoC

// Handles de tareas para monitoreo
static TaskHandle_t hTaskTelegram = nullptr;
static TaskHandle_t hTaskGPS = nullptr;
static TaskHandle_t hTaskCAN = nullptr;

// ========= Prototipos de tareas locales =========
void taskTelegram(void *pvParameters);
void taskGPS(void *pvParameters);
void taskGPRSWatchdog(void *pvParameters);
void logStackUsage();

// ========= setup =========
void setup() {
  Serial.begin(115200);
  delay(500);

  // --- WATCHDOG TIMER (30s) ---
  esp_task_wdt_init(30, true);  // 30 segundos, panic on timeout
  esp_task_wdt_add(NULL);       // Añadir tarea actual (setup/loop)

  // --- LOGGER ---
  initLogger(Serial, LOG_DEBUG);
  logMark("SETUP_BEGIN");
  logMsg(LOG_INFO, "SETUP", "Iniciando sistema con WDT (30s)...");

  // === CAN primero ===
  initCAN();
  logTWAIStatus();
  if (!isTWAIStarted()) {
    logMsg(LOG_WARN, "TWAI", "No está RUNNING. Revisa twai_start() / wiring / bitrate");
  }

  // Esperar SoC por CAN (máx 3 s) ANTES de tocar el módem
  soc = 0xFF; // SOC_UNKNOWN
  logMsg(LOG_INFO, "SOC", "Esperando SoC por CAN (máx 3 s)...");
  startWindow("WAIT_SOC_BOOT");
  const unsigned long startWait = millis();
  while ((millis() - startWait) < 3000 && soc == 0xFF) {
    checkCANInput();   // intenta recibir 0x541
    esp_task_wdt_reset();  // Reset WDT durante espera
    delay(10);
  }
  endWindow("WAIT_SOC_BOOT");
  if (soc != 0xFF) {
    logMsg(LOG_INFO, "SOC", String("SoC al arrancar: ") + String(soc) + "%");
    battery_onCan541(&soc, 1);  // Registrar en sistema robusto
  } else {
    logMsg(LOG_WARN, "SOC", "No se recibió SoC. Usando placeholder.");
    soc = SOC_PLACEHOLDER;  // Usar placeholder en lugar de 0
  }

  // === Módem ===
  logMark("MODEM_INIT");
  initModem();
  logMsg(LOG_INFO, "MODEM", "Inicializado");

  // Mutex del módem
  modemMutex = xSemaphoreCreateMutex();
  if (!modemMutex) {
    logMsg(LOG_ERROR, "MUTEX", "Error creando mutex del módem");
  } else {
    logMsg(LOG_INFO, "MUTEX", "Creado mutex del módem");
  }

  // Conexión GPRS
  logMsg(LOG_INFO, "GPRS", "Conectando...");
  if (connectGPRS()) {
    logMsg(LOG_INFO, "GPRS", "Conectado");

    // (opcional) afinar radio para latencia baja (SIM7000)
    // tuneRadioLowLatency();

    vTaskDelay(pdMS_TO_TICKS(2000)); // breve respiro tras adjuntar datos

    // Hora desde módem (bloqueante aquí; nunca en la tarea de envío)
    if (modemMutex && xSemaphoreTake(modemMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
      startWindow("GET_TIME_FROM_MODEM");
      int h = 0, m = 0;
      if (getTimeFromModem(h, m)) {
        localHour   = h;
        minute      = m;
        hora_valida = true;

        // **Semilla del snapshot interno** para que el TX 200 ms no arranque con 00:00
        time_seed_from_setup();

        logMsg(LOG_INFO, "TIME", String("Hora desde módem: ") + h + ":" + (m < 10 ? "0" : "") + m);
      } else {
        logMsg(LOG_WARN, "TIME", "No se pudo obtener la hora del módem");
      }
      endWindow("GET_TIME_FROM_MODEM");
      xSemaphoreGive(modemMutex);
    } else {
      logMsg(LOG_WARN, "MUTEX", "No se pudo tomar el mutex para leer la hora");
    }

    // === Tareas ===
    // Core 0 → CAN (ticker + envío 200 ms). Idempotente dentro de startHourTasks()
    startHourTasks();
    logMsg(LOG_INFO, "TASK", "startHourTasks lanzado (ClockTicker + CANHourTX)");

    // RX continuo de CAN (SoC 0x541)
    xTaskCreatePinnedToCore(taskCANProcessing, "CANTask", 4096, NULL, 2, &hTaskCAN, 0);
    logMsg(LOG_INFO, "TASK", "CANTask creada en core 0");

    // Core 1 → Telecom (módem)
    xTaskCreatePinnedToCore(taskTelegram, "taskTelegram", 8192, nullptr, 3, &hTaskTelegram, 1);
    logMsg(LOG_INFO, "TASK", "TelegramTask creada en core 1");

    xTaskCreatePinnedToCore(taskGPS, "GPSTask", 6144, NULL, 2, &hTaskGPS, 1);
    logMsg(LOG_INFO, "TASK", "GPSTask creada en core 1");

    // Watchdog para reconexión GPRS
    xTaskCreatePinnedToCore(taskGPRSWatchdog, "GPRSWatch", 4096, NULL, 1, NULL, 1);
    logMsg(LOG_INFO, "TASK", "GPRSWatchdog creada en core 1");

  } else {
    logMsg(LOG_ERROR, "GPRS", "Error al conectar");

    // Aun sin GPRS, podemos querer enviar hora por CAN (si ya la tenemos local).
    // startHourTasks() es idempotente: si más tarde vuelves a llamarla, ignorará el segundo intento.
    startHourTasks();
    logMsg(LOG_INFO, "TASK", "startHourTasks lanzado sin GPRS");

    xTaskCreatePinnedToCore(taskCANProcessing, "CANTask", 4096, NULL, 2, &hTaskCAN, 0);
    logMsg(LOG_INFO, "TASK", "CANTask creada en core 0");

    // Watchdog para reconexión GPRS (incluso sin GPRS inicial)
    xTaskCreatePinnedToCore(taskGPRSWatchdog, "GPRSWatch", 4096, NULL, 1, NULL, 1);
    logMsg(LOG_INFO, "TASK", "GPRSWatchdog creada (intentará reconectar)");
  }

  // Periféricos
  initSD();
  logMsg(LOG_INFO, "SD", "initSD llamado");

  logMark("SETUP_END");
}



// ========= loop (no bloqueante, sin tocar módem) =========
void loop() {
  static TickType_t lastWake = xTaskGetTickCount();
  static uint32_t lastHbMs = 0;

  esp_task_wdt_reset();     // Reset WDT en cada iteración

  checkSoC();               // alerta y apagado si batería baja (no bloqueante)
  handleModoDiagnostico();  // lógica de modo diagnóstico (no debe tocar módem)

  // Heartbeat cada 5 s con estado resumido
  uint32_t now = millis();
  if (now - lastHbMs > 5000) {
    lastHbMs = now;
    uint8_t socEff = soc_effective();  // Usar SoC robusto
    String st = String("soc=") + String(socEff) +
                " gprs=" + (gprsConnected ? "1" : "0") +
                (hora_valida ? (" time=" + String(localHour) + ":" + (minute<10?"0":"") + String(minute)) : " time=NA");
    heartbeat(st);
    logStackUsage();  // Monitoreo de stack cada 5s
  }

  // GPS se gestiona en taskGPS con mutex del módem
  vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(100)); // 10 Hz estable
}

void taskTelegram(void *pvParameters) {
  bool inited = false;
  TickType_t lastWake = xTaskGetTickCount();

  // Pequeño respiro al sistema antes de empezar
  vTaskDelay(pdMS_TO_TICKS(250));
  logMark("TELEGRAM_TASK_START");

  for (;;) {
    // Si no hay GPRS o aún no existe el mutex → no intentamos nada
    if (!gprsConnected || modemMutex == nullptr) {
      if (inited) logMsg(LOG_WARN, "TELEGRAM", "GPRS OFF o sin mutex → esperando reconexión");
      inited = false;
      vTaskDelay(pdMS_TO_TICKS(5000));  // Esperar más tiempo si no hay GPRS
      // re-sincronizamos la temporización de vTaskDelayUntil
      lastWake = xTaskGetTickCount();
      continue;
    }

    // Primer arranque / reintento tras pérdida de GPRS
    if (!inited) {
      // warm-up corto para que el resto de tareas estabilicen
      vTaskDelay(pdMS_TO_TICKS(1000));
      logMsg(LOG_INFO, "TELEGRAM", "Init…");

      // 1) Handshake TLS (no envía bienvenida)
      bool ok_init = initTelegram();

      if (ok_init) {
        // 2) Bienvenida idempotente (solo una vez)
        for (int i = 0; i < 3 && !sendWelcomeOnce(); ++i) {
          vTaskDelay(pdMS_TO_TICKS(700));
        }

        // 3) Burst de polling 5s para absorber backlog
        uint32_t t0 = millis();
        while (millis() - t0 < 5000) {
          (void)checkTelegram(millis());
          vTaskDelay(pdMS_TO_TICKS(300));
        }
      }

      inited = ok_init;  // si falló, volverá a intentarlo en el siguiente ciclo
      // re-sincronizamos el reloj del vTaskDelayUntil tras los delays anteriores
      lastWake = xTaskGetTickCount();
      continue;
    }

    startWindow("TELEGRAM_POLL");
    (void)checkTelegram(millis());
    endWindow("TELEGRAM_POLL");

    // si el poll tardó más de ~500 ms, resetea el punto de referencia
    static const TickType_t CATCHUP_MS = 500;
    if (xTaskGetTickCount() - lastWake > pdMS_TO_TICKS(CATCHUP_MS)) {
      lastWake = xTaskGetTickCount();
    }

    // Ajustar periodo según si GPS está activo (menos contención)
    uint32_t period = gpsFixInProgress() ? 5000 : 2000;
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(period));
  }
}

// ========= Tarea: Watchdog GPRS (core 1) =========
// Monitorea conexión GPRS y recrea tareas si es necesario
void taskGPRSWatchdog(void *pvParameters) {
  logMsg(LOG_INFO, "GPRS_WD", "Watchdog GPRS iniciado");
  
  for (;;) {
    if (!gprsConnected) {
      logMsg(LOG_WARN, "GPRS_WD", "Desconectado - intentando reconectar...");
      
      if (connectGPRS()) {
        logMsg(LOG_INFO, "GPRS_WD", "Reconectado exitosamente");
        
        // Verificar si las tareas existen, si no, recrearlas
        if (hTaskTelegram == nullptr || eTaskGetState(hTaskTelegram) == eDeleted) {
          xTaskCreatePinnedToCore(taskTelegram, "taskTelegram", 8192, nullptr, 3, &hTaskTelegram, 1);
          logMsg(LOG_INFO, "GPRS_WD", "TelegramTask recreada");
        }
        
        if (hTaskGPS == nullptr || eTaskGetState(hTaskGPS) == eDeleted) {
          xTaskCreatePinnedToCore(taskGPS, "GPSTask", 6144, NULL, 2, &hTaskGPS, 1);
          logMsg(LOG_INFO, "GPRS_WD", "GPSTask recreada");
        }
      } else {
        logMsg(LOG_WARN, "GPRS_WD", "Reconexión falló, reintentando en 30s");
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(30000)); // Revisar cada 30s
  }
}

// ========= Monitoreo de Stack =========
void logStackUsage() {
  static uint32_t lastCheck = 0;
  uint32_t now = millis();
  
  if (now - lastCheck < 60000) return;  // Solo cada 60s
  lastCheck = now;
  
  if (hTaskTelegram && eTaskGetState(hTaskTelegram) != eDeleted) {
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(hTaskTelegram);
    if (hwm < 512) {
      logMsg(LOG_WARN, "STACK", String("taskTelegram bajo: ") + hwm + " bytes");
    } else {
      logMsg(LOG_DEBUG, "STACK", String("taskTelegram: ") + hwm + " bytes");
    }
  }
  
  if (hTaskGPS && eTaskGetState(hTaskGPS) != eDeleted) {
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(hTaskGPS);
    if (hwm < 512) {
      logMsg(LOG_WARN, "STACK", String("GPSTask bajo: ") + hwm + " bytes");
    } else {
      logMsg(LOG_DEBUG, "STACK", String("GPSTask: ") + hwm + " bytes");
    }
  }
  
  if (hTaskCAN && eTaskGetState(hTaskCAN) != eDeleted) {
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(hTaskCAN);
    if (hwm < 256) {
      logMsg(LOG_WARN, "STACK", String("CANTask bajo: ") + hwm + " bytes");
    } else {
      logMsg(LOG_DEBUG, "STACK", String("CANTask: ") + hwm + " bytes");
    }
  }
}








// ========= Tarea: GPS (core 1) =========
// Encargada de activar/leer/apagar GPS bajo demanda.
// checkGPSStatus() debe decidir internamente si debe actuar y puede usar el logger.
void taskGPS(void *pvParameters) {
  logMark("GPS_TASK_START");
  for (;;) {
    if (gprsConnected && modemMutex) {
      // Timeout aumentado a 12s y reintentos para reducir fallos
      if (xSemaphoreTake(modemMutex, pdMS_TO_TICKS(12000)) == pdTRUE) {
        logMsg(LOG_DEBUG, "GPS", "tick");
        checkGPSStatus(millis());  // gestionará ON→fix→OFF si procede
        xSemaphoreGive(modemMutex);
      } else {
        logMsg(LOG_WARN, "MUTEX", "GPS no pudo tomar el mutex (timeout 12s) - reintentando");
      }
    }
    // Reducir frecuencia a 2s para menos contención con Telegram
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}
