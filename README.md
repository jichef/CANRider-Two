# CanRider — Telemetría CAN para Vehículo Eléctrico

Sistema de telemetría en tiempo real para vehículos eléctricos con bus CAN. Lee los datos del BMS a través del bus CAN, los transmite a una base de datos en la nube (por LTE siempre disponible, o por WiFi cuando hay una red conocida al alcance) y los muestra en un portal web accesible desde cualquier dispositivo.

> **¿Primera vez con esto?** Hay una guía paso a paso pensada para gente sin experiencia previa: [jichef.github.io/CANRider-Two](https://jichef.github.io/CANRider-Two/). Ábrela en el navegador y sigue los pasos en orden.

---

## Aviso legal — léelo antes de instalar nada

Este proyecto se publica **tal cual («as is»), sin garantía de ningún tipo**. Instalarlo implica manipular el bus CAN real de tu vehículo, su instalación eléctrica y su batería. Un error de cableado, de configuración (por ejemplo, un `frame_id` o byte de CAN equivocado en `config.h`) o de uso puede dañar el vehículo, sus baterías, su electrónica, o provocar un comportamiento inesperado.

**Lo instalas y lo usas bajo tu propia cuenta y riesgo.** Ni el autor ni los colaboradores de este repositorio se hacen responsables de ningún daño, pérdida, avería o problema — material o de cualquier otro tipo — derivado de una instalación, configuración o uso incorrecto de este proyecto.

---

## ¿Qué hace?

- **Lee el bus CAN** del vehículo (estado de carga de baterías, etc.)
- **Emite una trama CAN**: la hora sincronizada por red, para la pantalla del vehículo — y **nunca nada más** que eso
- **Envía telemetría** cada 15 segundos a Supabase, por LTE siempre disponible o por **WiFi preferente** (opcional) cuando hay una red conocida al alcance — más barato y estable; vuelve a LTE solo si el WiFi deja de estar disponible
- **Posición GPS**, con respaldo automático por triangulación de celda (LBS, vía `AT+CLBS`) si no hay fix GPS — el portal marca esas posiciones como aproximadas y dibuja un círculo con el radio de precisión estimado
- **Registra viajes** automáticamente: empiezan con la primera trama CAN (moto encendida) y terminan cuando el bus lleva 8s en silencio (moto apagada) — distancia, velocidad máxima, consumo de batería y la traza real del recorrido (para pintarla en el mapa coloreada por velocidad; se guardan hasta 300 puntos por viaje, ~75 min a un punto cada 15s — pasado eso, el resto del viaje sigue contando para distancia/duración pero no se añaden más puntos al dibujo). También se puede guardar un viaje **sin CAN** (p.ej. en bici) a mano desde el portal cautivo de OTA — ver más abajo
- **Detección de sustracción**: si el GPS mide movimiento real (≥5 km/h) mientras el bus CAN lleva rato en silencio, no hay explicación normal — la moto no se mueve sola apagada. Puede ser indicio de que la están transportando sin la llave (no se activa durante un viaje manual sin CAN, ver arriba: ese movimiento es intencional). El portal muestra una alerta roja bien visible en cuanto se detecta
- **Actualización de firmware por WiFi (OTA, opcional)**: al apagar la moto, levanta su propio WiFi con un portal cautivo para subir un nuevo firmware, reiniciar el ESP32, o iniciar/detener un viaje manual sin CAN — sin cable, sin Arduino IDE, sin Bluetooth ni ninguna app
- **Panel web** en tiempo real con mapa (círculo de precisión cuando la posición es aproximada por LBS), historial de viajes (con opción de eliminarlos), indicadores de conexión y batería en gris cuando el dispositivo lleva más de 2 min sin reportar, y **gráficas de histórico** (batería A/B, velocidad, señal LTE con marcador de tramos WiFi) desplegables en un modal al pulsar la tarjeta correspondiente, con rango seleccionable (1H/6H/24H/7D/1M/1A)
- **Integración con Home Assistant** opcional (`custom_components/can_rider`)

---

## Arquitectura

```
[ Vehículo EV ]
      │  CAN bus (250 kbps)
      ▼
[ LilyGo T-SIM7000G ]  ←── firmware Arduino (main/main.ino)
      │  LTE (HTTPS) o WiFi (HTTPS) si hay red conocida al alcance
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
| **LilyGo T-SIM7000G** | Placa ESP32 con módem LTE Cat-M1/NB-IoT y GPS integrados (placa probada a fondo con moto real en este proyecto) — [tienda oficial](https://lilygo.cc/products/t-sim7000g), también en AliExpress |
| **LilyGo T-A7670G** (alternativa) | LTE Cat-1 — más compatible con operadores que no soportan bien Cat-M1 (más rápido y con mayor cobertura real que el SIM7000G en ese caso) — [tienda oficial](https://lilygo.cc/products/t-sim-a7670e), elige la variante «A7670G» en el desplegable (no la E ni la SA) **y no olvides seleccionar la opción «with GPS»** — también en AliExpress |
| **Transceptor CAN** | SN65HVD230, TJA1050 o similar, alimentado a 3.3V ([ejemplo](https://www.amazon.es/dp/B0F9FDK6RJ)) |
| **SIM con datos** | Con APN activo (M2M/IoT de cualquier operador) |
| **Antena LTE + antena GPS** | Las que incluye el kit de la placa elegida |
| **Batería 18650** | Como respaldo de alimentación (opcional pero recomendado) |
| **Conversor DC-DC de alta tensión a USB** | Debe aguantar la tensión de la batería principal del vehículo (48-72V en muchas eléctricas) — alimenta el ESP32 directamente desde la moto ([ejemplo: DC 8-85V a 5V/3A USB](https://www.amazon.es/dp/B09GFBB47L)) |
| **Acceso al bus CAN del vehículo** | Cable directo a CAN-H / CAN-L (el transceptor ya incluye la resistencia de terminación de 120 Ω). En muchos vehículos, alimentación + CAN-H/CAN-L están disponibles juntos en el conector de la ECU original |

### Conexiones (bus CAN)

| Placa | CAN TX | CAN RX |
|---|---|---|
| **T-SIM7000G** | GPIO 32 | GPIO 33 |
| **T-A7670G** | GPIO 22 | GPIO 21 |

Estos pines ya vienen puestos automáticamente en `config.h.example` según la placa que elijas — no hace falta tocarlos salvo que tu cableado sea distinto.

---

## Requisitos de software

- [Arduino IDE 2.x](https://www.arduino.cc/en/software) con soporte para ESP32
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

Para una guía visual y muy detallada, usa [jichef.github.io/CANRider-Two](https://jichef.github.io/CANRider-Two/). Resumen rápido aquí:

### 1. Descargar el proyecto

Entra en [github.com/jichef/CANRider-Two](https://github.com/jichef/CANRider-Two), pulsa el botón verde **Code** y luego **Download ZIP**. Descomprime el archivo en tu ordenador — esa carpeta descomprimida es con la que vas a trabajar en los siguientes pasos.

Si ya usas git, también puedes clonarlo directamente:
```bash
git clone https://github.com/jichef/CANRider-Two.git
cd CANRider-Two
```

### 2. Configurar Supabase

1. Entra en [supabase.com](https://supabase.com/) y crea un proyecto nuevo.
2. Anota la **URL del proyecto** y la **anon key** (*Project Settings → API*).
3. En el **SQL Editor**, pega y ejecuta el contenido completo de `supabase/schema.sql` — crea las dos tablas (`telemetry`, `trips`) de una vez. Es idempotente: se puede volver a ejecutar sin duplicar nada. Si vienes de una versión anterior del proyecto (con PostGIS, tablas `motorcycles`/`locations`/`can_signals`/etc.), este mismo script también limpia esos restos para que el esquema real coincida con el que usa el firmware actual.

### 3. Configurar el firmware

```bash
cp main/config.h.example main/config.h
```

Edita `main/config.h` — el propio archivo tiene marcado con `>>> CAMBIA ESTO <<<` exactamente qué rellenar (placa, credenciales de Supabase, APN, y el frame ID/byte de las 5 señales CAN por defecto: hora, batería A, batería B y estado de carga). Cada señal es independiente: puedes dejar comentadas las que no te interesen, el firmware simplemente las omite.

Si necesitas leer alguna señal CAN adicional a esas 5, o emitir alguna trama TX nueva, eso sí requiere editar `setupCANSignals()` dentro de `main/main.ino` directamente — es una decisión deliberada del proyecto: así nunca se puede ampliar lo que el ESP32 transmite por CAN solo con tocar un archivo de configuración.

Más abajo en el mismo archivo hay dos bloques marcados `>>> OPCIONAL <<<`, ninguno necesario para que CanRider funcione:

- **WiFi preferente sobre LTE** (`WIFI_FALLBACK_SSID_1`/`PASS_1`, y una segunda red opcional): si defines una red conocida, la telemetría se manda por ahí en vez de por LTE en cuanto esté al alcance. Solo redes de 2.4 GHz — el ESP32 no ve redes de 5 GHz.
- **Actualización OTA por WiFi** (`OTA_AP_PASSWORD`): al apagar la moto, el ESP32 levanta su propio WiFi `CanRiderTwo` con portal cautivo en `192.168.4.1` para subir un `.bin` (exportado desde Arduino IDE con `Sketch → Export Compiled Binary`) o reiniciar en remoto. Se apaga solo a los 4 min sin actividad o 2 min tras desconectarse el último cliente. Usa una contraseña propia de 8+ caracteres — es un secreto, como `SUPABASE_KEY`.

### 4. Cargar el firmware

1. Abre `main/main.ino` en Arduino IDE.
2. Conecta la placa al PC por USB.
3. Selecciona el puerto en **Tools → Port**.
4. Pulsa **Upload** (Ctrl+U).

Abre el **Serial Monitor** (115200 baud) para ver los logs de arranque.

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

Expone: batería A y B de la moto, batería del ESP32, velocidad, señal de red, seguimiento GPS (`device_tracker`), estado de carga (`binary_sensor`) y datos del último viaje. También incluye el sensor **«Movimiento Sin CAN»** (`binary_sensor`, clase `tamper`): se activa si el GPS mide movimiento real con el bus CAN en silencio (moto apagada) — útil para montar una automatización de aviso ante una posible sustracción.

---

## Actualización OTA (opcional)

Si configuraste `OTA_AP_PASSWORD` en el paso 3, no hace falta abrir la moto ni un cable cada vez que actualices el firmware. Guía completa paso a paso con capturas en [jichef.github.io/CANRider-Two](https://jichef.github.io/CANRider-Two/#ota) — resumen aquí:

1. Apaga la moto. En cuanto el bus CAN lleva unos segundos en silencio, el ESP32 levanta el WiFi **`CanRiderTwo`** (portal cautivo, IP `192.168.4.1`).
2. Conéctate a esa red con la contraseña de `OTA_AP_PASSWORD`. La página de actualización se abre sola en la mayoría de móviles/portátiles; si no, entra a mano en `http://192.168.4.1/`.
3. Sube el `.bin` (Arduino IDE → `Sketch → Export Compiled Binary`) o pulsa **Reiniciar ESP32** para un reinicio remoto sin actualizar nada.

El AP se apaga solo a los 4 min sin actividad HTTP, o 2 min tras desconectarse el último cliente (lo que llegue antes), o de inmediato si enciendes la moto a mitad de la ventana — nunca se puede actualizar con el vehículo en marcha. Si el WiFi de telemetría del paso 3 ya está conectado en ese momento, el AP de OTA no se levanta hasta que se libere la antena (comparten el mismo radio WiFi). Mientras el bus CAN siga en silencio, el AP se vuelve a levantar solo tras cada apagado por tiempo — sigue disponible aunque hayan pasado varios ciclos de 4 minutos sin que nadie se conecte.

### Viaje manual sin CAN (opcional)

La misma página de OTA incluye un botón **"Iniciar viaje (sin CAN)"** — pensado para cuando el ESP32 no va montado en la moto (p.ej. llevándolo en una bici de prueba, o cualquier uso sin bus CAN al que conectarse). Funciona como un viaje normal: guarda waypoints reales por GPS, distancia, velocidad máxima y la traza para el mapa — solo que el inicio/fin no depende de tramas CAN.

- **Iniciar**: pulsa el botón antes de salir. No hace falta quedarse conectado al AP — el viaje sigue grabándose por LTE aunque te alejes y pierdas el WiFi.
- **Detener**: si vuelves a tener alcance del AP (se relanza solo mientras el CAN siga en silencio), pulsa el mismo botón para cerrarlo al momento.
- **Cierre automático**: si no vuelves a pulsarlo, el viaje se cierra solo a los 5 minutos sin que el GPS marque más de 5 km/h reales — llegar a destino y parar ya lo termina, sin depender de tener el móvil a mano.

---

## Uso del portal web

### Panel de telemetría (`/`)

Muestra en tiempo real: batería A y B de la moto, velocidad, señal LTE (icono graduado según dBm, no solo on/off), batería del propio ESP32 (o "USB" si está enchufado — el diseño de la placa desconecta la lectura real en ese caso), indicador de si la posición es GPS real o aproximada por LBS (con un "hace X min/h/d" junto a la posición y un círculo en el mapa con el radio de precisión estimado), un indicador **WiFi/LTE** de por cuál de los dos se mandó la última lectura, mapa con la posición actual y el historial de viajes — al seleccionar un viaje se dibuja su recorrido real coloreado por velocidad (no solo una línea recta entre inicio y fin). Cada viaje del historial se puede eliminar con el icono de papelera (pide confirmación, no se puede deshacer). Cuando el dispositivo lleva más de 2 minutos sin reportar, los indicadores de conexión y batería pasan a gris — son el último dato conocido, no algo en vivo.

**Gráficas de histórico**: las tarjetas BATERÍA A, BATERÍA B, VELOCIDAD y SEÑAL son clicables — al pulsarlas se abre un modal con la evolución de esa métrica en el tiempo (rango seleccionable: 1H/6H/24H/7D/1M/1A). La de batería superpone A y B; la de señal dibuja solo dBm de LTE (el de WiFi no es comparable en la misma escala) y marca aparte, con un punto, los tramos en los que la telemetría se mandó por WiFi. SISTEMA no tiene gráfica asociada.

> **Nota:** `moving_without_can` (posible sustracción, ver más abajo) muestra una alerta roja bien visible en la parte superior del panel en cuanto la última lectura la marca — desaparece sola en cuanto una lectura posterior vuelve a false.

> **Seguridad CAN:** el firmware nunca transmite nada que no sea la trama de la hora, definida directamente en el código (`setupCANSignals()` en `main.ino`), nunca configurable de forma remota. No hay ninguna tabla en Supabase desde la que el firmware lea qué tramas escuchar o emitir — la configuración CAN vive solo en `config.h`/`main.ino`.

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
    │            2. Lee batería interna por ADC propio del ESP32 (no
    │               AT+CBC del módem, poco fiable) y señal de red
    │            3. Construye JSON con los datos CAN acumulados
    │            4. Si hay movimiento GPS con el bus CAN en silencio,
    │               marca moving_without_can (posible sustracción) —
    │               salvo que sea un viaje manual sin CAN en marcha
    │            5. HTTP POST → Supabase /telemetry
    │            6. Gestiona inicio/fin de viaje según actividad del bus
    │               CAN (o el botón de viaje manual, ver OTA más abajo)
    │               y acumula la traza del recorrido (lat/lon/velocidad
    │               por punto)
    │
    └── Task CAN (núcleo paralelo, cada 200 ms):
           · Emite la trama de la hora (solo con hora de red válida)
           · Procesa las tramas RX recibidas
           · Recupera el bus automáticamente si entra en bus-off

En paralelo a todo lo anterior, desde el arranque (no forma parte de esta
máquina de estados ni espera a que termine):

  · WiFi opcional — si hay redes conocidas en config.h, intenta unirse de
    fondo sin parar; en cuanto conecta, tanto la telemetría como el
    guardado de viajes se mandan por ahí en vez de por LTE
  · AP OTA opcional — en cuanto el bus CAN queda en silencio (moto
    apagada), si hay contraseña configurada, levanta el WiFi CanRiderTwo
    con portal cautivo para actualizar firmware, reiniciar en remoto, o
    iniciar/detener un viaje manual sin CAN
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
    └── schema.sql          # Esquema completo: telemetry, trips
```

---

## Solución de problemas

### El ESP32 no conecta a la red
- Comprueba el APN en `config.h` (debe coincidir exactamente con el de tu operador)
- Verifica que la SIM tiene datos activados y no está bloqueada por PIN
- Revisa los logs en el Serial Monitor buscando `[ERROR]` o `[TIMEOUT]`
- En itinerancia (roaming), algunos operadores necesitan un ciclo de radio (`AT+CFUN=0`/`1`) antes de activar datos — el firmware ya lo hace automáticamente para SIM7000G

### No aparecen datos en el panel web
- Comprueba que `NEXT_PUBLIC_SUPABASE_URL`, `NEXT_PUBLIC_SUPABASE_ANON_KEY` y `NEXT_PUBLIC_VEHICLE_ID` están correctos en las variables de entorno de Vercel
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

### El WiFi «CanRiderTwo» no aparece al apagar la moto
- Confirma que rellenaste `OTA_AP_PASSWORD` en `config.h` — sin eso, el firmware no levanta ningún AP
- Tarda unos segundos: se activa cuando el bus CAN lleva un rato en silencio, no en el instante exacto de apagar
- Si el WiFi opcional de telemetría está conectado en ese momento, el AP de OTA espera a que se libere la antena

### El WiFi de telemetría no conecta
- Revisa que `WIFI_FALLBACK_SSID_1`/`PASS_1` coinciden exactamente con tu red (sensible a mayúsculas)
- Solo funciona con redes de 2.4 GHz — el ESP32 no ve redes de 5 GHz
- Si no hay ninguna red conocida al alcance, es el comportamiento esperado: el firmware sigue con LTE y reintenta WiFi más adelante

---

## Licencia

MIT — libre para uso personal y comercial.
