#include <Arduino.h>
#include "driver/twai.h"

// Pines CAN (coincidentes con la configuración del receptor)
#define TX_GPIO GPIO_NUM_33
#define RX_GPIO GPIO_NUM_32

// Variables de simulación del vehículo
float soc = 100.0;        
float voltage = 84.0;    
float current = 0.0;     
float speed = 0.0;       
float temp = 25.0;       
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
    current = 2.0 + (speed * 0.8) + (random(0, 20) / 10.0);
    is_charging = false;
  } else {
    current = 0.2;
    is_charging = false;
  }

  if (current > 0) soc -= (current * dt) / 300.0; 
  if (soc < 0) soc = 0;
  voltage = 66.0 + (soc / 100.0) * 18.0;
  temp = 25.0 + (current * 0.05) + (random(0, 10) / 10.0);
}

void sendCAN() {
  twai_message_t msg;
  msg.extd = 0; 
  msg.rtr = 0;
  msg.data_length_code = 8;
  
  // 0x504: Voltaje y Corriente
  msg.identifier = 0x504;
  uint16_t v_send = (uint16_t)(voltage * 100);
  int16_t i_send = (int16_t)(current * 10);
  msg.data[0] = 0x00; msg.data[1] = 0x00;
  msg.data[2] = (v_send >> 8); msg.data[3] = (v_send & 0xFF);
  msg.data[4] = (i_send >> 8); msg.data[5] = (i_send & 0xFF);
  msg.data[6] = 0x00; msg.data[7] = 0x00;
  twai_transmit(&msg, 0);

  // 0x540: SoC y Temp
  msg.identifier = 0x540;
  msg.data[0] = (uint8_t)soc;
  msg.data[1] = 0x00; msg.data[2] = 0x00;
  msg.data[3] = (uint8_t)temp;
  msg.data[4] = (uint8_t)temp;
  msg.data[5] = (uint8_t)temp;
  msg.data[6] = (uint8_t)temp;
  msg.data[7] = 0x00;
  
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
      Serial.printf("SPD: %.1f km/h | SoC: %.1f%% | V: %.1fV | A: %.1fA\n", speed, soc, voltage, current);
    }
    last_print = now;
  }
  delay(250); 
}
