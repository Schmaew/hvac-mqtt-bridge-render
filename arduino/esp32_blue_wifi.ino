//----------------- MQTT Protocal -----------------
// MUST define buffer size BEFORE including PubSubClient
#define MQTT_MAX_PACKET_SIZE 2048

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_wpa2.h" 
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

#define USE_ENTERPRISE_WIFI false  // Use NaCl regular WiFi

// WiFi และ MQTT Configuration
bool checkWifi = false;
const char* ssid = "NaCl - CE KMITL";
const char* password = "Jirasak Parinya Paratpanu Permpoon 601";

// ========== WiFi KMITL-Legacy (WPA2 Enterprise) - NOT USED ==========
const char* enterprise_ssid = "KMITL-Legacy";
const char* eap_identity = "65010023";       // Student ID
const char* eap_username = "65010023";       // Username
const char* eap_password = "jT5yonhaqk";     // Password    

// MQTT Broker settings - EMQX Cloud (SSL)
const char* mqtt_server = "f81277a8.ala.us-east-1.emqxsl.com";
const int mqtt_port = 8883;                           // Port 8883 for SSL
const char* mqtt_user = "admin";              // MQTT Username
const char* mqtt_password = "admin123";       // MQTT Password
const char* device_id = "ESP32_HVAC_002";

WiFiClientSecure espClient;
PubSubClient mqtt_client(espClient);

// Global variables สำหรับ 15 parameters
float ambient_temp = 0.0;
float condenser_temp = 0.0;
float comp_current = 0.0;
float fan_current = 0.0;
float airflow_velocity = 0.0;
float air_pressure = 0.0;
float vibration_amp = 0.0;    // mm/s²
float vibration_freq = 0.0;   // Hz
float refrigerant_flow = 0.0;
float sound_level = 0.0;
float dust_concentration = 0.0;
// float evap_fan_current = 0.0;  // Removed - same as comp_current
float evap_temp = 0.0;
float supply_air_temp = 0.0;
float return_air_temp = 0.0;

// DHT22 และ BMP280 data
float dht_humidity = 0.0;
float bmp_pressure = 0.0;
float bmp_temperature = 0.0;
float bmp_altitude = 0.0;

// NTP Time settings
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600; // GMT+7 for Thailand
const int daylightOffset_sec = 0;

// System status
bool sensorsReady = false;
bool wifiConnected = false;
bool mqttConnected = false;
bool ntpSynced = false;

// Heartbeat timing
unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 60000;  // Send heartbeat every 60 seconds

//// Timing variables
//unsigned long previousSensorTime = 0;
//unsigned long previousMqttTime = 0;
//unsigned long lastMqttReconnect = 0;
//
//// Intervals
//const long sensorInterval = 2000;    // Read sensors every 2 seconds
//const long mqttInterval = 5000;      // Send MQTT data every 5 seconds
//const long mqttReconnectInterval = 5000;
//
//
//// 15 HVAC Parameters (Global Variables)
//float ambient_temp = 0.0;          // อุณหภูมิโดยรอบ
//float condenser_temp = 0.0;        // อุณหภูมิคอนเดนเซอร์
//float comp_current = 0.0;          // กระแสคอมเพรสเซอร์
//float fan_current = 0.0;           // กระแสพัดลม
//float airflow_velocity = 0.0;      // ความเร็วลม
//float pressure = 0.0;              // ความดัน
//float vibration_amp = 0.0;         // ค่าการสั่น
//float vibration_freq = 0.0;        // ความถี่การสั่น
//float refrigerant_flow = 0.0;      // การไหลสารทำความเย็น
//float sound_level = 0.0;           // ระดับเสียง
//float dust_concentration = 0.0;    // ความหนาแน่นฝุ่น
//float evap_fan_current = 0.0;      // กระแสพัดลมอีแวป
//float evap_temp = 0.0;             // อุณหภูมิอีแวป
//float supply_air_temp = 0.0;       // อุณหภูมิลมจ่าย
//float return_air_temp = 0.0;       // อุณหภูมิลมกลับ
//
//// Additional derived values
//float total_current = 0.0;         // รวมกระแสทั้งหมด
//float temp_diff = 0.0;             // ผลต่างอุณหภูมิ
//
//------------------------------ ESP-NOW --------------------------------
#include <esp_now.h>
#include <esp_wifi.h>

typedef struct sensor_data {
  float temperature;
  float pressure;
  float dust;
  float dustAvg;
  int sensor_id;
} sensor_data;

sensor_data receivedData;
bool dataReceived = false;
unsigned long receiveCount = 0;

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  receiveCount++;
  
  Serial.println("\n🎉 ESP-NOW DATA RECEIVED!");
  Serial.printf("From: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  
  if (len != sizeof(sensor_data)) {
    Serial.printf("❌ Size mismatch! Expected: %d, Got: %d\n", 
                  sizeof(sensor_data), len);
    return;
  }
  
  memcpy(&receivedData, incomingData, sizeof(receivedData));
  dataReceived = true;
  
  Serial.printf("📊 Temp: %.2f°C, Press: %.2f hPa, Dust: %.1f µg/m³, Avg: %.1f µg/m³, ID: %d\n",
                receivedData.temperature, 
                receivedData.pressure, 
                receivedData.dust,
                receivedData.dustAvg,
                receivedData.sensor_id);
  Serial.printf("📈 Total received: %lu\n", receiveCount);
  Serial.println("====================================\n");
}

//void publishEspnowData() {
//  if (!mqtt_client.connected()) return;
//  
//  DynamicJsonDocument doc(512);
//  doc["device_id"] = device_id;
//  doc["temp"] = receivedData.temperature;
//  doc["pressure"] = receivedData.pressure;
//  doc["sensor_id"] = receivedData.sensor_id;
//  doc["count"] = receiveCount;
//  
//  String json;
//  serializeJson(doc, json);
//  
//  if (mqtt_client.publish("hvac/sensor/combined_data", json.c_str())) {
//    Serial.println("✅ MQTT Published");
//    Serial.println(json);
//  } else {
//    Serial.println("❌ MQTT Publish failed");
//  }
//}

//----------------------------- Vibration ---------------------------------
#define VIBRATION_PIN 4

unsigned long lastVibrationTime = 0;
unsigned long vibrationCount = 0;
bool isVibrating = false;

// Calibration constants
const float AMP_SCALE_FACTOR = 2.0;
const float FREQ_SCALE_FACTOR = 1.0;
const float MIN_VIBRATION_THRESHOLD = 0.05;

// Timing variables
unsigned long previousVibrationRead = 0;
const long vibrationReadInterval = 100;  // อ่านทุก 100ms

//---------------------------- Temperature --------------------------------
#include "DHT.h"
#define DHTPIN 5     
#define DHTTYPE DHT11 
DHT dht(DHTPIN, DHTTYPE);

float temperature;
float altitude;

// Timing variables for non-blocking delays
unsigned long previousTempTime = 0;
unsigned long previousVibrationTime = 0;
unsigned long previousPressureTime = 0;
unsigned long previousSoundTime = 0;

// Intervals (in milliseconds)
const long tempInterval = 5000;      // Temperature reading every 1.5 seconds
const long vibrationInterval = 50;  // Vibration check every 100ms (faster response)
const long pressureInterval = 5000;   // Pressure reading every 200ms
const long soundInterval = 200;  

//---------------------------- Air pressure ---------------------------------
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>

// กำหนด I2C pins สำหรับ ESP32
#define SDA_PIN 21
#define SCL_PIN 22

// สร้าง object สำหรับ BMP280
Adafruit_BMP280 bmp;

float air_temperature = 0.0;
float air_attitude = 0.0;
//------------------------------- MAX4466 -----------------------------------
int SOUND_SENSOR_PIN = 34;
int val = 0;
int I = 0;

//-------------------- Clamp Current - From Arduino -------------------------
#pragma pack(1)
struct SensorData {
  float power_value;    
  float irms_value;     
  uint16_t checksum;    
};
#pragma pack()

uint16_t calculateXORChecksum(uint8_t* data, size_t length) {
  uint16_t checksum = 0;
  for (size_t i = 0; i < length; i++) {
    checksum ^= data[i];
  }
  return checksum;
}

//-----------------------------------------------------------------------------
// Status flags
bool tempDataReady = false;
bool vibrationDataReady = false;
bool pressureDataReady = false;

void setup() {
  pinMode(VIBRATION_PIN, INPUT);

  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 16, 17);
  
  Serial.println("ESP32 ready to receive current data");
  Serial.print("Expected data size: ");
  Serial.print(sizeof(SensorData));
  Serial.println(" bytes");
  
  Serial.println(F("DHT Sensor Test with ESP32"));
  dht.begin();
  
  // Initialize I2C and BMP280
  Wire.begin(SDA_PIN, SCL_PIN);
  if (bmp.begin(0x76)) {
    bmp280Ready = true;
    Serial.println("✅ BMP280 initialized (address 0x76)");
  } else if (bmp.begin(0x77)) {
    bmp280Ready = true;
    Serial.println("✅ BMP280 initialized (address 0x77)");
  } else {
    bmp280Ready = false;
    Serial.println("⚠️ BMP280 not found - will use ESP-NOW data");
  }

  Serial.println("Configuration complete!");
  Serial.println("Waiting for readings...\n");
  delay(500);  
  
  Serial.println("====================================");
  Serial.println("📡 Listening...\n");

  // Initialize WiFi and services
  setupWiFi();
  
  if (wifiConnected) {
    setupNTP();
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init failed");
    return;
  }
  Serial.println("✅ ESP-NOW initialized");
  
  if (esp_now_register_recv_cb(OnDataRecv) == ESP_OK) {
    Serial.println("✅ Receive callback registered");
  }
  
  // Setup MQTT with SSL
  espClient.setInsecure();  // Allow connection without certificate validation (for EMQX Cloud)
  mqtt_client.setBufferSize(2048);
  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setCallback(mqttCallback);
  
  if (wifiConnected) {
    connectMQTT();
  }
  delay(1000);  
}

void setupNTP() {
  if (!wifiConnected) return;
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    ntpSynced = true;
    Serial.println("✅ NTP time synchronized");
  } else {
    Serial.println("⚠️ NTP sync failed - using millis() timestamps");
  }
}

void mqttCallback(char* topic, byte* message, unsigned int length) {
  Serial.print("📨 Message received [");
  Serial.print(topic);
  Serial.print("]: ");
  
  String messageTemp;
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();
  
  // Handle commands
  if (messageTemp == "status") {
    publishStatus();
  } else if (messageTemp == "restart") {
    Serial.println("🔄 Restarting ESP32...");
    ESP.restart();
  }
}

void publishStatus() {
  if (!mqtt_client.connected()) return;
  
  String json = "{";
  json += "\"device_id\":\"" + String(device_id) + "\",";
  json += "\"timestamp\":\"" + getCurrentTimestamp() + "\",";
  json += "\"status\":\"online\",";
  json += "\"uptime\":" + String(millis() / 1000) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"wifi_rssi\":" + String(WiFi.RSSI());
  json += "}";
  
  // Use correct topic format for bridge: hvac/sensors/{device_id}/status
  String topic = "hvac/sensors/" + String(device_id) + "/status";
  mqtt_client.publish(topic.c_str(), json.c_str());
  
  Serial.println("📤 Status published");
}

void publishHeartbeat() {
  if (!mqtt_client.connected()) return;
  
  String json = "{";
  json += "\"device_id\":\"" + String(device_id) + "\",";
  json += "\"timestamp\":\"" + getCurrentTimestamp() + "\",";
  json += "\"status\":\"online\",";
  json += "\"firmware_version\":\"v1.0.0\",";
  json += "\"uptime\":" + String(millis() / 1000) + ",";
  json += "\"free_memory\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"wifi_rssi\":" + String(WiFi.RSSI());
  json += "}";
  
  // Use correct topic format for bridge: hvac/sensors/{device_id}/heartbeat
  String topic = "hvac/sensors/" + String(device_id) + "/heartbeat";
  mqtt_client.publish(topic.c_str(), json.c_str());
  
  Serial.println("💓 Heartbeat published");
}

//-------------------------- Function Vibration --------------------------------

void MeasureVibration() {
  static unsigned long lastVibTime = 0;
  static unsigned long vibStartTime = 0;
  static int vibPulses = 0;
  static unsigned long measureWindow = 0;
  static unsigned long lastState = LOW;
  
  unsigned long currentTime = millis();
  int vibState = digitalRead(VIBRATION_PIN);
  
  // ตรวจจับการเปลี่ยน state จาก LOW → HIGH
  if (vibState == HIGH && lastState == LOW) {
    vibPulses++;
    vibStartTime = currentTime;
    isVibrating = true;
    
    if (measureWindow == 0) {
      measureWindow = currentTime;
    }
  }
  
  // คำนวณ amplitude จาก state HIGH
  if (vibState == HIGH && isVibrating) {
    unsigned long vibDuration = currentTime - vibStartTime;
    // ยิ่ง duration ยาว แสดงว่าการสั่นแรง
    float intensity = constrain(vibDuration / 50.0, 0.1, 3.0);
    vibration_amp = intensity * AMP_SCALE_FACTOR;
    
    if (vibration_amp < MIN_VIBRATION_THRESHOLD) {
      vibration_amp = 0.0;
    }
  }
  
  // Reset เมื่อ state กลับเป็น LOW
  if (vibState == LOW && lastState == HIGH) {
    isVibrating = false;
  }
  
  // คำนวณ frequency ทุก 2 วินาที
  if (currentTime - measureWindow >= 5000 && measureWindow != 0) {
    vibration_freq = (vibPulses / 2.0) * FREQ_SCALE_FACTOR;
    
    // ถ้าไม่มี pulse ให้รีเซ็ตค่า
    if (vibPulses == 0) {
      vibration_amp = 0.0;
      vibration_freq = 0.0;
    }
    
    // Reset variables
    vibPulses = 0;
    measureWindow = currentTime;
  }
  
  // Auto-reset หากไม่มีการสั่นนาน
  if (currentTime - lastVibTime > 3000 && vibState == LOW) {
    vibration_amp = 0.0;
    vibration_freq = 0.0;
  }
  
  if (vibState == HIGH) {
    lastVibTime = currentTime;
  }
  
  lastState = vibState;
}

// =================================================================
// Display Functions - Arduino Compatible
// =================================================================

void printVibrationData() {
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 2000) { // Print ทุก 2 วินาที
    lastPrint = millis();
    
    Serial.println("\n📳 VIBRATION DATA");
    Serial.println("==================");
    
    Serial.print("Amplitude: ");
    Serial.print(vibration_amp, 4);
    Serial.println(" mm/s²");
    
    Serial.print("Frequency: ");
    Serial.print(vibration_freq, 2);
    Serial.println(" Hz");
    
    Serial.print("Level: ");
    Serial.println(getVibrationLevel(vibration_amp));
    
    Serial.print("Digital: ");
    Serial.println(digitalRead(VIBRATION_PIN) == HIGH ? "HIGH" : "LOW");
    
    Serial.println("==================");
  }
}

String getVibrationLevel(float amplitude) {
  if (amplitude == 0.0) return "No Vibration";
  else if (amplitude < 0.5) return "Very Low";
  else if (amplitude < 1.0) return "Low";
  else if (amplitude < 1.5) return "Medium";
  else if (amplitude < 2.0) return "High";
  else return "Very High";
}

void printCompactVibration() {
  Serial.print("Vib: ");
  Serial.print(vibration_amp, 3);
  Serial.print(" mm/s² @ ");
  Serial.print(vibration_freq, 1);
  Serial.print(" Hz [");
  Serial.print(getVibrationLevel(vibration_amp));
  Serial.println("]");
}

//------------------------------- WiFi -----------------------------------
// ===== SIMPLE & ROBUST WiFi Connection for NaCl =====
bool scanAndConnect() {
  Serial.println("\n� Connecting to NaCl WiFi...");
  Serial.printf("   SSID: %s\n", ssid);
  
  // Complete WiFi reset
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(500);
  WiFi.mode(WIFI_STA);
  delay(500);
  
  // Disable power saving for more stable connection
  esp_wifi_set_ps(WIFI_PS_NONE);
  
  // Simple direct connection - no scanning needed
  Serial.println("� Starting connection...");
  WiFi.begin(ssid, password);
  
  // Wait up to 30 seconds with status updates
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    delay(500);
    attempts++;
    
    if (attempts % 10 == 0) {
      int status = WiFi.status();
      Serial.printf("\n   [%ds] Status: %d ", attempts/2, status);
      if (status == 1) Serial.print("(IDLE)");
      else if (status == 4) Serial.print("(CONNECT_FAIL)");
      else if (status == 5) Serial.print("(CONNECTION_LOST)");
      else if (status == 6) Serial.print("(DISCONNECTED)");
      else if (status == 7) Serial.print("(NO_SSID_AVAIL)");
    } else {
      Serial.print(".");
    }
    
    // If stuck in IDLE too long, retry
    if (attempts == 30 && WiFi.status() == WL_IDLE_STATUS) {
      Serial.println("\n⚠️ Stuck in IDLE, retrying...");
      WiFi.disconnect(true);
      delay(1000);
      WiFi.begin(ssid, password);
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi Connected!");
    Serial.printf("   IP:   %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("   RSSI: %d dBm\n", WiFi.RSSI());
    Serial.printf("   Channel: %d\n", WiFi.channel());
    wifiConnected = true;
    return true;
  }
  
  Serial.println("\n❌ Connection FAILED!");
  Serial.printf("   Final Status: %d\n", WiFi.status());
  wifiConnected = false;
  return false;
}

// ===== SETUP WIFI (เรียกครั้งเดียวใน setup()) =====
void setupWiFi() {
  Serial.println("\n🌐 WiFi Setup Starting...");
  Serial.println("====================================");

  // ⚠️ ต้องเป็น AP_STA เพื่อให้ ESP-NOW ทำงานได้พร้อมกับ WiFi STA
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(true);
  delay(1000);

  // สร้าง SoftAP สำหรับ ESP-NOW ก่อน
  WiFi.softAP("ESP32_ESPNOW", "");
  Serial.println("✅ SoftAP created for ESP-NOW");
  delay(500);

  // เชื่อมต่อ WiFi
  if (scanAndConnect()) {
    Serial.println("====================================\n");
  } else {
    Serial.println("⚠️  Will retry WiFi in loop()");
    Serial.println("====================================\n");
  }
}

// ========== ตรวจสอบและ Reconnect WiFi ==========
void checkWiFiConnection() {
  static unsigned long lastCheck = 0;
  
  // ตรวจสอบทุก 10 วินาที
  if (millis() - lastCheck > 10000) {
    lastCheck = millis();
    
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("⚠️ WiFi disconnected! Reconnecting...");
      wifiConnected = false;
      setupWiFi();
    }
  }
}

// ========== แสดงข้อมูล WiFi ==========
void printWiFiInfo() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n📡 WiFi Information:");
    Serial.println("========================");
    Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("DNS: %s\n", WiFi.dnsIP().toString().c_str());
    Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
    Serial.printf("Channel: %d\n", WiFi.channel());
    Serial.println("========================\n");
  } else {
    Serial.println("❌ WiFi not connected");
  }
}

String getCurrentTimestamp() {
  if (!ntpSynced) {
    return String(millis()); // Fallback to millis
  }
  
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return String(millis());
  }
  
  char timeString[30];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeString);
}

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;
  
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();
}

void connectMQTT() {
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ Skip MQTT — no WiFi");
    return;
  }

  if (mqtt_client.connected()) return; // เชื่อมอยู่แล้ว ออกเลย

  Serial.print("🔗 Attempting MQTT connection to ");
  Serial.print(mqtt_server);
  Serial.print(":");
  Serial.print(mqtt_port);
  Serial.println("...");

  String clientId = String(device_id) + "-" + String(random(0xffff), HEX);
  bool connected = false;

  if (strlen(mqtt_user) > 0 && strlen(mqtt_password) > 0) {
    Serial.println("🔐 Using username/password authentication");
    connected = mqtt_client.connect(clientId.c_str(), mqtt_user, mqtt_password);
  } else {
    Serial.println("🌐 Connecting to public broker (no auth)");
    connected = mqtt_client.connect(clientId.c_str());
  }

  if (connected) {
    Serial.println("✅ MQTT Connected!");
    Serial.print("📋 Client ID: "); Serial.println(clientId);

    if (strlen(mqtt_user) > 0) {
      Serial.print("👤 Username: "); Serial.println(mqtt_user);
    }

    String commandTopic = "hvac/sensors/" + String(device_id) + "/command";
    mqtt_client.subscribe(commandTopic.c_str());
    Serial.print("📡 Subscribed to: "); Serial.println(commandTopic);

  } else {
    // ❌ ไม่ retry ที่นี่ — แค่รายงาน แล้วให้ loop() วนมาเรียกใหม่เอง
    Serial.print("❌ MQTT failed, rc=");
    Serial.println(mqtt_client.state());
  }
}

// Mock sensor data functions
float mockDHT11Temperature() {
  return random(200, 450) / 10.0; // 20.0 - 45.0°C
}

float mockSCT013Current() {
  return random(50, 200) / 10.0; // 5.0 - 20.0A
}

float mockAirSpeed() {
  return random(5, 50) / 10.0; // 0.5 - 5.0 m/s
}

float mockBMP280Pressure() {
  return random(9800, 10200) / 10.0; // 980.0 - 1020.0 hPa
}

bool mockSW420Vibration() {
  return random(0, 100) > 80; // 20% chance of vibration detection
}

String mockRefrigerantFlow() {
  return random(0, 2) == 0 ? "High" : "Low";
}

float mockSoundLevel() {
  return random(300, 800) / 10.0; // 30.0 - 80.0 dB
}

float mockGP2Y10Dust() {
  return random(0, 500) / 10.0; // 0.0 - 50.0 µg/m³
}

void MeasureAirPressure() {
  if (bmp280Ready) {
    // Read from local BMP280 sensor
    float temp = bmp.readTemperature();
    float pres = bmp.readPressure() / 100.0F;  // Convert to hPa
    float alt = bmp.readAltitude(1013.25);     // Sea level pressure
    
    // Validate readings
    if (!isnan(temp) && temp > -40 && temp < 85) {
      bmp_temperature = temp;
    }
    if (!isnan(pres) && pres > 300 && pres < 1100) {
      bmp_pressure = pres;
    }
    if (!isnan(alt) && alt > -500 && alt < 9000) {
      bmp_altitude = alt;
    }
  } else {
    // Fallback to ESP-NOW data if BMP280 not available
    if (receivedData.temperature != 0) {
      bmp_temperature = receivedData.temperature;
    }
    if (receivedData.pressure != 0) {
      bmp_pressure = receivedData.pressure;
    }
    bmp_altitude = 0.0;  // No altitude from ESP-NOW
  }
//  
//  Serial.print("Approx Altitude: ");
//  Serial.print(altitude, 1);
//  Serial.println(" m");
//  
//  // Calculate height using standard formulas
//  float calculatedAltitude = 44330.0 * (1.0 - pow(air_pressure / 1013.25, 0.1903));
//  Serial.print("Calculated Altitude: ");
//  Serial.print(calculatedAltitude, 1);
//  Serial.println(" m");
//  Serial.println("------------------------\n");    
}

void MeasureTemperature() {
    dht_humidity = dht.readHumidity();
    float dht_temp = dht.readTemperature();
    float dht_temp_F = dht.readTemperature(true);

    if (isnan(dht_humidity)) {
      dht_humidity = 0.0;
    }

    if (isnan(dht_temp)) {
      dht_temp = 0.0;
    }
    
    if (isnan(dht_humidity) || isnan(dht_temp)) {
      Serial.println("❌ Failed to read DHT22!");
      ambient_temp = 0; // Default value
      dht_humidity = 60.0;
    } else {
      ambient_temp = dht_temp;
    }
  
    Serial.print(F("Humidity: "));
    Serial.print(dht_humidity);
    Serial.print(F("%  Temperature: "));
    Serial.print(dht_temp);
    Serial.print(F("°C "));
    Serial.print(dht_temp_F);
    Serial.print(F("°F ")); 
}

void MeasureSoundLevel() {
  int rawSound = analogRead(SOUND_SENSOR_PIN);
  sound_level = map(rawSound, 0, 1023, 30, 100);
  
  Serial.print("Sound Level: ");
  Serial.print(sound_level);
  Serial.println(" dB");
}

void MeasureDustFilter() {
  dust_concentration = receivedData.dust; // 0.5 + (random(0, 4950) / 100.0);  // 0.5-50 µg/m³
}
void receiveCurrentFromArduino() {
  // เคลียร์ buffer หากมีข้อมูลขยะหรือไม่ครบ
  static unsigned long lastClear = 0;
  if (millis() - lastClear > 5000) { // ทุก 5 วินาที
    lastClear = millis();
    
    if (Serial2.available() > 0 && Serial2.available() < sizeof(SensorData)) {
      Serial.print("Clearing buffer with ");
      Serial.print(Serial2.available());
      Serial.println(" incomplete bytes");
      
      while (Serial2.available()) {
        uint8_t trash = Serial2.read();
        Serial.printf("%02X ", trash);
      }
      Serial.println(" (cleared)");
    }
  }
  
  // Timeout-based clearing - หากไม่มีข้อมูลใหม่เกิน 3 วินาที
  static unsigned long lastReceive = 0;
  if (Serial2.available() > 0) {
    lastReceive = millis();
  }
  
  if (millis() - lastReceive > 3000 && Serial2.available() > 0) {
    Serial.println("Timeout - clearing stale buffer data");
    while (Serial2.available()) {
      Serial2.read();
    }
  }
  
  // อ่านข้อมูลจาก Arduino
  if (Serial2.available() >= sizeof(SensorData)) {
    SensorData data;
    
    int bytesRead = Serial2.readBytes((uint8_t*)&data, sizeof(data));
    
    if (bytesRead == sizeof(SensorData)) {
      // ตรวจสอบว่าข้อมูลไม่ใช่ NaN หรือ Infinity
      if (isnan(data.power_value) || isinf(data.power_value) || 
          isnan(data.irms_value) || isinf(data.irms_value)) {
        Serial.println("Invalid current data - NaN or Infinity");
        return;
      }
      
      // คำนวณ checksum
      uint16_t calculated_checksum = calculateXORChecksum((uint8_t*)&data, sizeof(data) - sizeof(data.checksum));
      
      if (calculated_checksum == data.checksum) {
        // อัพเดทค่า comp_current จากข้อมูลจริง
        comp_current = data.irms_value;
        
        Serial.printf("✅ Arduino Current - Power: %.2fW, Current: %.3fA\n", 
                     data.power_value, data.irms_value);

        // เก็บค่า power สำหรับใช้ในระบบ
        float power_consumption = data.power_value;
        
        // วิเคราะห์ระดับการใช้พลังงาน
        if (power_consumption > 3000) {
          Serial.println("⚠️ Very high power consumption!");
        } else if (power_consumption > 2000) {
          Serial.println("⚠️ High power consumption");
        }
        
      } else {
        Serial.println("❌ Current data checksum mismatch");
        
        // Debug: แสดงข้อมูล binary ที่ผิด
        Serial.print("Raw data: ");
        uint8_t* ptr = (uint8_t*)&data;
        for (int i = 0; i < sizeof(data); i++) {
          Serial.printf("%02X ", ptr[i]);
        }
        Serial.println();
      }
    } else {
      Serial.println("❌ Incomplete current data received");
      
      // เคลียร์ข้อมูลที่ไม่สมบูรณ์ทันที
      while (Serial2.available()) {
        Serial2.read();
      }
    }
  }
}

void mockHVACTemperatures() {
  // ใช้ ambient_temp จาก DHT22 เป็นฐาน
  condenser_temp = 0.0; // ambient_temp + 15.0 + (random(0, 2500) / 100.0);    // +15-40°C
  evap_temp = 0.0; // ambient_temp - 15.0 - (random(0, 1000) / 100.0);         // -15-25°C
  supply_air_temp = 0.0; // ambient_temp - 5.0 - (random(0, 500) / 100.0);     // -5-10°C
  return_air_temp = 0.0; // ambient_temp + (random(-200, 200) / 100.0);        // ±2°C
}

void mockOtherCurrents() {
  // เฉพาะ fan_current และ evap_fan_current ที่ยัง mock
  fan_current = 0.0 ; // 2.0 + (random(0, 400) / 100.0);       // 2-6A
//  evap_fan_current = 0.0 // 1.5 + (random(0, 300) / 100.0);  // 1.5-4.5A
}

void mockEnvironmental() {
  airflow_velocity = 0.0; // 1.5 + (random(0, 650) / 100.0);     // 1.5-8 m/s
  // pressure จาก BMP280 แล้ว
  //  sound_level = 35.0 + (random(0, 4000) / 100.0);        // 35-75 dB
  //  dust_concentration = 0.0; // 0.5 + (random(0, 4950) / 100.0);  // 0.5-50 µg/m³
  refrigerant_flow = 0.0; // 5.0 + (random(0, 1500) / 100.0);    // 5-20 kg/h
}

void readAllSensors() {
  MeasureTemperature();
  MeasureAirPressure();
  MeasureVibration();   
  MeasureSoundLevel();
  MeasureDustFilter();
  
  // รับข้อมูล current จาก Arduino
  receiveCurrentFromArduino();
  
  // Mock ข้อมูลอื่นๆ
  mockHVACTemperatures(); 
  mockOtherCurrents();         
  mockEnvironmental();

  // Print Debug
  printVibrationData();
  if (vibration_amp > 0.1) {
    Serial.println("🔴 Vibration detected!");
  }  
}

String createHVACJSON() {
  StaticJsonDocument<512> doc;  // Reduced size after removing unused fields
  
  // Device info
  doc["device_id"] = device_id;
  doc["timestamp"] = getCurrentTimestamp();
  doc["esp_millis"] = millis();
  
  // HVAC Parameters (active sensors only)
  doc["ambient_temp"] = roundf(ambient_temp * 10) / 10.0;
  doc["comp_current"] = roundf(comp_current * 100) / 100.0;
  doc["airflow_velocity"] = roundf(airflow_velocity * 10) / 10.0;
  doc["bmp280_pressure"] = roundf(bmp_pressure * 10) / 10.0;
  doc["vibration_amp"] = roundf(vibration_amp * 1000) / 1000.0;
  doc["vibration_freq"] = roundf(vibration_freq * 10) / 10.0;
  doc["sound_level"] = roundf(sound_level * 10) / 10.0;
  doc["dust_concentration"] = roundf(dust_concentration * 10) / 10.0;
  
  // Additional sensor data
  doc["dht22_humidity"] = roundf(dht_humidity * 10) / 10.0;
  doc["bmp280_temperature"] = roundf(bmp_temperature * 10) / 10.0;
  doc["bmp280_altitude"] = roundf(bmp_altitude * 10) / 10.0;
  
  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

// =================================================================
// MQTT Publishing
// =================================================================

// แก้ไข publishHVACData() - เอาคอมเมนต์ออก
void publishHVACData() {
  if (!mqtt_client.connected()) {
    Serial.println("❌ MQTT not connected");
    return;
  }
  
  Serial.println("📤 Creating JSON data...");
  String json = createHVACJSON();
  
  if (json.length() < 10) {
    Serial.println("❌ JSON creation failed");
    return;
  }
  
  String topic = "hvac/sensors/" + String(device_id) + "/data";

  // แสดง JSON ตัวอย่าง
  Serial.println("📝 JSON Preview:");
  Serial.println(json.substring(0, 200) + "...");
  
  //  // ลองส่งข้อความทดสอบก่อน
  delay(100);
  
  // ส่งข้อมูลจริง
  Serial.print("📤 Publishing to ");
  Serial.print(topic);
  Serial.print(" (");
  Serial.print(json.length());
  Serial.println(" bytes)...");
  
  // Check MQTT connection before publish
  if (!mqtt_client.connected()) {
    Serial.println("❌ MQTT disconnected before publish!");
    return;
  }
  
  // Use direct publish with length parameter
  bool result = mqtt_client.publish(topic.c_str(), (const uint8_t*)json.c_str(), json.length(), false);
  
  if (result) {
    Serial.println("✅ Published successfully!");
    // Force send by calling loop
    mqtt_client.loop();
  } else {
    Serial.println("❌ Failed to publish MQTT message");
    Serial.print("📊 Message size: ");
    Serial.print(json.length());
    Serial.println(" bytes");
    Serial.print("🔧 MQTT State: ");
    Serial.println(mqtt_client.state());
    Serial.print("💾 Free Heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");
    Serial.print("🔌 MQTT Connected: ");
    Serial.println(mqtt_client.connected() ? "Yes" : "No");
    
    // ลองส่งเฉพาะข้อมูลสำคัญ
    tryCompactPublish();
  }
}

// ฟังก์ชันสำรองสำหรับส่งข้อมูลแบบย่อ
void tryCompactPublish() {
  Serial.println("🔄 Trying compact publish...");
  
  String compactJson = "{";
  compactJson += "\"device_id\":\"" + String(device_id) + "\",";
  compactJson += "\"timestamp\":\"" + getCurrentTimestamp() + "\",";
  compactJson += "\"esp_millis\":" + String(millis()) + ",";
  compactJson += "\"ambient_temp\":" + String(ambient_temp, 2) + ",";
  compactJson += "\"pressure\":" + String(bmp_pressure, 2) + ",";
  compactJson += "\"sound_level\":" + String(sound_level, 2);
  compactJson += "}";
  
  // Use correct topic format for bridge: hvac/sensors/{device_id}/data
  String topic = "hvac/sensors/" + String(device_id) + "/data";
  bool compactResult = mqtt_client.publish(topic.c_str(), compactJson.c_str());
  Serial.print("📦 Compact publish: ");
  Serial.println(compactResult ? "✅ Success" : "❌ Failed");
}


// เพิ่มฟังก์ชันตรวจสอบความพร้อม
void debugSystemStatus() {
  Serial.println("\n🔍 SYSTEM DEBUG INFO");
  Serial.println("==================");
  Serial.print("WiFi Status: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  Serial.print("MQTT Status: ");
  Serial.println(mqtt_client.connected() ? "Connected" : "Disconnected");
  Serial.print("MQTT State: ");
  Serial.println(mqtt_client.state());
  Serial.print("Free Heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");
  Serial.print("JSON Buffer Size: 2048 bytes");
  Serial.println();
  Serial.println("Variable Values:");
  Serial.print("  ambient_temp: "); Serial.println(ambient_temp);
  Serial.print("  bmp_temperature: "); Serial.println(bmp_temperature);
  Serial.print("  bmp_pressure: "); Serial.println(bmp_pressure);
  Serial.println("==================\n");
}

unsigned long lastPublish = 0;
bool firstRun = true;
const unsigned long publishInterval = 300000; // 5 นาที

void loop() {
  // 1. Check WiFi
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi disconnected — reconnecting...");
    wifiConnected = false;
    while (!wifiConnected) {
      Serial.println("🔄 Scanning for WiFi...");
      wifiConnected = scanAndConnect();
      if (!wifiConnected) {
        Serial.println("⚠️  Not found, retrying in 15s...");
        delay(15000);
      }
    }
  }

  // 2. Check MQTT
  if (!mqtt_client.connected()) {
    Serial.println("🔄 MQTT disconnected — reconnecting now...");
    connectMQTT();
    if (!mqtt_client.connected()) {
      Serial.println("⚠  MQTT failed, will retry next loop");
      delay(3000);
      return;
    }
  }
  mqtt_client.loop();

  // 3. Normal tasks — ทำทุก 5 นาที
  unsigned long currentMillis = millis();
  
  if (firstRun || (currentMillis - lastPublish >= publishInterval)) {
    lastPublish = currentMillis;
    firstRun = false;
    
    readAllSensors();
    publishHVACData();
    debugSystemStatus();
  }

  // 4. Heartbeat
  if (currentMillis - lastHeartbeat >= heartbeatInterval) {
    lastHeartbeat = currentMillis;
    publishHeartbeat();
  }
}