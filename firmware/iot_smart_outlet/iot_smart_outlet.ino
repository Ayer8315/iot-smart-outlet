#include <WiFi.h>
#include <PubSubClient.h>
#include <IRremote.hpp>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const char* ssid = "YOUR_WIFI_SSID";          // <-- Wi-Fi adinizi yazin
const char* password = "YOUR_WIFI_PASSWORD";  // <-- Wi-Fi sifrenizi yazin

const char* mqtt_server = "192.168.1.100";    // <-- Raspberry Pi (Mosquitto) IP adresi
const int mqtt_port = 1883;

const char* topic_lamba_cmd = "pinar/lamba/komut";
const char* topic_fan_cmd = "pinar/fan/komut";

const char* topic_sicaklik = "pinar/sicaklik";
const char* topic_nem = "pinar/nem";
const char* topic_isik = "pinar/isik";

#define LAMBA_RELAY_PIN 23
#define FAN_RELAY_PIN 26
#define BUTTON_PIN 18
#define IR_PIN 19

#define DHTPIN 4
#define DHTTYPE DHT11

#define LDR_PIN 34

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

DHT dht(DHTPIN, DHTTYPE);

uint32_t irLampCode = 3108437760;
uint32_t irFanCode  = 3810328320;

bool lambaDurum = false;
bool fanDurum = false;

float sonSicaklik = 0;
float sonNem = 0;
String sonIsik = "BILINMIYOR";
String mqttDurum = "BAGLANMIYOR";

WiFiClient espClient;
PubSubClient client(espClient);

void oledYazdir() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  display.setCursor(0, 0);
  display.print("MQTT:");
  display.print(mqttDurum);

  display.setCursor(0, 12);
  display.print("LAMBA:");
  display.print(lambaDurum ? "ACIK" : "KAPALI");

  display.setCursor(0, 24);
  display.print("FAN:");
  display.print(fanDurum ? "ACIK" : "KAPALI");

  display.setCursor(0, 36);
  display.print("T:");
  display.print(sonSicaklik, 1);
  display.print("C N:");
  display.print(sonNem, 0);
  display.print("%");

  display.setCursor(0, 48);
  display.print("ORTAM:");
  display.print(sonIsik);

  display.display();
}

void applyLampRelay() {
  digitalWrite(LAMBA_RELAY_PIN, lambaDurum ? LOW : HIGH);
}

void applyFanRelay() {
  digitalWrite(FAN_RELAY_PIN, fanDurum ? LOW : HIGH);
}

void setLamp(bool newState) {
  lambaDurum = newState;
  applyLampRelay();
  Serial.println(lambaDurum ? "LAMBA ACIK" : "LAMBA KAPALI");
  oledYazdir();
}

void setFan(bool newState) {
  fanDurum = newState;
  applyFanRelay();
  Serial.println(fanDurum ? "FAN ACIK" : "FAN KAPALI");
  oledYazdir();
}

void toggleLamp() {
  setLamp(!lambaDurum);
}

void toggleFan() {
  setFan(!fanDurum);
}

void callback(char* topic, byte* payload, unsigned int length) {
  String mesaj = "";

  for (unsigned int i = 0; i < length; i++) {
    mesaj += (char)payload[i];
  }

  mesaj.trim();

  Serial.print("MQTT mesaj geldi [");
  Serial.print(topic);
  Serial.print("] : ");
  Serial.println(mesaj);

  if (String(topic) == topic_lamba_cmd) {
    if (mesaj == "1") {
      setLamp(true);
    } else if (mesaj == "0") {
      setLamp(false);
    }
  }

  if (String(topic) == topic_fan_cmd) {
    if (mesaj == "1") {
      setFan(true);
    } else if (mesaj == "0") {
      setFan(false);
    }
  }
}

void connectWiFi() {
  Serial.print("WiFi baglaniyor: ");
  Serial.println(ssid);

  mqttDurum = "WIFI YOK";
  oledYazdir();

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi baglandi");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  mqttDurum = "BAGLANMIYOR";
  oledYazdir();
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("MQTT baglaniyor...");

    mqttDurum = "BAGLANIYOR";
    oledYazdir();

    String clientId = "ESP32Client-";
    clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("baglandi");

      mqttDurum = "BAGLANDI";
      oledYazdir();

      client.subscribe(topic_lamba_cmd);
      client.subscribe(topic_fan_cmd);
    } else {
      Serial.print(" hata, rc=");
      Serial.print(client.state());
      Serial.println(" -> 2 sn sonra tekrar");

      mqttDurum = "BAGLANAMADI";
      oledYazdir();

      delay(2000);
    }
  }
}

void publishSensorData() {
  static unsigned long lastSensorTime = 0;

  if (millis() - lastSensorTime >= 2000) {
    lastSensorTime = millis();

    float nem = dht.readHumidity();
    float sicaklik = dht.readTemperature();

    if (isnan(nem) || isnan(sicaklik)) {
      Serial.println("DHT11 okunamadi");
    } else {
      sonNem = nem;
      sonSicaklik = sicaklik;

      Serial.print("Sicaklik: ");
      Serial.print(sicaklik);
      Serial.print(" C | Nem: ");
      Serial.print(nem);
      Serial.println(" %");

      client.publish(topic_sicaklik, String(sicaklik, 1).c_str());
      client.publish(topic_nem, String(nem, 1).c_str());
    }

    int ldrRaw = digitalRead(LDR_PIN);

    int isikDurumu;

    if (ldrRaw == HIGH) {
      isikDurumu = 1;
      sonIsik = "KARANLIK";
      Serial.println("Isik Durumu: KARANLIK");
    } else {
      isikDurumu = 0;
      sonIsik = "AYDINLIK";
      Serial.println("Isik Durumu: AYDINLIK");
    }

    client.publish(topic_isik, String(isikDurumu).c_str());

    oledYazdir();
  }
}

void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED bulunamadi");
    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  display.setCursor(0, 0);
  display.println("SISTEM BASLIYOR");
  display.display();

  digitalWrite(LAMBA_RELAY_PIN, HIGH);
  digitalWrite(FAN_RELAY_PIN, HIGH);

  pinMode(LAMBA_RELAY_PIN, OUTPUT);
  pinMode(FAN_RELAY_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LDR_PIN, INPUT);

  applyLampRelay();
  applyFanRelay();

  dht.begin();

  IrReceiver.begin(IR_PIN);

  oledYazdir();

  connectWiFi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  Serial.println("Sistem hazir");
  oledYazdir();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (!client.connected()) {
    reconnectMQTT();
  } else {
    mqttDurum = "BAGLANDI";
  }

  client.loop();

  publishSensorData();

  static int lastButtonState = HIGH;
  int buttonState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH && buttonState == LOW) {
    delay(30);
    if (digitalRead(BUTTON_PIN) == LOW) {
      toggleLamp();
      while (digitalRead(BUTTON_PIN) == LOW) {
      }
      delay(30);
    }
  }

  lastButtonState = buttonState;

  if (IrReceiver.decode()) {
    uint32_t gelen = IrReceiver.decodedIRData.decodedRawData;

    Serial.print("IR kod: ");
    Serial.println(gelen);

    if (gelen == irLampCode) {
      toggleLamp();
      delay(200);
    }

    if (gelen == irFanCode) {
      toggleFan();
      delay(200);
    }

    IrReceiver.resume();
  }
}