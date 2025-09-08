/*///////////////////////////////////////////////////////////// 
/                                                             /
/               ESP32 Based LoRa TRANSMITTER                  /
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

// Pin definitions for joysticks (8-bit ADC)
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
#define BUFFER_SIZE 6   // seq(1) + 4 mapped bytes + crc(1)

static RadioEvents_t RadioEvents;
bool lora_idle = true;

// ChaCha32 key & nonce
uint8_t key[32] = {
  0x9A,0x4D,0xE1,0x2F,0xA3,0x7C,0x58,0xB1,
  0x63,0x0E,0xD9,0xF7,0x41,0x22,0x8B,0x6F,
  0x3C,0x91,0xA4,0x5D,0x0B,0x2A,0xEE,0x70,
  0x87,0xC5,0x14,0x39,0xDA,0x01,0x6B,0x4F
};
uint8_t nonce[12] = {
  0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,0x10,0x32,0x54,0x76
};
// -----------------------------------------------------------------------------------------------

// Sequence counter
uint8_t seq_counter = 0;

// Temporary buffers
uint8_t txPlain[4];                 // mapped joystick magnitudes (0..255)
uint8_t txPayload[BUFFER_SIZE];     // seq + txPlain + crc
uint8_t ciphertext[BUFFER_SIZE];

// ---------------- CRC-8 (CRC-8-CCITT polynomial 0x07) ----------------
uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x80) crc = (crc << 1) ^ 0x07;
      else crc <<= 1;
    }
  }
  return crc;
}
// -------------------------------------------------------------------

// ---------------- Mapping functions ----------------

// Linear mapping with dead zone; returns magnitude 0..255
uint8_t map_linear_deadzone(uint8_t adc, uint8_t center, uint8_t dead_low, uint8_t dead_high, uint8_t min_adc = 0, uint8_t max_adc = 255) {
  if (adc >= dead_low && adc <= dead_high) return 0;

  if (adc < dead_low) {
    // map [min_adc .. dead_low-1] -> [255 .. 1] (magnitude)
    int in_min = int(min_adc);
    int in_max = int(dead_low - 1);
    if (in_max <= in_min) return 255;
    int v = map(adc, in_min, in_max, 255, 1);
    if (v < 0) v = 0; if (v > 255) v = 255;
    return (uint8_t)v;
  } else {
    // map [dead_high+1 .. max_adc] -> [1 .. 255]
    int in_min = int(dead_high + 1);
    int in_max = int(max_adc);
    if (in_max <= in_min) return 255;
    int v = map(adc, in_min, in_max, 1, 255);
    if (v < 0) v = 0; if (v > 255) v = 255;
    return (uint8_t)v;
  }
}

// Steering cubic mapping (sporty near center, aggressive near edges)
// center, dead zone, min and max ADC values as parameters
uint8_t map_steer_cubic(uint8_t adc, uint8_t center, uint8_t dead_low, uint8_t dead_high, uint8_t min_adc = 8, uint8_t max_adc = 255) {
  if (adc >= dead_low && adc <= dead_high) return 0;

  float norm;
  if (adc < center) {
    // normalize from center..min_adc to 0..1
    float denom = float(center - min_adc);
    if (denom <= 0.0f) return 255;
    norm = float(center - adc) / denom;  // 0..1
  } else {
    float denom = float(max_adc - center);
    if (denom <= 0.0f) return 255;
    norm = float(adc - center) / denom;  // 0..1
  }
  if (norm < 0.0f) norm = 0.0f;
  if (norm > 1.0f) norm = 1.0f;

  // cubic exponent p = 3
  float scaled = powf(norm, 3.0f) * 255.0f;
  if (scaled < 0.0f) scaled = 0.0f;
  if (scaled > 255.0f) scaled = 255.0f;
  return (uint8_t)(scaled + 0.5f);
}
// --------------------------------------------------

// ---------------- LoRa callbacks ----------------
void OnTxDone(void) { lora_idle = true; }
void OnTxTimeout(void) { Radio.Sleep(); lora_idle = true; }
// ------------------------------------------------

void setup() {
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  analogReadResolution(8); // 8-bit ADC as requested

  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                    LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                    true, 0, 0, LORA_IQ_INVERSION_ON, 3000);

  Serial.println("LoRa Transmitter w/ ChaCha32 + PWM mapping (cubic steer)");
}

// ---------------- main loop ----------------
void loop() {
  if (!lora_idle) {
    Radio.IrqProcess();
    delay(5);
    return;
  }

  lora_idle = false;

  // Read raw joystick ADCs (8-bit)
  uint8_t raw_LHZ = (uint8_t)analogRead(LHZ_PIN);
  uint8_t raw_LVT = (uint8_t)analogRead(LVT_PIN);
  uint8_t raw_RHZ = (uint8_t)analogRead(RHZ_PIN);
  uint8_t raw_RVT = (uint8_t)analogRead(RVT_PIN);

  // Map each axis to PWM magnitude (0..255)
  // LVT => drive (linear), center ~121, dead 116-121
  txPlain[1-1] = map_linear_deadzone(raw_LHZ, 123, 120, 125, 0, 255); // LHZ mapped (aux steer-like)
  txPlain[2-1] = map_linear_deadzone(raw_LVT, 121, 116, 121, 0, 255); // LVT -> drive
  txPlain[3-1] = map_steer_cubic(raw_RHZ, 125, 123, 128, 8, 255);     // RHZ -> steering (cubic)
  txPlain[4-1] = map_linear_deadzone(raw_RVT, 118, 115, 120, 0, 255); // RVT mapped (aux drive-like)

  // Build payload: seq, 4 mapped bytes, crc
  txPayload[0] = seq_counter;
  memcpy(&txPayload[1], txPlain, 4);
  uint8_t crc = crc8(txPlain, 4);
  txPayload[5] = crc;

  // Encrypt the whole payload (seq + mapped + crc)
  chacha32_encrypt(key, nonce, txPayload, ciphertext, BUFFER_SIZE);

  // Send encrypted packet
  Radio.Send(ciphertext, BUFFER_SIZE);

  // Verbose debug printing
  Serial.print("Seq: "); Serial.print(seq_counter);
  Serial.print(" | Plain: ");
  for (int i = 0; i < 4; ++i) {
    Serial.print(txPlain[i], DEC);
    Serial.print(i < 3 ? " " : " ");
  }
  Serial.print(" | Cipher: ");
  for (int i = 0; i < BUFFER_SIZE; ++i) {
    if (ciphertext[i] < 0x10) Serial.print('0');
    Serial.print(ciphertext[i], HEX);
    Serial.print(i < BUFFER_SIZE-1 ? " " : " ");
  }
  Serial.print("| CRC: ");
  Serial.println(crc);
  //Serial.print((crc == crc8(txPlain,4)) ? "OK" : "FAIL");
  //Serial.println();

  seq_counter++; // next packet

  Radio.IrqProcess();
  delay(10); // small pacing delay
}
