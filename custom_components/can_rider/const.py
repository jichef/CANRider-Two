DOMAIN = "can_rider"
CONF_SUPABASE_URL = "supabase_url"
CONF_SUPABASE_KEY = "supabase_key"
CONF_VEHICLE_ID = "vehicle_id"
CONF_BOARD_MODEL = "board_model"

DEFAULT_NAME = "CanRider"
DEFAULT_BOARD_MODEL = "A7670G"

# Debe coincidir con MODEM_A7670G / MODEM_SIM7000G de main/config.h en el firmware.
BOARD_MODEL_NAMES = {
    "A7670G": "LilyGo T-A7670G (ESP32)",
    "SIM7000G": "LilyGo T-SIM7000G (ESP32)",
}
