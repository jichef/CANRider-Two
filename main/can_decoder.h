#ifndef CAN_DECODER_H
#define CAN_DECODER_H

#include <Arduino.h>
#include "driver/twai.h"
#include "can_config.h"

// Estructura para almacenar los datos de una batería
struct BatteryData {
  float voltage = 0;
  float current = 0;
  int soc = -1;
  float temp = 0;
  bool is_charging = false;
  uint8_t mode = 0; 
};

// Datos globales para Batería A y B
BatteryData batA;
BatteryData batB;

// Declaración externa para rastrear actividad CAN
extern uint32_t lastCanActivityTime;

// Función auxiliar para extraer valores basados en reglas
float extractValueFromFrame(const uint8_t *data, const CANRule &rule) {
    uint32_t rawValue = 0;
    
    if (rule.length == 1) {
        rawValue = data[rule.start_byte];
    } else if (rule.length == 2) {
        if (rule.big_endian) {
            rawValue = (data[rule.start_byte] << 8) | data[rule.start_byte + 1];
        } else {
            rawValue = (data[rule.start_byte + 1] << 8) | data[rule.start_byte];
        }
    }

    float finalValue;
    if (rule.is_signed && rule.length == 2) {
        finalValue = (float)((int16_t)rawValue) * rule.factor;
    } else {
        finalValue = (float)rawValue * rule.factor;
    }
    
    return finalValue;
}

void decodeCANFrame(const twai_message_t &msg) {
  uint32_t id = msg.identifier;
  const uint8_t *data = msg.data;

  lastCanActivityTime = millis();

  // Determinar si es Batería A o B
  // Si el ID coincide directamente con la regla, es Bat A. 
  // Si coincide con ID + offset, es Bat B.
  
  auto processRule = [&](const CANRule &rule, float &targetA, float &targetB) {
      if (id == rule.id) {
          targetA = extractValueFromFrame(data, rule);
      } else if (id == rule.id + manualConfig.bat_b_offset) {
          targetB = extractValueFromFrame(data, rule);
      }
  };

  processRule(manualConfig.voltage, batA.voltage, batB.voltage);
  processRule(manualConfig.current, batA.current, batB.current);
  processRule(manualConfig.soc, batA.soc, batB.soc); // Nota: soc es float aquí internamente por extractValue
  
  // Caso especial SOC: convertir a int si es necesario
  if (id == manualConfig.soc.id) batA.soc = (int)extractValueFromFrame(data, manualConfig.soc);
  if (id == manualConfig.soc.id + manualConfig.bat_b_offset) batB.soc = (int)extractValueFromFrame(data, manualConfig.soc);

  processRule(manualConfig.temp, batA.temp, batB.temp);

  // Lógica fija mínima para carga (esto también podría ser una regla en el futuro)
  if (id == 0x506 || id == 0x507) {
      BatteryData &bat = (id == 0x507) ? batB : batA;
      bat.is_charging = (data[0] & 0x01) || (data[1] & 0x10);
      bat.mode = data[7];
  }
}

// Inicialización de CAN (TWAI) en ESP32
bool can_setup(int rx_pin, int tx_pin) {
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)tx_pin, (gpio_num_t)rx_pin, TWAI_MODE_NORMAL);
  g_config.tx_queue_len = 10; 
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
  if (twai_receive(&message, pdMS_TO_TICKS(10)) == ESP_OK) {
    decodeCANFrame(message);
  }
}

uint8_t can_tx_seq = 0; 
bool can_send_time(uint8_t hour, uint8_t minute, uint8_t second = 0) {
  twai_message_t message;
  message.identifier = 0x510;
  message.extd = 0;
  message.rtr = 0;
  message.data_length_code = 8;
  
  message.data[0] = 0xA1;
  message.data[1] = can_tx_seq++;
  message.data[2] = 0x01;
  message.data[3] = 0x00;
  message.data[4] = 0x70;
  message.data[5] = hour;
  message.data[6] = minute;
  message.data[7] = 0x00;
  
  esp_err_t err = twai_transmit(&message, pdMS_TO_TICKS(10));
  return err == ESP_OK;
}

#endif
