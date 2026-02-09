#ifndef CAN_DECODER_H
#define CAN_DECODER_H

#include <Arduino.h>
#include "driver/twai.h"

// Estructura para almacenar los datos de una batería
struct BatteryData {
  float voltage = 0;
  float current = 0;
  int soc = -1;
  float temp = 0;
  bool is_charging = false;
  uint8_t mode = 0; // 0x10 = charging, 0x20 = riding
};

// Datos globales para Batería A y B
BatteryData batA;
BatteryData batB;

void decodeCANFrame(const twai_message_t &msg) {
  uint32_t id = msg.identifier;
  const uint8_t *data = msg.data;

  // Determinar si es Batería A o B (B-mode es ID + 1)
  bool isBatB = (id % 2 != 0 && id >= 0x505 && id <= 0x54F);
  BatteryData &bat = isBatB ? batB : batA;
  uint32_t baseId = isBatB ? id - 1 : id;

  switch (baseId) {
    case 0x504: // Charging Info
      bat.voltage = (float)((data[2] << 8) | data[3]) / 100.0f;
      bat.current = (float)((int16_t)((data[4] << 8) | data[5])) / 10.0f;
      break;

    case 0x506: // State and Charging
      bat.is_charging = (data[0] & 0x01) || (data[1] & 0x10);
      bat.mode = data[7];
      break;

    case 0x540: // State
      bat.soc = data[0];
      // Promedio de las 4 temperaturas (bytes 3, 4, 5, 6)
      bat.temp = (float)(data[3] + data[4] + data[5] + data[6]) / 4.0f;
      break;

    case 0x54E: // Battery parameters
      // SoC también disponible aquí, lo usamos si no tenemos el de 0x540
      if (bat.soc == -1) bat.soc = data[4];
      if (bat.voltage == 0) bat.voltage = (float)((data[5] << 8) | data[6]) / 10.0f;
      break;
  }
}

// Inicialización de CAN (TWAI) en ESP32
// Por defecto pines 21 (TX) y 22 (RX) son comunes, pero en T-A7670G 
// debemos verificar los pines disponibles. 
bool can_setup(int rx_pin, int tx_pin) {
  // Modo LISTEN_ONLY: Pasivo total. No envía ACKs ni tramas.
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)tx_pin, (gpio_num_t)rx_pin, TWAI_MODE_LISTEN_ONLY);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS(); 
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    if (twai_start() == ESP_OK) {
      return true;
    }
  }
  return false;
}

void can_update() {
  twai_status_info_t status;
  if (twai_get_status_info(&status) == ESP_OK) {
    if (status.state == TWAI_STATE_BUS_OFF) {
      Serial.printf("⚠️ CAN Bus-Off! Errores: TX:%u, RX:%u. Recuperando...\n", status.tx_error_counter, status.rx_error_counter);
      twai_initiate_recovery();
      return;
    } else if (status.state == TWAI_STATE_STOPPED) {
      twai_start();
      return;
    }
  }

  twai_message_t message;
  // Volvemos a timeout 0 para que la tarea sea lo más rápida posible
  while (twai_receive(&message, 0) == ESP_OK) {
    Serial.print("📥 CAN RX ID=0x");
    Serial.print(message.identifier, HEX);
    Serial.print(message.extd ? " EXT " : " STD ");
    Serial.print("DLC=");
    Serial.print(message.data_length_code);
    Serial.print(" DATA=");

    for (int i = 0; i < message.data_length_code; i++) {
      if (message.data[i] < 16) Serial.print("0");
      Serial.print(message.data[i], HEX);
      Serial.print(" ");
    }
    Serial.println();

    decodeCANFrame(message);
  }
}

bool can_send_time(uint8_t hour, uint8_t minute) {
  twai_message_t message;
  message.identifier = 0x5A1;
  message.extd = 0;           // Trama estándar
  message.rtr = 0;            // No es remota
  message.data_length_code = 8;
  message.data[0] = 0xA1;
  message.data[1] = 0x00;
  message.data[2] = 0x01;
  message.data[3] = 0x00;
  message.data[4] = 0x70;
  message.data[5] = hour;
  message.data[6] = minute;
  message.data[7] = 0x00;
  
  esp_err_t err = twai_transmit(&message, pdMS_TO_TICKS(10));
  if (err != ESP_OK) {
    // Si ves este error, revisa el cable del pin TX (33)
    Serial.printf("❌ Error enviando hora: %s\n", esp_err_to_name(err));
  } else {
    Serial.printf("📤 CAN TX Time: %02d:%02d\n", hour, minute);
  }
  return err == ESP_OK;
}

#endif
