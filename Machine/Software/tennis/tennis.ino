#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// ====================== ESP-NOW / PAIRING ======================
#include "ehajo_master.h"

// ====================== HARDWARE PINS ======================
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

// ====================== INITIALISIERUNG ======================
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

// Long-Press-Erkennung (Master Pair Pin)
static uint32_t pairPressStart = 0;

// --- Spiel-Konfiguration ---
static const int TOP_UI_H = 12;
static const int FIELD_Y0 = TOP_UI_H + 1;
static const int PADDLE_W = 3;
static const int PADDLE_H = 14;
static const int PADDLE_XL = 4;
static const int PADDLE_XR = SCREEN_WIDTH - 4 - PADDLE_W;
static const int BALL_R = 2;
static const int SCORE_TO_WIN = 10;

enum GameMode { MODE_IDLE, MODE_COUNTDOWN, MODE_RUNNING, MODE_GAMEOVER };

struct Particle { float x, y, vx, vy; bool active; };
Particle particles[20];

struct GameState {
  float paddleYL, paddleYR, ballX, ballY, ballVX, ballVY;
  int scoreL, scoreR;
  uint32_t lastFrameMs, countdownStartMs;
  uint32_t ledEffectTimer;
  GameMode mode;
};
GameState gs;

// ====================== LED FUNKTIONEN ======================
void setAllLeds(uint32_t color) {
  for(int i=0; i<NUM_LEDS; i++) strip.setPixelColor(i, color);
  // strip.show() wird vom Aufrufer gemacht, um Doppelaufrufe zu vermeiden
}

// Hilfsfunktion fuer Regenbogenfarben
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if (WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

void updateLeds(uint32_t now) {
  if (gs.mode == MODE_RUNNING) {
    static uint16_t j = 0;
    j += 2;
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, Wheel(((i * 256 / NUM_LEDS) + j) & 255));
    }
    strip.show();

    if (now - gs.ledEffectTimer < 200) {
      strip.setPixelColor(0, strip.Color(255, 255, 255));
      strip.setPixelColor(1, strip.Color(255, 255, 255));
      strip.setPixelColor(2, strip.Color(255, 255, 255));
      strip.setPixelColor(3, strip.Color(255, 255, 255));
      strip.show();
    }
  } else if (gs.mode == MODE_GAMEOVER) {
    if (now - gs.ledEffectTimer < 2000) {
      if (gs.scoreL >= SCORE_TO_WIN) {
        strip.setPixelColor(0, strip.Color(0, 255, 0)); strip.setPixelColor(1, strip.Color(0, 255, 0));
        strip.setPixelColor(2, strip.Color(0, 255, 0)); strip.setPixelColor(3, strip.Color(0, 255, 0));
      } else {
        strip.setPixelColor(0, strip.Color(255, 0, 0)); strip.setPixelColor(1, strip.Color(255, 0, 0));
        strip.setPixelColor(2, strip.Color(255, 0, 0)); strip.setPixelColor(3, strip.Color(255, 0, 0));
      }
      strip.show();
    } else {
      static uint16_t j_over = 0;
      j_over += 2;
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, Wheel(((i * 256 / NUM_LEDS) + j_over) & 255));
      }
      strip.show();
    }
  }
}

// ====================== GAME LOGIC ======================
void initParticles(bool leftSide) {
  for(int i=0; i<20; i++) {
    particles[i].active = true;
    particles[i].x = leftSide ? 30 : 90;
    particles[i].y = 35;
    particles[i].vx = (random(-10, 10) / 10.0f);
    particles[i].vy = (random(-15, -5) / 10.0f);
  }
}

void updateParticles() {
  for(int i=0; i<20; i++) {
    if(!particles[i].active) continue;
    particles[i].x += particles[i].vx;
    particles[i].y += particles[i].vy;
    particles[i].vy += 0.05f;
    if(particles[i].y > 64) particles[i].active = false;
    display.drawPixel((int)particles[i].x, (int)particles[i].y, SSD1306_WHITE);
  }
}

void resetBall(bool toRight) {
  gs.ballX = SCREEN_WIDTH / 2.0f;
  gs.ballY = FIELD_Y0 + (SCREEN_HEIGHT - FIELD_Y0) / 2.0f;
  gs.ballVX = toRight ? 1.4f : -1.4f;
  gs.ballVY = (random(-12, 12) / 10.0f);
  gs.ledEffectTimer = millis();
}

// ====================== SETUP ======================
void setup() {
  Serial.begin(115200);
  pinMode(MASTER_PAIR_PIN, INPUT_PULLUP);
  randomSeed((uint32_t)esp_random());

  strip.begin();
  strip.setBrightness(255);
  setAllLeds(strip.Color(0,0,0));

  SPI.begin(OLED_CLK, -1, OLED_MOSI, OLED_CS);
  SPI.setFrequency(40000000);
  display.begin(SSD1306_SWITCHCAPVCC);

  display.setTextColor(SSD1306_WHITE);

  // STA-Mode (funktioniert fuer Empfang auf Machine)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);
  delay(500);

  wifiTweak();

  // --- wireless init ---
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
  Serial.println("[ARCADE] Tennis master boot");
  g_autoPairUntil = millis() + 20000; // 20s Auto-Pair Fenster nach Boot
  gs.mode = MODE_IDLE;
}

// ====================== LOOP ======================
void loop() {
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

  // 1. LED-Update drosseln
  static uint32_t lastLedUpdate = 0;
  if (now - lastLedUpdate > 33) {
    lastLedUpdate = now;
    updateLeds(now);
  }

  // 2. Paddle-Werte sofort lesen
  int targetYL = map(g_rxP1, 0, 4095, FIELD_Y0, SCREEN_HEIGHT - PADDLE_H);
  int targetYR = map(g_rxP2, 0, 4095, FIELD_Y0, SCREEN_HEIGHT - PADDLE_H);

  // 3. Spiellogik / Input
  if (gs.mode == MODE_IDLE || gs.mode == MODE_GAMEOVER) {
    if (g_rxButtons & 0x01) {
      gs.mode = MODE_COUNTDOWN;
      gs.countdownStartMs = now;
      gs.scoreL = 0; gs.scoreR = 0;
      resetBall(true);
    }
  }
  else if (gs.mode == MODE_COUNTDOWN) {
    int val = 3 - (int)((now - gs.countdownStartMs) / 1000);
    if (val <= 0) gs.mode = MODE_RUNNING;
  }
  else if (gs.mode == MODE_RUNNING) {
    static uint32_t lastPhysicsMs = 0;
    if (now - lastPhysicsMs >= 16) {
      lastPhysicsMs = now;
      gs.ballX += gs.ballVX; gs.ballY += gs.ballVY;
      if (gs.ballY <= FIELD_Y0 + BALL_R || gs.ballY >= SCREEN_HEIGHT - BALL_R) gs.ballVY *= -1;

      if (gs.ballVX < 0 && gs.ballX <= PADDLE_XL + PADDLE_W + BALL_R) {
        if (gs.ballY >= targetYL && gs.ballY <= targetYL + PADDLE_H) {
          gs.ballVX *= -1.08f; gs.ballVY = ((gs.ballY - targetYL) / (float)PADDLE_H - 0.5f) * 4.0f;
          gs.ballX = PADDLE_XL + PADDLE_W + BALL_R + 1;
        } else if (gs.ballX < 0) {
          gs.scoreR++; resetBall(true);
          if (gs.scoreR >= SCORE_TO_WIN) { gs.mode = MODE_GAMEOVER; initParticles(false); }
        }
      }
      if (gs.ballVX > 0 && gs.ballX >= PADDLE_XR - BALL_R) {
        if (gs.ballY >= targetYR && gs.ballY <= targetYR + PADDLE_H) {
          gs.ballVX *= -1.08f; gs.ballVY = ((gs.ballY - targetYR) / (float)PADDLE_H - 0.5f) * 4.0f;
          gs.ballX = PADDLE_XR - BALL_R - 1;
        } else if (gs.ballX > SCREEN_WIDTH) {
          gs.scoreL++; resetBall(false);
          if (gs.scoreL >= SCORE_TO_WIN) { gs.mode = MODE_GAMEOVER; initParticles(true); }
        }
      }
    }
  }

  // 4. Display-Update auf 30 FPS begrenzen
  static uint32_t lastDisplayMs = 0;
  if (now - lastDisplayMs >= 33) {
    lastDisplayMs = now;

    display.clearDisplay();

    display.drawFastHLine(0, TOP_UI_H, SCREEN_WIDTH, SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(10, 2);  display.print(gs.scoreL);
    display.setCursor(110, 2); display.print(gs.scoreR);
    for (int y = FIELD_Y0; y < SCREEN_HEIGHT; y += 6) {
      display.drawFastVLine(SCREEN_WIDTH/2, y, 3, SSD1306_WHITE);
    }

    display.fillRect(PADDLE_XL, targetYL, PADDLE_W, PADDLE_H, SSD1306_WHITE);
    display.fillRect(PADDLE_XR, targetYR, PADDLE_W, PADDLE_H, SSD1306_WHITE);

    if (gs.mode == MODE_IDLE) {
      display.setCursor(25, 30); display.print("TENNIS READY");
      if ((now / 500) % 2) { display.setCursor(30, 45); display.print("PRESS START"); }
    }
    else if (gs.mode == MODE_GAMEOVER) {
      updateParticles();
      display.setTextSize(2);
      display.setCursor(gs.scoreL >= SCORE_TO_WIN ? 10 : 75, 25);
      display.print("WIN!");
      display.setTextSize(1);
      if ((now / 500) % 2) { display.setCursor(30, 50); display.print("START AGAIN"); }
    }
    else if (gs.mode == MODE_COUNTDOWN) {
      int val = 3 - (int)((now - gs.countdownStartMs) / 1000);
      if (val < 1) val = 1;
      display.setTextSize(3);
      display.setCursor(55, 25); display.print(val);
      display.setTextSize(1);
    }

    display.fillCircle((int)gs.ballX, (int)gs.ballY, BALL_R, SSD1306_WHITE);

    display.display();
  }
}
