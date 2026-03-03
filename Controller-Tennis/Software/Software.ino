#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <string.h>

// ========= PINS (dein Controller) =========
#define PIN_POT1   0
#define PIN_POT2   1
#define PIN_BTN    20
#define PIN_LED    21

static const uint8_t WIFI_CH = 1;

static void wifiTweak() {
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_max_tx_power(78);
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
}

static const uint32_t PAIR_TIMEOUT_MS   = 30000;

// ========= eHaJo ESPNOW PROTOKOLL (v1) =========
#define EH_MAGIC0 'E'
#define EH_MAGIC1 'H'
#define EH_MAGIC2 'J'
#define EH_MAGIC3 '1'
#define EH_VER    1

enum EhMsgType : uint8_t {
  EH_HELLO   = 1,
  EH_OFFER   = 2,
  EH_CONFIRM = 3
};

struct __attribute__((packed)) EhHello {
  uint8_t magic[4];
  uint8_t ver;
  uint8_t type;
  uint16_t nonce;
  uint8_t crc8;
};

struct __attribute__((packed)) EhOffer {
  uint8_t magic[4];
  uint8_t ver;
  uint8_t type;
  uint16_t nonce;
  uint8_t ctrlMac[6];
  uint8_t crc8;
};

struct __attribute__((packed)) EhConfirm {
  uint8_t magic[4];
  uint8_t ver;
  uint8_t type;
  uint16_t nonce;
  uint8_t crc8;
};

static uint8_t crc8_xor(const uint8_t* d, int n) {
  uint8_t c = 0;
  for (int i=0;i<n;i++) c ^= d[i];
  return c;
}

static bool eh_magic_ok(const uint8_t* m) {
  return (m[0]==EH_MAGIC0 && m[1]==EH_MAGIC1 && m[2]==EH_MAGIC2 && m[3]==EH_MAGIC3);
}

// ========= Storage =========
Preferences prefs;
static uint8_t BCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

static uint8_t masterMac[6] = {0};
static uint8_t selfMac[6] = {0};
static bool hasMaster = false;

// ========= Controller Packet =========
struct CtrlPacket {
  uint32_t seq;
  uint16_t p1, p2;
  uint8_t  buttons;
  uint8_t  crc8;
};

static uint8_t crc8_simple(const uint8_t* d, int n) {
  uint8_t c = 0;
  for (int i=0;i<n;i++) c ^= d[i];
  return c;
}

static void printMac(const char* tag, const uint8_t mac[6]) {
  Serial.print(tag);
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static bool loadMaster(uint8_t out[6]) {
  prefs.begin("ehajo_arcade", true);
  size_t n = prefs.getBytesLength("master");
  if (n != 6) { prefs.end(); return false; }
  prefs.getBytes("master", out, 6);
  prefs.end();
  for (int i=0;i<6;i++) if (out[i] != 0x00) return true;
  return false;
}

static void saveMaster(const uint8_t mac[6]) {
  prefs.begin("ehajo_arcade", false);
  prefs.putBytes("master", mac, 6);
  prefs.end();
}

static void clearMaster() {
  prefs.begin("ehajo_arcade", false);
  prefs.remove("master");
  prefs.end();
  memset(masterMac, 0, 6);
  hasMaster = false;
}

static void addPeer(const uint8_t mac[6], int channel) {
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6);
  p.encrypt = false;
  p.channel = channel;
  p.ifidx = WIFI_IF_STA;
  esp_now_del_peer(mac);
  esp_err_t r = esp_now_add_peer(&p);
  Serial.printf("[CTRL] addPeer ch=%d result=%d\n", channel, (int)r);
}

static void forceChannel1() {
  esp_wifi_set_channel(WIFI_CH, WIFI_SECOND_CHAN_NONE);
}

static inline bool isBtnDown() {
  return digitalRead(PIN_BTN) == LOW;
}

// ========= Pairing State =========
static bool pairing = false;
static uint16_t g_pairNonce = 0;
static volatile bool gotBeacon = false;
static uint8_t beaconFromMac[6] = {0};

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  // Sofortige Debug-Ausgabe um zu sehen ob ÜBERHAUPT was ankommt
  Serial.print(".");

  if (!pairing || len < (int)sizeof(EhOffer)) return;

  EhOffer o;
  memcpy(&o, data, sizeof(o));

  if (!eh_magic_ok(o.magic) || o.ver != EH_VER || o.type != EH_OFFER) return;



  // CRC Check
  uint8_t c = crc8_xor((const uint8_t*)&o, sizeof(o) - 1);
  if (c != o.crc8) return;

  // Nonce Check
  if (o.nonce != g_pairNonce) return;

  memcpy(beaconFromMac, info->src_addr, 6);
  gotBeacon = true;
}

static void runPairing(const char* reason) {
  Serial.print("[CTRL] Pairing start: ");
  Serial.println(reason);

  pairing = true;
  gotBeacon = false;
  g_pairNonce = (uint16_t)random(1, 65535);

  digitalWrite(PIN_LED, HIGH);

  uint32_t t0 = millis();
  uint32_t lastLog = 0;

  while (!gotBeacon) {
    uint32_t now = millis();

    // HELLO senden (Broadcast) ~10 Hz
    static uint32_t lastHello = 0;
    if (now - lastHello > 100) {
      lastHello = now;
      EhHello h = {};
      h.magic[0]=EH_MAGIC0; h.magic[1]=EH_MAGIC1; h.magic[2]=EH_MAGIC2; h.magic[3]=EH_MAGIC3;
      h.ver = EH_VER;
      h.type = EH_HELLO;
      h.nonce = g_pairNonce;
      h.crc8 = crc8_xor((const uint8_t*)&h, sizeof(h) - 1);
      esp_err_t sr = esp_now_send(BCAST, (const uint8_t*)&h, sizeof(h));
      if (sr != ESP_OK) Serial.printf("[CTRL] HELLO send FAIL: %d\n", (int)sr);
    }

    if (now - lastLog > 2000) {
      lastLog = now;
      Serial.println("[CTRL] still waiting...");
      // Doppelblinken als alive
      digitalWrite(PIN_LED, LOW);  delay(70);
      digitalWrite(PIN_LED, HIGH); delay(70);
      digitalWrite(PIN_LED, LOW);  delay(70);
      digitalWrite(PIN_LED, HIGH);
    }

    if (now - t0 > PAIR_TIMEOUT_MS) {
      Serial.println("[CTRL] Pairing TIMEOUT");
      pairing = false;
      digitalWrite(PIN_LED, LOW);
      return;
    }
    yield();
    delay(10);
  }

  memcpy(masterMac, (const void*)beaconFromMac, 6);
  hasMaster = true;
  saveMaster(masterMac);

  addPeer(masterMac, 1);

  EhConfirm cfm = {};
  cfm.magic[0]=EH_MAGIC0; cfm.magic[1]=EH_MAGIC1; cfm.magic[2]=EH_MAGIC2; cfm.magic[3]=EH_MAGIC3;
  cfm.ver = EH_VER;
  cfm.type = EH_CONFIRM;
  cfm.nonce = g_pairNonce;
  cfm.crc8 = crc8_xor((const uint8_t*)&cfm, sizeof(cfm) - 1);

  esp_err_t r = esp_now_send(masterMac, (const uint8_t*)&cfm, sizeof(cfm));

  printMac("[CTRL] PAIRED with ", masterMac);
  Serial.print("[CTRL] ACK send result: ");
  Serial.println((int)r);

  delay(300);

  pairing = false;
  digitalWrite(PIN_LED, LOW);
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\n=== eHaJo Controller (ESP32-C3 SuperMini) ===");
  randomSeed((uint32_t)micros());

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  pinMode(PIN_BTN, INPUT_PULLUP);
  analogReadResolution(12);

  // STA-Mode ohne AutoConnect
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  WiFi.setAutoReconnect(false);
  wifiTweak();
  esp_wifi_get_mac(WIFI_IF_STA, selfMac);

  forceChannel1();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }

  esp_now_register_recv_cb(onRecv);

  addPeer(BCAST, 1);

  hasMaster = loadMaster(masterMac);

  // NUR beim Einstecken (boot-hold) oder wenn kein Master gespeichert
  bool bootHold = isBtnDown();
  if (bootHold) {
    clearMaster();
    runPairing("boot-hold button");
  } else if (!hasMaster) {
    runPairing("no master stored");
  }

  if (hasMaster) {
    addPeer(masterMac, 1);
    printMac("[CTRL] Master loaded ", masterMac);
  } else {
    Serial.println("[CTRL] No master -> hold button while plugging in to pair");
  }
}

void loop() {
  if (!hasMaster || pairing) { delay(10); return; }

  static uint32_t lastSend = 0;
  uint32_t now = millis();
  if (now - lastSend < 10) return;
  lastSend = now;

  CtrlPacket pkt;
  pkt.seq = now;
  pkt.p1  = (uint16_t)analogRead(PIN_POT1);
  pkt.p2  = (uint16_t)analogRead(PIN_POT2);
  pkt.buttons = isBtnDown() ? 0x01 : 0x00;
  pkt.crc8 = crc8_simple((const uint8_t*)&pkt, sizeof(pkt) - 1);

  esp_now_send(masterMac, (const uint8_t*)&pkt, sizeof(pkt));
}
