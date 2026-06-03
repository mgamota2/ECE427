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

//         RDSEED_16 = 3'b000,
//         RDRAND_16 = 3'b100,
//         RDSEED_32 = 3'b001,
//         RDRAND_32 = 3'b101,
//         RDSEED_64 = 3'b010,
//         RDRAND_64 = 3'b110


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

#define RESET_HOLD_MS   10
#define RESET_SETTLE_MS 10

#define RING_BUF_SIZE   8192  // Increased buffer size to prevent drops

#define NUM_BITS 10


#include "soc/gpio_struct.h"


// --- Framing ---
// Each sample is 3 bytes: [SYNC=0xFF][high_byte bits9:8][low_byte bits7:0]
// 0xFF never appears as high byte of a valid 10-bit sample (max high byte = 0x03)
// so it is an unambiguous frame marker Python can use to re-sync
#define FRAME_SYNC 0xFF

const int RAND_PINS[10] = {
    RAND_BYTE_0, RAND_BYTE_1, RAND_BYTE_2, RAND_BYTE_3,
    RAND_BYTE_4, RAND_BYTE_5, RAND_BYTE_6, RAND_BYTE_7,
    RAND_BYTE_8, RAND_BYTE_9
};

// --- Double (Ping-Pong) Buffer ---
// Stores raw 10-bit values; framing applied at flush time
uint16_t buf_a[RING_BUF_SIZE];
uint16_t buf_b[RING_BUF_SIZE];

volatile uint16_t* write_buf  = buf_a;
volatile uint16_t  write_head = 0;

volatile bool buf_a_full = false;
volatile bool buf_b_full = false;

volatile uint32_t isr_clk_edges     = 0;
volatile uint32_t isr_valid_samples = 0;
volatile bool     isr_overflow      = false;

// Global array to prevent stack overflow during flush
uint8_t out_buf[RING_BUF_SIZE * 3];

// Direct GPIO register read — replaces 10x digitalRead calls in ISR
// Saves ~2us per ISR invocation vs digitalRead loop
uint16_t IRAM_ATTR read_rand_bits_fast() {
    uint32_t reg0 = GPIO.in;           // pins 0-31
    uint32_t reg1 = GPIO.in1.val;          // pins 32-53

    uint16_t val = 0;
    if (reg0 & (1u << 17)) val |= (1u << 0);   // RAND_BYTE_0
    if (reg0 & (1u << 18)) val |= (1u << 1);   // RAND_BYTE_1
    if (reg0 & (1u <<  9)) val |= (1u << 2);   // RAND_BYTE_2
    if (reg0 & (1u << 10)) val |= (1u << 3);   // RAND_BYTE_3
    if (reg0 & (1u << 11)) val |= (1u << 4);   // RAND_BYTE_4
    if (reg0 & (1u << 12)) val |= (1u << 5);   // RAND_BYTE_5
    if (reg0 & (1u << 13)) val |= (1u << 6);   // RAND_BYTE_6
    if (reg0 & (1u << 14)) val |= (1u << 7);   // RAND_BYTE_7
    if (reg0 & (1u << 21)) val |= (1u << 8);   // RAND_BYTE_8
    if (reg1 & (1u << 15)) val |= (1u << 9);   // RAND_BYTE_9 (GPIO47 = reg1 bit15)
    return val;
}

void IRAM_ATTR on_slow_clk_rising() {
    isr_clk_edges++;

    if (!(GPIO.in & (1u << RAND_VALID))) return;

    if (write_head < RING_BUF_SIZE) {
        write_buf[write_head++] = read_rand_bits_fast();
        isr_valid_samples++;
    }

    if (write_head >= RING_BUF_SIZE) {
        if (write_buf == buf_a) {
            buf_a_full = true;
            if (!buf_b_full) {
                write_buf = buf_b;
                write_head = 0;
            } else {
                isr_overflow = true;
            }
        } else {
            buf_b_full = true;
            if (!buf_a_full) {
                write_buf = buf_a;
                write_head = 0;
            } else {
                isr_overflow = true;
            }
        }
    }
}



void flush_samples() {
    uint16_t* read_buf = nullptr;
    
    noInterrupts();
    if (buf_a_full) {
        read_buf = buf_a;
    } else if (buf_b_full) {
        read_buf = buf_b;
    }
    bool overflow = isr_overflow;
    isr_overflow = false;
    interrupts();

    if (overflow) {
        // Send a reserved error frame [0xFE][0xFF][0xFF] so Python can detect it
        uint8_t err[3] = {0xFE, 0xFF, 0xFF};
        Serial.write(err, 3);
    }

    if (read_buf != nullptr) {
        uint16_t out_idx = 0;

        for (uint16_t i = 0; i < RING_BUF_SIZE; i++) {
            uint16_t val = read_buf[i];
            out_buf[out_idx++] = FRAME_SYNC;              // 0xFF sync
            out_buf[out_idx++] = (val >> 8) & 0x03;      // bits [9:8]
            out_buf[out_idx++] = val & 0xFF;              // bits [7:0]
        }

        Serial.write(out_buf, out_idx);

        noInterrupts();
        if (read_buf == buf_a) buf_a_full = false;
        if (read_buf == buf_b) buf_b_full = false;
        
        // If ISR was stuck because both were full, unstick it
        if (write_head >= RING_BUF_SIZE) {
            write_buf = (read_buf == buf_a) ? buf_a : buf_b;
            write_head = 0;
        }
        interrupts();
    }
}


void setup() {

    Serial.begin(921600);

    pinMode(OE_N_TO_LEGEND,     OUTPUT); digitalWrite(OE_N_TO_LEGEND,     LOW);
    pinMode(OE_N_FROM_LEGEND_1, OUTPUT); digitalWrite(OE_N_FROM_LEGEND_1, LOW);
    pinMode(OE_N_FROM_LEGEND_2, OUTPUT); digitalWrite(OE_N_FROM_LEGEND_2, LOW);

    pinMode(RAND_REQ_TYPE_2, OUTPUT); digitalWrite(RAND_REQ_TYPE_2, HIGH);
    pinMode(RAND_REQ_TYPE_1, OUTPUT); digitalWrite(RAND_REQ_TYPE_1, HIGH);
    pinMode(RAND_REQ_TYPE_0, OUTPUT); digitalWrite(RAND_REQ_TYPE_0, LOW);
    pinMode(RAND_REQ,        OUTPUT); digitalWrite(RAND_REQ,        HIGH);

    pinMode(DEBUG1, OUTPUT); digitalWrite(DEBUG1, LOW);

    pinMode(LEGEND_SS_n,  OUTPUT); digitalWrite(LEGEND_SS_n,  HIGH);
    pinMode(LEGEND_MOSI,  OUTPUT); digitalWrite(LEGEND_MOSI,  LOW);

    pinMode(LEGEND_SLOW_CLK,       INPUT_PULLDOWN);
    pinMode(RAND_VALID,            INPUT_PULLDOWN);
    for (int i = 0; i < 10; i++) pinMode(RAND_PINS[i], INPUT);
    pinMode(LEGEND_OUTPUT_1,       INPUT_PULLDOWN);
    pinMode(LEGEND_OUTPUT_2,       INPUT_PULLDOWN);
    pinMode(LEGEND_MISO,           INPUT_PULLDOWN);
    pinMode(LEGEND_SPI_DATA_READY, INPUT_PULLDOWN);

    pinMode(LEGEND_nRST, OUTPUT);
    digitalWrite(LEGEND_nRST, LOW);
    delay(RESET_HOLD_MS);
    digitalWrite(LEGEND_nRST, HIGH);
    delay(RESET_SETTLE_MS);

    attachInterrupt(digitalPinToInterrupt(LEGEND_SLOW_CLK),
                    on_slow_clk_rising, RISING);

    const int resolution = 1;         
    const uint32_t freq = 100000;               
    ledcAttach(LEGEND_SCLK_IC_CLK, freq, resolution);
    ledcWrite(LEGEND_SCLK_IC_CLK, 1); // duty = 1/2 = 50%
    Serial.println("Configured as pwm");

}


uint32_t total_valid_samples = 0;
uint32_t total_clk_edges     = 0;

uint32_t throughput_window_start_ms = 0;
uint32_t throughput_window_samples  = 0;
#define THROUGHPUT_WINDOW_MS 10000

void loop() {
    // digitalWrite(DEBUG1, (GPIO.in >> RAND_VALID) & 1);

    if (buf_a_full || buf_b_full) {
        noInterrupts();
        total_clk_edges     += isr_clk_edges;
        total_valid_samples += isr_valid_samples;
        isr_clk_edges        = 0;
        isr_valid_samples    = 0;
        interrupts();
        flush_samples();
    }
    
}
