#include <Arduino.h>
#include "driver/twai.h"

// Pines CAN (coincidentes con la configuración del receptor)
#define CAN_RX_PIN 21
#define CAN_TX_PIN 22

// Variables de simulación del vehículo
float soc = 100.0;        // Empieza al 100% con cada reinicio
float voltage = 84.0;    // Voltaje para sistema de 72V (84V cargado)
float current = 0.0;     // Amperios
float speed = 0.0;       // km/h
float temp = 25.0;       // Temperatura en Celsius
bool is_charging = false;
uint32_t last_update = 0;
float target_speed = 0.0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n--- SIMULADOR DE VEHÍCULO CAN ---");
  Serial.println("Iniciando bus CAN a 250kbps...");

  // Configuración del driver TWAI (CAN) del ESP32
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
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
  target_speed = 45.0; // Velocidad objetivo inicial
}

void updateSimulation() {
  uint32_t now = millis();
  float dt = (now - last_update) / 1000.0;
  if (dt <= 0) return;
  last_update = now;

  // Lógica de movimiento: aceleración y frenado suave
  if (abs(speed - target_speed) < 1.0) {
    // Si llegamos a la velocidad objetivo, definimos una nueva tras un tiempo
    static uint32_t last_target_change = 0;
    if (now - last_target_change > 5000) {
      target_speed = random(0, 85); // Cambiar destino aleatoriamente
      last_target_change = now;
      Serial.print("\n[SIM] Nueva velocidad objetivo: ");
      Serial.println(target_speed);
    }
  }
  
  // Aceleración de 5 km/h por segundo
  if (speed < target_speed) speed += 5.0 * dt;
  else if (speed > target_speed) speed -= 7.0 * dt;
  if (speed < 0) speed = 0;

  // Simular corriente basada en la demanda de velocidad
  if (speed > 2) {
    // Consumo base + proporcional a la velocidad
    current = 2.0 + (speed * 0.8) + (random(0, 20) / 10.0);
    is_charging = false;
  } else {
    current = 0.2; // Consumo residual en parado
    is_charging = false;
  }

  // Descarga de batería (SoC)
  // Simulamos una descarga acelerada para que sea visible (10% por minuto a 50A)
  if (current > 0) {
    soc -= (current * dt) / 300.0; 
  }
  if (soc < 0) soc = 0;

  // El voltaje cae linealmente con el SoC (84V a 66V)
  voltage = 66.0 + (soc / 100.0) * 18.0;
  
  // La temperatura sube ligeramente con el consumo
  temp = 25.0 + (current * 0.05) + (random(0, 10) / 10.0);
}

void sendCAN() {
  twai_message_t msg;
  msg.extd = 0; // Standard Frame
  msg.rtr = 0;
  msg.data_length_code = 8;
  
  // Trama 0x504: Voltaje y Corriente (Batería A)
  msg.identifier = 0x504;
  uint16_t v_send = (uint16_t)(voltage * 100);       // Escala 0.01
  int16_t i_send = (int16_t)(current * 10);         // Escala 0.1
  msg.data[0] = 0x00;
  msg.data[1] = 0x00;
  msg.data[2] = (v_send >> 8) & 0xFF;
  msg.data[3] = v_send & 0xFF;
  msg.data[4] = (i_send >> 8) & 0xFF;
  msg.data[5] = i_send & 0xFF;
  msg.data[6] = 0x00;
  msg.data[7] = 0x00;
  twai_transmit(&msg, pdMS_TO_TICKS(10));

  // Trama 0x506: Estado y Modo
  msg.identifier = 0x506;
  msg.data[0] = is_charging ? 0x01 : 0x00;
  msg.data[1] = 0x00;
  msg.data[2] = 0x00;
  msg.data[3] = 0x00;
  msg.data[4] = 0x00;
  msg.data[5] = 0x00;
  msg.data[6] = 0x00;
  msg.data[7] = is_charging ? 0x10 : 0x20; // 0x10 Charging, 0x20 Riding
  twai_transmit(&msg, pdMS_TO_TICKS(10));

  // Trama 0x540: SoC y Temperatura (promedio de 4 sensores)
  msg.identifier = 0x540;
  msg.data[0] = (uint8_t)soc;
  msg.data[1] = 0x00;
  msg.data[2] = 0x00;
  msg.data[3] = (uint8_t)temp;
  msg.data[4] = (uint8_t)temp;
  msg.data[5] = (uint8_t)temp;
  msg.data[6] = (uint8_t)temp;
  msg.data[7] = 0x00;
  twai_transmit(&msg, pdMS_TO_TICKS(10));

  // Trama 0x54E: SoC redundante y Voltaje (escala 0.1)
  msg.identifier = 0x54E;
  uint16_t v_short = (uint16_t)(voltage * 10);
  msg.data[0] = 0x00;
  msg.data[1] = 0x00;
  msg.data[2] = 0x00;
  msg.data[3] = 0x00;
  msg.data[4] = (uint8_t)soc;
  msg.data[5] = (v_short >> 8) & 0xFF;
  msg.data[6] = v_short & 0xFF;
  msg.data[7] = 0x00;
  twai_transmit(&msg, pdMS_TO_TICKS(10));
}

void loop() {
  updateSimulation();
  sendCAN();
  
  // Monitor serie para depuración
  static uint32_t last_print = 0;
  if (millis() - last_print > 1000) {
    Serial.print("SPD: "); Serial.print(speed, 1); Serial.print(" km/h | ");
    Serial.print("SoC: "); Serial.print(soc, 1); Serial.print("% | ");
    Serial.print("V: "); Serial.print(voltage, 1); Serial.print("V | ");
    Serial.print("A: "); Serial.print(current, 1); Serial.println("A");
    last_print = millis();
  }
  
  delay(250); // Frecuencia de envío (~4Hz)
}
