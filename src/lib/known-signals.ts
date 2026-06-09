// Variables que el dashboard sabe mostrar.
// Si creas una señal CAN con uno de estos nombres, aparecerá en pantalla
// con su etiqueta y unidad correctas.

export type KnownSignal = {
  name: string
  unit: string
  summary: string    // una línea, qué mide
  hint: string       // cómo configurarla (scale, signed, bits…)
}

export const KNOWN_SIGNALS: KnownSignal[] = [
  {
    name: 'soc',
    unit: '%',
    summary: 'Estado de carga de la batería (0–100 %)',
    hint: 'El raw suele venir ya en %, usa scale=1. Comprueba que coincide con el cuadro de instrumentos.',
  },
  {
    name: 'pack_voltage',
    unit: 'V',
    summary: 'Tensión total del pack de baterías',
    hint: 'Si el raw viene en décimas de voltio (lo más habitual), usa scale=0.1. Rango típico: 40–100 V.',
  },
  {
    name: 'battery_current',
    unit: 'A',
    summary: 'Corriente del pack — positivo=carga, negativo=descarga',
    hint: 'Activa "Con signo". Si el raw viene en décimas de amperio, usa scale=0.1. Verifica la convención de tu BMS (algunos invierten el signo).',
  },
  {
    name: 'charge_current',
    unit: 'A',
    summary: 'Corriente activa durante la carga',
    hint: 'Solo tiene valor mientras el cargador está conectado. scale=0.1 si el raw viene en décimas de amperio.',
  },
  {
    name: 'cell_voltage',
    unit: 'V',
    summary: 'Tensión de celda individual (la más representativa del BMS)',
    hint: 'El raw suele venir en milésimas de voltio. Usa scale=0.001. Rango típico por celda: 2.5–4.2 V (Li-ion).',
  },
  {
    name: 'bms_charging',
    unit: '',
    summary: 'Flag de carga activa: 1 = cargando, 0 = no',
    hint: 'Es un único bit dentro de un byte de estado. Rellena "Bit mask" con el valor del bit (ej: 0x10 = bit 4). byte_length=1, scale=1.',
  },
  {
    name: 'temp1',
    unit: '°C',
    summary: 'Temperatura de celda o zona 1',
    hint: 'Algunos BMS codifican la temperatura con un offset de +40 (raw = T + 40). Si las lecturas parecen 40 °C por encima, usa offset_val=−40.',
  },
  {
    name: 'temp2',
    unit: '°C',
    summary: 'Temperatura de celda o zona 2',
    hint: 'Igual que temp1. Aplica el mismo offset si es necesario.',
  },
  {
    name: 'temp3',
    unit: '°C',
    summary: 'Temperatura de celda o zona 3',
    hint: 'Igual que temp1. Aplica el mismo offset si es necesario.',
  },
  {
    name: 'temp4',
    unit: '°C',
    summary: 'Temperatura de celda o zona 4',
    hint: 'Igual que temp1. Aplica el mismo offset si es necesario.',
  },
  {
    name: 'charging_status_raw',
    unit: '',
    summary: 'Byte de estado de carga sin procesar',
    hint: 'Guarda el byte entero tal cual (scale=1, byte_length=1). Útil para diagnóstico; consulta el manual de tu BMS para interpretar los valores.',
  },
  {
    name: 'battery_mode_raw',
    unit: '',
    summary: 'Byte de modo de operación del BMS sin procesar',
    hint: 'Guarda el byte entero tal cual (scale=1, byte_length=1). Consulta el manual de tu BMS para los valores posibles.',
  },
  {
    name: 'max_voltage',
    unit: 'V',
    summary: 'Tensión máxima de carga configurada en el BMS',
    hint: 'Límite superior que el BMS permite. scale=0.1 si el raw viene en décimas. No cambia durante el uso normal.',
  },
  {
    name: 'max_charge_current',
    unit: 'A',
    summary: 'Corriente máxima de carga configurada en el BMS',
    hint: 'Límite de corriente que el BMS permite al cargador. scale=0.1 si el raw viene en décimas. No cambia durante el uso normal.',
  },

  // ── Reloj TX — el ESP32 emite la hora NITZ a la pantalla ──────────────────
  {
    name: 'clock_hours',
    unit: 'h',
    summary: 'Horas del reloj emitidas al bus CAN (0–23)',
    hint: 'Direccion TX. El firmware escribe la hora NITZ en este byte. Configura frame_id=0x510 (o el que lea tu pantalla), byte_start=5, scale=1, byte_length=1.',
  },
  {
    name: 'clock_minutes',
    unit: 'min',
    summary: 'Minutos del reloj emitidos al bus CAN (0–59)',
    hint: 'Direccion TX. Mismo frame_id que clock_hours; byte_start=6. El firmware actualiza el valor NITZ cada tx_interval_ms.',
  },
]
