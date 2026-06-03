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

#define SPI_DELAY_US 5 // 5us half-period = 100kHz clock for fast SPI

void legend_spi_tick(int cycles = 1) {
  for (int i = 0; i < cycles; i++) {
    digitalWrite(LEGEND_SCLK_IC_CLK, HIGH);
    delayMicroseconds(SPI_DELAY_US);
    digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
    delayMicroseconds(SPI_DELAY_US);
  }
}

void spi_write_custom(uint32_t logical_cmd, int lead_in_clocks,
                      int extra_clocks) {
  digitalWrite(LEGEND_SS_n, LOW);
  delayMicroseconds(10);

  for (int i = 0; i < lead_in_clocks; i++) {
    legend_spi_tick(1);
  }

  for (int i = 21; i >= 0; i--) {
    digitalWrite(LEGEND_MOSI, (logical_cmd >> i) & 0x1);
    legend_spi_tick(1);
  }

  digitalWrite(LEGEND_SS_n, HIGH);
  delayMicroseconds(10);

  if (extra_clocks == 0) {
    legend_spi_tick(1);
  } else {
    for (int j = 0; j < extra_clocks; j++) {
      legend_spi_tick(1);
    }
  }
}

void reset_asic() {
  digitalWrite(LEGEND_nRST, LOW);
  legend_spi_tick(20);
  digitalWrite(LEGEND_nRST, HIGH);
  legend_spi_tick(10);
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
  reset_asic();

  Serial.println("System Reset Complete. Ready to sweep.");
  delay(1000);
}

// Samples LEGEND_OUTPUT_1 over 'samples' clock cycles.
// Sampling is done at the END of the high phase, just before the falling edge,
// to give the latch the maximum possible time to resolve its metastable state.
// Note: At delay_us=1, effective SCLK is ~385kHz due to digitalWrite overhead
// (~150ns/call).
int count_ones(int pin, int samples, int delay_us) {
  int ones = 0;
  for (int i = 0; i < samples; i++) {
    digitalWrite(LEGEND_SCLK_IC_CLK,
                 HIGH);          // Rising edge: latch starts evaluating
    delayMicroseconds(delay_us); // Wait for latch to resolve

    // Sample at end of high phase, after the latch has had full time to settle
    if (digitalRead(pin) == HIGH) {
      ones++;
    }

    digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
    delayMicroseconds(delay_us);
  }
  return ones;
}

bool sweep_started = false;
bool sweep_done = false;

void loop() {
  if (!sweep_started) {
    if (Serial.available() > 0) {
      String cmd = Serial.readStringUntil('\n');
      cmd.trim();
      if (cmd == "START") {
        sweep_started = true;
        sweep_done = false;
      }
    }
    return;
  }

  if (sweep_done)
    return;

  Serial.println("STARTING LATCH CALIBRATION SWEEP");
  Serial.println("Source,Freq_us,P_cal,N_cal,Samples,Ones");

  const int SAMPLES_PER_CALIB = 10000;

  // High-resolution sweep at ~500kHz (1µs half-period + ~150ns GPIO overhead ≈
  // 385kHz actual)
  int freq_delays_us[] = {1};
  int num_freqs = 1;

  for (int f_idx = 0; f_idx < num_freqs; f_idx++) {
    reset_asic();
    delay(10);

    int current_delay = freq_delays_us[f_idx];

    // Outer loop: entropy source
    for (int cell_idx = 0; cell_idx < 32; cell_idx++) {

      // Inner loops: all 4096 calibration combinations
      for (int n_val = 0; n_val < 64; n_val++) {
        for (int p_val = 0; p_val < 64; p_val++) {

          // 1. Write calibration register first
          // Bits [11:6] = N_cal (biases toward 0)
          // Bits  [5:0] = P_cal (biases toward 1)
          uint32_t calib_word = (n_val << 6) | p_val;
          uint32_t reg_cmd =
              (1UL << 21) | (0UL << 20) | (7UL << 16) | calib_word;
          spi_write_custom(reg_cmd, 1, 5);

          // 2. Allow the analog bias network to settle
          // delayMicroseconds(200);

          // 3. Re-assert MUX AFTER calibration write to guarantee output
          // routing is correct Mod Select Out1 (Bits 13:12) = 1 (Latch), Cell
          // Select (Bits 11:7) = cell_idx
          uint32_t out1_cmd = (1UL << 12) | ((uint32_t)cell_idx << 7);
          spi_write_custom(out1_cmd, 1, 5);

          // 4. Sample
          int ones =
              count_ones(LEGEND_OUTPUT_1, SAMPLES_PER_CALIB, current_delay);

          // 5. Log: Source,Freq_us,P_cal,N_cal,Samples,Ones
          Serial.print(cell_idx);
          Serial.print(",");
          Serial.print(current_delay);
          Serial.print(",");
          Serial.print(p_val);
          Serial.print(",");
          Serial.print(n_val);
          Serial.print(",");
          Serial.print(SAMPLES_PER_CALIB);
          Serial.print(",");
          Serial.println(ones);
        }
      }
    }
  }

  Serial.println("SWEEP COMPLETE");
  sweep_done = true;
  sweep_started = false;
}
