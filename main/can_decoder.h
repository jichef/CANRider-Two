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

// Declaración externa para rastrear actividad CAN
extern uint32_t lastCanActivityTime;

void decodeCANFrame(const twai_message_t &msg) {
  uint32_t id = msg.identifier;
  const uint8_t *data = msg.data;

  // Si recibimos una trama válida de las que nos interesan, actualizamos actividad
  if (id >= 0x500 && id <= 0x5FF) {
    lastCanActivityTime = millis();
  }

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
  twai_message_t message;
  // Recibir tramas con un pequeño timeout para dar respiro (como recib.ino)
  if (twai_receive(&message, pdMS_TO_TICKS(10)) == ESP_OK) {
    // Imprimir para debug como recib.ino
    Serial.printf("📥 RX ID=0x%03X DATA=", message.identifier);
    for (int i = 0; i < message.data_length_code; i++) Serial.printf("%02X ", message.data[i]);
    Serial.println();

    decodeCANFrame(message);
  }
}

uint8_t can_tx_seq = 0; // Contador de secuencia para tramas TX

bool can_send_time(uint8_t hour, uint8_t minute, uint8_t second = 0) {
  twai_message_t message;
  message.identifier = 0x510;
  message.extd = 0;           // Trama estándar
  message.rtr = 0;            // No es remota
  message.data_length_code = 8;
  
  // Formato OLD robusto (Firma A1 + Secuencia + Hora/Min)
  message.data[0] = 0xA1;           // Firma emisor
  message.data[1] = can_tx_seq++;   // Secuencia incremental
  message.data[2] = 0x01;
  message.data[3] = 0x00;
  message.data[4] = 0x70;
  message.data[5] = hour;
  message.data[6] = minute;
  message.data[7] = 0x00;
  
  esp_err_t err = twai_transmit(&message, pdMS_TO_TICKS(10));
  if (err != ESP_OK) {
    Serial.printf("❌ Error enviando hora (0x510): %s\n", esp_err_to_name(err));
  } else {
    // Solo loguear cada 10 envíos para no saturar si va a 200ms
    if (can_tx_seq % 10 == 0) {
      Serial.printf("📤 CAN TX Time (0x510): %02d:%02d:%02d (Seq: %d)\n", hour, minute, second, can_tx_seq);
    }
  }
  return err == ESP_OK;
}

#endif
