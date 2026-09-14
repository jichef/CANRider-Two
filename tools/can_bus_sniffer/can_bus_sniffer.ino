// ═══════════════════════════════════════════════════════════════════════════
// CanRider — sniffer CAN pasivo (banco de pruebas)
// ═══════════════════════════════════════════════════════════════════════════
// Sketch independiente para un ESP32 + transceptor CAN (no es parte del
// firmware principal de main/). A diferencia de can_bus_simulator.ino, este
// SOLO escucha — TWAI_MODE_LISTEN_ONLY nunca intenta transmitir ni acusar
// recibo (ACK), así que nunca acumula errores de transmisión ni puede
// entrar en bus-off por su cuenta. Útil precisamente para eso: si este
// sniffer sí ve tramas (p.ej. la hora, 0x510) pero un nodo en modo NORMAL en
// el mismo bus no consigue ni enviar ni recibir, apunta a que el camino de
// TRANSMISIÓN de ese otro nodo está roto (el pin D/TX del transceptor, o su
// cableado) — el fallo de ACK constante lo empuja a bus-off, que desconecta
// también su recepción, aunque su camino RX por separado esté bien.
//
// Cableado: igual que can_bus_simulator.ino — CAN-H con CAN-H, CAN-L con
// CAN-L, GND común. Ajusta los pines de abajo a tu cableado real.

#include "driver/twai.h"

// ── Placa — AJUSTA ESTO a tu cableado real ──────────────────────────────────
#define SNIFFER_BOARD_GENERIC_4_5
// #define SNIFFER_BOARD_SIM7000G
// #define SNIFFER_BOARD_A7670G

#if defined(SNIFFER_BOARD_GENERIC_4_5)
  #define CAN_TX_PIN 4
  #define CAN_RX_PIN 5
#elif defined(SNIFFER_BOARD_SIM7000G)
  #define CAN_TX_PIN 32
  #define CAN_RX_PIN 33
#elif defined(SNIFFER_BOARD_A7670G)
  #define CAN_TX_PIN 22
  #define CAN_RX_PIN 21
#else
  #error "Define arriba qué placa usas (SNIFFER_BOARD_...)"
#endif

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n[SNIFFER] CanRider - sniffer CAN pasivo (LISTEN_ONLY)");
    Serial.printf("[SNIFFER] TX=%d RX=%d, 250 kbps\n", CAN_TX_PIN, CAN_RX_PIN);

    twai_general_config_t gcfg = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_LISTEN_ONLY);
    gcfg.rx_queue_len = 64;
    twai_timing_config_t tcfg = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t fcfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&gcfg, &tcfg, &fcfg);
    Serial.printf("[SNIFFER] driver_install: %s\n", esp_err_to_name(err));
    if (err != ESP_OK) while (1) delay(1000);

    err = twai_start();
    Serial.printf("[SNIFFER] twai_start: %s\n", esp_err_to_name(err));
    if (err != ESP_OK) while (1) delay(1000);

    Serial.println("[SNIFFER] listo, escuchando...");
}

void loop() {
    twai_message_t msg;
    if (twai_receive(&msg, pdMS_TO_TICKS(1000)) == ESP_OK) {
        Serial.printf("[RX] id=0x%03X %s dlc=%d data=",
                      msg.identifier, msg.extd ? "EXT" : "STD", msg.data_length_code);
        for (int i = 0; i < msg.data_length_code; i++) Serial.printf("%02X ", msg.data[i]);
        Serial.println();
    }
}
