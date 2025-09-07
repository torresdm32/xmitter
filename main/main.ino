/*///////////////////////////////////////////////////////////// 
/                                                             /
/               ESP32 Based LoRa TRANSMITTER                  /
/                                                             /
/                                                             /
/                                                             /
/                                                             /
/                                                             /
/                                                             /
/                                                             /
/                                                             /
/                                                             /
*//////////////////////////////////////////////////////////////

#include <Arduino.h>
#include <LoRaWan_APP.h>
#include <ChaCha32.h>

// Pin definitions for joysticks
#define LHZ_PIN 3
#define LVT_PIN 2
#define RHZ_PIN 20
#define RVT_PIN 19

// LoRa configuration
#define RF_FREQUENCY 915000000
#define TX_OUTPUT_POWER 5
#define LORA_BANDWIDTH 0
#define LORA_SPREADING_FACTOR 7
#define LORA_CODINGRATE 1
#define LORA_PREAMBLE_LENGTH 8
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false
#define BUFFER_SIZE 6   // seq(1) + joystick(4) + CRC(1)

static RadioEvents_t RadioEvents;
bool lora_idle = true;

// ChaCha32 key & nonce
uint8_t key[32] = {
  0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
  0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
  0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
  0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F
};
uint8_t nonce[12] = {
  0x00,0x00,0x00,0x01,
  0x00,0x00,0x00,0x02,
  0x00,0x00,0x00,0x03
};

uint8_t txPayload[BUFFER_SIZE];  // sequence + joystick + CRC
uint8_t plaintext[4];            // joystick bytes
uint8_t ciphertext[BUFFER_SIZE];

uint8_t seq = 0;

// --------------------
// CRC helper
// --------------------
uint8_t crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x07;
            else crc <<= 1;
        }
    }
    return crc;
}

// --------------------
// Callbacks
// --------------------
void OnTxDone(void) { lora_idle = true; }
void OnTxTimeout(void) { Radio.Sleep(); lora_idle = true; }

// --------------------
// Setup
// --------------------
void setup() {
    Serial.begin(115200);
    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

    analogReadResolution(8); // 8-bit ADC

    RadioEvents.TxDone = OnTxDone;
    RadioEvents.TxTimeout = OnTxTimeout;

    Radio.Init(&RadioEvents);
    Radio.SetChannel(RF_FREQUENCY);
    Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                      LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                      LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                      true, 0, 0, LORA_IQ_INVERSION_ON, 3000);

    Serial.println("LoRa Transmitter w/ ChaCha32 + CRC & SeqID");
}

// --------------------
// Loop
// --------------------
void loop() {
    if (lora_idle) {
        lora_idle = false;

        // Read joystick values
        plaintext[0] = (uint8_t)analogRead(LHZ_PIN);
        plaintext[1] = (uint8_t)analogRead(LVT_PIN);
        plaintext[2] = (uint8_t)analogRead(RHZ_PIN);
        plaintext[3] = (uint8_t)analogRead(RVT_PIN);

        // Build payload: seq + joystick + crc
        txPayload[0] = seq;
        memcpy(&txPayload[1], plaintext, 4);
        txPayload[5] = crc8(plaintext, 4);

        // Encrypt
        chacha32_encrypt(key, nonce, txPayload, ciphertext, BUFFER_SIZE);

        // Send
        Radio.Send(ciphertext, BUFFER_SIZE);

        // Debug print
        Serial.print("Seq: "); Serial.print(seq);
        Serial.print(" | Plain: ");
        for (int i = 0; i < 4; i++) Serial.print(plaintext[i]), Serial.print(" ");
        Serial.print("| Cipher: ");
        for (int i = 0; i < BUFFER_SIZE; i++) Serial.print(ciphertext[i], HEX), Serial.print(" ");
        Serial.print("| CRC "); Serial.println((txPayload[5] == crc8(plaintext,4)) ? "OK" : "FAIL");

        // Increment sequence
        seq++;

        delay(10);  // small delay to avoid overloading radio
    }

    Radio.IrqProcess();
}




/*
#include "LoRaWan_APP.h"
#include "Arduino.h"

// Default configuration settings from ESP-32 LoRa example sketch
#define RF_FREQUENCY 915000000  // Hz
#define TX_OUTPUT_POWER 5  // dBm
#define LORA_BANDWIDTH 0         // [0: 125 kHz, 250 kHz, 500 kHz, 3: Reserved]
#define LORA_SPREADING_FACTOR 7  // [SF7..SF12]
#define LORA_CODINGRATE 1        // [1: 4/5, 2: 4/6,3: 4/7, 4: 4/8]
#define LORA_PREAMBLE_LENGTH 8   // Same for Tx and Rx
#define LORA_SYMBOL_TIMEOUT 0    // Symbols
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false
#define RX_TIMEOUT_VALUE 1000
#define BUFFER_SIZE 30  // Define the payload size here

// Defining pins for horizontal and vertical directions of left and right joysticks
#define LHZ_PIN 3
#define LVT_PIN 2
#define RHZ_PIN 20
#define RVT_PIN 19

char txpacket[BUFFER_SIZE];
static RadioEvents_t RadioEvents;
bool lora_idle = true;

// Initializing variables to carry the values of the analog reads of each joystick movement
int LHZ, LVT, RHZ, RVT;

void OnTxDone(void);
void OnTxTimeout(void);

void setup() {
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                    LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                    true, 0, 0, LORA_IQ_INVERSION_ON, 3000);
}

void loop() {
  if (lora_idle == true) {
    delay(15);

    // Capture the values of the analog reads of each joystick movement as ints
    LHZ = analogRead(LHZ_PIN);
    LVT = analogRead(LVT_PIN);
    RHZ = analogRead(RHZ_PIN);
    RVT = analogRead(RVT_PIN);

    // Assembling a packet of comma separated values
    sprintf(txpacket, "%d,%d,%d,%d", LHZ, LVT, RHZ, RVT);

    Serial.printf("Sending packet: %s\n", txpacket);

    Radio.Send((uint8_t *)txpacket, strlen(txpacket));  //send the package out
    lora_idle = false;
  }
  Radio.IrqProcess();
}

void OnTxDone(void) {
  Serial.println("TX done......");
  lora_idle = true;
}

void OnTxTimeout(void) {
  Radio.Sleep();
  Serial.println("TX Timeout......");
  lora_idle = true;
}
*/