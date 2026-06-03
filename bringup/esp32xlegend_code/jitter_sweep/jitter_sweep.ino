// ============================================================
// jitter_sweep.ino
// Sweeps all 32 jitter entropy sources across multiple SCLK
// frequencies and logs bit counts to Serial as CSV.
// No calibration register is written — jitter sources have none.
//
// CSV format: Source,Freq_us,Samples,Ones
// ============================================================

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

#define SPI_DELAY_US 5 // 5us half-period = 100kHz SPI clock

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

  for (int i = 0; i < lead_in_clocks; i++)
    legend_spi_tick(1);

  for (int i = 21; i >= 0; i--) {
    digitalWrite(LEGEND_MOSI, (logical_cmd >> i) & 0x1);
    legend_spi_tick(1);
  }

  digitalWrite(LEGEND_SS_n, HIGH);
  delayMicroseconds(10);

  for (int j = 0; j < (extra_clocks == 0 ? 1 : extra_clocks); j++) {
    legend_spi_tick(1);
  }
}

void reset_asic() {
  digitalWrite(LEGEND_nRST, LOW);
  legend_spi_tick(20);
  digitalWrite(LEGEND_nRST, HIGH);
  legend_spi_tick(10);
}

// Sample LEGEND_OUTPUT_1 over 'samples' SCLK cycles.
// Sample is taken at end of high phase, after latch has settled.
int count_ones(int pin, int samples, int delay_us) {
  int ones = 0;
  for (int i = 0; i < samples; i++) {
    digitalWrite(LEGEND_SCLK_IC_CLK, HIGH);
    delayMicroseconds(delay_us);
    if (digitalRead(pin) == HIGH) {
      ones++;
    }
    digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
    delayMicroseconds(delay_us);
  }
  return ones;
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
  pinMode(LEGEND_nRST, OUTPUT);

  pinMode(LEGEND_OUTPUT_1, INPUT);
  pinMode(LEGEND_OUTPUT_2, INPUT);
  pinMode(LEGEND_MISO, INPUT);
  pinMode(LEGEND_SPI_DATA_READY, INPUT);

  reset_asic();
  Serial.println("Jitter Sweep Ready. Waiting for START...");
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

  Serial.println("STARTING JITTER SWEEP");
  Serial.println("Source,Freq_us,Samples,Ones");

  const int SAMPLES = 20000;

  // Sweep a range of frequencies to characterize jitter vs clock speed.
  // Jitter entropy depends on the ratio of ring oscillator period to SCLK
  // period — slower SCLK accumulates more jitter events per cycle → richer
  // entropy. Half-period delays: 1µs≈385kHz, 2µs≈217kHz, 5µs=100kHz,
  // 10µs=50kHz, 50µs=10kHz
  int freq_delays_us[] = {5};
  int num_freqs = 1;

  for (int f_idx = 0; f_idx < num_freqs; f_idx++) {
    reset_asic();
    delay(10);

    int current_delay = freq_delays_us[f_idx];

    for (int cell_idx = 0; cell_idx < 32; cell_idx++) {
      // MUX jitter source to Output 1:
      // Mod Select Out1 (Bits 13:12) = 2'b10 = Jitter  → (2UL << 12)
      // Cell Select Out1 (Bits 11:7)  = cell_idx        → (cell_idx << 7)
      uint32_t jitter_cmd = (1UL << 13) | ((uint32_t)cell_idx << 7);
      spi_write_custom(jitter_cmd, 1, 5);
      delay(50);
      int ones = count_ones(LEGEND_OUTPUT_1, SAMPLES, current_delay);

      Serial.print(cell_idx);
      Serial.print(",");
      Serial.print(current_delay);
      Serial.print(",");
      Serial.print(SAMPLES);
      Serial.print(",");
      Serial.println(ones);
    }
  }

  Serial.println("SWEEP COMPLETE");
  sweep_done = true;
  sweep_started = false;
}
