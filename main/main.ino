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

#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <AES.h>
#include <CTR.h>

// LoRa configuration
#define RF_FREQUENCY 915000000
#define TX_OUTPUT_POWER 5
#define LORA_BANDWIDTH 0
#define LORA_SPREADING_FACTOR 7
#define LORA_CODINGRATE 1
#define LORA_PREAMBLE_LENGTH 8
#define LORA_FIX_LENGTH_PAYLOAD_ON true
#define LORA_IQ_INVERSION_ON false

// Joystick pins
#define LHZ_PIN 3
#define LVT_PIN 2
#define RHZ_PIN 20
#define RVT_PIN 19

// Packet buffers
uint8_t txPacket[6];         // Plaintext: 4 ADC + sequence + CRC
uint8_t encryptedPacket[6];  // Ciphertext

static RadioEvents_t RadioEvents;
bool lora_idle = true;
uint8_t sequenceNumber = 0;

// AES-128 CTR object (Crypto library)
byte aesKey[16] = { /* your 128-bit key */ };
CTR<AES128> ctr;

// CRC8 calculation
uint8_t calculateCRC8(uint8_t *data, uint8_t length) {
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x07;
            else crc <<= 1;
        }
    }
    return crc;
}

// LoRa callbacks
void OnTxDone(void) { lora_idle = true; }
void OnTxTimeout(void) { Radio.Sleep(); lora_idle = true; }

void setup() {
    Serial.begin(115200);
    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

    // Set ADC to 8-bit resolution (0-255)
    analogReadResolution(8);

    // LoRa radio setup
    RadioEvents.TxDone = OnTxDone;
    RadioEvents.TxTimeout = OnTxTimeout;
    Radio.Init(&RadioEvents);
    Radio.SetChannel(RF_FREQUENCY);
    Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                      LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                      LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                      true, 0, 0, LORA_IQ_INVERSION_ON, 3000);

    // Initialize AES CTR key
    ctr.setKey(aesKey, sizeof(aesKey));
}

void loop() {
    if (!lora_idle) {
        Radio.IrqProcess();
        return;
    }

    // Read joystick analogs (0-255)
    txPacket[0] = analogRead(LHZ_PIN);
    txPacket[1] = analogRead(LVT_PIN);
    txPacket[2] = analogRead(RHZ_PIN);
    txPacket[3] = analogRead(RVT_PIN);

    txPacket[4] = sequenceNumber++;             // Sequence number
    txPacket[5] = calculateCRC8(txPacket, 5);  // CRC8

    // Set IV/nonce for CTR mode (use sequence number)
    byte iv[16] = {0};
    iv[0] = txPacket[4];

    // Encrypt 6-byte packet using Crypto library
    ctr.setIV(iv, sizeof(iv));
    ctr.encrypt(txPacket, encryptedPacket, sizeof(txPacket));

    // Debug output
    Serial.print("Sending encrypted packet: ");
    for (int i = 0; i < 6; i++) Serial.printf("%02X ", encryptedPacket[i]);
    Serial.println();

    // Send packet over LoRa
    Radio.Send(encryptedPacket, 6);
    lora_idle = false;

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