// LEGEND is a custom random number generator ASIC. It has a debug mode and a default mode. In debug
// mode, the user can test submodules and route external inputs (INPUT pin) to intermediate submodules and route
// internal signals to the OUTPUT1 and OUTPUT 2 pins. In default mode, the RAND_REQ_TYPE pins are configured
// to request a 16, 32, or 64 bit RDSEED or RDRAND. Rand req must go high to submit the request. The output is
// sampled from the RAND_BYTE [0:15] pins, only [0:9] is sent to the ESP32 S3 WROOM. LEGEND operates at 1.2V
// so level shifters are used for all inputs and outputs. These level shifters are enabled with "OE_N_TO_LEGEND", 
// "OE_N_FROM_LEGEND1" and "OE_N_FROM_LEGEND2". Output bits in default mode are sampled when RAND_VALID is high
// and the bits are updated on the rising edge of SLOW_CLK.
// In debug mode, the CLK pin is used as the SPI clock to interface with a custom 22-bit message SPI interface. 
// All commands are register reads or writes, but write commands that write to the "debug state register" are treated
// specially as "Mux configuration commands". These commands select which submodules the output and input pins connect to.

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
#define DEBUG1 1

#define HALF_PERIOD     1   // 1 ms half period -> 500 Hz clock
#define RESET_HOLD_MS   10
#define RESET_SETTLE_MS 10

#define SLOW_CLK_PERIOD_MS  (HALF_PERIOD * 2 * 2)
#define REPORT_INTERVAL_MS  ((RING_BUF_SIZE - 1) * SLOW_CLK_PERIOD_MS)


// --- Ring buffer for captured samples ---
#define RING_BUF_SIZE 64

struct RandSample {
    uint16_t rand_bits;   // RAND_BYTE[9:0], 10 bits
    uint32_t timestamp_ms;
};

RandSample ring_buf[RING_BUF_SIZE];
volatile uint16_t ring_head = 0;  // next write index
volatile uint16_t ring_count = 0; // how many valid entries

// Parallel array of RAND_BYTE pins in bit order [0..9]
const int RAND_PINS[10] = {
    RAND_BYTE_0, RAND_BYTE_1, RAND_BYTE_2, RAND_BYTE_3,
    RAND_BYTE_4, RAND_BYTE_5, RAND_BYTE_6, RAND_BYTE_7,
    RAND_BYTE_8, RAND_BYTE_9
};

// Read all 10 RAND_BYTE pins and pack into a uint16_t (bit 0 = RAND_BYTE_0)
uint16_t read_rand_bits() {
    uint16_t val = 0;
    for (int i = 0; i < 10; i++) {
        if (digitalRead(RAND_PINS[i])) {
            val |= (1u << i);
        }
    }
    return val;
}

// Push a sample into the ring buffer
void push_sample(uint16_t bits) {
    ring_buf[ring_head].rand_bits    = bits;
    ring_buf[ring_head].timestamp_ms = millis();
    ring_head = (ring_head + 1) % RING_BUF_SIZE;
    if (ring_count < RING_BUF_SIZE) ring_count++;
}

// Drain and print all buffered samples over Serial
void flush_samples() {
    if (ring_count == 0) return;

    // Walk from oldest to newest
    uint16_t oldest = (ring_head - ring_count + RING_BUF_SIZE) % RING_BUF_SIZE;
    for (uint16_t i = 0; i < ring_count; i++) {
        uint16_t idx = (oldest + i) % RING_BUF_SIZE;
        RandSample &s = ring_buf[idx];
        Serial.printf("[%8lu ms] RAND_BYTE[9:0] = 0x%03X  (%04u decimal)  0b",
                      s.timestamp_ms, s.rand_bits, s.rand_bits);
        // Print binary MSB first
        for (int b = 9; b >= 0; b--) {
            Serial.print((s.rand_bits >> b) & 1);
        }
        Serial.println();
    }
    ring_count = 0;
    ring_head  = 0;
}

void setup() {
    Serial.begin(115200);

    // --- Enable level shifters ---
    pinMode(OE_N_TO_LEGEND, OUTPUT);
    digitalWrite(OE_N_TO_LEGEND, LOW);

    pinMode(OE_N_FROM_LEGEND_1, OUTPUT);
    digitalWrite(OE_N_FROM_LEGEND_1, LOW);

    pinMode(OE_N_FROM_LEGEND_2, OUTPUT);
    digitalWrite(OE_N_FROM_LEGEND_2, LOW);

    // --- Default mode control pins ---
    pinMode(RAND_REQ_TYPE_2, OUTPUT); digitalWrite(RAND_REQ_TYPE_2, LOW);
    pinMode(RAND_REQ_TYPE_1, OUTPUT); digitalWrite(RAND_REQ_TYPE_1, HIGH);
    pinMode(RAND_REQ_TYPE_0, OUTPUT); digitalWrite(RAND_REQ_TYPE_0, LOW);
    pinMode(RAND_REQ, OUTPUT);        digitalWrite(RAND_REQ, HIGH);

    pinMode(LEGEND_SCLK_IC_CLK, OUTPUT); digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
    pinMode(DEBUG1, OUTPUT);             digitalWrite(DEBUG1, LOW);

    // --- SPI idle ---
    pinMode(LEGEND_SS_n, OUTPUT);  digitalWrite(LEGEND_SS_n, HIGH);
    pinMode(LEGEND_MOSI, OUTPUT);  digitalWrite(LEGEND_MOSI, LOW);

    // --- LEGEND outputs: inputs to ESP32 ---
    pinMode(LEGEND_SLOW_CLK, INPUT_PULLDOWN);
    pinMode(RAND_VALID,       INPUT_PULLDOWN);
    for (int i = 0; i < 10; i++) {
        pinMode(RAND_PINS[i], INPUT_PULLDOWN);
    }
    // Unused LEGEND outputs — still configure to avoid floating
    pinMode(LEGEND_OUTPUT_1,        INPUT_PULLDOWN);
    pinMode(LEGEND_OUTPUT_2,        INPUT_PULLDOWN);
    pinMode(LEGEND_MISO,            INPUT_PULLDOWN);
    pinMode(LEGEND_SPI_DATA_READY,  INPUT_PULLDOWN);

    // --- Reset sequence ---
    pinMode(LEGEND_nRST, OUTPUT);
    digitalWrite(LEGEND_nRST, LOW);
    delay(RESET_HOLD_MS);
    digitalWrite(LEGEND_nRST, HIGH);
    delay(RESET_SETTLE_MS);

    Serial.println("LEGEND running — sampling RAND_BYTE[9:0] on SLOW_CLK rising edge when RAND_VALID=1");
}

// Track previous SLOW_CLK state for edge detection
bool clk_prev = false;

// Accumulate edges between Serial prints
uint32_t total_clk_edges  = 0;
uint32_t total_valid_samples = 0;
uint32_t last_report_ms   = 0;

void loop() {
    // --- Drive ESP32 clock and mirror to LED ---
    digitalWrite(LEGEND_SCLK_IC_CLK, HIGH);
    digitalWrite(DEBUG1, HIGH);
    delay(HALF_PERIOD);

    digitalWrite(LEGEND_SCLK_IC_CLK, LOW);
    digitalWrite(DEBUG1, LOW);
    delay(HALF_PERIOD);

    // --- Detect SLOW_CLK rising edge from LEGEND ---
    bool clk_now = digitalRead(LEGEND_SLOW_CLK);
    if (clk_now && !clk_prev) {
        // Rising edge — bits are now valid/updated per the datasheet
        total_clk_edges++;

        if (digitalRead(RAND_VALID)) {
            uint16_t bits = read_rand_bits();
            push_sample(bits);
            total_valid_samples++;
        }
    }
    clk_prev = clk_now;

    // --- Periodically flush buffered samples to Serial ---
    uint32_t now = millis();
    if (now - last_report_ms >= REPORT_INTERVAL_MS) {
        Serial.printf("--- CLK edges: %lu  |  valid samples: %lu ---\n",
                      total_clk_edges, total_valid_samples);
        flush_samples();
        last_report_ms = now;
    }
}
