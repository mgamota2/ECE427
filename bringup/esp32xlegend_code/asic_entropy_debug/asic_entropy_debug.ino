#define LEGEND_OUTPUT_2 4
#define LEGEND_OUTPUT_1 5
#define LEGEND_SLOW_CLK 6
#define RAND_VALID 7
#define LEGEND_MISO 15
#define LEGEND_SPI_DATA_READY 16
#define RAND_BYTE_0 17
#define RAND_BYTE_1 18
#define OE_N_FROM_LEGEND_1 8
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

#define HALF_PERIOD_MS 100 // Slowed back down for visual check

enum SignalType {
  STUCK_LOW,
  STUCK_HIGH,
  SYSTEM_CLOCK,
  RANDOM_ENTROPY,
  UNKNOWN
};

void legend_sclk_tick(int cycles = 1) {
  for (int i = 0; i < cycles; i++) {
    digitalWrite(LEGEND_SCLK_IC_CLK, HIGH);
    delayMicroseconds(500);
    digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
    delayMicroseconds(500);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(OE_N_TO_LEGEND, OUTPUT);
  digitalWrite(OE_N_TO_LEGEND, LOW);
  pinMode(OE_N_FROM_LEGEND_1, OUTPUT);
  digitalWrite(OE_N_FROM_LEGEND_1, LOW);
  pinMode(OE_N_FROM_LEGEND_2, OUTPUT);
  digitalWrite(OE_N_FROM_LEGEND_2, LOW);
  pinMode(LED1, OUTPUT);
  digitalWrite(LED1, LOW);
  pinMode(LEGEND_SS_n, OUTPUT);
  digitalWrite(LEGEND_SS_n, HIGH);
  pinMode(LEGEND_MOSI, OUTPUT);
  digitalWrite(LEGEND_MOSI, LOW);
  pinMode(LEGEND_SCLK_IC_CLK, OUTPUT);
  digitalWrite(LEGEND_SCLK_IC_CLK, LOW);

  pinMode(LEGEND_OUTPUT_1, INPUT);
  pinMode(LEGEND_OUTPUT_2, INPUT);
  pinMode(LEGEND_MISO, INPUT);
  pinMode(LEGEND_SPI_DATA_READY, INPUT);

  // Clocked Reset
  pinMode(LEGEND_nRST, OUTPUT);
  digitalWrite(LEGEND_nRST, LOW);
  legend_sclk_tick(20);
  digitalWrite(LEGEND_nRST, HIGH);
  legend_sclk_tick(10);

  Serial.println("System Reset Complete. Manual Diagnostic Engine Loaded.");
}

// Renamed from spi_write_aligned, removed the << 2 shift.
// This is now proven to be perfectly aligned 1:1.
void spi_write(uint32_t logical_cmd) {
  digitalWrite(LEGEND_SS_n, LOW);
  delayMicroseconds(50);

  legend_sclk_tick(1);

  for (int i = 21; i >= 0; i--) {
    digitalWrite(LEGEND_MOSI, (logical_cmd >> i) & 0x1);
    legend_sclk_tick(1);
  }

  for (int j = 0; j < 5; j++) {
    legend_sclk_tick(1);
  }

  digitalWrite(LEGEND_SS_n, HIGH);
  legend_sclk_tick(1);
}

void mirror_output(unsigned long ms, int pin = LEGEND_OUTPUT_2) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    digitalWrite(LEGEND_SCLK_IC_CLK, HIGH);
    delayMicroseconds(10); // Wait for level shifter & ASIC propagation
    digitalWrite(LED1, digitalRead(pin));
    delay(HALF_PERIOD_MS);

    digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
    delayMicroseconds(10); // Wait for level shifter & ASIC propagation
    digitalWrite(LED1, digitalRead(pin));
    delay(HALF_PERIOD_MS);
  }
  digitalWrite(LED1, LOW);
}

SignalType classify_pin(int pin) {
  int high_count = 0;
  int clock_matches = 0;
  int transitions = 0;
  int last_val = -1;
  const int samples = 100;

  for (int i = 0; i < samples; i++) {
    // Phase HIGH
    digitalWrite(LEGEND_SCLK_IC_CLK, HIGH);
    delayMicroseconds(100);
    int val_h = digitalRead(pin);
    if (val_h)
      high_count++;
    if (val_h == 1)
      clock_matches++;
    if (last_val != -1 && val_h != last_val)
      transitions++;
    last_val = val_h;

    // Phase LOW
    digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
    delayMicroseconds(100);
    int val_l = digitalRead(pin);
    if (val_l)
      high_count++;
    if (val_l == 0)
      clock_matches++;
    if (val_l != last_val)
      transitions++;
    last_val = val_l;
  }

  if (high_count == samples * 2)
    return STUCK_HIGH;
  if (high_count == 0)
    return STUCK_LOW;
  if (clock_matches == samples * 2)
    return SYSTEM_CLOCK;
  if (transitions > (samples / 2))
    return RANDOM_ENTROPY;
  return UNKNOWN;
}

void print_signal_type(SignalType t) {
  switch (t) {
  case STUCK_LOW:
    Serial.print("LOW    ");
    break;
  case STUCK_HIGH:
    Serial.print("HIGH   ");
    break;
  case SYSTEM_CLOCK:
    Serial.print("CLOCK  ");
    break;
  case RANDOM_ENTROPY:
    Serial.print("RANDOM ");
    break;
  default:
    Serial.print("STATIC ");
    break;
  }
}

void spi_write_custom(uint32_t logical_cmd, int lead_in_clocks,
                      int extra_clocks) {
  digitalWrite(LEGEND_SS_n, LOW);
  delayMicroseconds(50);

  for (int i = 0; i < lead_in_clocks; i++) {
    legend_sclk_tick(1);
  }

  for (int i = 21; i >= 0; i--) {
    digitalWrite(LEGEND_MOSI, (logical_cmd >> i) & 0x1);
    legend_sclk_tick(1);
  }

  digitalWrite(LEGEND_SS_n, HIGH);
  delayMicroseconds(50);

  if (extra_clocks == 0) {
    legend_sclk_tick(1);
  } else {
    for (int j = 0; j < extra_clocks; j++) {
      legend_sclk_tick(1);
    }
  }
}

uint32_t spi_read_register(uint8_t reg_addr) {
  // 1. Send the READ command
  // Bit 21 = 1 (Reg Op), Bit 20 = 1 (Read Op), Bits 19:16 = Address
  uint32_t read_cmd = (1UL << 21) | (1UL << 20) | ((uint32_t)reg_addr << 16);

  // Use lead_in = 1 since we proved it's perfectly aligned!
  // CRITICAL: Use 5 extra clocks at the end so the ASIC has time to latch the
  // data into the TX shifter!
  spi_write_custom(read_cmd, 1, 5);

  // Wait for the ASIC to process the command and latch data_to_send into
  // tx_shifter
  delayMicroseconds(100);

  // 2. Perform a dummy transaction to clock the data out on MISO
  uint32_t received_data = 0;

  digitalWrite(LEGEND_SS_n, LOW);
  delayMicroseconds(50);
  legend_sclk_tick(1); // lead_in = 1

  for (int i = 21; i >= 0; i--) {
    // Send read_cmd again to prevent overwriting the MUX state with 0x00
    digitalWrite(LEGEND_MOSI, (read_cmd >> i) & 0x1);

    // The ASIC shifts MISO out on the rising edge, so we MUST sample it
    // BEFORE the rising edge (while the clock is still LOW).
    delayMicroseconds(50);
    if (digitalRead(LEGEND_MISO) == HIGH) {
      received_data |= (1UL << i);
    }

    // Now generate the clock pulse to shift the next bit
    digitalWrite(LEGEND_SCLK_IC_CLK, HIGH);
    delayMicroseconds(500);
    digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
    delayMicroseconds(500);
  }

  digitalWrite(LEGEND_SS_n, HIGH);
  legend_sclk_tick(1);

  // The MISO data consistently arrives with a 1-bit delay (right-shifted by 1).
  // We reconstruct the perfect 22-bit word by shifting it left by 1.
  return (received_data << 1);
}

void loop() {
  Serial.println("\n========================================================");
  Serial.println("  SWEEPING ALL LATCH ENTROPY SOURCES (10s EACH)");
  Serial.println("========================================================");



  // Hard reset
  digitalWrite(LEGEND_nRST, LOW);
  legend_sclk_tick(2);
  digitalWrite(LEGEND_nRST, HIGH);
  legend_sclk_tick(2);

  for (int cell_idx = 0; cell_idx < 32; cell_idx++) {
    Serial.print("\n--- Routing Latch Source #");
    Serial.print(cell_idx);
    Serial.println(" to Output 1 ---");
    

    // Mod Select Out 1 (Bits 13:12) = 1 (Latch) -> (1UL << 12)
    // Cell Select Out 1 (Bits 11:7) = cell_idx -> (cell_idx << 7)
    uint32_t out1_cmd = (1UL << 12) | ((uint32_t)cell_idx << 7);

    // Execute MUX Select
    spi_write_custom(out1_cmd, 1, 5);
    delay(10);

    // Classify the pin internally first
    Serial.print("Classification: ");
    SignalType t1 = classify_pin(LEGEND_OUTPUT_1);
    print_signal_type(t1);
    Serial.println();

    Serial.println("Mirroring to LED for 2 seconds...");
    // mirror_output naturally toggles SCLK, which should trigger latch
    // evaluation
    mirror_output(2000, LEGEND_OUTPUT_1);

    Serial.print("Reading reg 3 (After evaluation clocks): 0x");
    Serial.println(spi_read_register(0x3), HEX);
  }

  Serial.println("\nLatch Sweep Complete. Restarting in 5s...");
  delay(5000);
}
