# CanRider — Telemetría CAN para Vehículo Eléctrico

Sistema de telemetría en tiempo real para vehículos eléctricos con bus CAN. Lee los datos del BMS a través del bus CAN, los transmite vía LTE a una base de datos en la nube y los muestra en un portal web accesible desde cualquier dispositivo — sin depender de WiFi.

> **¿Primera vez con esto?** Hay una guía paso a paso pensada para gente sin experiencia previa: [`docs/index.html`](docs/index.html). Ábrela en el navegador y sigue los pasos en orden.

---

## Aviso legal — léelo antes de instalar nada

Este proyecto se publica **tal cual («as is»), sin garantía de ningún tipo**. Instalarlo implica manipular el bus CAN real de tu vehículo, su instalación eléctrica y su batería. Un error de cableado, de configuración (por ejemplo, un `frame_id` o byte de CAN equivocado en `config.h`) o de uso puede dañar el vehículo, sus baterías, su electrónica, o provocar un comportamiento inesperado.

**Lo instalas y lo usas bajo tu propia cuenta y riesgo.** Ni el autor ni los colaboradores de este repositorio se hacen responsables de ningún daño, pérdida, avería o problema — material o de cualquier otro tipo — derivado de una instalación, configuración o uso incorrecto de este proyecto.

---

## ¿Qué hace?

- **Lee el bus CAN** del vehículo (estado de carga de baterías, etc.)
- **Emite una trama CAN**: la hora sincronizada por red, para la pantalla del vehículo — y **nunca nada más** que eso
- **Envía telemetría** cada 15 segundos a Supabase vía LTE (sin WiFi)
- **Posición GPS**, con respaldo automático por triangulación de celda (LBS) si no hay fix GPS
- **Registra viajes** automáticamente: distancia, velocidad máxima, consumo de batería
- **Panel web** en tiempo real con mapa, historial de viajes y estado del sistema
- **Integración con Home Assistant** opcional (`custom_components/can_rider`)

---

## Arquitectura

```
[ Vehículo EV ]
      │  CAN bus (250 kbps)
      ▼
[ LilyGo T-SIM7000G ]  ←── firmware Arduino (main/main.ino)
      │  LTE (HTTPS)
      ▼
[ Supabase ]  ←── base de datos PostgreSQL en la nube
      │  WebSocket / REST
      ▼
[ Portal Web ]  ←── Next.js (este repositorio, carpeta src/)
      │
      └──  Home Assistant (opcional, custom_components/can_rider)
```

El ESP32 actúa como ECU secundaria: escucha tramas del bus CAN y emite la trama de la hora — y solo esa, nunca nada configurable por el usuario en tiempo de ejecución. Es una decisión de seguridad deliberada, no una limitación técnica.

---

## Hardware necesario

| Componente | Descripción |
|---|---|
| **LilyGo T-SIM7000G** | Placa ESP32 con módem LTE Cat-M1/NB-IoT y GPS integrados (placa probada y recomendada) |
| **Transceptor CAN** | SN65HVD230, TJA1050 o similar, alimentado a 3.3V |
| **SIM con datos** | Con APN activo (M2M/IoT de cualquier operador) |
| **Antena LTE + antena GPS** | Las que incluye el kit del T-SIM7000G |
| **Batería 18650** | Como respaldo de alimentación (opcional pero recomendado) |
| **Conversor DC-DC de alta tensión a USB** | Debe aguantar la tensión de la batería principal del vehículo (48-72V en muchas eléctricas) — alimenta el ESP32 directamente desde la moto |
| **Acceso al bus CAN del vehículo** | Cable directo a CAN-H / CAN-L (el transceptor ya incluye la resistencia de terminación de 120 Ω). En muchos vehículos, alimentación + CAN-H/CAN-L están disponibles juntos en el conector de la ECU original |

También es compatible con el **LilyGo T-A7670G** (más velocidad de datos LTE Cat-1), pero su variante estándar no lleva GPS integrado — necesita un módulo GPS externo (p.ej. Quectel L76K) aparte, con su propio cableado. Si no tienes ese módulo, usa el T-SIM7000G.

### Conexiones (bus CAN)

| Placa | CAN TX | CAN RX |
|---|---|---|
| **T-SIM7000G** | GPIO 32 | GPIO 33 |
| **T-A7670G** | GPIO 22 | GPIO 21 |

Estos pines ya vienen puestos automáticamente en `config.h.example` según la placa que elijas — no hace falta tocarlos salvo que tu cableado sea distinto.

---

## Requisitos de software

- [Arduino IDE 2.x](https://www.arduino.cc/en/software) con soporte para ESP32
- [Node.js 18+](https://nodejs.org/) y npm (solo si vas a ejecutar el portal web en local)
- Cuenta gratuita en [Supabase](https://supabase.com/)
- Cuenta gratuita en [Vercel](https://vercel.com/) (para publicar el portal sin servidor propio)

### Instalar soporte ESP32 en Arduino IDE

1. Abre **Arduino IDE → Preferences**
2. En *Additional boards manager URLs* añade:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Ve a **Tools → Board → Boards Manager**, busca `esp32` e instala el paquete de Espressif.
4. Selecciona la placa: **Tools → Board → ESP32 Arduino → ESP32 Dev Module**
5. Instala la librería **TinyGSM** desde **Tools → Manage Libraries**.

---

## Instalación paso a paso

Para una guía visual y muy detallada, usa [`docs/index.html`](docs/index.html). Resumen rápido aquí:

### 1. Clonar el repositorio

```bash
git clone https://github.com/jichef/CANRider-Two.git
cd CANRider-Two
```

### 2. Configurar Supabase

1. Entra en [supabase.com](https://supabase.com/) y crea un proyecto nuevo.
2. Anota la **URL del proyecto** y la **anon key** (*Project Settings → API*).
3. En el **SQL Editor**, pega y ejecuta el contenido completo de `supabase/schema.sql` — crea las tres tablas (`can_signals`, `telemetry`, `trips`) de una vez. Es idempotente: se puede volver a ejecutar sin duplicar nada.

### 3. Configurar el firmware

```bash
cp main/config.h.example main/config.h
```

Edita `main/config.h` — el propio archivo tiene marcado con `>>> CAMBIA ESTO <<<` exactamente qué rellenar (placa, credenciales de Supabase, APN, y el frame ID/byte de las 5 señales CAN por defecto: hora, batería A, batería B y estado de carga). Cada señal es independiente: puedes dejar comentadas las que no te interesen, el firmware simplemente las omite.

Si necesitas leer alguna señal CAN adicional a esas 4, o emitir alguna trama TX nueva, eso sí requiere editar `setupCANSignals()` dentro de `main/main.ino` directamente — es una decisión deliberada del proyecto: así nunca se puede ampliar lo que el ESP32 transmite por CAN solo con tocar un archivo de configuración.

### 4. Cargar el firmware

1. Abre `main/main.ino` en Arduino IDE.
2. Conecta la placa al PC por USB.
3. Selecciona el puerto en **Tools → Port**.
4. Pulsa **Upload** (Ctrl+U).

Abre el **Serial Monitor** (115200 baud) para ver los logs de arranque.

### 5. Instalar el portal web (opcional, en local)

```bash
npm install
```

Crea `.env.local` en la raíz del proyecto:

```
NEXT_PUBLIC_SUPABASE_URL=https://XXXX.supabase.co
NEXT_PUBLIC_SUPABASE_ANON_KEY=eyJ...
NEXT_PUBLIC_VEHICLE_ID=tu-uuid-de-vehiculo-aqui
```

```bash
npm run dev
```

Abre [http://localhost:3000](http://localhost:3000).

---

## Despliegue en producción (Vercel)

1. Sube el proyecto a un repositorio de GitHub (o haz un fork de este).
2. Importa el repositorio en [vercel.com](https://vercel.com/) → **Add New → Project**.
3. En **Environment Variables**, añade:
   - `NEXT_PUBLIC_SUPABASE_URL`
   - `NEXT_PUBLIC_SUPABASE_ANON_KEY`
   - `NEXT_PUBLIC_VEHICLE_ID`
4. Pulsa **Deploy**.

Si cambias variables de entorno después del primer deploy, tienes que forzar un **Redeploy** manual — Vercel no las recoge solo con el push.

---

## Home Assistant (opcional)

Copia la carpeta `custom_components/can_rider` a tu instalación de Home Assistant (`config/custom_components/`), reinicia HA, y añade la integración desde **Ajustes → Dispositivos y servicios → Añadir integración → CanRider**. Te pedirá la URL y anon key de Supabase, el `VEHICLE_ID` y el modelo de placa.

Expone: batería A y B de la moto, batería del ESP32, velocidad, señal de red, seguimiento GPS (`device_tracker`), estado de carga y datos del último viaje.

---

## Uso del portal web

### Panel de telemetría (`/`)

Muestra en tiempo real: batería A y B de la moto, velocidad, señal LTE, batería del ESP32, indicador de si la posición es GPS real o aproximada por LBS, mapa con la posición actual y el historial de viajes.

> **Seguridad CAN:** el firmware nunca transmite nada que no sea la trama de la hora, definida directamente en el código (`setupCANSignals()` en `main.ino`), nunca configurable de forma remota. La tabla `can_signals` de Supabase no tiene efecto en tiempo de ejecución — se mantiene solo como referencia opcional (ver comentario en `supabase/schema.sql`).

---

## Flujo de arranque del ESP32

```
Encendido
    │
    ▼
[MODEM_BOOT] — Inicializa el módem LTE, espera respuesta AT
    │
    ▼
[NET_SETUP]  — Conecta a la red (APN, registro)
               Obtiene hora de red (NITZ)
    │
    ▼
[HTTP_SETUP] — Comprueba conectividad con Supabase
               Enciende el GPS (con alimentación de antena si aplica)
    │
    ▼
[RUNNING]    — Bucle principal cada 15 s:
    │            1. Lee GPS (respaldo por LBS si no hay fix)
    │            2. Lee batería interna (AT+CBC) y señal de red
    │            3. Construye JSON con los datos CAN acumulados
    │            4. HTTP POST → Supabase /telemetry
    │            5. Gestiona inicio/fin de viaje automáticamente
    │
    └── Task CAN (núcleo paralelo, cada 200 ms):
           · Emite la trama de la hora (solo con hora de red válida)
           · Procesa las tramas RX recibidas
           · Recupera el bus automáticamente si entra en bus-off
```

---

## Estructura del repositorio

```
CanRider/
├── main/
│   ├── main.ino           # Firmware principal (Arduino/ESP32)
│   ├── structs.h          # Definición de tipos C++ (CANSignal, TimeRef, etc.)
│   ├── config.h.example   # Plantilla de configuración (copia como config.h)
│   ├── StreamDebugger.h   # Ver tráfico AT real del módem por Serial
│   └── AT/                # Librería de utilidades AT para LilyGo (utilities.h)
│
├── src/
│   ├── app/
│   │   └── page.tsx               # Panel de telemetría (/)
│   ├── components/
│   │   ├── DashboardContent.tsx   # UI del panel principal
│   │   └── Map.tsx                # Mapa Leaflet
│   └── lib/
│       └── supabase.ts            # Cliente Supabase (browser, anon key)
│
├── custom_components/
│   └── can_rider/          # Integración de Home Assistant
│
├── docs/
│   └── index.html          # Guía visual paso a paso (GitHub Pages)
│
└── supabase/
    └── schema.sql          # Esquema completo: can_signals, telemetry, trips
```

---

## Solución de problemas

### El ESP32 no conecta a la red
- Comprueba el APN en `config.h` (debe coincidir exactamente con el de tu operador)
- Verifica que la SIM tiene datos activados y no está bloqueada por PIN
- Revisa los logs en el Serial Monitor buscando `[ERROR]` o `[TIMEOUT]`
- En itinerancia (roaming), algunos operadores necesitan un ciclo de radio (`AT+CFUN=0`/`1`) antes de activar datos — el firmware ya lo hace automáticamente para SIM7000G

### No aparecen datos en el panel web
- Comprueba que `NEXT_PUBLIC_SUPABASE_URL`, `NEXT_PUBLIC_SUPABASE_ANON_KEY` y `NEXT_PUBLIC_VEHICLE_ID` están correctos (en `.env.local` o en las variables de entorno de Vercel)
- Verifica que las tablas se han creado en Supabase (SQL Editor → Table Editor)
- Confirma que el `VEHICLE_ID` en `config.h` es el mismo UUID que usas para filtrar en el portal
- En Vercel, un cambio de variables de entorno necesita un **Redeploy** manual para aplicarse

### El GPS no consigue posición
- Revisa que la antena GPS esté bien conectada al conector correspondiente (distinto del de la antena celular)
- Puede tardar varios minutos en frío, especialmente en interior — sácala al exterior con vista al cielo
- Mientras no hay fix GPS, el firmware intenta un respaldo automático por LBS (triangulación de celda, mucha menos precisión pero mejor que nada)

### Las señales CAN no se reciben
- Comprueba `CAN_SPEED` en `config.h` (250 kbps por defecto en este proyecto; ajusta si tu vehículo usa otra)
- Verifica los pines `CAN_TX_PIN` / `CAN_RX_PIN` y el cableado del transceptor
- Comprueba que has descomentado y rellenado el bloque 5/5 de `config.h` — si lo dejas tal cual viene (comentado), el firmware no transmite ni lee ninguna señal (`[CAN] 0 señales` en el Serial Monitor)
- Si el Serial Monitor muestra `[CAN] Bus-off detectado`, el propio firmware se recupera solo — si se repite mucho, revisa la terminación del bus/cableado

---

## Licencia

MIT — libre para uso personal y comercial.
