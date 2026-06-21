# CanRider — Telemetría CAN para Vehículo Eléctrico

Sistema de telemetría en tiempo real para vehículos eléctricos con bus CAN. Lee los datos del BMS y otros módulos a través del bus CAN, los transmite vía LTE a una base de datos en la nube y los muestra en un portal web accesible desde cualquier dispositivo.

---

## ¿Qué hace?

- **Lee el bus CAN** del vehículo (estado de carga, tensión, corriente, temperaturas, etc.)
- **Emite tramas CAN** configurables (p. ej. hora sincronizada a la pantalla del vehículo)
- **Envía telemetría** cada 15 segundos a Supabase vía LTE (sin WiFi)
- **Registra viajes** automáticamente: distancia (GPS), velocidad máxima, consumo de batería
- **Panel web** en tiempo real con mapa, historial de viajes y estado del sistema
- **Portal de configuración** para definir las señales CAN sin tocar el firmware

---

## Arquitectura

```
[ Vehículo EV ]
      │  CAN bus
      ▼
[ LilyGo T-A7670G ]  ←── firmware Arduino (main/main.ino)
      │  LTE (HTTP POST)
      ▼
[ Supabase ]  ←── base de datos PostgreSQL en la nube
      │  WebSocket / REST
      ▼
[ Portal Web ]  ←── Next.js (este repositorio, carpeta src/)
```

El ESP32 actúa como ECU secundaria: escucha tramas del bus CAN y puede emitir las suyas propias (solo las que el usuario haya configurado explícitamente, nunca nada hardcodeado).

---

## Hardware necesario

| Componente | Descripción |
|---|---|
| **LilyGo T-A7670G** | Placa ESP32 con módem LTE integrado |
| **Transceptor CAN** | SN65HVD230 o similar (conectado a los pines CAN del ESP32, asi como energia 3.3) |
| **SIM con datos** | Tarjeta SIM con APN activo (cualquier operador) |
| **Acceso al bus CAN** | Conector OBD2 o cable directo a CAN-H / CAN-L del vehículo |

### Conexiones por defecto

| Pin ESP32 | Función |
|---|---|
| GPIO 21 | CAN TX (hacia el transceptor) |
| GPIO 22 | CAN RX (desde el transceptor) |

> Los pines se pueden cambiar en `main/config.h`.

---

## Requisitos de software

- [Arduino IDE 2.x](https://www.arduino.cc/en/software) con soporte para ESP32
- [Node.js 18+](https://nodejs.org/) y npm
- Cuenta gratuita en [Supabase](https://supabase.com/)
- Cuenta en [Vercel](https://vercel.com/) (opcional, para publicar el portal)

### Instalar soporte ESP32 en Arduino IDE

1. Abre **Arduino IDE → Preferences**
2. En *Additional boards manager URLs* añade:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Ve a **Tools → Board → Boards Manager**, busca `esp32` e instala el paquete de Espressif.
4. Selecciona la placa: **Tools → Board → ESP32 Arduino → ESP32 Dev Module**

---

## Instalación paso a paso

### 1. Clonar el repositorio

```bash
git clone https://github.com/tu-usuario/CanRider.git
cd CanRider
```

### 2. Configurar Supabase

#### 2a. Crear el proyecto

1. Entra en [supabase.com](https://supabase.com/) y crea un proyecto nuevo.
2. Anota la **URL del proyecto** y la **anon key** (las encontrarás en *Project Settings → API*).

#### 2b. Crear las tablas

En el **SQL Editor** de Supabase, ejecuta los siguientes archivos en orden:

```sql
-- 1. Tabla de señales CAN
-- Copia y ejecuta el contenido de: supabase/schema.sql

-- 2. Tabla de telemetría
-- Copia y ejecuta el contenido de: supabase/telemetry_schema.sql

-- 3. Tabla de viajes
-- Copia y ejecuta el contenido de: supabase/trips_schema.sql
```

> Después de crear las tablas puedes cargar señales CAN de ejemplo ejecutando `supabase/seed_cpx.sql` (edita primero el `:VEHICLE_ID` por un UUID real, por ejemplo el que generes con `gen_random_uuid()`).

### 3. Configurar el firmware

```bash
# Copia la plantilla de configuración
cp main/config.h.example main/config.h
```

Edita `main/config.h` con tus valores:

```cpp
#define APN          "internet.tuoperadora.es"   // APN de tu SIM
#define SUPABASE_URL "https://XXXX.supabase.co"  // URL de tu proyecto
#define SUPABASE_KEY "eyJ..."                    // anon key de Supabase
#define VEHICLE_ID   "550e8400-e29b-41d4-a716-446655440000"  // UUID único para este vehículo

#define CAN_TX_PIN   21   // GPIO conectado al TX del transceptor CAN
#define CAN_RX_PIN   22   // GPIO conectado al RX del transceptor CAN
#define CAN_SPEED    TWAI_TIMING_CONFIG_500KBITS()  // velocidad del bus CAN
```

> `config.h` está en `.gitignore` y nunca se sube al repositorio — contiene credenciales reales.

### 4. Cargar el firmware

1. Abre `main/main.ino` en Arduino IDE.
2. Conecta el LilyGo al PC por USB.
3. Selecciona el puerto en **Tools → Port**.
4. Pulsa **Upload** (Ctrl+U).

Abre el **Serial Monitor** (115200 baud) para ver los logs de arranque.

### 5. Instalar el portal web

```bash
npm install
```

Crea el archivo de entorno:

```
# Crea el archivo .env.local en la raíz del proyecto con este contenido:
NEXT_PUBLIC_SUPABASE_URL=https://XXXX.supabase.co
NEXT_PUBLIC_SUPABASE_ANON_KEY=eyJ...
```

Arranca el servidor de desarrollo:

```bash
npm run dev
```

Abre [http://localhost:3000](http://localhost:3000) en el navegador.

---

## Despliegue en producción (Vercel)

1. Sube el proyecto a un repositorio de GitHub.
2. Importa el repositorio en [vercel.com](https://vercel.com/).
3. En la configuración del proyecto, añade las variables de entorno:
   - `NEXT_PUBLIC_SUPABASE_URL`
   - `NEXT_PUBLIC_SUPABASE_ANON_KEY`
4. Pulsa **Deploy**.

---

## Uso del portal web

### Panel de telemetría (`/`)

Muestra en tiempo real:

- **SOC** — estado de carga de la batería del vehículo
- **Velocidad** — en km/h (GPS)
- **Tensión del pack** — voltios del paquete de baterías
- **Estado del sistema** — READY / CHARGING
- **Señal LTE** — potencia en dBm
- **Batería ESP32** — nivel y voltaje de la batería interna del módulo
- **Datos del bus CAN** — corriente, temperatura de celdas, tensión por celda, corriente de carga
- **Mapa en tiempo real** — posición GPS con Leaflet
- **Historial de viajes** — lista con distancia, duración, velocidad máxima y consumo

### Configuración de señales CAN (`/signals`)

Permite definir sin tocar el firmware qué tramas leer o emitir:

| Campo | Descripción |
|---|---|
| **Frame ID** | ID del mensaje CAN en hexadecimal (ej. `0x200`) |
| **Dirección** | `RX` = leer del bus · `TX` = emitir al bus |
| **Signal name** | Nombre interno (debe coincidir con las columnas de telemetría) |
| **Byte start** | Primer byte del payload (0–7) |
| **Byte length** | Número de bytes del campo (1, 2 o 4) |
| **Scale / Offset** | `valor_real = raw × scale + offset` |
| **Bit mask** | Para extraer un único bit de un byte de estado |
| **TX interval** | Cada cuántos ms emitir la trama (solo para TX) |

> **Seguridad CAN:** El firmware nunca emite ninguna trama que no esté definida en esta tabla. Si la tabla está vacía, no se emite nada.

#### Señales conocidas

El portal incluye autocompletado con nombres y sugerencias de configuración para señales habituales de BMS:

| Signal name | Descripción |
|---|---|
| `soc` | Estado de carga (%) |
| `pack_voltage` | Tensión total del pack (V) |
| `battery_current` | Corriente (A) — positivo=carga |
| `cell_voltage` | Tensión por celda (V) |
| `temp1`…`temp4` | Temperaturas de celdas (°C) |
| `bms_charging` | Flag de carga activa |
| `clock_hours` | Hora NITZ → pantalla del vehículo (TX) |
| `clock_minutes` | Minutos NITZ → pantalla del vehículo (TX) |

---

## Flujo de arranque del ESP32

```
Encendido
    │
    ▼
[MODEM_BOOT] — Inicializa el módem LTE, espera respuesta AT
    │
    ▼
[NET_SETUP]  — Conecta a la red (APN, registro GPRS)
               Obtiene hora NITZ por la red
               Obtiene posición GPS inicial
    │
    ▼
[HTTP_SETUP] — Comprueba conectividad con Supabase
               Descarga la tabla can_signals
               Configura el driver CAN con las señales descargadas
    │
    ▼
[RUNNING]    — Bucle principal cada 15 s:
    │            1. Lee GPS y hora
    │            2. Lee batería interna (AT+CBC)
    │            3. Lee señal de red (AT+CSQ)
    │            4. Construye JSON con todos los datos CAN acumulados
    │            5. HTTP POST → Supabase /telemetry
    │            6. Gestiona inicio/fin de viaje automáticamente
    │
    └── Task CAN (núcleo paralelo, cada 200 ms):
           · Emite las tramas TX configuradas
           · Procesa las tramas RX recibidas
```

---

## Estructura del repositorio

```
CanRider/
├── main/
│   ├── main.ino           # Firmware principal (Arduino/ESP32)
│   ├── structs.h          # Definición de tipos C++ (CANSignal, TimeRef, etc.)
│   ├── config.h.example   # Plantilla de configuración (copia como config.h)
│   └── AT/                # Librería de utilidades AT para LilyGo
│
├── src/
│   ├── app/
│   │   ├── page.tsx           # Panel de telemetría (/)
│   │   └── signals/page.tsx   # Configuración de señales CAN (/signals)
│   ├── components/
│   │   ├── DashboardContent.tsx  # UI del panel principal
│   │   ├── SignalsConfig.tsx      # CRUD de señales CAN
│   │   └── Map.tsx               # Mapa Leaflet
│   └── lib/
│       └── known-signals.ts      # Señales conocidas con hints
│
└── supabase/
    ├── schema.sql           # Tabla can_signals
    ├── telemetry_schema.sql # Tabla telemetry
    ├── trips_schema.sql     # Tabla trips
    └── seed_cpx.sql         # Señales de ejemplo para BMS genérico
```

---

## Solución de problemas

### El ESP32 no conecta a la red
- Comprueba el APN en `config.h` (debe coincidir exactamente con el de tu operador)
- Verifica que la SIM tiene datos activados y no está bloqueada por PIN
- Revisa los logs en el Serial Monitor buscando `[ERROR]` o `[TIMEOUT]`

### No aparecen datos en el panel web
- Comprueba que `NEXT_PUBLIC_SUPABASE_URL` y `NEXT_PUBLIC_SUPABASE_ANON_KEY` están correctos en `.env.local`
- Verifica que las tres tablas se han creado en Supabase (SQL Editor → Table Editor)
- Confirma que el `VEHICLE_ID` en `config.h` es el mismo UUID que usas para filtrar en el portal

### Las señales CAN no se reciben
- Comprueba la velocidad del bus: `CAN_SPEED` en `config.h` (500 kbps es el estándar automotriz más habitual)
- Verifica los pines `CAN_TX_PIN` / `CAN_RX_PIN` y el cableado del transceptor
- Confirma que `frame_id`, `byte_start` y `byte_length` son correctos para tu BMS
- El bus CAN necesita terminación de 120 Ω en cada extremo

---

## Licencia

MIT — libre para uso personal y comercial.
