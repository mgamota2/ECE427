// #include <WiFi.h>
// #include <WebServer.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// --- LEGEND ASIC Pins ---
#define LEGEND_OUTPUT_2 4
#define LEGEND_OUTPUT_1 5
#define LEGEND_SLOW_CLK 6
#define RAND_VALID 7
#define LEGEND_MISO 15
#define LEGEND_SPI_DATA_READY 16
#define OE_N_FROM_LEGEND_1 8
#define RAND_BYTE_0 17
#define RAND_BYTE_1 18
#define RAND_BYTE_2 9
#define RAND_BYTE_3 10
#define RAND_BYTE_4 11
#define RAND_BYTE_5 12
#define RAND_BYTE_6 13
#define RAND_BYTE_7 14
#define RAND_BYTE_8 21
#define RAND_BYTE_9 47
#define OE_N_FROM_LEGEND_2 48
#define LEGEND_nRST 35
#define LEGEND_SCLK_IC_CLK 36
#define RAND_REQ 37
#define LEGEND_MOSI 38
#define LEGEND_SS_n 39
#define RAND_REQ_TYPE_2 40
#define RAND_REQ_TYPE_1 41
#define RAND_REQ_TYPE_0 42
#define OE_N_TO_LEGEND 2
#define LED1 1

// --- Config ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

int clk_delay_us = 2; // Fast enough for clocks
// WebServer server(80);

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
BLEAdvertising* pAdvertising = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
unsigned long last_random_time = 0;

enum ModeType { MODE_RAW, MODE_RDSEED, MODE_RDRAND };
enum SourceType { SRC_LATCH, SRC_JITTER };

ModeType current_mode = MODE_RAW;
SourceType current_source = SRC_LATCH;

void custom_delay_us(uint32_t us) {
  if (us >= 1000) {
    delay(us / 1000);
  }
  delayMicroseconds(us % 1000);
}

void legend_sclk_tick(int cycles = 1) {
  for (int i = 0; i < cycles; i++) {
    digitalWrite(LEGEND_SCLK_IC_CLK, HIGH);
    custom_delay_us(clk_delay_us);
    digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
    custom_delay_us(clk_delay_us);
  }
}

void spi_write_custom(uint32_t logical_cmd, int lead_in_clocks, int extra_clocks) {
  digitalWrite(LEGEND_SS_n, LOW);
  delayMicroseconds(10);
  for (int i = 0; i < lead_in_clocks; i++) legend_sclk_tick(1);
  for (int i = 21; i >= 0; i--) {
    digitalWrite(LEGEND_MOSI, (logical_cmd >> i) & 0x1);
    legend_sclk_tick(1);
  }
  digitalWrite(LEGEND_SS_n, HIGH);
  delayMicroseconds(10);
  if (extra_clocks == 0) legend_sclk_tick(1);
  else for (int j = 0; j < extra_clocks; j++) legend_sclk_tick(1);
}

void auto_calibrate_latch() {
  Serial.println("Auto-calibrating latch (Cell 4)...");
  for (int p = 0; p < 64; p += 4) {
    for (int n = 0; n < 64; n += 4) {
      uint32_t calib_word = (n << 6) | p;
      uint32_t reg_cmd = (1UL << 21) | (0UL << 20) | (7UL << 16) | calib_word;
      spi_write_custom(reg_cmd, 1, 5);
      
      uint32_t out1_cmd = (1UL << 12) | (4 << 7); // M=1, Cell=4
      spi_write_custom(out1_cmd, 1, 5);
      
      int ones = 0;
      for(int i=0; i<1000; i++) {
        digitalWrite(LEGEND_SCLK_IC_CLK, HIGH);
        custom_delay_us(clk_delay_us);
        if (digitalRead(LEGEND_OUTPUT_1) == HIGH) ones++;
        digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
        custom_delay_us(clk_delay_us);
      }
      
      if (abs(ones - 500) < 30) {
        Serial.printf("Calibrated! P=%d N=%d\n", p, n);
        return;
      }
    }
  }
  Serial.println("Calibration finished (no perfect match found).");
}

uint8_t get_random_byte_hw(ModeType mode) {
    if (mode == MODE_RDSEED) { // 000
        digitalWrite(RAND_REQ_TYPE_2, LOW);
        digitalWrite(RAND_REQ_TYPE_1, LOW);
        digitalWrite(RAND_REQ_TYPE_0, LOW);
    } else {                   // 100
        digitalWrite(RAND_REQ_TYPE_2, HIGH);
        digitalWrite(RAND_REQ_TYPE_1, LOW);
        digitalWrite(RAND_REQ_TYPE_0, LOW);
    }

    digitalWrite(RAND_REQ, HIGH);

    int timeout = 100000;
    while (digitalRead(RAND_VALID) == LOW && timeout > 0) {
        digitalWrite(LEGEND_SCLK_IC_CLK, HIGH);
        delayMicroseconds(1);
        digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
        delayMicroseconds(1);
        timeout--;
    }

    uint8_t val = 0;
    if (digitalRead(RAND_BYTE_0) == HIGH) val |= (1 << 0);
    if (digitalRead(RAND_BYTE_1) == HIGH) val |= (1 << 1);
    if (digitalRead(RAND_BYTE_2) == HIGH) val |= (1 << 2);
    if (digitalRead(RAND_BYTE_3) == HIGH) val |= (1 << 3);
    if (digitalRead(RAND_BYTE_4) == HIGH) val |= (1 << 4);
    if (digitalRead(RAND_BYTE_5) == HIGH) val |= (1 << 5);
    if (digitalRead(RAND_BYTE_6) == HIGH) val |= (1 << 6);
    if (digitalRead(RAND_BYTE_7) == HIGH) val |= (1 << 7);

    digitalWrite(RAND_REQ, LOW);
    
    for(int i=0; i<10; i++) {
        digitalWrite(LEGEND_SCLK_IC_CLK, HIGH);
        delayMicroseconds(1);
        digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
        delayMicroseconds(1);
    }
    return val;
}

// Generates 1 byte of entropy by clocking LEGEND_OUTPUT_1 eight times
uint8_t get_random_byte() {
  if (current_mode == MODE_RDSEED || current_mode == MODE_RDRAND) {
      return get_random_byte_hw(current_mode);
  }
  
  // MODE_RAW
  uint8_t rand_byte = 0;
  for (int i = 0; i < 8; i++) {
    digitalWrite(LEGEND_SCLK_IC_CLK, HIGH);
    custom_delay_us(clk_delay_us);
    
    if (digitalRead(LEGEND_OUTPUT_1) == HIGH) {
      rand_byte |= (1 << i);
    }
    
    digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
    custom_delay_us(clk_delay_us);
  }
  return rand_byte;
}

// --- WiFi API Handlers ---
/*
void handleRoot() {
  server.send(200, "text/plain", "LEGEND ASIC API\n/rand?bytes=X\n/config?mode=raw|rdseed|rdrand&source=latch|jitter\n");
}

void handleConfig() {
  if (server.hasArg("mode")) {
    String m = server.arg("mode");
    if (m == "raw") current_mode = MODE_RAW;
    else if (m == "rdseed") current_mode = MODE_RDSEED;
    else if (m == "rdrand") current_mode = MODE_RDRAND;
  }
  if (server.hasArg("source")) {
    String s = server.arg("source");
    if (s == "latch") current_source = SRC_LATCH;
    else if (s == "jitter") current_source = SRC_JITTER;
  }
  
  if (current_mode == MODE_RAW) {
    if (current_source == SRC_LATCH) {
      auto_calibrate_latch();
      uint32_t out1_cmd = (1UL << 12) | (0 << 7); // M=1 Latch
      spi_write_custom(out1_cmd, 1, 5);
    } else {
      uint32_t out1_cmd = (2UL << 12) | (0 << 7); // M=2 Jitter
      spi_write_custom(out1_cmd, 1, 5);
    }
  }
  server.send(200, "text/plain", "Configured.");
}

void handleRand() {
  int num_bytes = 16;
  if (server.hasArg("bytes")) {
    num_bytes = server.arg("bytes").toInt();
  }
  if (num_bytes > 1024) num_bytes = 1024; // Sanity limit
  
  uint8_t* buf = (uint8_t*)malloc(num_bytes);
  for(int i=0; i<num_bytes; i++) {
    buf[i] = get_random_byte();
  }
  
  // Return as hex string
  String out = "";
  for(int i=0; i<num_bytes; i++) {
    char hex[3];
    sprintf(hex, "%02x", buf[i]);
    out += hex;
  }
  free(buf);
  server.send(200, "text/plain", out);
}
*/

// --- BLE Callbacks ---
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      digitalWrite(LED1, HIGH); // LED ON when connected
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      digitalWrite(LED1, LOW); // LED OFF when disconnected
    }
};

void setup() {
  Serial.begin(115200);

  // ASIC Pin Setup
  pinMode(OE_N_TO_LEGEND, OUTPUT); digitalWrite(OE_N_TO_LEGEND, LOW);
  pinMode(OE_N_FROM_LEGEND_1, OUTPUT); digitalWrite(OE_N_FROM_LEGEND_1, LOW);
  pinMode(OE_N_FROM_LEGEND_2, OUTPUT); digitalWrite(OE_N_FROM_LEGEND_2, LOW);
  pinMode(LED1, OUTPUT); digitalWrite(LED1, LOW);
  pinMode(LEGEND_SS_n, OUTPUT); digitalWrite(LEGEND_SS_n, HIGH);
  pinMode(LEGEND_MOSI, OUTPUT); digitalWrite(LEGEND_MOSI, LOW);
  pinMode(LEGEND_SCLK_IC_CLK, OUTPUT); digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
  pinMode(LEGEND_OUTPUT_1, INPUT);
  pinMode(LEGEND_OUTPUT_2, INPUT);
  pinMode(LEGEND_MISO, INPUT);
  pinMode(LEGEND_SPI_DATA_READY, INPUT);

  pinMode(RAND_REQ, OUTPUT); digitalWrite(RAND_REQ, LOW);
  pinMode(RAND_REQ_TYPE_0, OUTPUT); digitalWrite(RAND_REQ_TYPE_0, LOW);
  pinMode(RAND_REQ_TYPE_1, OUTPUT); digitalWrite(RAND_REQ_TYPE_1, LOW);
  pinMode(RAND_REQ_TYPE_2, OUTPUT); digitalWrite(RAND_REQ_TYPE_2, LOW);
  
  pinMode(RAND_BYTE_0, INPUT);
  pinMode(RAND_BYTE_1, INPUT);
  pinMode(RAND_BYTE_2, INPUT);
  pinMode(RAND_BYTE_3, INPUT);
  pinMode(RAND_BYTE_4, INPUT);
  pinMode(RAND_BYTE_5, INPUT);
  pinMode(RAND_BYTE_6, INPUT);
  pinMode(RAND_BYTE_7, INPUT);
  pinMode(RAND_BYTE_8, INPUT);
  pinMode(RAND_BYTE_9, INPUT);

  // Clocked Reset
  pinMode(LEGEND_nRST, OUTPUT);
  digitalWrite(LEGEND_nRST, LOW);
  legend_sclk_tick(20);
  digitalWrite(LEGEND_nRST, HIGH);
  legend_sclk_tick(10);
  
  auto_calibrate_latch();
  
  // Set default MUX to Latch (M=1), Cell=4 for MODE_RAW
  uint32_t out1_cmd = (1UL << 12) | (4 << 7); 
  spi_write_custom(out1_cmd, 1, 5);
  
  // --- WiFi Setup ---
  /*
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  int wifi_timeout = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_timeout < 20) {
    delay(500);
    Serial.print(".");
    wifi_timeout++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected.");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    server.on("/", handleRoot);
    server.on("/config", handleConfig);
    server.on("/rand", handleRand);
    server.begin();
  } else {
    Serial.println("\nWiFi Failed.");
  }
  */

  // --- BLE Setup ---
  BLEDevice::init("LEGEND_RNG");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  // We use Notify so the app automatically receives updates without requesting
  pCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID,
                        BLECharacteristic::PROPERTY_READ   |
                        BLECharacteristic::PROPERTY_NOTIFY |
                        BLECharacteristic::PROPERTY_INDICATE
                      );
  pCharacteristic->addDescriptor(new BLE2902());
  
  pService->start();
  
  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  
  pAdvertising->setMinPreferred(0x12);
  pAdvertising->start();
  Serial.println("BLE Started. UUID: " SERVICE_UUID);

  Serial.println("API READY");
}

void loop() {
  // Handle HTTP Requests
  // server.handleClient();

  // BLE Connection Management
  if (!deviceConnected && oldDeviceConnected) {
      delay(500); // give the bluetooth stack the chance to get things ready
      pServer->startAdvertising(); // restart advertising
      Serial.println("Restarting BLE Advertising...");
      oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
      oldDeviceConnected = deviceConnected;
  }

  // Every 5 seconds, grab random bytes and Notify / Update Advertising
  if (millis() - last_random_time > 1000) {
      last_random_time = millis();
      
      uint8_t buf[16];
      for(int i=0; i<16; i++) {
          buf[i] = get_random_byte();
      }

      // 1. If connected, Push via Notify
      if (deviceConnected) {
          pCharacteristic->setValue(buf, 16);
          pCharacteristic->notify();
          Serial.println("Pushed 16 random bytes via BLE Notify.");
      }
      
      // 2. Broadcast in Advertising Data (for anyone scanning to see)
      BLEAdvertisementData advData = BLEAdvertisementData();
      advData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
      // We embed the first 4 random bytes directly into the manufacturer data!
      String mfgData = "";
      mfgData += (char)0xFF; // Manufacturer ID (dummy)
      mfgData += (char)0xFF;
      for(int i=0; i<4; i++) mfgData += (char)buf[i];
      advData.setManufacturerData(mfgData);
      
      pAdvertising->setAdvertisementData(advData);
  }

  // Handle Serial Commands
  /*
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    char op = cmd.charAt(0);
    String argStr = "";
    if (cmd.length() > 2) {
      argStr = cmd.substring(2);
    }

    // Command: B <bytes>
    // Returns <bytes> random bytes from LEGEND_OUTPUT_1 in HEX
    if (op == 'B' || op == 'b') {
      int bytes = argStr.toInt();
      if (bytes <= 0) bytes = 16;
      Serial.print("OK ");
      for(int i=0; i<bytes; i++) {
        uint8_t b = get_random_byte();
        if (b < 16) Serial.print("0");
        Serial.print(b, HEX);
      }
      Serial.println();
    } 
    // Command: F <delay_us>
    else if (op == 'F' || op == 'f') {
      clk_delay_us = argStr.toInt();
      Serial.print("OK ");
      Serial.println(clk_delay_us);
    } 
    else {
      Serial.println("ERR UNKNOWN_CMD (Use 'B <bytes>' for random data)");
    }
  }
  */
}
