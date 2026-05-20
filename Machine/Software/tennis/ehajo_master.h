#ifndef EHAJO_MASTER_H
#define EHAJO_MASTER_H

#include "ehajo_protocol.h"

// Forward declaration: muss in der .ino definiert werden
static void pairingDisplay(uint32_t now);

// ====================== Master State ======================
static const uint32_t PAIR_TIMEOUT_MS = 60000;

static uint8_t g_mac[6] = {0};

static bool     g_pairing = false;
static uint32_t g_pairStart = 0;

static bool     ctrlPeerAdded = false;
static uint8_t  ctrlMac[6] = {0};
static uint16_t ctrlSession = 0;
static bool     g_ackRepeatActive = false;
static uint32_t g_ackRepeatUntil = 0;
static uint32_t g_lastAckSend = 0;
static bool     g_firstCtrlLogged = false;

// Auto-Pairing nach Boot
static uint32_t g_autoPairUntil = 0;

// Fuer pairingDisplay
static volatile bool g_ackPending = false;

// Controller-Daten (volatile, von Callback geschrieben)
volatile uint16_t g_rxP1 = 2048, g_rxP2 = 2048;
volatile uint8_t  g_rxButtons = 0;
volatile uint32_t g_lastRxMs = 0;

static void fillMagic(uint8_t magic[4]) {
  magic[0] = EH_MAGIC0;
  magic[1] = EH_MAGIC1;
  magic[2] = EH_MAGIC2;
  magic[3] = EH_MAGIC3;
}

static void acceptCtrlPacket(const CtrlPacket &pkt) {
  g_rxP1 = pkt.p1;
  g_rxP2 = pkt.p2;
  g_rxButtons = pkt.buttons;
  g_lastRxMs = millis();
}

static bool sendPairAck(const uint8_t mac[6], uint16_t session) {
  PairAck ack;
  fillMagic(ack.magic);
  ack.type = MSG_ACK;
  ack.session = session;
  ack.crc8 = crc8_simple((const uint8_t*)&ack, sizeof(ack) - 1);

  esp_err_t err = esp_now_send(mac, (const uint8_t*)&ack, sizeof(ack));
  if (err != ESP_OK) {
    Serial.printf("[ARCADE] WARN: PairAck send failed: %d\n", (int)err);
    return false;
  }
  return true;
}

static void queuePairAck(uint32_t now) {
  g_ackRepeatActive = true;
  g_ackRepeatUntil = now + 3000;
  g_lastAckSend = 0;
}

static void servicePairAck(uint32_t now) {
  if (!g_ackRepeatActive || !ctrlPeerAdded) return;

  if ((int32_t)(now - g_ackRepeatUntil) >= 0) {
    g_ackRepeatActive = false;
    return;
  }

  if (g_lastAckSend != 0 && now - g_lastAckSend < 80) return;
  g_lastAckSend = now;
  sendPairAck(ctrlMac, ctrlSession);
  sendPairAck(BCAST, ctrlSession);
}

// ====================== Pairing Funktionen ======================
static void startPairing() {
  Serial.println("[ARCADE] Pairing START (simple ESP-NOW)");

  g_pairing = true;
  g_pairStart = millis();
  g_ackPending = false;
  g_ackRepeatActive = false;
  g_ackRepeatUntil = 0;
  g_lastAckSend = 0;
  ctrlPeerAdded = false;
  g_firstCtrlLogged = false;
  memset(ctrlMac, 0, sizeof(ctrlMac));
  ctrlSession = 0;

  forcePairingChannel();
  if (!addPeer(BCAST, WIFI_CH)) {
    Serial.println("[ARCADE] WARN: add broadcast peer failed in startPairing");
  }
}

static void pairingLoop() {
  uint32_t now = millis();

  if (now - g_pairStart > PAIR_TIMEOUT_MS) {
    g_pairing = false;
    Serial.println("[ARCADE] Pairing TIMEOUT");
    return;
  }

  g_ackPending = ctrlPeerAdded;  // Indikator fuer Display
  pairingDisplay(now);
  delay(30);
}

// ====================== ESP-NOW Empfangs-Callback ======================
static void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len == (int)sizeof(PairPing)) {
    PairPing ping;
    memcpy(&ping, data, sizeof(ping));

    if (!eh_magic_ok(ping.magic)) return;
    if (ping.type != MSG_PING) return;
    if (crc8_simple((const uint8_t*)&ping, sizeof(ping) - 1) != ping.crc8) return;

    if (!g_pairing) {
      if (ctrlPeerAdded &&
          memcmp(info->src_addr, ctrlMac, 6) == 0) {
        ctrlSession = ping.session;
        queuePairAck(millis());
      }
      return;
    }

    if (!addPeer(info->src_addr, WIFI_CH)) {
      Serial.println("[ARCADE] WARN: add paired controller as peer failed");
      return;
    }

    memcpy(ctrlMac, info->src_addr, 6);
    ctrlSession = ping.session;
    ctrlPeerAdded = true;
    g_pairing = false;
    g_autoPairUntil = 0;

    Serial.printf("[PAIR] Controller %02X:%02X:%02X:%02X:%02X:%02X session=0x%04X\n",
                  ctrlMac[0], ctrlMac[1], ctrlMac[2], ctrlMac[3], ctrlMac[4], ctrlMac[5],
                  ctrlSession);

    // ACK aus dem Hauptloop wiederholen. So bleibt der Callback kurz und
    // verlorene ACKs blockieren das Pairing nicht dauerhaft.
    queuePairAck(millis());
    return;
  }

  if (len == (int)sizeof(CtrlPacket)) {
    CtrlPacket pkt;
    memcpy(&pkt, data, sizeof(pkt));
    if (crc8_simple((const uint8_t*)&pkt, sizeof(pkt) - 1) != pkt.crc8) return;

    if (!ctrlPeerAdded && g_pairing) {
      if (!addPeer(info->src_addr, WIFI_CH)) {
        Serial.println("[ARCADE] WARN: add controller from control packet failed");
        return;
      }

      memcpy(ctrlMac, info->src_addr, 6);
      ctrlSession = 0;
      ctrlPeerAdded = true;
      g_pairing = false;
      g_autoPairUntil = 0;

      Serial.printf("[PAIR] Controller %02X:%02X:%02X:%02X:%02X:%02X by control packet\n",
                    ctrlMac[0], ctrlMac[1], ctrlMac[2],
                    ctrlMac[3], ctrlMac[4], ctrlMac[5]);
    }

    if (!ctrlPeerAdded) return;
    if (memcmp(info->src_addr, ctrlMac, 6) != 0) return;

    if (!g_firstCtrlLogged) {
      g_firstCtrlLogged = true;
      Serial.println("[ARCADE] First controller data received");
    }

    acceptCtrlPacket(pkt);
  }
}

#endif
