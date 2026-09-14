// ═══════════════════════════════════════════════════════════════════════════
// CanRider — simulador de bus CAN (banco de pruebas)
// ═══════════════════════════════════════════════════════════════════════════
// Sketch independiente para un SEGUNDO ESP32 + transceptor CAN (no es parte
// del firmware principal de main/). Se comporta como si fuera la moto: emite
// las mismas tramas que el BMS real (batería A/B, estado de carga), para
// poder probar la recepción del dispositivo principal sin tener la moto
// delante — útil ahora mismo para aislar si el problema de "sin CAN" está en
// el cableado/transceptor del dispositivo principal, en el propio cableado
// hacia la moto, o en la ECU de la moto.
//
// Además escucha cualquier trama que le llegue y la imprime por Serial — así
// se ve directamente si la trama de la hora (0x510) del dispositivo
// principal está llegando hasta aquí, sin depender del panel de la moto.
//
// Cableado: CAN-H con CAN-H, CAN-L con CAN-L, GND común entre los dos
// transceptores. Si son los dos únicos nodos del bus de pruebas (sin la moto
// de por medio), cada extremo necesita su resistencia de 120 Ω entre CAN-H y
// CAN-L — muchos módulos SN65HVD230/TJA1050 ya la traen puenteable con un
// jumper. Ajusta los pines de abajo a como lo hayas cableado tú.
//
// No toca nada del proyecto principal ni de Supabase — esto es solo tráfico
// CAN local en el banco.

#include "driver/twai.h"

// ── Placa del emisor — AJUSTA ESTO a tu cableado real ───────────────────────
// Mismo espíritu que MODEM_A7670G/MODEM_SIM7000G en main/config.h: un nombre
// de placa en vez de un número de pin suelto — así no se puede tener un pin
// "sin etiquetar" que luego nadie sepa a qué placa correspondía (justo el
// error que se encontró en config.h: pines del A7670G activos mientras
// corría el firmware del SIM7000G, sin que nada avisara). Activa una sola
// opción; si usas otra placa distinta, añade tu propio #elif con sus pines.
#define SIM_BOARD_CUSTOM
// #define SIM_BOARD_LILYGO_SIM7000G   // ESP32 T-SIM7000G de repuesto como emisor

#if defined(SIM_BOARD_LILYGO_SIM7000G)
  #define CAN_TX_PIN 32
  #define CAN_RX_PIN 33
#elif defined(SIM_BOARD_CUSTOM)
  #define CAN_TX_PIN 4    // <- pon aquí tu cableado real
  #define CAN_RX_PIN 5
#else
  #error "Define arriba qué placa usas (SIM_BOARD_...)"
#endif

// Mismas frame_id/byte que usa main/config.h — cámbialos aquí también si los
// cambiaste allí.
#define BATTERY_A_FRAME 0x540
#define BATTERY_A_BYTE  0
#define BATTERY_B_FRAME 0x541
#define BATTERY_B_BYTE  0
#define CHARGING_FRAME   0x506
#define CHARGING_BYTE    1
#define CHARGING_BITMASK 0x10

static uint32_t lastSendMs = 0;
#define SEND_INTERVAL_MS 200   // igual que txIntervalMs por defecto en main.ino

// SoC simulado: sube y baja despacio entre 55-70% para que se note en el
// portal que llegan datos frescos, no un valor estático que podría parecer
// caché.
static int  socA = 60, socB = 65;
static int  dirA = 1,  dirB = -1;
static uint32_t lastSocStepMs = 0;

static bool charging = false;
static uint32_t lastChargeToggleMs = 0;

void sendFrame(uint32_t id, uint8_t byteIdx, uint8_t value) {
    twai_message_t msg = {};
    msg.identifier       = id;
    msg.extd             = 0;
    msg.data_length_code = 8;
    msg.data[byteIdx]    = value;
    if (twai_transmit(&msg, pdMS_TO_TICKS(5)) != ESP_OK) {
        Serial.printf("[TX] fallo al mandar 0x%03X\n", id);
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[SIM] CanRider - simulador de bus CAN (banco de pruebas)");
    Serial.printf("[SIM] TX=%d RX=%d, 250 kbps\n", CAN_TX_PIN, CAN_RX_PIN);

    twai_general_config_t gcfg = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
    gcfg.rx_queue_len = 64;
    twai_timing_config_t tcfg = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t fcfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&gcfg, &tcfg, &fcfg) != ESP_OK || twai_start() != ESP_OK) {
        Serial.println("[SIM] ERROR al iniciar TWAI — revisa los pines/transceptor");
    } else {
        Serial.println("[SIM] TWAI listo, emitiendo bateria A/B + estado de carga cada 200ms");
        Serial.println("[SIM] Escuchando cualquier trama entrante (p.ej. la hora, 0x510)...");
    }
}

void loop() {
    uint32_t now = millis();

    // — Recepción: imprime cualquier trama que llegue —
    twai_message_t rx;
    while (twai_receive(&rx, 0) == ESP_OK) {
        Serial.printf("[RX] id=0x%03X len=%d data=", rx.identifier, rx.data_length_code);
        for (int i = 0; i < rx.data_length_code; i++) Serial.printf("%02X ", rx.data[i]);
        Serial.println();
    }

    // — SoC simulado: un paso arriba/abajo cada segundo —
    if (now - lastSocStepMs > 1000) {
        lastSocStepMs = now;
        socA += dirA; if (socA >= 70 || socA <= 55) dirA = -dirA;
        socB += dirB; if (socB >= 70 || socB <= 55) dirB = -dirB;
    }

    // — Estado de carga simulado: cambia cada 20s —
    if (now - lastChargeToggleMs > 20000) {
        lastChargeToggleMs = now;
        charging = !charging;
        Serial.printf("[SIM] Cambiando estado de carga simulado a: %s\n", charging ? "cargando" : "no cargando");
    }

    // — Emisión periódica —
    if (now - lastSendMs >= SEND_INTERVAL_MS) {
        lastSendMs = now;
        sendFrame(BATTERY_A_FRAME, BATTERY_A_BYTE, (uint8_t)socA);
        sendFrame(BATTERY_B_FRAME, BATTERY_B_BYTE, (uint8_t)socB);
        sendFrame(CHARGING_FRAME, CHARGING_BYTE, charging ? CHARGING_BITMASK : 0x00);
    }
}
