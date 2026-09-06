/*
 * ESP8266 CAN Data Transmitter for TinyML IDS
 * Receives dataset rows via Serial and transmits to ESP32 via WiFi
 * 
 * Hardware: ESP8266 (NodeMCU/Wemos D1 Mini)
 * Connection: USB to Laptop for Serial input
 * 
 * Setup Instructions:
 * 1. Update WiFi credentials below (WIFI_SSID and WIFI_PASSWORD)
 * 2. Note the IP address shown on Serial Monitor after connecting
 * 3. Update this IP in ESP32 receiver code (ESP32_SERVER_IP)
 * 4. Upload to ESP8266
 * 5. Open Serial Monitor at 115200 baud
 * 6. Paste dataset rows in format: CAN_ID,DLC,DATA[0-7]
 */

// ============================================
// INCLUDES
// ============================================
#include <ESP8266WiFi.h>

// ============================================
// WIFI CONFIGURATION - UPDATE THESE!
// ============================================
const char* WIFI_SSID = "YOUR_WIFI_SSID";          // Your WiFi network name
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";  // Your WiFi password

// ============================================
// ESP32 RECEIVER CONFIGURATION
// ============================================
const char* ESP32_SERVER_IP = "192.168.0.100";   // ESP32 IP, printed on its Serial Monitor at boot
const uint16_t ESP32_SERVER_PORT = 8888;         // Port for communication

// ============================================
// GLOBAL VARIABLES
// ============================================
WiFiClient client;
unsigned long message_count = 0;
unsigned long successful_transmissions = 0;
unsigned long failed_transmissions = 0;

// ============================================
// SETUP FUNCTION
// ============================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n========================================");
  Serial.println("ESP8266 CAN Data Transmitter");
  Serial.println("TinyML IDS - Wireless Module");
  Serial.println("========================================\n");

  // Connect to WiFi
  Serial.print("[1/2] Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.mode(WIFI_STA);
  
  int retry_count = 0;
  while (WiFi.status() != WL_CONNECTED && retry_count < 30) {
    delay(500);
    Serial.print(".");
    retry_count++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n  ✓ WiFi Connected!");
    Serial.print("  Local IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("  Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\n  ✗ WiFi Connection Failed!");
    Serial.println("  Please check credentials and restart.");
    while (1) { delay(1000); }
  }

  Serial.println("\n[2/2] System Configuration:");
  Serial.print("  Target ESP32 IP: ");
  Serial.println(ESP32_SERVER_IP);
  Serial.print("  Target Port: ");
  Serial.println(ESP32_SERVER_PORT);
  
  Serial.println("\n========================================");
  Serial.println("SYSTEM READY");
  Serial.println("========================================");
  Serial.println("Paste CAN dataset rows to transmit");
  Serial.println("Format: CAN_ID,DLC,DATA[0-7]");
  Serial.println("Example: 0165000,8,0e,e8,7f,00,00,00,07,9e\n");
  Serial.println("> Waiting for input...\n");
}

// ============================================
// TRANSMIT DATA TO ESP32
// ============================================
bool transmitToESP32(String data) {
  // Try to connect to ESP32
  if (!client.connect(ESP32_SERVER_IP, ESP32_SERVER_PORT)) {
    Serial.println("✗ Connection to ESP32 failed!");
    return false;
  }
  
  // Send data with newline terminator
  client.println(data);
  client.flush();
  
  // Wait for acknowledgment (optional, with timeout)
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 1000) {
      Serial.println("✗ ESP32 response timeout!");
      client.stop();
      return false;
    }
    delay(10);
  }
  
  // Read acknowledgment
  String response = client.readStringUntil('\n');
  client.stop();
  
  return response.startsWith("OK");
}

// ============================================
// VALIDATE DATASET ROW FORMAT
// ============================================
bool validateDatasetRow(String input) {
  input.trim();
  
  if (input.length() == 0) {
    return false;
  }
  
  // Count commas (should have at least 1 for CAN_ID,DLC)
  int comma_count = 0;
  for (int i = 0; i < input.length(); i++) {
    if (input.charAt(i) == ',') {
      comma_count++;
    }
  }
  
  // Valid format: CAN_ID,DLC,D0,D1,D2,D3,D4,D5,D6,D7 (9 commas)
  // Minimum: CAN_ID,DLC (1 comma)
  if (comma_count < 1) {
    return false;
  }
  
  return true;
}

// ============================================
// PRINT STATISTICS
// ============================================
void printStatistics() {
  Serial.println("\n========== TRANSMISSION STATS ==========");
  Serial.printf("Total Messages:  %lu\n", message_count);
  Serial.printf("Successful:      %lu (%.1f%%)\n", 
                successful_transmissions,
                (float)successful_transmissions / message_count * 100);
  Serial.printf("Failed:          %lu (%.1f%%)\n", 
                failed_transmissions,
                (float)failed_transmissions / message_count * 100);
  Serial.printf("WiFi Status:     %s\n", 
                WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  Serial.printf("Signal Strength: %d dBm\n", WiFi.RSSI());
  Serial.println("========================================\n");
}

// ============================================
// LOOP FUNCTION
// ============================================
void loop() {
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠ WiFi disconnected! Reconnecting...");
    WiFi.reconnect();
    delay(5000);
    return;
  }
  
  // Check for serial input
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    if (input.length() == 0) {
      return;
    }
    
    // Validate format
    if (!validateDatasetRow(input)) {
      Serial.println("✗ ERROR: Invalid format!");
      Serial.println("  Expected: CAN_ID,DLC,D0,D1,D2,D3,D4,D5,D6,D7");
      Serial.println("  Example:  0165000,8,0e,e8,7f,00,00,00,07,9e");
      Serial.println();
      return;
    }
    
    message_count++;
    
    // Display transmission attempt
    Serial.println("\n----------------------------------------");
    Serial.printf("[%lu] Transmitting to ESP32...\n", message_count);
    Serial.print("  Data: ");
    Serial.println(input);
    
    // Transmit to ESP32
    if (transmitToESP32(input)) {
      successful_transmissions++;
      Serial.println("  ✓ Transmission successful!");
    } else {
      failed_transmissions++;
      Serial.println("  ✗ Transmission failed!");
    }
    Serial.println("----------------------------------------");
    
    // Print statistics every 50 messages
    if (message_count % 50 == 0) {
      printStatistics();
    }
    
    Serial.println("\n> Paste next dataset row:");
  }
  
  delay(10);  // Small delay to prevent watchdog issues
}
