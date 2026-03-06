#include <Arduino.h>
#include <string.h>
#include "ehajo_protocol.h"

// ========= PINS =========
#define PIN_POT1   0
#define PIN_POT2   1
#define PIN_BTN    20
#define PIN_LED    21

static uint8_t selfMac[6] = {0};

static inline bool isBtnDown() {
  return digitalRead(PIN_BTN) == LOW;
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\n=== eHaJo Controller v3 - Broadcast (ESP32-C3) ===");

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  pinMode(PIN_BTN, INPUT_PULLUP);
  analogReadResolution(12);

  // STA-Mode (einfachste Konfig)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);
  delay(500);

  wifiTweak();
  esp_wifi_get_mac(WIFI_IF_STA, selfMac);
  Serial.printf("[CTRL] MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
    selfMac[0], selfMac[1], selfMac[2], selfMac[3], selfMac[4], selfMac[5]);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }

  forceChannel1();
  addPeer(BCAST, WIFI_CH);

  Serial.println("[CTRL] Ready - sending CtrlPackets as broadcast");
}

void loop() {
  static uint32_t lastSend = 0;
  uint32_t now = millis();
  if (now - lastSend < 10) return;  // ~100Hz
  lastSend = now;

  CtrlPacket pkt;
  pkt.seq = now;
  pkt.p1  = (uint16_t)analogRead(PIN_POT1);
  pkt.p2  = (uint16_t)analogRead(PIN_POT2);
  pkt.buttons = isBtnDown() ? 0x01 : 0x00;
  pkt.crc8 = crc8_simple((const uint8_t*)&pkt, sizeof(pkt) - 1);

  esp_now_send(BCAST, (const uint8_t*)&pkt, sizeof(pkt));
}
