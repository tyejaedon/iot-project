#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <DHT.h>
#include <ArduinoJson.h> 

#define DHTPIN 21
#define MQ135_PIN 34 
#define SOIL_PIN 32  
#define DHTTYPE DHT22

// Network Credentials & Target
const char* WIFI_SSID = "CirkitWifi";
const char* WIFI_PASS = ""; 
BASE_URL = "https://iot-project-cb938-default-rtdb.firebaseio.com/farm_nodes/bed_001.json";

DHT dht(DHTPIN, DHTTYPE);

// --- EVENT THRESHOLDS ---
// The system will only transmit if the change in reading exceeds these values
const float TEMP_EVENT_THRESHOLD = 0.5;   
const float HUM_EVENT_THRESHOLD  = 2.0;   
const int   GAS_EVENT_THRESHOLD  = 50;    
const int   SOIL_EVENT_THRESHOLD = 5;     

// --- STATE MEMORY ---
// Stores the last transmitted values to compare against new readings
float lastTemp = -999.0;
float lastHum = -999.0;
int lastGas = -999;
int lastSoilPct = -999;

// --- TIMING ---
unsigned long previousSensorPollMillis = 0;
const long SENSOR_POLL_INTERVAL = 2000; // Check sensors locally every 2 seconds

unsigned long lastTransmitMillis = 0;
const long MAX_IDLE_TIME = 300000; // Force atransmit every 5 minutes regardless of events

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

// Function dedicated to pushing data to the cloud
void transmitDataEvent(float t, float h, int gas, int soilPct) {
  Serial.println(">>> EVENT TRIGGERED: Constructing Payload...");
  
  StaticJsonDocument<200> doc;
  doc["temp"] = round(t * 10.0) / 10.0; 
  doc["humidity"] = round(h * 10.0) / 10.0;
  doc["gas"] = gas;
  doc["soil_moist_pct"] = soilPct;
  doc["status"] = "ONLINE";
  doc["uptime_ms"] = millis();

  String payload;
  serializeJson(doc, payload);
  Serial.println("Transmitting: " + payload);

  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure(); 
    
    HTTPClient http;
    if (http.begin(client, FIREBASE_URL)) {
      http.addHeader("Content-Type", "application/json");
      
      int httpResponseCode = http.PATCH(payload); 
      
      if (httpResponseCode > 0) {
        Serial.printf("Firebase Response code: %d\n", httpResponseCode);
        
        // Update state memory ONLY if transmission was successful
        lastTemp = t;
        lastHum = h;
        lastGas = gas;
        lastSoilPct = soilPct;
        lastTransmitMillis = millis();
        
      } else {
        Serial.printf("Error code: %d. Msg: %s\n", httpResponseCode, 
        http.errorToString(httpResponseCode).c_str());
      }
      http.end();
    }
  } else {
    Serial.println("Transmission aborted: No WiFi connection.");
    connectWiFi(); // Try to reconnect for the next cycle
  }
}

void setup() {
  Serial.begin(115200);
  
  dht.begin();
  pinMode(MQ135_PIN, INPUT);
  pinMode(SOIL_PIN, INPUT);

  connectWiFi();
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. Local Sensor Polling Loop (Fast)
  if (currentMillis - previousSensorPollMillis >= SENSOR_POLL_INTERVAL) {
    previousSensorPollMillis = currentMillis;

    float currentTemp = dht.readTemperature();
    float currentHum = dht.readHumidity();
    int currentGas = analogRead(MQ135_PIN);
    int rawMoisture = analogRead(SOIL_PIN);
    
    int currentSoilPct = map(rawMoisture, 4095, 0, 0, 100); 
    currentSoilPct = constrain(currentSoilPct, 0, 100);

    if (isnan(currentHum) || isnan(currentTemp)) {
      Serial.println("Failed to read from DHT sensor!");
      return; 
    }

    // 2. Event Evaluation Logic
    bool eventDetected = false;

    if (abs(currentTemp - lastTemp) >= TEMP_EVENT_THRESHOLD) eventDetected = true;
    if (abs(currentHum - lastHum) >= HUM_EVENT_THRESHOLD) eventDetected = true;
    if (abs(currentGas - lastGas) >= GAS_EVENT_THRESHOLD) eventDetected = true;
    if (abs(currentSoilPct - lastSoilPct) >= SOIL_EVENT_THRESHOLD) eventDetected = true;

    // Heartbeat check: Has it been too long since our last update?
    if (currentMillis - lastTransmitMillis >= MAX_IDLE_TIME) {
      Serial.println("Heartbeat interval reached.");
      eventDetected = true;
    }

    // 3. Dispatch Event
    if (eventDetected) {
      transmitDataEvent(currentTemp, currentHum, currentGas, currentSoilPct);
    }
  }
}
