#include <Arduino.h>
#include "driver/twai.h"

// Pines CAN (coincidentes con la configuración del receptor)
#define TX_GPIO GPIO_NUM_33
#define RX_GPIO GPIO_NUM_32

// Variables de simulación del vehículo
struct SimBattery {
  float soc = 100.0;
  float voltage = 84.0;
  float current = 0.0;
  float temp = 25.0;
};

SimBattery batA, batB;

float speed = 0.0;       
bool is_charging = false;
uint32_t last_update = 0;
float target_speed = 0.0;

// Lógica de trayectos (Activo/Pausa)
bool is_paused = false;
uint32_t last_state_change = 0;
const uint32_t ACTIVE_DURATION = 30000; // 30 segundos activo
const uint32_t PAUSE_DURATION = 60000;  // 60 segundos pausa

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  uint64_t chipid = ESP.getEfuseMac();
  
  Serial.println("\n--- SIMULADOR DE VEHÍCULO CAN ---");
  Serial.printf("Chip: %s (Rev %d) | Cores: %d\n", ESP.getChipModel(), chip_info.revision, chip_info.cores);
  Serial.printf("MAC: %04X%08X\n", (uint16_t)(chipid >> 32), (uint32_t)chipid);
  Serial.println("---------------------------------");
  
  Serial.println("Iniciando bus CAN a 250kbps...");

  // Volvemos a NO_ACK: Envía tramas sin esperar confirmación
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TX_GPIO, (gpio_num_t)RX_GPIO, TWAI_MODE_NO_ACK);
  g_config.alerts_enabled = TWAI_ALERT_ALL;

  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS(); 
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    if (twai_start() == ESP_OK) {
      Serial.println("CAN: OK");
    } else {
      Serial.println("CAN: Error al iniciar");
    }
  } else {
    Serial.println("CAN: Error al instalar driver");
  }

  last_update = millis();
  target_speed = 45.0; 
}

void updateSimulation() {
  uint32_t now = millis();
  float dt = (now - last_update) / 1000.0;
  if (dt <= 0) return;
  last_update = now;

  if (abs(speed - target_speed) < 1.0) {
    static uint32_t last_target_change = 0;
    if (now - last_target_change > 5000) {
      target_speed = random(0, 85);
      last_target_change = now;
      Serial.printf("\n[SIM] Nueva velocidad objetivo: %.1f\n", target_speed);
    }
  }
  
  if (speed < target_speed) speed += 5.0 * dt;
  else if (speed > target_speed) speed -= 7.0 * dt;
  if (speed < 0) speed = 0;

  if (speed > 2) {
    batA.current = 2.0 + (speed * 0.4) + (random(0, 10) / 10.0);
    batB.current = 2.0 + (speed * 0.4) + (random(0, 10) / 10.0);
    is_charging = false;
  } else {
    batA.current = 0.1;
    batB.current = 0.1;
    is_charging = false;
  }

  // Descarga Batería A
  if (batA.current > 0) batA.soc -= (batA.current * dt) / 300.0; 
  if (batA.soc < 0) batA.soc = 0;
  batA.voltage = 66.0 + (batA.soc / 100.0) * 18.0;
  batA.temp = 25.0 + (batA.current * 0.05) + (random(0, 10) / 10.0);

  // Descarga Batería B (Simétrica)
  if (batB.current > 0) batB.soc -= (batB.current * dt) / 300.0; 
  if (batB.soc < 0) batB.soc = 0;
  batB.voltage = 66.0 + (batB.soc / 100.0) * 18.0;
  batB.temp = 25.0 + (batB.current * 0.05) + (random(0, 10) / 10.0);
}

void sendCAN() {
  twai_message_t msg;
  msg.extd = 0; 
  msg.rtr = 0;
  msg.data_length_code = 8;
  
  // --- BATERÍA A ---
  // 0x504: Voltaje y Corriente
  msg.identifier = 0x504;
  uint16_t v_send_a = (uint16_t)(batA.voltage * 100);
  int16_t i_send_a = (int16_t)(batA.current * 10);
  msg.data[2] = (v_send_a >> 8); msg.data[3] = (v_send_a & 0xFF);
  msg.data[4] = (i_send_a >> 8); msg.data[5] = (i_send_a & 0xFF);
  twai_transmit(&msg, 0);

  // 0x540: SoC y Temp
  msg.identifier = 0x540;
  msg.data[0] = (uint8_t)batA.soc;
  msg.data[3] = (uint8_t)batA.temp;
  msg.data[4] = (uint8_t)batA.temp;
  msg.data[5] = (uint8_t)batA.temp;
  msg.data[6] = (uint8_t)batA.temp;
  twai_transmit(&msg, 0);

  // --- BATERÍA B ---
  // 0x505: Voltaje y Corriente (B = A+1)
  msg.identifier = 0x505;
  uint16_t v_send_b = (uint16_t)(batB.voltage * 100);
  int16_t i_send_b = (int16_t)(batB.current * 10);
  msg.data[2] = (v_send_b >> 8); msg.data[3] = (v_send_b & 0xFF);
  msg.data[4] = (i_send_b >> 8); msg.data[5] = (i_send_b & 0xFF);
  twai_transmit(&msg, 0);

  // 0x541: SoC y Temp (B = A+1)
  msg.identifier = 0x541;
  msg.data[0] = (uint8_t)batB.soc;
  msg.data[3] = (uint8_t)batB.temp;
  msg.data[4] = (uint8_t)batB.temp;
  msg.data[5] = (uint8_t)batB.temp;
  msg.data[6] = (uint8_t)batB.temp;
  
  esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(50));
  if (err != ESP_OK) {
    Serial.printf("[CAN] Error TX: 0x%X\n", err);
  }
}

void checkCanStatus() {
  uint32_t alerts;
  twai_read_alerts(&alerts, 0);
  if (alerts & TWAI_ALERT_BUS_OFF) {
    Serial.println("[CAN] BUS OFF! Recuperando...");
    twai_initiate_recovery();
  }
  if (alerts & TWAI_ALERT_BUS_RECOVERED) {
    twai_start();
  }
}

void checkIncomingCAN() {
  twai_message_t msg;
  while (twai_receive(&msg, 0) == ESP_OK) {
    if (msg.identifier == 0x5A1) {
      Serial.printf("[TIME] Sincronización recibida -> %02d:%02d\n", msg.data[5], msg.data[6]);
    } else {
      Serial.printf("[CAN] Recibido ID: 0x%03X\n", msg.identifier);
    }
  }
}

void loop() {
  uint32_t now = millis();

  // Control manual por Serial
  if (Serial.available()) {
    String cmd = Serial.readString();
    cmd.trim();
    cmd.toLowerCase();
    
    if (cmd == "pausa" || cmd == "pause") {
      is_paused = true;
      speed = 0;
      target_speed = 0;
      Serial.println("\n🛑 [SIM] PAUSA MANUAL ACTIVADA");
    } else if (cmd == "play" || cmd == "run") {
      is_paused = false;
      target_speed = 45.0;
      last_update = millis();
      Serial.println("\n🚀 [SIM] MARCHA REANUDADA");
    }
  }

  checkCanStatus();
  checkIncomingCAN();

  if (!is_paused) {
    updateSimulation();
    sendCAN();
  }
  
  static uint32_t last_print = 0;
  if (now - last_print > 1000) {
    if (is_paused) {
      Serial.println("[SIM] PAUSADO (Escribe 'play' para continuar)");
    } else {
      Serial.printf("SPD: %.1f km/h | BatA: %.1f%% (%.1fV) | BatB: %.1f%% (%.1fV)\n", speed, batA.soc, batA.voltage, batB.soc, batB.voltage);
    }
    last_print = now;
  }
  delay(250); 
}
