#ifndef EHAJO_PROTOCOL_H
#define EHAJO_PROTOCOL_H

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_mac.h>

// ====================== eHaJo ESPNOW PROTOKOLL (v2) ======================
// 2-Wege-Pairing: PairPing (Controller->Machine) + PairAck (Machine->Controller)
// Implizite Bestaetigung durch erstes gueltiges CtrlPacket.

#define EH_MAGIC0 'E'
#define EH_MAGIC1 'H'
#define EH_MAGIC2 '2'
#define EH_MAGIC3 '\0'

enum MsgType : uint8_t {
  MSG_PING = 1,
  MSG_ACK  = 2
};

// PairPing: Controller sendet broadcast waehrend Pairing (8 Bytes)
struct __attribute__((packed)) PairPing {
  uint8_t  magic[4];   // "EH2\0"
  uint8_t  type;       // MSG_PING
  uint16_t session;    // zufaellig pro Pairing-Versuch
  uint8_t  crc8;
};

// PairAck: Machine antwortet broadcast (8 Bytes)
struct __attribute__((packed)) PairAck {
  uint8_t  magic[4];   // "EH2\0"
  uint8_t  type;       // MSG_ACK
  uint16_t session;    // Echo der Session-ID
  uint8_t  crc8;
};

// CtrlPacket: Controller -> Machine (10 Bytes, unveraendert)
struct __attribute__((packed)) CtrlPacket {
  uint32_t seq;
  uint16_t p1, p2;
  uint8_t  buttons;
  uint8_t  crc8;
};

// ====================== Konstanten ======================
static const uint8_t WIFI_CH = 1;
static uint8_t BCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// ====================== Hilfsfunktionen ======================
static inline uint8_t crc8_xor(const uint8_t* d, int n) {
  uint8_t c = 0;
  for (int i=0;i<n;i++) c ^= d[i];
  return c;
}

static inline bool eh_magic_ok(const uint8_t* m) {
  return (m[0]==EH_MAGIC0 && m[1]==EH_MAGIC1 && m[2]==EH_MAGIC2 && m[3]==EH_MAGIC3);
}

static inline uint8_t crc8_simple(const uint8_t* d, size_t n) {
  uint8_t c = 0;
  for (size_t i=0;i<n;i++) c ^= d[i];
  return c;
}

static inline void wifiTweak() {
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_max_tx_power(78);
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
}

static inline void forceChannel1() {
  esp_wifi_set_channel(WIFI_CH, WIFI_SECOND_CHAN_NONE);
}

static inline void addPeer(const uint8_t mac[6], int channel) {
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6);
  p.encrypt = false;
  p.channel = channel;
  p.ifidx = WIFI_IF_STA;
  esp_now_del_peer(mac);
  esp_now_add_peer(&p);
}

#endif
