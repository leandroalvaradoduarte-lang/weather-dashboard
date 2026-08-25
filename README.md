# Estación Meteorológica — Servidor local + Dashboard

Sistema local que recibe las lecturas del ESP32 (y opcionalmente del
Arduino), las guarda en una base de datos SQLite, y las muestra en una
página web con lectura en vivo e historial (6H / 12H / 1D / 1W).

## Estructura del proyecto

```
weather-dashboard/
├── app.py                     # Servidor Flask (API + guarda en SQLite)
├── requirements.txt
├── station.db                 # Se crea automáticamente al arrancar
├── templates/
│   └── index.html             # Página del dashboard
├── static/
│   ├── css/style.css
│   └── js/app.js
└── esp32_example/
    └── esp32_http_post.ino    # Firmware ESP32 con WiFi + envío HTTP
```

## 1. Preparar el servidor (PC o Raspberry Pi)

Necesitas Python 3.9+ instalado. Luego:

```bash
cd weather-dashboard
pip install -r requirements.txt
python app.py
```

Vas a ver algo como:

```
Running on all addresses (0.0.0.0)
Running on http://127.0.0.1:5000
```

Deja esa ventana abierta (el servidor debe seguir corriendo).

## 2. Encontrar la IP de esa PC en tu red local

- Windows: `ipconfig` → busca "Dirección IPv4" (ej. `192.168.1.50`)
- Linux/Raspberry Pi: `ip addr` o `hostname -I`

El ESP32 y la PC deben estar conectados **a la misma red WiFi**.

## 3. Configurar el ESP32

Abre `esp32_example/esp32_http_post.ino` en el IDE de Arduino y
edita estas tres líneas con tus datos:

```cpp
const char* WIFI_SSID = "TU_RED_WIFI";
const char* WIFI_PASSWORD = "TU_CLAVE_WIFI";
const char* SERVIDOR_IP = "192.168.1.50";   // la IP que viste en el paso 2
```

Sube el código al ESP32. Cada 5 segundos va a leer la presión con el
BMP280 y enviarla al servidor con una petición HTTP POST.

El código envía **presión, temperatura y humedad** desde la ESP32.
El Arduino Uno no participa en el envío porque no tiene WiFi propio.

## 4. Ver el dashboard

Desde cualquier dispositivo en la misma red (celular, laptop),
abre en el navegador:

```
http://<IP-de-la-PC>:5000
```

Ejemplo: `http://192.168.1.50:5000`

Vas a ver:
- El medidor de presión en vivo (estilo barómetro)
- Temperatura y humedad (cuando el Arduino esté conectado)
- Un indicador de estado: **Clima estable / Precaución / Alerta**
- Un historial con gráfica, con pestañas 6H / 12H / 1D / 1W

La página se actualiza sola cada 5 segundos, no hay que recargarla.

## 5. Publicar en Vercel con Neon

Vercel ejecuta la aplicación, pero no debe usarse `station.db` como base de
datos en producción. Crea una base PostgreSQL en Neon y copia su cadena de
conexión. En Vercel, agrega una variable de entorno llamada `DATABASE_URL`
con esa cadena, incluyendo `sslmode=require` si Neon lo solicita.

Luego importa este repositorio en Vercel y despliega. El archivo
`vercel.json` ya configura la entrada de Flask. La URL pública tendrá esta
forma:

```
https://tu-proyecto.vercel.app
```

Después cambia en el sketch de la ESP32 únicamente el servidor destino:

```cpp
const char* SERVIDOR_IP = "tu-proyecto.vercel.app";
```

Para una URL HTTPS, el sketch debe usar `WiFiClientSecure` y configurar el
certificado o una validación TLS apropiada; no se debe enviar la cadena de
Neon al firmware. La API local seguirá usando HTTP durante las pruebas.

## 6. Cómo funciona el cálculo de alerta

El servidor replica la misma lógica que ya tienen en el firmware:

- Diferencia ≥ 0.5 hPa entre lecturas → **Precaución**
- Diferencia ≥ 1.5 hPa entre lecturas → **Alerta**
- Variación acumulada ≥ 4 hPa en las últimas 3 horas → **Alerta**

Esto queda guardado en la base de datos junto con cada lectura, así
que el historial también muestra en qué momentos hubo alertas.

## Notas para la exposición / informe

- La base de datos es un solo archivo (`station.db`), fácil de
  respaldar o copiar para el informe final.
- Si quieren que el dashboard sea accesible desde fuera de la red
  local (por ejemplo para mostrarlo en Expotecnia desde otro lugar),
  se puede migrar más adelante a un hosting como Render o Railway
  sin cambiar casi nada del código — solo la base de datos pasaría
  de SQLite a algo como PostgreSQL si el hosting lo requiere.
