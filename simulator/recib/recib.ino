#include <Arduino.h>
#include "driver/twai.h"

#define TX_GPIO GPIO_NUM_14
#define RX_GPIO GPIO_NUM_15

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("📥 RECEPTOR CAN (sniffer)");

  twai_general_config_t g_config =
      TWAI_GENERAL_CONFIG_DEFAULT(
          TX_GPIO,
          RX_GPIO,
          TWAI_MODE_LISTEN_ONLY  // 👈 CLAVE
      );

  twai_timing_config_t t_config =
      TWAI_TIMING_CONFIG_250KBITS();

  twai_filter_config_t f_config =
      TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t err;
  err = twai_driver_install(&g_config, &t_config, &f_config);
  Serial.printf("driver_install: %s\n", esp_err_to_name(err));
  if (err != ESP_OK) while (1);

  err = twai_start();
  Serial.printf("twai_start: %s\n", esp_err_to_name(err));
  if (err != ESP_OK) while (1);

  Serial.println("✅ RECEPTOR LISTO (LISTEN_ONLY)");
}

void loop() {
  twai_message_t msg;
  if (twai_receive(&msg, pdMS_TO_TICKS(1000)) == ESP_OK) {
    Serial.print("📥 RX ID=0x");
    Serial.print(msg.identifier, HEX);
    Serial.print(msg.extd ? " EXT " : " STD ");
    Serial.print("DLC=");
    Serial.print(msg.data_length_code);
    Serial.print(" DATA=");

    for (int i = 0; i < msg.data_length_code; i++) {
      if (msg.data[i] < 16) Serial.print("0");
      Serial.print(msg.data[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
  }
}
