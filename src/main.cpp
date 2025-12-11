#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <DHT20.h>
#include <TFT_eSPI.h>

// Wi-Fi credentials
#define WIFI_SSID     "WIFI_SSID"
#define WIFI_PASSWORD "WIFI_PASSWORD"

// ThingSpeak configuration
// One channel corresponds to one API key, and field1/2/3 is mapped to different data
const char *THINGSPEAK_API_KEY = "API_KEY";
const char *THINGSPEAK_URL     = "http://api.thingspeak.com/update";

// Sensor and GPIO pins
const int MQ135_PIN   = 33;
const int BUZZER_PIN  = 27;

// The time interval for sending data to the cloud (ThingSpeak can be as fast as once every 15 seconds)
const unsigned long TELEMETRY_INTERVAL = 15000;

TFT_eSPI tft = TFT_eSPI(); // TFT screen
DHT20 dht;
unsigned long lastSendTime = 0;

void connectWiFi() {
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 30) {
        delay(500);
        Serial.print(".");
        retry++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("Failed to connect WiFi.");
    }
}

// Read sensor data: temperature, humidity, air quality (original value of MQ-135)
bool readSensors(float &temperature, float &humidity, int &airQualityRaw) {
    // DHT20
    dht.read();
    temperature = dht.getTemperature();
    humidity    = dht.getHumidity();

    if (isnan(temperature) || isnan(humidity)) {
        Serial.println("Failed to read from DHT20!");
        return false;
    }

    // MQ-135(0 ~ 4095)
    airQualityRaw = analogRead(MQ135_PIN);

    Serial.print("Temp: ");
    Serial.print(temperature);
    Serial.print(" °C, Hum: ");
    Serial.print(humidity);
    Serial.print(" %, MQ135(AQ): ");
    Serial.println(airQualityRaw);

    return true;
}

// Provide a "grade string" based on air quality
String classifyAirQuality(int airQualityRaw) {
    // The larger the value, the higher the gas concentration
    if (airQualityRaw < 800) {
        return "Good";
    } else if (airQualityRaw < 1500) {
        return "Moderate";
    } else {
        return "BAD! OPEN WINDOWS!";
    }
}

// Display data on the TFT
void updateDisplay(float temperature, float humidity, int airQualityRaw) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);

    String airText = "AQ: " + String(airQualityRaw);
    String airLevel = classifyAirQuality(airQualityRaw);

    tft.setCursor(10, 20);
    tft.print("Temp: ");
    tft.print(temperature, 1);
    tft.println(" C");

    tft.setCursor(10, 50);
    tft.print("Hum:  ");
    tft.print(humidity, 1);
    tft.println(" %");

    tft.setCursor(10, 80);
    tft.print(airText);

    tft.setCursor(10, 110);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    if (airQualityRaw > 1500) {
        Serial.println("Air Quality: BAD! OPEN WINDOWS!");
        tft.setTextColor(TFT_RED, TFT_BLACK);
    }
    tft.print(airLevel);
}

// When the air quality is very poor, let the buzzer sound
void checkAndAlert(int airQualityRaw) {
    if (airQualityRaw > 1500) {
        for (int i = 0; i < 2; i++) {
            tone(BUZZER_PIN, 440);
            delay(250);
            noTone(BUZZER_PIN);
            delay(250);
        }
    }
}

// Send data to ThingSpeak：field1 = AQ, field2 = Temp, field3 = Hum
void sendToThingSpeak(float temperature, float humidity, int airQualityRaw) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, skip ThingSpeak update.");
        return;
    }

    WiFiClient client;
    HTTPClient http;

    // Construct the request URL using GET
    String url = String(THINGSPEAK_URL) +
                "?api_key=" + THINGSPEAK_API_KEY +
                "&field1=" + String(airQualityRaw) +
                "&field2=" + String(temperature, 2) +
                "&field3=" + String(humidity, 2);

    Serial.print("Sending to ThingSpeak: ");
    Serial.println(url);

    http.begin(client, url);
    int httpCode = http.GET();

    if (httpCode > 0) {
        Serial.print("ThingSpeak response code: ");
        Serial.println(httpCode);
        String payload = http.getString();
        Serial.print("Response: ");
        Serial.println(payload);
    } else {
        Serial.print("ThingSpeak request failed, error: ");
        Serial.println(http.errorToString(httpCode));
    }

    http.end();
}

// setup / loop
void setup() {
    Serial.begin(9600);
    delay(1000);

    tft.begin();
    tft.setRotation(1); // Horizontal display

    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(10, 20);
    tft.println("Air Quality Monitor");

    // I2C (DHT20)
    Wire.begin(); // TTGO defaults SDA=21, SCL=22
    dht.begin();

    // MQ-135 pin
    pinMode(MQ135_PIN, INPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // connet WiFi
    connectWiFi();

    delay(1000);
}

void loop() {
    delay(500);
    unsigned long now = millis();

    if (now - lastSendTime >= TELEMETRY_INTERVAL) {
        float temperature = 0.0;
        float humidity    = 0.0;
        int airQualityRaw = 0;

        if (readSensors(temperature, humidity, airQualityRaw)) {
        updateDisplay(temperature, humidity, airQualityRaw);
        checkAndAlert(airQualityRaw);
        sendToThingSpeak(temperature, humidity, airQualityRaw);
        } else {
        Serial.println("Skip update because sensor read failed.");
        }

        lastSendTime = now;
    }

    // Use the CPU for other tasks during the rest of the time.
    delay(100);
}
