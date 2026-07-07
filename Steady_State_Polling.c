#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <DHT.h>
#include <ArduinoJson.h> 

#define DHTPIN 21
#define MQ135_PIN 34 
#define SOIL_PIN 3
#define DHTTYPE DHT22

// Network Credentials & Target
const char* WIFI_SSID = "CirkitWifi";
const char* WIFI_PASS = "";
const String FIREBASE_URL = 
"https://iot-project-cb938-default-rtdb.firebaseio.com/farm_nodes/bed_001.json";

DHT dht(DHTPIN, DHTTYPE);

// Timing variables to prevent blocking delay()
unsigned long previousMillis = 0;
const long interval = 5000; // 5 seconds polling

void connectWiFi() {
  Serial.print("Connecting to WiFi..");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected. IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi Connection Failed.");
  }
}

void setup() {
  Serial.begin(115200);
  
  // Initialize Sensors
  dht.begin();
  pinMode(MQ135_PIN, INPUT);
  pinMode(SOIL_PIN, INPUT);

  connectWiFi();
}

void loop() {
  unsigned long currentMillis = millis();

  // Reconnect WiFi if lost
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  // Non-blocking execution
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // 1. Read Sensors
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int gasLevel = analogRead(MQ135_PIN);
    int rawMoisture = analogRead(SOIL_PIN);
    
    // Map raw ADC to percentage 
    int moisturePct = map(rawMoisture, 4095, 0, 0, 100); 
    moisturePct = constrain(moisturePct, 0, 100);

    // Validate sensor readings
    if (isnan(h) || isnan(t)) {
      Serial.println("Failed to read from DHT sensor!");
      return; 
    }

    // 2. Build JSON Payload using ArduinoJson
    StaticJsonDocument<200> doc;
    doc["temp"] = round(t * 10.0) / 10.0; // Keep 1 decimal place
    doc["humidity"] = round(h * 10.0) / 10.0;
    doc["gas"] = gasLevel;
    doc["soil_moist_pct"] = moisturePct;
    doc["status"] = "ONLINE";
    doc["uptime_ms"] = millis();

    String payload;
    serializeJson(doc, payload);
    
    Serial.println("Transmitting: " + payload);

    // 3. Transmit via HTTPS
    if (WiFi.status() == WL_CONNECTED) {
      WiFiClientSecure client;
      client.setInsecure(); 
      
      HTTPClient http;
      if (http.begin(client, FIREBASE_URL)) {
        http.addHeader("Content-Type", "application/json");
        
        int httpResponseCode = http.PATCH(payload); 
        
        if (httpResponseCode > 0) {
          Serial.printf("Firebase Response code: %d\n", httpResponseCode);
        } else {
          Serial.printf("Error code: %d. Msg: %s\n", httpResponseCode, 
          http.errorToString(httpResponseCode).c_str());
        }
        http.end();
      }
    }
  }
}
