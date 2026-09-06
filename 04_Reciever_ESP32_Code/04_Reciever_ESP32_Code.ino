/*
 * ESP32 CAN Bus Intrusion Detection System with TinyML - RAW DATASET INPUT VERSION
 * Accepts raw dataset rows: CAN_ID(hex),DLC,DATA[0],...,DATA[7]
 * ESP32 performs all preprocessing (hex->numeric conversion, scaling)
 * 
 * Hardware Connections:
 * - SD Card Module: VCC->VIN, GND->GND, MISO->GPIO19, MOSI->GPIO23, SCK->GPIO18, CS->GPIO5
 * - I2C LCD (16x2): VCC->3.3V, GND->GND, SDA->GPIO21, SCL->GPIO22
 * - CAN Transceiver: Connected via CAN controller (implementation specific)
 */

// ============================================
// INCLUDES
// ============================================
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>

// TensorFlow Lite for ESP32
#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "model_data.h"  // Your TFLite model as C array

// ============================================
// WIFI CONFIGURATION - UPDATE THESE!
// ============================================
const char* WIFI_SSID = "YOUR_WIFI_SSID";          // Your WiFi network name
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";  // Your WiFi password
const uint16_t SERVER_PORT = 8888;                // WiFi server port

// ============================================
// PIN DEFINITIONS
// ============================================
#define SD_CS 5           // SD Card Chip Select
#define SD_MISO 19        // SD Card MISO
#define SD_MOSI 23        // SD Card MOSI
#define SD_SCK 18         // SD Card SCK
#define LCD_SDA 21        // I2C LCD Data
#define LCD_SCL 22        // I2C LCD Clock

// RGB LED Pins (Common Anode)
#define LED_RED 25        // Red pin of RGB LED
#define LED_GREEN 26      // Green pin of RGB LED

// ============================================
// GLOBAL VARIABLES - LCD
// ============================================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ============================================
// GLOBAL VARIABLES - WiFi Server
// ============================================
WiFiServer server(SERVER_PORT);
WiFiClient serverClient;

// ============================================
// GLOBAL VARIABLES - TensorFlow Lite
// ============================================
namespace {
  tflite::ErrorReporter* error_reporter = nullptr;
  const tflite::Model* model = nullptr;
  tflite::MicroInterpreter* interpreter = nullptr;
  TfLiteTensor* input = nullptr;
  TfLiteTensor* output = nullptr;

  constexpr int kTensorArenaSize = 30 * 1024;
  uint8_t tensor_arena[kTensorArenaSize];
}

// ============================================
// SCALER PARAMETERS (from scaler_params.json)
// ============================================
// MinMaxScaler parameters extracted from your trained model
const float data_min[10] = {
  0.0,      // CAN_ID_numeric min
  0.0,      // DLC min
  0.0,      // DATA[0] min
  0.0,      // DATA[1] min
  0.0,      // DATA[2] min
  0.0,      // DATA[3] min
  0.0,      // DATA[4] min
  0.0,      // DATA[5] min
  0.0,      // DATA[6] min
  0.0       // DATA[7] min
};

const float scale[10] = {
  1.453218005952381e-07,  // CAN_ID_numeric scale
  0.125,                  // DLC scale
  0.010101010101010102,   // DATA[0] scale
  0.010101010101010102,   // DATA[1] scale
  0.010101010101010102,   // DATA[2] scale
  0.010101010101010102,   // DATA[3] scale
  0.010101010101010102,   // DATA[4] scale
  0.010101010101010102,   // DATA[5] scale
  0.010101010101010102,   // DATA[6] scale
  0.010101010101010102    // DATA[7] scale
};

// ============================================
// LABEL MAPPING
// ============================================
const char* attack_labels[3] = {"Attack Free", "DoS", "Fuzzy"};

// ============================================
// STATISTICS
// ============================================
struct Statistics {
  unsigned long total_messages = 0;
  unsigned long attack_free = 0;
  unsigned long dos_attacks = 0;
  unsigned long fuzzy_attacks = 0;
  unsigned long inference_time_us = 0;
  unsigned long wifi_messages = 0;
  unsigned long serial_messages = 0;
};
Statistics stats;

// ============================================
// LED STATE TRACKING
// ============================================
unsigned long lastGreenBlink = 0;
bool greenLedState = false;
int currentSystemState = 0;  // 0 = Attack Free, 1 = DoS, 2 = Fuzzy
unsigned long greenOnTime = 150;   // LED on duration (ms)
unsigned long greenOffTime = 2000;  // LED off duration (ms)

// ============================================
// SETUP FUNCTION
// ============================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("========================================");
  Serial.println("ESP32 TinyML IDS - WIRELESS VERSION");
  Serial.println("========================================");

  // Initialize RGB LED pins
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_RED, HIGH);   // OFF for common anode
  digitalWrite(LED_GREEN, HIGH); // OFF for common anode

  // Initialize I2C LCD
  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TinyML IDS");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  
  Serial.println("[1/4] Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.mode(WIFI_STA);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  
  int retry_count = 0;
  while (WiFi.status() != WL_CONNECTED && retry_count < 30) {
    delay(500);
    Serial.print(".");
    retry_count++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n  ✓ WiFi Connected!");
    Serial.print("  IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("  Signal: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    
    server.begin();
    Serial.print("  Server started on port ");
    Serial.println(SERVER_PORT);
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(2000);
  } else {
    Serial.println("\n  ✗ WiFi Failed!");
    Serial.println("  Continuing in Serial-only mode...");
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Failed!");
    lcd.setCursor(0, 1);
    lcd.print("Serial Mode");
    delay(2000);
  }
  
  Serial.println("[2/4] Initializing SD Card...");
  
  // Initialize SD Card with custom pins
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("ERROR: SD Card initialization failed!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SD Card Error!");
    while (1) { delay(1000); }  // Halt
  }
  Serial.println("  ✓ SD Card initialized");

  // Initialize TensorFlow Lite
  Serial.println("[3/4] Initializing TensorFlow Lite...");
  
  static tflite::MicroErrorReporter micro_error_reporter;
  error_reporter = &micro_error_reporter;

  model = tflite::GetModel(model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.printf("Model version mismatch! Got %d, expected %d\n", 
                  model->version(), TFLITE_SCHEMA_VERSION);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Model Error!");
    while (1) { delay(1000); }
  }
  Serial.println("  ✓ Model loaded");

  static tflite::MicroMutableOpResolver<10> resolver;
  resolver.AddFullyConnected();
  resolver.AddSoftmax();
  resolver.AddRelu();
  resolver.AddQuantize();
  resolver.AddDequantize();

  static tflite::MicroInterpreter static_interpreter(
    model, resolver, tensor_arena, kTensorArenaSize, error_reporter);
  interpreter = &static_interpreter;

  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    Serial.println("ERROR: AllocateTensors() failed!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Tensor Error!");
    while (1) { delay(1000); }
  }
  Serial.println("  ✓ Tensors allocated");

  input = interpreter->input(0);
  output = interpreter->output(0);

  Serial.printf("  Input: [%d, %d] Type: %d\n", input->dims->size, input->dims->data[1], input->type);
  Serial.printf("  Output: [%d, %d] Type: %d\n", output->dims->size, output->dims->data[1], output->type);
  
  // Check tensor types (should be kTfLiteFloat32 = 1)
  if (input->type != kTfLiteFloat32) {
    Serial.println("WARNING: Input tensor is not Float32!");
  }
  if (output->type != kTfLiteFloat32) {
    Serial.println("WARNING: Output tensor is not Float32!");
  }
  Serial.println("  ✓ TensorFlow Lite ready");

  Serial.println("[4/4] System Ready!");
  Serial.println("========================================");
  Serial.println("Data Input Modes:");
  Serial.println("  1. WiFi from ESP8266 transmitter");
  Serial.println("  2. Serial Monitor (backup mode)");
  Serial.println("\nFormat: CAN_ID(decimal),DLC,DATA[0-7](hex)");
  Serial.println("Example: 0165000,8,0e,e8,7f,00,00,00,07,9e\n");
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(">>> UPDATE ESP8266 WITH THIS IP <<<");
    Serial.print(">>> ESP32 IP: ");
    Serial.print(WiFi.localIP());
    Serial.println(" <<<");
    Serial.println("========================================\n");
    Serial.println("> Waiting for WiFi/Serial input...\n");
  } else {
    Serial.println("========================================\n");
    Serial.println("> Waiting for Serial input...\n");
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TinyML EV IDS");
  lcd.setCursor(0, 1);
  lcd.print("System Ready...");
  
  delay(2000);
}

// ============================================
// PREPROCESSING FUNCTION (RAW DATASET FORMAT)
// ============================================
void preprocessRawDataset(uint32_t can_id, uint8_t dlc, uint8_t data[8], float* output_features) {
  // Feature 0: CAN_ID (already numeric, just scale)
  output_features[0] = (float(can_id) - data_min[0]) * scale[0];
  
  // Feature 1: DLC (scaled)
  output_features[1] = (float(dlc) - data_min[1]) * scale[1];
  
  // Features 2-9: DATA bytes (already converted from hex, just scale)
  for (int i = 0; i < 8; i++) {
    output_features[i + 2] = (float(data[i]) - data_min[i + 2]) * scale[i + 2];
  }
}

// ============================================
// INFERENCE FUNCTION (RAW DATASET INPUT)
// ============================================
int runInferenceRaw(uint32_t can_id, uint8_t dlc, uint8_t data[8]) {
  unsigned long start_time = micros();
  
  // Preprocess raw dataset row
  float features[10];
  preprocessRawDataset(can_id, dlc, data, features);
  
  // Debug: Print input features (first message only)
  static bool first_run = true;
  if (first_run) {
    Serial.println("\n=== DEBUG: First Inference ===");
    Serial.printf("CAN_ID: %lu, DLC: %d\n", can_id, dlc);
    Serial.print("Data bytes (hex): ");
    for (int i = 0; i < 8; i++) {
      Serial.printf("0x%02X ", data[i]);
    }
    Serial.println();
    Serial.print("Scaled features: ");
    for (int i = 0; i < 10; i++) {
      Serial.printf("%.6f ", features[i]);
    }
    Serial.println();
  }
  
  // Copy to input tensor
  for (int i = 0; i < 10; i++) {
    input->data.f[i] = features[i];
  }
  
  // Run inference
  TfLiteStatus invoke_status = interpreter->Invoke();
  if (invoke_status != kTfLiteOk) {
    Serial.println("ERROR: Invoke() failed!");
    return -1;
  }
  
  // Debug: Print raw output values
  if (first_run) {
    Serial.print("Raw outputs: ");
    for (int i = 0; i < 3; i++) {
      Serial.printf("%.6f ", output->data.f[i]);
    }
    Serial.println();
    
    // Check if outputs are NaN or Inf
    for (int i = 0; i < 3; i++) {
      if (isnan(output->data.f[i])) {
        Serial.printf("WARNING: Output[%d] is NaN!\n", i);
      }
      if (isinf(output->data.f[i])) {
        Serial.printf("WARNING: Output[%d] is Inf!\n", i);
      }
    }
    first_run = false;
    Serial.println("================================\n");
  }
  
  // Get prediction
  int predicted_class = 0;
  float max_prob = output->data.f[0];
  
  for (int i = 1; i < 3; i++) {
    if (output->data.f[i] > max_prob) {
      max_prob = output->data.f[i];
      predicted_class = i;
    }
  }
  
  stats.inference_time_us = micros() - start_time;
  
  return predicted_class;
}

// ============================================
// LOG TO SD CARD
// ============================================
void logToSD(uint32_t can_id, uint8_t dlc, uint8_t data[8], int prediction) {
  File logFile = SD.open("/intrusion_log.csv", FILE_APPEND);
  
  if (logFile) {
    // Format: timestamp,can_id,dlc,data0,data1,...,data7,prediction
    logFile.printf("%lu,%lu,%d,", millis(), can_id, dlc);
    
    for (int i = 0; i < 8; i++) {
      logFile.printf("0x%02X", data[i]);
      if (i < 7) logFile.print(",");
    }
    
    logFile.printf(",%s\n", attack_labels[prediction]);
    logFile.close();
  } else {
    Serial.println("ERROR: Failed to open log file!");
  }
}

// ============================================
// UPDATE LCD DISPLAY
// ============================================
void updateLCD(int prediction, float confidence) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Status: ");
  lcd.print(attack_labels[prediction]);
  lcd.setCursor(0, 1);
  lcd.printf("Conf: %.2f%%", confidence * 100.0);
}

// ============================================
// PRINT STATISTICS
// ============================================
void printStatistics() {
  Serial.println("\n========== STATISTICS ==========");
  Serial.printf("Total Messages: %lu\n", stats.total_messages);
  Serial.printf("  WiFi:         %lu (%.1f%%)\n",
                stats.wifi_messages,
                stats.total_messages > 0 ? (float)stats.wifi_messages / stats.total_messages * 100 : 0);
  Serial.printf("  Serial:       %lu (%.1f%%)\n",
                stats.serial_messages,
                stats.total_messages > 0 ? (float)stats.serial_messages / stats.total_messages * 100 : 0);
  Serial.println("--------------------------------");
  Serial.printf("Attack Free:    %lu (%.1f%%)\n", 
                stats.attack_free, 
                stats.total_messages > 0 ? (float)stats.attack_free / stats.total_messages * 100 : 0);
  Serial.printf("DoS Attacks:    %lu (%.1f%%)\n", 
                stats.dos_attacks, 
                stats.total_messages > 0 ? (float)stats.dos_attacks / stats.total_messages * 100 : 0);
  Serial.printf("Fuzzy Attacks:  %lu (%.1f%%)\n", 
                stats.fuzzy_attacks, 
                stats.total_messages > 0 ? (float)stats.fuzzy_attacks / stats.total_messages * 100 : 0);
  Serial.printf("Avg Inference:  %lu us (%.2f ms)\n", 
                stats.inference_time_us, 
                stats.inference_time_us / 1000.0);
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi Signal:    %d dBm\n", WiFi.RSSI());
  }
  Serial.println("================================\n");
}

// ============================================
// PARSE RAW DATASET ROW FROM SERIAL
// ============================================
bool parseRawDatasetRow(uint32_t* can_id, uint8_t* dlc, uint8_t data[8]) {
  // Expected format from dataset: "CAN_ID,DLC,DATA[0],DATA[1],...,DATA[7]"
  // Example: "0165000,8,0e,e8,7f,00,00,00,07,9e"
  // CAN_ID and DLC are decimal, DATA bytes are hex
  
  if (!Serial.available()) {
    return false;
  }
  
  String input = Serial.readStringUntil('\n');
  input.trim();
  
  if (input.length() == 0) {
    return false;
  }
  
  // Parse comma-separated values
  String tokens[10];
  int index = 0;
  int start = 0;
  
  for (int i = 0; i <= input.length() && index < 10; i++) {
    if (i == input.length() || input.charAt(i) == ',') {
      String token = input.substring(start, i);
      token.trim();
      tokens[index] = token;
      index++;
      start = i + 1;
    }
  }
  
  // Validate we got at least CAN ID and DLC
  if (index < 2) {
    Serial.println("ERROR: Invalid format!");
    Serial.println("Expected: CAN_ID,DLC,D0,D1,D2,D3,D4,D5,D6,D7");
    Serial.println("Example: 0165000,8,0e,e8,7f,00,00,00,07,9e");
    return false;
  }
  
  // Extract CAN_ID as decimal number
  *can_id = strtoul(tokens[0].c_str(), NULL, 10);
  
  // Extract DLC as decimal
  *dlc = tokens[1].toInt();
  
  // Extract data bytes as HEX (fill with 0 if not provided)
  for (int i = 0; i < 8; i++) {
    if (i + 2 < index) {
      // Parse as hex (remove 0x prefix if present)
      String hexStr = tokens[i + 2];
      if (hexStr.startsWith("0x") || hexStr.startsWith("0X")) {
        hexStr = hexStr.substring(2);
      }
      data[i] = strtoul(hexStr.c_str(), NULL, 16);
    } else {
      data[i] = 0;
    }
  }
  
  return true;
}

// ============================================
// RGB LED CONTROL FUNCTIONS (Common Anode)
// ============================================
void setLED(bool red, bool green) {
  digitalWrite(LED_RED, red ? LOW : HIGH);
  digitalWrite(LED_GREEN, green ? LOW : HIGH);
}

void ledOff() {
  setLED(false, false);
}

void ledRed() {
  setLED(true, false);
}

void ledGreen() {
  setLED(false, true);
}

void blinkWarning() {
  for (int i = 0; i < 5; i++) {
    ledRed();
    delay(100);
    ledOff();
    delay(100);
  }
}

// ============================================
// PROCESS DATA (from WiFi or Serial)
// ============================================
void processData(String input, bool fromWiFi) {
  uint32_t can_id;
  uint8_t dlc;
  uint8_t data[8];
  
  input.trim();
  if (input.length() == 0) return;
  
  // Parse comma-separated values
  String tokens[10];
  int index = 0;
  int start = 0;
  
  for (int i = 0; i <= input.length() && index < 10; i++) {
    if (i == input.length() || input.charAt(i) == ',') {
      String token = input.substring(start, i);
      token.trim();
      tokens[index] = token;
      index++;
      start = i + 1;
    }
  }
  
  if (index < 2) return;
  
  can_id = strtoul(tokens[0].c_str(), NULL, 10);
  dlc = tokens[1].toInt();
  
  for (int i = 0; i < 8; i++) {
    if (i + 2 < index) {
      String hexStr = tokens[i + 2];
      if (hexStr.startsWith("0x") || hexStr.startsWith("0X")) {
        hexStr = hexStr.substring(2);
      }
      data[i] = strtoul(hexStr.c_str(), NULL, 16);
    } else {
      data[i] = 0;
    }
  }
  
  // Run inference
  int prediction = runInferenceRaw(can_id, dlc, data);
  
  if (prediction >= 0) {
    stats.total_messages++;
    if (fromWiFi) stats.wifi_messages++;
    else stats.serial_messages++;
    
    if (prediction == 0) stats.attack_free++;
    else if (prediction == 1) stats.dos_attacks++;
    else if (prediction == 2) stats.fuzzy_attacks++;
    
    // Handle LED based on prediction
    if (prediction != currentSystemState) {
      if (prediction == 0) {
        currentSystemState = 0;
      } else {
        currentSystemState = prediction;
        blinkWarning();
        ledRed();
      }
    }
    
    float confidence = output->data.f[prediction];
    
    Serial.println("\n========================================");
    Serial.printf("[%lu] %s Input\n", stats.total_messages, fromWiFi ? "WiFi" : "Serial");
    Serial.printf("CAN ID: %lu | DLC: %d\n", can_id, dlc);
    
    Serial.print("  Data (hex): ");
    for (int i = 0; i < 8; i++) {
      Serial.printf("0x%02X ", data[i]);
    }
    Serial.println();
    
    Serial.printf("  PREDICTION: %s\n", attack_labels[prediction]);
    Serial.printf("  Confidence: %.2f%%\n", confidence * 100);
    
    Serial.printf("  Probabilities:\n");
    Serial.printf("    Attack_Free: %.2f%%\n", output->data.f[0] * 100);
    Serial.printf("    DoS:         %.2f%%\n", output->data.f[1] * 100);
    Serial.printf("    Fuzzy:       %.2f%%\n", output->data.f[2] * 100);
    Serial.printf("  Inference Time: %lu us (%.2f ms)\n", 
                  stats.inference_time_us, stats.inference_time_us / 1000.0);
    Serial.println("========================================");
    
    updateLCD(prediction, confidence);
    logToSD(can_id, dlc, data, prediction);
    
    if (stats.total_messages % 100 == 0) {
      printStatistics();
    }
  }
}

// ============================================
// LOOP FUNCTION (WiFi + Serial)
// ============================================
void loop() {
  // Handle LED blinking based on system state (airplane wing style)
  if (currentSystemState == 0) {
    unsigned long currentTime = millis();
    unsigned long elapsed = currentTime - lastGreenBlink;
    
    if (greenLedState && elapsed >= greenOnTime) {
      // Turn off after 100ms
      ledOff();
      greenLedState = false;
      lastGreenBlink = currentTime;
    } else if (!greenLedState && elapsed >= greenOffTime) {
      // Turn on after 1000ms
      ledGreen();
      greenLedState = true;
      lastGreenBlink = currentTime;
    }
  }
  
  // Check WiFi client
  if (WiFi.status() == WL_CONNECTED) {
    if (server.hasClient()) {
      if (serverClient && serverClient.connected()) {
        serverClient.stop();
      }
      serverClient = server.available();
      Serial.println("\n>>> ESP8266 Transmitter Connected <<<\n");
    }
    
    if (serverClient && serverClient.connected() && serverClient.available()) {
      String input = serverClient.readStringUntil('\n');
      serverClient.println("OK");
      processData(input, true);
    }
  }
  
  // Check serial input (backup mode)
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    processData(input, false);
    Serial.println("\n> Paste next dataset row:");
  }
  
  delay(10);
}
