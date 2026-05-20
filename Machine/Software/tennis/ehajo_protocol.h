#ifndef EHAJO_PROTOCOL_H
#define EHAJO_PROTOCOL_H

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_mac.h>

// ====================== eHaJo ESP-NOW PROTOKOLL ======================
// Pairing ist bewusst simpel:
// Controller sendet PairPing als Broadcast, Machine antwortet mit PairAck.
// Die Session-ID verhindert alte/zufaellige ACKs. Bei mehreren Geraeten im Raum
// darf immer nur ein Set gleichzeitig im Pairing-Modus sein.

#define EH_MAGIC0 'E'
#define EH_MAGIC1 'H'
#define EH_MAGIC2 '2'
#define EH_MAGIC3 '\0'

enum MsgType : uint8_t {
  MSG_PING = 1,
  MSG_ACK  = 2
};

struct __attribute__((packed)) PairPing {
  uint8_t  magic[4];
  uint8_t  type;
  uint16_t session;
  uint8_t  crc8;
};

struct __attribute__((packed)) PairAck {
  uint8_t  magic[4];
  uint8_t  type;
  uint16_t session;
  uint8_t  crc8;
};

struct __attribute__((packed)) CtrlPacket {
  uint32_t seq;
  uint16_t p1, p2;
  uint8_t  buttons;
  uint8_t  crc8;
};

static const uint8_t WIFI_CH = 6;
static uint8_t BCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

static inline bool eh_magic_ok(const uint8_t* m) {
  return (m[0] == EH_MAGIC0 && m[1] == EH_MAGIC1 && m[2] == EH_MAGIC2 && m[3] == EH_MAGIC3);
}

static inline uint8_t crc8_simple(const uint8_t* d, size_t n) {
  uint8_t c = 0;
  for (size_t i = 0; i < n; i++) c ^= d[i];
  return c;
}

static inline void wifiTweak() {
  esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
  if (err != ESP_OK) {
    Serial.printf("[EH] esp_wifi_set_ps failed: %d\n", (int)err);
  }

  err = esp_wifi_set_max_tx_power(78); // 19.5 dBm, Maximum beim ESP32-C3
  if (err != ESP_OK) {
    Serial.printf("[EH] esp_wifi_set_max_tx_power failed: %d\n", (int)err);
  }

  err = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
  if (err != ESP_OK) {
    Serial.printf("[EH] esp_wifi_set_bandwidth failed: %d\n", (int)err);
  }

  // Langsame ESP-NOW-Rate fuer mehr Reichweitenreserve bei kleinen Paketen.
  err = esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_1M_L);
  if (err != ESP_OK) {
    Serial.printf("[EH] esp_wifi_config_espnow_rate failed: %d\n", (int)err);
  }
}

static inline bool forcePairingChannel() {
  esp_wifi_set_promiscuous(true);
  esp_err_t err = esp_wifi_set_channel(WIFI_CH, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  if (err != ESP_OK) {
    Serial.printf("[EH] esp_wifi_set_channel(%u) failed: %d\n", WIFI_CH, (int)err);
    return false;
  }
  uint8_t primary = 0;
  wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&primary, &secondary);
  Serial.printf("[EH] channel=%u\n", primary);
  return true;
}

static inline bool addPeer(const uint8_t mac[6], uint8_t channel) {
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6);
  p.encrypt = false;
  p.channel = channel;
  p.ifidx = WIFI_IF_STA;

  esp_now_del_peer(mac);
  esp_err_t err = esp_now_add_peer(&p);
  if (err != ESP_OK) {
    Serial.printf("[EH] esp_now_add_peer(%02X:%02X:%02X:%02X:%02X:%02X ch=%u) failed: %d\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], channel, (int)err);
    return false;
  }
  return true;
}

#endif
