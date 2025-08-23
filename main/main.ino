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
