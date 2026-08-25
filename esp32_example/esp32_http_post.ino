/*
  Extensión del firmware ESP32 + BMP280 del proyecto original.
  Agrega conexión WiFi y envío HTTP POST al servidor local (Flask)
  para que la página web pueda mostrar la lectura en vivo y guardarla
  en el historial.

  Cambios respecto al código original:
    - Se agregan las librerías WiFi.h y HTTPClient.h
    - Se agregan las credenciales de tu red WiFi
    - Se agrega la función enviarLectura() que hace el POST
    - Se llama enviarLectura() dentro de loop(), en el mismo bloque
      donde ya se lee la presión cada "intervalo" (5 segundos)

  La ESP32 obtiene su propia IP automáticamente por DHCP.
  SERVIDOR_IP es la IP de la laptop donde corre app.py y no es
  la IP que se configura en la ESP32. Puedes verla con "ipconfig".
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// ---------- Configuración de red ----------
const char* WIFI_SSID = "Estudiantes";
const char* WIFI_PASSWORD = "Estud1ant3$$mep";
const char* SERVIDOR_IP = "10.10.93.55";   // destino: laptop con app.py
const int SERVIDOR_PUERTO = 5000;

const int DHT_PIN = 4;
#define DHT_TYPE DHT11

LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_BMP280 bmp;
DHT dht(DHT_PIN, DHT_TYPE);

const int ledVerde = 12;
const int ledAmarillo = 14;
const int ledRojo = 26;
const int buzzer = 27;

float presionActual = 0;
float ultimaTemperatura = NAN;
float ultimaHumedad = NAN;
String estadoClima = "Sin lectura";
unsigned long ultimoCambioBuzzer = 0;
bool buzzerActivo = false;

unsigned long ultimoMonitoreo = 0;
const unsigned long intervalo = 5000;

void actualizarAlertas(float presionHpa) {
  if (presionHpa < 1000.0) {
    estadoClima = "Clima Inestable";
    digitalWrite(ledRojo, HIGH);
    digitalWrite(ledAmarillo, LOW);
    digitalWrite(ledVerde, LOW);

    if (millis() - ultimoCambioBuzzer >= 200) {
      ultimoCambioBuzzer = millis();
      buzzerActivo = !buzzerActivo;
      digitalWrite(buzzer, buzzerActivo ? HIGH : LOW);
    }
  } else if (presionHpa <= 1010.0) {
    estadoClima = "Precaucion";
    digitalWrite(ledRojo, LOW);
    digitalWrite(ledAmarillo, HIGH);
    digitalWrite(ledVerde, LOW);
    digitalWrite(buzzer, LOW);
    buzzerActivo = false;
  } else {
    estadoClima = "Clima Estable";
    digitalWrite(ledRojo, LOW);
    digitalWrite(ledAmarillo, LOW);
    digitalWrite(ledVerde, HIGH);
    digitalWrite(buzzer, LOW);
    buzzerActivo = false;
  }
}

void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  lcd.clear();
  lcd.print("Conectando WiFi...");
  Serial.print("Conectando a: ");
  Serial.println(WIFI_SSID);

  for (int intento = 0; intento < 3 && WiFi.status() != WL_CONNECTED; intento++) {
    Serial.print("Intento WiFi ");
    Serial.println(intento + 1);
    WiFi.disconnect(true);
    delay(500);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long inicio = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
      delay(300);
    }
  }

  lcd.clear();
  if (WiFi.status() == WL_CONNECTED) {
    lcd.print("WiFi conectado");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    Serial.println("WiFi conectado");
    Serial.print("IP ESP32: ");
    Serial.println(WiFi.localIP());
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
  } else {
    lcd.print("Sin WiFi");
    lcd.setCursor(0, 1);
    lcd.print("Modo local");
    Serial.print("WiFi no conectado. Estado: ");
    Serial.println(WiFi.status());
  }
  delay(1500);
}

// Envía la lectura de presión al servidor local. Si tu Arduino Uno
// reenvía temperatura/humedad por Serial (ver nota al final del
// archivo), puedes agregar esos valores al mismo JSON.
void enviarLectura(float presionHpa, float temperaturaC, float humedadPct) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No se envia: WiFi desconectado");
    return;
  }

  HTTPClient http;
  String url = "http://" + String(SERVIDOR_IP) + ":" + String(SERVIDOR_PUERTO) + "/api/reading";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  String cuerpo = "{\"pressure_hpa\":" + String(presionHpa, 2) +
                  ",\"temperature_c\":" + String(temperaturaC, 1) +
                  ",\"humidity_pct\":" + String(humedadPct, 1) + "}";

  int codigoRespuesta = http.POST(cuerpo);
  Serial.print("POST ");
  Serial.println(url);
  Serial.print("JSON: ");
  Serial.println(cuerpo);
  Serial.print("HTTP: ");
  Serial.println(codigoRespuesta);
  if (codigoRespuesta <= 0) {
    Serial.print("Error HTTP: ");
    Serial.println(http.errorToString(codigoRespuesta));
  }
  http.end();
}

void enviarPresionSinDht(float presionHpa) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = "http://" + String(SERVIDOR_IP) + ":" + String(SERVIDOR_PUERTO) + "/api/reading";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  String cuerpo = "{\"pressure_hpa\":" + String(presionHpa, 2) + "}";
  int codigoRespuesta = http.POST(cuerpo);
  Serial.println("DHT11 sin lectura; se envia solo presion");
  Serial.print("HTTP: ");
  Serial.println(codigoRespuesta);
  if (codigoRespuesta <= 0) {
    Serial.print("Error HTTP: ");
    Serial.println(http.errorToString(codigoRespuesta));
  }
  http.end();
}

void setup() {
  pinMode(ledVerde, OUTPUT);
  pinMode(ledAmarillo, OUTPUT);
  pinMode(ledRojo, OUTPUT);
  pinMode(buzzer, OUTPUT);

  Serial.begin(115200);
  Wire.begin(21, 22);

  lcd.init();
  lcd.backlight();
  dht.begin();

  bool status = bmp.begin(0x76);
  if (!status) status = bmp.begin(0x77);

  if (!status) {
    Serial.println("ERROR: no se encuentra BMP280 en 0x76 ni 0x77");
    lcd.clear();
    lcd.print("ERR: BMP280!");
    while (1);
  }

  conectarWiFi();

}

void loop() {
  unsigned long tiempoActual = millis();

  if (tiempoActual - ultimoMonitoreo >= intervalo) {
    ultimoMonitoreo = tiempoActual;

    presionActual = bmp.readPressure() / 100.0F;

    actualizarAlertas(presionActual);
    Serial.print("Presion: ");
    Serial.print(presionActual, 2);
    Serial.print(" hPa | Estado: ");
    Serial.println(estadoClima);

    float temperatura = dht.readTemperature();
    float humedad = dht.readHumidity();
    if (!isnan(temperatura) && !isnan(humedad)) {
      ultimaTemperatura = temperatura;
      ultimaHumedad = humedad;
      enviarLectura(presionActual, temperatura, humedad);
    } else {
      Serial.println("No se pudo leer el DHT11");
      if (!isnan(ultimaTemperatura) && !isnan(ultimaHumedad)) {
        Serial.println("Se reutiliza la ultima lectura valida del DHT11");
        enviarLectura(presionActual, ultimaTemperatura, ultimaHumedad);
      } else {
        enviarPresionSinDht(presionActual);
      }
    }

  }

  if (estadoClima == "Clima Inestable") {
    actualizarAlertas(presionActual);
  }

  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }
}

void apagarLeds() {
  digitalWrite(ledVerde, LOW);
  digitalWrite(ledAmarillo, LOW);
  digitalWrite(ledRojo, LOW);
  digitalWrite(buzzer, LOW);
}

/*
  ---------------------------------------------------------------
  Nota sobre temperatura y humedad (Arduino Uno + DHT11):
  ---------------------------------------------------------------
  El Arduino Uno no tiene WiFi, así que no puede llamar a la API
  directamente. Tienes dos opciones simples:

  Opción A (recomendada, más simple): en la página se muestra solo
  presión en vivo desde el ESP32, y temperatura/humedad quedan
  visibles en la pantalla LCD del Arduino como ya lo tienen. El
  servidor y la web ya soportan enviar esos campos si más adelante
  agregan la opción B.

  Opción B: conectar el pin TX del Arduino al RX2 del ESP32 (con un
  divisor de voltaje 5V->3.3V) y enviar por Serial una línea como
  "T:29.1,H:57.0". El ESP32 la lee con Serial2.readStringUntil('\n'),
  la separa, y agrega esos valores al mismo JSON de enviarLectura()
  antes de mandarlo:

    String cuerpo = "{\"pressure_hpa\":" + String(presionHpa,2) +
                     ",\"temperature_c\":" + temp +
                     ",\"humidity_pct\":" + hum + "}";
*/
