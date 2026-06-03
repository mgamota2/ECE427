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

enum SignalType {
  STUCK_LOW,
  STUCK_HIGH,
  SYSTEM_CLOCK,
  RANDOM_ENTROPY,
  UNKNOWN
};

int clk_delay_us = 500;

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

  Serial.println("READY");
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
  uint32_t read_cmd = (1UL << 21) | (1UL << 20) | ((uint32_t)reg_addr << 16);

  // Send the command and explicitly give it 5 extra clocks to latch tx_shifter
  spi_write_custom(read_cmd, 1, 5);
  delayMicroseconds(100);

  // 2. Perform a dummy transaction to clock the data out on MISO
  uint32_t received_data = 0;

  digitalWrite(LEGEND_SS_n, LOW);
  delayMicroseconds(50);
  legend_sclk_tick(1); // lead_in = 1

  for (int i = 21; i >= 0; i--) {
    // Send all 1s as a dummy command to prevent overwriting MUX state
    digitalWrite(LEGEND_MOSI, HIGH);

    // Read BEFORE the rising edge to compensate for FSM latency
    delayMicroseconds(50);
    if (digitalRead(LEGEND_MISO) == HIGH) {
      received_data |= (1UL << i);
    }

    // Generate clock pulse
    digitalWrite(LEGEND_SCLK_IC_CLK, HIGH);
    custom_delay_us(clk_delay_us);
    digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
    custom_delay_us(clk_delay_us);
  }

  // The MISO data consistently arrives with a 1-bit delay, so the very last bit
  // (Bit 0) is sitting on the MISO line RIGHT NOW, after the loop finishes.
  uint32_t final_data = (received_data << 1);
  delayMicroseconds(50);
  if (digitalRead(LEGEND_MISO) == HIGH) {
    final_data |= 1UL;
  }

  digitalWrite(LEGEND_SS_n, HIGH);
  legend_sclk_tick(1);

  return final_data;
}

void mirror_output(unsigned long ms, int pin) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    digitalWrite(LEGEND_SCLK_IC_CLK, HIGH);
    delayMicroseconds(10); // Wait for level shifter & ASIC propagation
    digitalWrite(LED1, digitalRead(pin));
    custom_delay_us(clk_delay_us);

    digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
    delayMicroseconds(10); // Wait for level shifter & ASIC propagation
    digitalWrite(LED1, digitalRead(pin));
    custom_delay_us(clk_delay_us);
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
    digitalWrite(LEGEND_SCLK_IC_CLK, HIGH);
    custom_delay_us(clk_delay_us);
    int val_h = digitalRead(pin);
    if (val_h)
      high_count++;
    if (val_h == 1)
      clock_matches++;
    if (last_val != -1 && val_h != last_val)
      transitions++;
    last_val = val_h;

    digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
    custom_delay_us(clk_delay_us);
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

String get_signal_type_str(SignalType t) {
  switch (t) {
  case STUCK_LOW:
    return "LOW";
  case STUCK_HIGH:
    return "HIGH";
  case SYSTEM_CLOCK:
    return "CLOCK";
  case RANDOM_ENTROPY:
    return "RANDOM";
  default:
    return "STATIC";
  }
}

void loop() {
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0)
      return;

    char op = cmd.charAt(0);
    String argStr = "";
    if (cmd.length() > 2) {
      argStr = cmd.substring(2);
    }

    // Command: R <reg_hex>
    // Reads from register and returns: OK <hex_value>
    if (op == 'R' || op == 'r') {
      uint8_t reg = strtol(argStr.c_str(), NULL, 16);
      uint32_t val = spi_read_register(reg);
      Serial.print("OK ");
      Serial.println(val, HEX);
    }
    // Command: W <cmd_hex>
    // Writes raw 22-bit SPI command and returns: OK
    else if (op == 'W' || op == 'w') {
      uint32_t val = strtol(argStr.c_str(), NULL, 16);
      spi_write_custom(val, 1, 5);
      Serial.println("OK");
    }
    // Command: C <1_or_2>
    // Classifies Output 1 or 2 and returns: OK <CLASSIFICATION>
    else if (op == 'C' || op == 'c') {
      int p = argStr.toInt();
      SignalType t = classify_pin(p == 1 ? LEGEND_OUTPUT_1 : LEGEND_OUTPUT_2);
      Serial.print("OK ");
      Serial.println(get_signal_type_str(t));
    }
    // Command: X
    // Hard resets the ASIC
    else if (op == 'X' || op == 'x') {
      digitalWrite(LEGEND_nRST, LOW);
      legend_sclk_tick(2);
      digitalWrite(LEGEND_nRST, HIGH);
      legend_sclk_tick(2);
      Serial.println("OK");
    }
    // Command: M <pin_num> <ms>
    // Mirrors the specified output pin to the LED for ms milliseconds
    else if (op == 'M' || op == 'm') {
      int space_idx = argStr.indexOf(' ');
      if (space_idx != -1) {
        int p = argStr.substring(0, space_idx).toInt();
        unsigned long ms = argStr.substring(space_idx + 1).toInt();
        int target_pin = (p == 1) ? LEGEND_OUTPUT_1 : LEGEND_OUTPUT_2;
        mirror_output(ms, target_pin);
        Serial.println("OK");
      } else {
        Serial.println("ERR INVALID_ARGS");
      }
    }
    // Command: F <delay_us>
    // Sets the software clock half-period in microseconds
    else if (op == 'F' || op == 'f') {
      clk_delay_us = argStr.toInt();
      Serial.print("OK ");
      Serial.println(clk_delay_us);
    }
    // Command: E <samples>
    // Evaluates LEGEND_OUTPUT_1 for <samples> clocks and returns the number of
    // 1s
    else if (op == 'E' || op == 'e') {
      int samples = argStr.toInt();
      if (samples <= 0)
        samples = 1000;

      int ones = 0;
      for (int i = 0; i < samples; i++) {
        digitalWrite(LEGEND_SCLK_IC_CLK, HIGH);
        custom_delay_us(clk_delay_us);

        if (digitalRead(LEGEND_OUTPUT_1) == HIGH) {
          ones++;
        }

        digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
        custom_delay_us(clk_delay_us);
      }
      Serial.print("OK ");
      Serial.println(ones);
    }
    // Command: D <cycles>
    // Debug Strobe: Runs SCLK for <cycles> cycles, pulsing LED1 HIGH for the
    // exact window that the calibration sweep would sample LEGEND_OUTPUT_1. Use
    // a 3-channel scope: Ch1=SCLK, Ch2=LEGEND_OUTPUT_1, Ch3=LED1 to verify the
    // sample point is landing in the settled output window.
    else if (op == 'D' || op == 'd') {
      int cycles = argStr.toInt();
      if (cycles <= 0)
        cycles = 100;

      for (int i = 0; i < cycles; i++) {
        digitalWrite(LEGEND_SCLK_IC_CLK,
                     HIGH);            // Rising edge: latch starts evaluating
        custom_delay_us(clk_delay_us); // Wait half-period for latch to settle

        digitalWrite(LED1, HIGH);           // Strobe: we are sampling RIGHT NOW
        (void)digitalRead(LEGEND_OUTPUT_1); // This is the exact sample point
        digitalWrite(LED1, LOW);            // Strobe ends

        digitalWrite(LEGEND_SCLK_IC_CLK, LOW); // Falling edge
        custom_delay_us(clk_delay_us);
      }
      Serial.println("OK");
    } else {
      Serial.println("ERR UNKNOWN_CMD");
    }
  }
}
