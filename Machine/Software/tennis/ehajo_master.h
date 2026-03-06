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

// Auto-Pairing nach Boot
static uint32_t g_autoPairUntil = 0;

// Fuer pairingDisplay
static volatile bool g_ackPending = false;

// Controller-Daten (volatile, von Callback geschrieben)
volatile uint16_t g_rxP1 = 2048, g_rxP2 = 2048;
volatile uint8_t  g_rxButtons = 0;
volatile uint32_t g_lastRxMs = 0;

// ====================== Pairing Funktionen ======================
static void startPairing() {
  Serial.println("[ARCADE] Pairing START (v3 - broadcast)");

  g_pairing = true;
  g_pairStart = millis();
  g_ackPending = false;

  forceChannel1();
  addPeer(BCAST, WIFI_CH);

  ctrlPeerAdded = false;
}

static void pairingLoop() {
  uint32_t now = millis();

  if (now - g_pairStart > PAIR_TIMEOUT_MS) {
    g_pairing = false;
    Serial.println("[ARCADE] Pairing TIMEOUT");
    return;
  }

  forceChannel1();

  // Kein ACK noetig - wir warten einfach auf CtrlPackets
  g_ackPending = ctrlPeerAdded;  // Indikator fuer Display

  pairingDisplay(now);
  delay(30);
}

// ====================== ESP-NOW Empfangs-Callback ======================
static void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  // ===== Controller-Paket (10 Bytes) =====
  if (len == (int)sizeof(CtrlPacket)) {
    CtrlPacket pkt; memcpy(&pkt, data, sizeof(pkt));
    if (crc8_simple((const uint8_t*)&pkt, sizeof(pkt) - 1) == pkt.crc8) {

      // Erstes gueltiges Paket -> paired!
      if (!ctrlPeerAdded) {
        addPeer(info->src_addr, 0);
        ctrlPeerAdded = true;
        g_pairing = false;
        Serial.printf("[PAIR] CtrlPacket from %02X:%02X:%02X:%02X:%02X:%02X -> paired!\n",
                      info->src_addr[0],info->src_addr[1],info->src_addr[2],
                      info->src_addr[3],info->src_addr[4],info->src_addr[5]);
      }

      g_rxP1 = pkt.p1;
      g_rxP2 = pkt.p2;
      g_rxButtons = pkt.buttons;
      g_lastRxMs = millis();
    }
  }
}

#endif
