#include <Arduino.h>
#include <string.h>
#include "ehajo_protocol.h"

// ========= PINS =========
#define PIN_POT1   0
#define PIN_POT2   1
#define PIN_BTN    20
#define PIN_LED    21

static uint8_t selfMac[6] = {0};
static uint8_t machineMac[6] = {0};
static bool paired = false;
static uint16_t pairSession = 0;

static volatile esp_now_send_status_t lastSendStatus = ESP_NOW_SEND_FAIL;
static volatile bool sendStatusSeen = false;

static inline bool isBtnDown() {
  return digitalRead(PIN_BTN) == LOW;
}

static void fillMagic(uint8_t magic[4]) {
  magic[0] = EH_MAGIC0;
  magic[1] = EH_MAGIC1;
  magic[2] = EH_MAGIC2;
  magic[3] = EH_MAGIC3;
}

static void sendPairPing(uint32_t now) {
  static uint32_t lastPairPingMs = 0;
  if (now - lastPairPingMs < 250) return;
  lastPairPingMs = now;

  PairPing ping;
  fillMagic(ping.magic);
  ping.type = MSG_PING;
  ping.session = pairSession;
  ping.crc8 = crc8_simple((const uint8_t*)&ping, sizeof(ping) - 1);

  esp_err_t err = esp_now_send(BCAST, (const uint8_t*)&ping, sizeof(ping));
  if (err != ESP_OK) {
    static uint32_t lastErrPrint = 0;
    if (now - lastErrPrint > 1000) {
      lastErrPrint = now;
      Serial.printf("[CTRL] PairPing send failed: %d\n", (int)err);
    }
  }
}

static void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len != (int)sizeof(PairAck)) return;

  PairAck ack;
  memcpy(&ack, data, sizeof(ack));

  if (!eh_magic_ok(ack.magic)) return;
  if (ack.type != MSG_ACK) return;
  if (ack.session != pairSession) return;
  if (crc8_simple((const uint8_t*)&ack, sizeof(ack) - 1) != ack.crc8) return;

  memcpy(machineMac, info->src_addr, 6);
  if (!addPeer(machineMac, WIFI_CH)) {
    Serial.println("[CTRL] WARN: add paired machine failed");
  }

  if (!paired) {
    paired = true;
    digitalWrite(PIN_LED, HIGH);
    Serial.printf("[CTRL] Paired with Machine %02X:%02X:%02X:%02X:%02X:%02X\n",
                  machineMac[0], machineMac[1], machineMac[2],
                  machineMac[3], machineMac[4], machineMac[5]);
  }
}

static void onDataSent(const wifi_tx_info_t* info, esp_now_send_status_t status) {
  lastSendStatus = status;
  sendStatusSeen = true;
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\n=== eHaJo Controller - Simple Pairing (ESP32-C3) ===");

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  pinMode(PIN_BTN, INPUT_PULLUP);
  analogReadResolution(12);

  pairSession = (uint16_t)esp_random();
  if (pairSession == 0) pairSession = 1;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  WiFi.setSleep(false);
  delay(200);

  wifiTweak();
  forcePairingChannel();

  esp_wifi_get_mac(WIFI_IF_STA, selfMac);
  Serial.printf("[CTRL] MAC %02X:%02X:%02X:%02X:%02X:%02X channel=%u\n",
                selfMac[0], selfMac[1], selfMac[2],
                selfMac[3], selfMac[4], selfMac[5], WIFI_CH);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[CTRL] ESP-NOW init failed");
    return;
  }

  forcePairingChannel();
  if (!addPeer(BCAST, WIFI_CH)) {
    Serial.println("[CTRL] WARN: add broadcast peer failed");
  }

  esp_now_register_recv_cb(onDataRecv);
  esp_now_register_send_cb(onDataSent);
  Serial.printf("[CTRL] Pairing session 0x%04X\n", pairSession);
}

void loop() {
  static uint32_t lastSend = 0;
  uint32_t now = millis();

  if (!paired) {
    sendPairPing(now);
    static uint32_t lastStatusPrint = 0;
    if (sendStatusSeen && now - lastStatusPrint > 1000) {
      lastStatusPrint = now;
      Serial.printf("[CTRL] pair send status: %s\n",
                    lastSendStatus == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
    }
  }

  if (now - lastSend < 10) return;  // ~100 Hz
  lastSend = now;

  CtrlPacket pkt;
  pkt.seq = now;
  pkt.p1 = (uint16_t)analogRead(PIN_POT1);
  pkt.p2 = (uint16_t)analogRead(PIN_POT2);
  pkt.buttons = isBtnDown() ? 0x01 : 0x00;
  pkt.crc8 = crc8_simple((const uint8_t*)&pkt, sizeof(pkt) - 1);

  const uint8_t *targetMac = paired ? machineMac : BCAST;
  esp_err_t err = esp_now_send(targetMac, (const uint8_t*)&pkt, sizeof(pkt));
  if (err != ESP_OK) {
    static uint32_t lastErrPrint = 0;
    if (now - lastErrPrint > 1000) {
      lastErrPrint = now;
      Serial.printf("[CTRL] CtrlPacket send failed: %d\n", (int)err);
    }
  }
}
