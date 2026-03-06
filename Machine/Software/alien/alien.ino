#include <Arduino.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// ====================== ESP-NOW / PAIRING ======================
#include "ehajo_master.h"

// -------------------- PINS (wie bei euch) --------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_MOSI 4
#define OLED_CLK  2
#define OLED_DC   1
#define OLED_CS   5
#define OLED_RST  0

#define MASTER_PAIR_PIN 10

#define LED_PIN    7
#define NUM_LEDS   4

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, OLED_DC, OLED_RST, OLED_CS);
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ====================== Pairing Display Callback ======================
// Wird von pairingLoop() in ehajo_master.h aufgerufen
static void pairingDisplay(uint32_t now) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(18, 0); display.print("PAIRING MODE");

  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
  display.setCursor(0, 14); display.print("MAC:");
  display.setCursor(0, 24); display.print(macStr);

  display.setCursor(0, 38); display.print("Waiting for Ctrl...");
  display.display();

  strip.clear();
  for (int k=0;k<NUM_LEDS;k++)
    strip.setPixelColor(k, ((now/200)%2) ? strip.Color(40,40,0) : strip.Color(0,0,0));
  strip.show();
}

static const uint32_t PAIR_LONGPRESS_MS = 1500;

// ====================== GAME: Alien Angriff =================
static const int TOP_UI_H  = 12;
static const int FIELD_Y0  = TOP_UI_H + 1;
static const int FIELD_Y1  = SCREEN_HEIGHT - 1;

static const uint8_t BTN_FIRE = 0x01;

// BULLETS: sichtbar + langsam
static const int BULLET_W = 1;
static const int BULLET_H = 2;

// Bewegungstakt fuer Bullets (je hoeher, desto langsamer)
static const uint32_t BULLET_TICK_MS = 20;

// Rendering-Update (fps)
static const uint32_t DISPLAY_TICK_MS = 33;   // ~30 fps
static const uint32_t LED_TICK_MS     = 33;

enum GameMode { MODE_IDLE, MODE_RUNNING, MODE_GAMEOVER, MODE_WIN };

struct Bullet {
  int16_t x, y;
  int8_t  vy;           // Pixel pro Bullet-Tick
  bool active;
  bool fromPlayer;
};

static const int MAX_BULLETS = 10;
Bullet bullets[MAX_BULLETS];

// ---------- WIN / FIREWORKS ----------
struct Spark {
  int16_t x16, y16;   // position in 1/16 pixel units
  int16_t vx16, vy16; // velocity in 1/16 pixel units per tick
  uint8_t life;       // ticks remaining
  bool active;
};

static const uint8_t MAX_SPARKS = 24;
static Spark sparks[MAX_SPARKS];
static uint32_t g_nextWinTick = 0;
static uint32_t g_nextBurstMs = 0;

static void clearSparks(){
  for(int i=0;i<MAX_SPARKS;i++) sparks[i].active = false;
}

static void spawnBurst(){
  int cx = random(18, SCREEN_WIDTH-18);
  int cy = random(FIELD_Y0+6, 42);

  for(int i=0;i<MAX_SPARKS;i++){
    sparks[i].active = true;
    sparks[i].x16 = (int16_t)(cx*16);
    sparks[i].y16 = (int16_t)(cy*16);

    int vx = (int)random(-32, 33);
    int vy = (int)random(-32, 33);
    if(abs(vx) < 6) vx = (vx < 0) ? -6 : 6;
    if(abs(vy) < 6) vy = (vy < 0) ? -6 : 6;

    sparks[i].vx16 = (int16_t)vx;
    sparks[i].vy16 = (int16_t)vy;

    sparks[i].life = (uint8_t)random(14, 28);
  }
}

static const int SHIP_W = 9;
static const int SHIP_H = 4;

// Aliens: nur 2 Reihen, mehr Abstand
static const int ALIEN_COLS = 10;
static const int ALIEN_ROWS = 2;

static const int ALIEN_W = 6;
static const int ALIEN_H = 4;
static const int ALIEN_SPX = 6;
static const int ALIEN_SPY = 5;
static const int ALIEN_START_X = 8;
static const int ALIEN_START_Y = FIELD_Y0 + 1; // hoeher starten

// Alien-Sprites (6x4) - 5 verschiedene Typen, pro Zeile zufaellig neu je Level
static const uint8_t ALIEN_TYPES = 5;

// Jede Zeile ist ein 6-Bit-Muster (bit0 = links, bit5 = rechts)
static const uint8_t alienSprite[ALIEN_TYPES][ALIEN_H] PROGMEM = {
  // Typ 0: "Classic"
  { 0b011110, 0b111111, 0b101101, 0b010010 },
  // Typ 1: "Squid"
  { 0b001100, 0b011110, 0b111111, 0b101101 },
  // Typ 2: "Crab"
  { 0b010010, 0b111111, 0b011110, 0b110011 },
  // Typ 3: "Bot"
  { 0b011110, 0b110011, 0b111111, 0b001100 },
  // Typ 4: "Mask"
  { 0b111111, 0b101101, 0b111111, 0b010010 }
};

static uint8_t rowAlienType[ALIEN_ROWS] = {0};

struct Alien { bool alive; };
Alien aliens[ALIEN_ROWS][ALIEN_COLS];

struct GameState {
  GameMode mode = MODE_IDLE;

  int16_t shipX = (SCREEN_WIDTH - SHIP_W)/2;
  int16_t shipY = FIELD_Y1 - SHIP_H;

  int16_t aliensOffX = 0, aliensOffY = 0;
  int8_t  aliensDir = 1;
  uint32_t aliensStepMs = 520;           // etwas langsamer starten
  uint32_t nextAliensStep = 0;

  uint32_t nextPlayerShot = 0;
  uint32_t nextEnemyShot  = 0;

  uint16_t score = 0;
  int8_t lives = 3;

  uint16_t level = 1;

  // WIN screen timing
  uint32_t winStartMs = 0;

  uint8_t prevButtons = 0;
  uint32_t hitFlashUntil = 0;
} gs;

// Ticker
static uint32_t g_nextBulletStep = 0;

static inline bool rectHit(int ax,int ay,int aw,int ah,int bx,int by,int bw,int bh){
  return !(ax+aw<=bx || bx+bw<=ax || ay+ah<=by || by+bh<=ay);
}

static void resetAliens(){
  for(int r=0;r<ALIEN_ROWS;r++) for(int c=0;c<ALIEN_COLS;c++) aliens[r][c].alive=true;
  gs.aliensOffX=0; gs.aliensOffY=0; gs.aliensDir=1;

  // pro Level zufaellige Alien-Typen je Zeile (und moeglichst nicht doppelt)
  for(int r=0;r<ALIEN_ROWS;r++){
    uint8_t t = (uint8_t)random(ALIEN_TYPES);
    if(r>0 && ALIEN_TYPES>1){
      while(t == rowAlienType[r-1]) t = (uint8_t)random(ALIEN_TYPES);
    }
    rowAlienType[r] = t;
  }

  // Aliens werden pro Level etwas schneller
  // Level 1: 520ms, danach -40ms pro Level, aber nicht schneller als 180ms
  int step = 520 - (int)(gs.level - 1) * 40;
  if(step < 180) step = 180;
  gs.aliensStepMs=(uint32_t)step;

  gs.nextAliensStep=millis()+gs.aliensStepMs;
}

static void resetBullets(){
  for(int i=0;i<MAX_BULLETS;i++) bullets[i].active=false;
}

static void newGame(){
  gs.mode=MODE_RUNNING; gs.score=0; gs.lives=3; gs.level=1;
  gs.shipX=(SCREEN_WIDTH-SHIP_W)/2; gs.shipY=FIELD_Y1-SHIP_H;
  gs.nextPlayerShot=millis(); gs.nextEnemyShot=millis()+900;
  gs.hitFlashUntil=0;
  resetAliens();
  resetBullets();
  g_nextBulletStep = millis() + BULLET_TICK_MS;
}

static void startWin(uint32_t now){
  gs.mode = MODE_WIN;
  gs.winStartMs = now;

  // stop anything in flight
  resetBullets();

  // fireworks
  clearSparks();
  g_nextWinTick = now;
  g_nextBurstMs = now;
  spawnBurst();
}

static void spawnBullet(int16_t x,int16_t y,int8_t vy,bool fromPlayer){
  for(int i=0;i<MAX_BULLETS;i++){
    if(!bullets[i].active){
      bullets[i].active=true;
      bullets[i].x=x;
      bullets[i].y=y;
      bullets[i].vy=vy;
      bullets[i].fromPlayer=fromPlayer;
      return;
    }
  }
}

static int aliveAliensCount(){
  int n=0;
  for(int r=0;r<ALIEN_ROWS;r++) for(int c=0;c<ALIEN_COLS;c++) if(aliens[r][c].alive) n++;
  return n;
}

static void updateShipFromPoti(){
  int tx = map((int)g_rxP1, 0, 4095, 0, SCREEN_WIDTH - SHIP_W);
  gs.shipX = (gs.shipX*3 + tx)/4;
}

static void updateAliens(uint32_t now){
  if(now < gs.nextAliensStep) return;
  gs.nextAliensStep = now + gs.aliensStepMs;

  int minCol=999,maxCol=-1,minRow=999,maxRow=-1;
  for(int r=0;r<ALIEN_ROWS;r++){
    for(int c=0;c<ALIEN_COLS;c++){
      if(!aliens[r][c].alive) continue;
      if(c<minCol) minCol=c; if(c>maxCol) maxCol=c;
      if(r<minRow) minRow=r; if(r>maxRow) maxRow=r;
    }
  }

  if(maxCol < 0){
    gs.level++;
    // geschafft!!! (freak!)
    if(gs.level >= 100){
      startWin(now);
      return;
    }
    // Level geschafft -> naechstes Level
    resetAliens();

    // Bullets fuer neues Level loeschen
    resetBullets();
    gs.nextPlayerShot = now;
    gs.nextEnemyShot  = now + 700;

    // Bullet-Ticker sauber neu starten
    g_nextBulletStep = now + BULLET_TICK_MS;
    return;
  }

  int blockX0 = ALIEN_START_X + gs.aliensOffX + minCol*(ALIEN_W+ALIEN_SPX);
  int blockX1 = ALIEN_START_X + gs.aliensOffX + maxCol*(ALIEN_W+ALIEN_SPX) + ALIEN_W;

  bool hitRight = (blockX1 >= SCREEN_WIDTH-2);
  bool hitLeft  = (blockX0 <= 2);

  if((gs.aliensDir>0 && hitRight) || (gs.aliensDir<0 && hitLeft)){
    gs.aliensDir *= -1;
    gs.aliensOffY += 2; // weniger runter pro bounce
  } else {
    gs.aliensOffX += gs.aliensDir * 2;
  }

  int blockY1 = ALIEN_START_Y + gs.aliensOffY + maxRow*(ALIEN_H+ALIEN_SPY) + ALIEN_H;
  if(blockY1 >= gs.shipY - 1) gs.mode = MODE_GAMEOVER;
}

static void updateBullets(uint32_t now){
  if (now < g_nextBulletStep) return;
  // catch-up, falls es laggt
  while (now >= g_nextBulletStep) g_nextBulletStep += BULLET_TICK_MS;

  for(int i=0;i<MAX_BULLETS;i++){
    if(!bullets[i].active) continue;

    bullets[i].y += bullets[i].vy;

    // Bounds: beruecksichtige Hoehe
    if(bullets[i].y + BULLET_H < FIELD_Y0 || bullets[i].y > FIELD_Y1){
      bullets[i].active=false;
      continue;
    }

    if(bullets[i].fromPlayer){
      for(int r=0;r<ALIEN_ROWS;r++){
        for(int c=0;c<ALIEN_COLS;c++){
          if(!aliens[r][c].alive) continue;
          int ax = ALIEN_START_X + gs.aliensOffX + c*(ALIEN_W+ALIEN_SPX);
          int ay = ALIEN_START_Y + gs.aliensOffY + r*(ALIEN_H+ALIEN_SPY);

          if(rectHit(bullets[i].x, bullets[i].y, BULLET_W, BULLET_H, ax, ay, ALIEN_W, ALIEN_H)){
            aliens[r][c].alive=false;
            bullets[i].active=false;
            gs.score += 10;

            int alive = aliveAliensCount();
            if(alive>0){
              uint32_t t = 200 + (alive*8);
              if(t < gs.aliensStepMs) gs.aliensStepMs = t;
            }
            break;
          }
        }
      }
    } else {
      if(rectHit(bullets[i].x, bullets[i].y, BULLET_W, BULLET_H, gs.shipX, gs.shipY, SHIP_W, SHIP_H)){
        bullets[i].active=false;
        gs.lives--;
        gs.hitFlashUntil=now+300;
        gs.shipX=(SCREEN_WIDTH-SHIP_W)/2;

        if(gs.lives <= 0) gs.mode=MODE_GAMEOVER;
      }
    }
  }
}

static bool anyEnemyBulletActive(){
  for (int i=0;i<MAX_BULLETS;i++){
    if (bullets[i].active && !bullets[i].fromPlayer) return true;
  }
  return false;
}


static void handleShooting(uint32_t now){
  uint8_t b = g_rxButtons;
  bool firePressed = (b & BTN_FIRE) && !(gs.prevButtons & BTN_FIRE);
  gs.prevButtons = b;

  if(gs.mode == MODE_IDLE){
    if(firePressed) newGame();
    return;
  }
  if(gs.mode == MODE_GAMEOVER){
    if(firePressed){
      gs.mode=MODE_IDLE;
      resetBullets();
    }
    return;
  }

  // Player shoot cooldown
  if(firePressed && now >= gs.nextPlayerShot){
    gs.nextPlayerShot = now + 240;
    // Langsam: -1 pro Bullet-Tick
    spawnBullet(gs.shipX + SHIP_W/2, gs.shipY - 2, -1, true);
  }

  // Enemy shots
  if(now >= gs.nextEnemyShot){
    gs.nextEnemyShot = now + (uint32_t)random(700, 1200);

    // nur 1 Alien-Schuss gleichzeitig
    if (anyEnemyBulletActive()) return;

    int tries=16;
    while(tries--){
      int c = random(0, ALIEN_COLS);
      int rHit=-1;
      for(int r=ALIEN_ROWS-1;r>=0;r--){
        if(aliens[r][c].alive){ rHit=r; break; }
      }
      if(rHit>=0){
        int ax = ALIEN_START_X + gs.aliensOffX + c*(ALIEN_W+ALIEN_SPX) + ALIEN_W/2;
        int ay = ALIEN_START_Y + gs.aliensOffY + rHit*(ALIEN_H+ALIEN_SPY) + ALIEN_H;
        spawnBullet(ax, ay+1, +1, false);
        break;
      }
    }
  }
}

// ====================== DRAWING ======================
static void drawUI(){
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.drawFastHLine(0, TOP_UI_H, SCREEN_WIDTH, SSD1306_WHITE);

  display.setCursor(0, 2);  display.print("S:"); display.print(gs.score);
  display.setCursor(52, 2); display.print("L:"); display.print((int)gs.lives);

  display.setCursor(92, 2); display.print("LV:"); display.print(gs.level);
}

static void drawShip(uint32_t now){
  if(now < gs.hitFlashUntil && ((now/60)%2)) return;
  display.drawRect(gs.shipX, gs.shipY, SHIP_W, SHIP_H, SSD1306_WHITE);
  display.drawPixel(gs.shipX + SHIP_W/2, gs.shipY - 1, SSD1306_WHITE);
}

static void drawAliens(){
  for(int r=0;r<ALIEN_ROWS;r++){
    uint8_t t = rowAlienType[r] % ALIEN_TYPES;

    for(int c=0;c<ALIEN_COLS;c++){
      if(!aliens[r][c].alive) continue;

      int ax = ALIEN_START_X + gs.aliensOffX + c*(ALIEN_W+ALIEN_SPX);
      int ay = ALIEN_START_Y + gs.aliensOffY + r*(ALIEN_H+ALIEN_SPY);

      // 6x4 Pixel-Sprite zeichnen
      for(int yy=0; yy<ALIEN_H; yy++){
        uint8_t row = pgm_read_byte(&alienSprite[t][yy]);
        for(int xx=0; xx<ALIEN_W; xx++){
          if(row & (1U<<xx)) display.drawPixel(ax+xx, ay+yy, SSD1306_WHITE);
        }
      }

      // Mini-"Augenblinzeln"/Akzent: alle ~250ms ein Pixel toggeln (subtil)
      if(((millis()/250) & 1) == 0){
        display.drawPixel(ax + 2, ay + 1, SSD1306_WHITE);
      }
    }
  }
}

static void drawBullets(){
  for(int i=0;i<MAX_BULLETS;i++){
    if(!bullets[i].active) continue;

    int x = bullets[i].x;
    int y = bullets[i].y;

    if (x < 0) x = 0;
    if (x > SCREEN_WIDTH - BULLET_W) x = SCREEN_WIDTH - BULLET_W;

    display.fillRect(x, y, BULLET_W, BULLET_H, SSD1306_WHITE);
  }
}

static void drawStartScreen(uint32_t now){
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(26, 18); display.print("Alien Angriff");
  if((now/500)%2){ display.setCursor(32, 48); display.print("PRESS FIRE"); }
}

static void drawGameOver(uint32_t now){
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(34, 18); display.print("GAME OVER");
  display.setCursor(28, 34); display.print("SCORE: "); display.print(gs.score);
  if((now/500)%2){ display.setCursor(32, 48); display.print("PRESS FIRE"); }
}

// ===== WIN SCREEN (Level 100) =====
static void drawWin(uint32_t now){
  if(now >= g_nextWinTick){
    g_nextWinTick = now + 30;

    if(now >= g_nextBurstMs){
      g_nextBurstMs = now + (uint32_t)random(220, 520);
      spawnBurst();
    }

    for(int i=0;i<MAX_SPARKS;i++){
      if(!sparks[i].active) continue;

      sparks[i].vy16 += 2; // gravity

      sparks[i].x16 += sparks[i].vx16;
      sparks[i].y16 += sparks[i].vy16;

      if(sparks[i].life) sparks[i].life--;
      if(sparks[i].life == 0) sparks[i].active = false;

      int x = sparks[i].x16 / 16;
      int y = sparks[i].y16 / 16;
      if(x < 0 || x >= SCREEN_WIDTH || y < FIELD_Y0 || y >= SCREEN_HEIGHT){
        sparks[i].active = false;
      }
    }
  }

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(25, 0);
  display.print("DURCHGESPIELT!");

  for(int i=0;i<MAX_SPARKS;i++){
    if(!sparks[i].active) continue;
    int x = sparks[i].x16 / 16;
    int y = sparks[i].y16 / 16;
    display.drawPixel(x, y, SSD1306_WHITE);
    if(((now/60)&1) && y+1 < SCREEN_HEIGHT) display.drawPixel(x, y+1, SSD1306_WHITE);
  }

  if(((now/250)&1)==0){
    display.setTextSize(2);
    display.setCursor(50, 22);
    display.print("GG!");
    display.setTextSize(1);
    display.setCursor(8, 50);
    display.print("Hold Fire for Reset");
  }
}

static void updateLeds(uint32_t now){
  bool hitFlash = (now < gs.hitFlashUntil);

  for(int i=0;i<NUM_LEDS;i++){
    uint32_t col = strip.Color(0,0,0);

    if(hitFlash){
      col = ((now/70)%2) ? strip.Color(40,0,0) : strip.Color(0,0,0);
    } else if(gs.mode == MODE_RUNNING){
      col = (i < gs.lives) ? strip.Color(0,35,0) : strip.Color(0,0,0);
      if(i==3 && gs.score>0 && ((now/160)%2)) col = strip.Color(0,0,35);
    } else if(gs.mode == MODE_IDLE){
      uint8_t v = (uint8_t)(10 + 10 * (1.0f + sinf(now/600.0f))*0.5f);
      col = strip.Color(0,0,v);
    } else if(gs.mode == MODE_WIN){
      uint8_t r = (uint8_t)random(0, 255);
      uint8_t g = (uint8_t)random(0, 255);
      uint8_t b = (uint8_t)random(0, 255);
      col = strip.Color(r,g,b);
    } else {
      col = ((now/220)%2) ? strip.Color(35,0,0) : strip.Color(0,0,0);
    }

    strip.setPixelColor(i, col);
  }
  strip.show();
}

// ====================== SETUP/LOOP ======================
static uint32_t pairPressStart = 0;

void setup(){
  Serial.begin(115200);
  delay(50);

  SPI.begin(OLED_CLK, -1, OLED_MOSI, OLED_CS);
  display.begin(SSD1306_SWITCHCAPVCC);
  display.setTextColor(SSD1306_WHITE);

  strip.begin();
  strip.setBrightness(255);
  strip.show();

  pinMode(MASTER_PAIR_PIN, INPUT_PULLUP);
  randomSeed((uint32_t)esp_random());

  // --- STA-Mode (funktioniert fuer Empfang auf Machine) ---
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);
  delay(500);

  wifiTweak();

  esp_read_mac(g_mac, ESP_MAC_WIFI_STA);
  Serial.printf("[ARCADE] STA MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
    g_mac[0],g_mac[1],g_mac[2],g_mac[3],g_mac[4],g_mac[5]);
  Serial.printf("[ARCADE] WiFi.status=%d mode=%d\n", (int)WiFi.status(), (int)WiFi.getMode());

  if (esp_now_init() != ESP_OK) {
    display.clearDisplay();
    display.setCursor(0,0);
    display.print("esp_now_init FAIL");
    display.display();
    while(true){ delay(1000); }
  }
  Serial.println("[ARCADE] ESP-NOW init OK");

  // Channel NACH esp_now_init setzen
  forceChannel1();

  esp_now_register_recv_cb(onDataRecv);

  Serial.println("[ARCADE] Alien Angriff master boot");
  g_autoPairUntil = millis() + 20000; // 20s Auto-Pair Fenster nach Boot

  gs.mode = MODE_IDLE;
  resetBullets();
  resetAliens();
  g_nextBulletStep = millis() + BULLET_TICK_MS;
}

void loop(){
  uint32_t now = millis();

  // Auto-Pairing kurz nach Boot, solange noch kein Controller aktiv ist
  if (!ctrlPeerAdded && !g_pairing && (g_autoPairUntil != 0) && (now < g_autoPairUntil)) {
    startPairing();
  }

  // Long press -> Pairing Mode (Arcade)
  if (digitalRead(MASTER_PAIR_PIN) == LOW) {
    if (pairPressStart == 0) pairPressStart = now;
    if (!g_pairing && (now - pairPressStart) > PAIR_LONGPRESS_MS) {
      startPairing();
    }
  } else {
    pairPressStart = 0;
  }

  if (g_pairing) {
    pairingLoop();
    return;
  }

  // LEDs
  static uint32_t lastLed = 0;
  if(now - lastLed >= LED_TICK_MS){
    lastLed = now;
    updateLeds(now);
  }

  // --- WIN MODE: 2s FIRE halten fuer Neustart ---
  static uint32_t winFirePressStart = 0;

  if(gs.mode == MODE_WIN){

    bool firePressed = (g_rxButtons & BTN_FIRE);

    if(firePressed){
      if(winFirePressStart == 0){
        winFirePressStart = now;   // Zeit merken
      }

      // 2 Sekunden gehalten?
      if(now - winFirePressStart > 1000){
        winFirePressStart = 0;
        newGame();
      }
    } else {
      // Taste losgelassen -> Reset Timer
      winFirePressStart = 0;
    }

    gs.prevButtons = g_rxButtons;
  } else {
    updateShipFromPoti();
    handleShooting(now);
  }

  if(gs.mode == MODE_RUNNING){
    updateAliens(now);
    updateBullets(now);
  }

  // Display (langsamer = ruhiger)
  static uint32_t lastDisp = 0;
  if(now - lastDisp >= DISPLAY_TICK_MS){
    lastDisp = now;

    display.clearDisplay();
    if(gs.mode == MODE_IDLE) {
      drawStartScreen(now);
    } else if(gs.mode == MODE_GAMEOVER) {
      drawGameOver(now);
    } else if(gs.mode == MODE_WIN) {
      drawWin(now);
    } else {
      drawUI();
      drawAliens();
      drawBullets();
      drawShip(now);
    }
    display.display();
  }
}
