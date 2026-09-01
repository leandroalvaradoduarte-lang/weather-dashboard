#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// ---------- Configuración de red ----------
const char* WIFI_SSID = "Estudiantes";
const char* WIFI_PASSWORD = "Estud1ant3$$mep";
const char* SERVIDOR_HOST = "weather-dashboard-eta-bay.vercel.app";

// ---------- Configuración de Pines y Sensores ----------
const int DHT_PIN = 4;
#define DHT_TYPE DHT11

LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_BMP280 bmp;
DHT dht(DHT_PIN, DHT_TYPE);

const int ledVerde = 12;
const int ledAmarillo = 14;
const int ledRojo = 26;
const int buzzer = 27;

// ---------- Variables Globales ----------
float presionActual = 0;
float ultimaTemperatura = NAN;
float ultimaHumedad = NAN;
String estadoClima = "Sin lectura";

unsigned long ultimoCambioBuzzer = 0;
bool buzzerActivo = false;

unsigned long ultimoMonitoreo = 0;
const unsigned long intervalo = 60000; // Envía datos al servidor cada 1 minuto

// Variables para el carrusel de la pantalla LCD
unsigned long ultimoCambioLCD = 0;
const long intervaloLCD = 3000;    // Cambia de página cada 3 segundos
int paginaLCD = 1;

// ---------- Función de Alertas Físicas y Semáforo ----------
void actualizarAlertas(float presionHpa) {
  if (presionHpa < 1000.0) {
    estadoClima = "Inestable";
    digitalWrite(ledRojo, HIGH);
    digitalWrite(ledAmarillo, LOW);
    digitalWrite(ledVerde, LOW);

    // Patrón de pitido intermitente para la alarma
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
    estadoClima = "Estable";
    digitalWrite(ledRojo, LOW);
    digitalWrite(ledAmarillo, LOW);
    digitalWrite(ledVerde, HIGH);
    digitalWrite(buzzer, LOW);
    buzzerActivo = false;
  }
}

// ---------- Función para gestionar el carrusel en la LCD ----------
void gestionarLCD() {
  if (millis() - ultimoCambioLCD >= intervaloLCD) {
    ultimoCambioLCD = millis();
    paginaLCD = (paginaLCD == 1) ? 2 : 1;
    lcd.clear();
  }

  if (paginaLCD == 1) {
    lcd.setCursor(0, 0);
    lcd.print("Temp: "); 
    if (!isnan(ultimaTemperatura)) lcd.print(ultimaTemperatura, 1); 
    else lcd.print("--");
    lcd.print(" C");

    lcd.setCursor(0, 1);
    lcd.print("Pres: "); 
    lcd.print(presionActual, 1); 
    lcd.print(" hPa");
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Hum:  "); 
    if (!isnan(ultimaHumedad)) lcd.print(ultimaHumedad, 1); 
    else lcd.print("--");
    lcd.print(" %");

    lcd.setCursor(0, 1);
    lcd.print("St: "); 
    lcd.print(estadoClima);
  }
}

// ---------- Conexión Wi-Fi Inteligente ----------
void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  lcd.clear();
  lcd.print("Conectando WiFi...");
  Serial.print("Conectando a: ");
  Serial.println(WIFI_SSID);

  for (int intento = 0; intento < 3 && WiFi.status() != WL_CONNECTED; intento++) {
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
    Serial.println("¡WiFi conectado con éxito!");
    Serial.print("IP ESP32: ");
    Serial.println(WiFi.localIP());
  } else {
    lcd.print("Sin WiFi");
    lcd.setCursor(0, 1);
    lcd.print("Modo local");
    Serial.println("No se pudo conectar al Wi-Fi.");
  }
  delay(1500);
}

// ---------- Enviar datos completos al servidor web ----------
void enviarLectura(float presionHpa, float temperaturaC, float humedadPct) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No se envia: WiFi desconectado");
    return;
  }

  WiFiClientSecure cliente;
  cliente.setInsecure();
  HTTPClient http;
  String url = "https://" + String(SERVIDOR_HOST) + "/api/reading";

  http.begin(cliente, url);
  http.addHeader("Content-Type", "application/json");

  String cuerpo = "{\"pressure_hpa\":" + String(presionHpa, 2) +
                  ",\"temperature_c\":" + String(temperaturaC, 1) +
                  ",\"humidity_pct\":" + String(humedadPct, 1) + "}";

  int codigoRespuesta = http.POST(cuerpo);
  Serial.print("HTTP POST a Web -> Código: ");
  Serial.println(codigoRespuesta);
  
  if (codigoRespuesta <= 0) {
    Serial.print("Error HTTP: ");
    Serial.println(http.errorToString(codigoRespuesta));
  }
  http.end();
}

// ---------- Enviar solo presión si falla el DHT11 ----------
void enviarPresionSinDht(float presionHpa) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure cliente;
  cliente.setInsecure();
  HTTPClient http;
  String url = "https://" + String(SERVIDOR_HOST) + "/api/reading";
  
  http.begin(cliente, url);
  http.addHeader("Content-Type", "application/json");
  
  String cuerpo = "{\"pressure_hpa\":" + String(presionHpa, 2) + "}";
  int codigoRespuesta = http.POST(cuerpo);
  
  Serial.println("DHT11 sin lectura; se envía solo presión al servidor");
  Serial.print("HTTP Código: ");
  Serial.println(codigoRespuesta);
  http.end();
}

// ======================= SETUP =======================
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
    digitalWrite(ledRojo, HIGH);
    digitalWrite(buzzer, HIGH);
    while (1);
  }

  conectarWiFi();
}

// ======================= LOOP =======================
void loop() {
  unsigned long tiempoActual = millis();

  // Tarea periódica de lectura de sensores y envío web (cada 5 segundos)
  if (tiempoActual - ultimoMonitoreo >= intervalo) {
    ultimoMonitoreo = tiempoActual;

    presionActual = bmp.readPressure() / 100.0F;
    actualizarAlertas(presionActual);

    Serial.print("Presión: ");
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
      Serial.println("Advertencia: No se pudo leer el DHT11");
      if (!isnan(ultimaTemperatura) && !isnan(ultimaHumedad)) {
        enviarLectura(presionActual, ultimaTemperatura, ultimaHumedad);
      } else {
        enviarPresionSinDht(presionActual);
      }
    }
  }

  // Mantiene activo el parpadeo del buzzer si el clima sigue inestable
  if (estadoClima == "Inestable") {
    actualizarAlertas(presionActual);
  }

  // Verifica el estado del Wi-Fi por si se desconecta
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }

  // Ejecuta permanentemente el carrusel visual de la pantalla LCD
  gestionarLCD();
}
