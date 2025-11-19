// src/display.cpp
#include "display.h"
#include "globals.h"
#include "config.h"

#include <SPIFFS.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <algorithm> // std::min

// Evitar choque de macros LOW/HIGH con qrcodegen.hpp
#ifdef LOW
  #define _QR_OLD_LOW
  #undef LOW
#endif
#ifdef HIGH
  #define _QR_OLD_HIGH
  #undef HIGH
#endif

#include "qrcodegen.hpp"

#ifdef _QR_OLD_LOW
  #undef _QR_OLD_LOW
  #define LOW 0x0
#endif
#ifdef _QR_OLD_HIGH
  #undef _QR_OLD_HIGH
  #define HIGH 0x1
#endif

static const unsigned long ACCESS_SCREEN_MS = 5000UL; // 5s

// ----------------- Helpers gráficos -----------------
static void drawCheckIcon(int cx, int cy, int r) {
  tft.fillCircle(cx, cy, r, ST77XX_GREEN);
  int x1 = cx - r/2;
  int y1 = cy;
  int x2 = cx - r/8;
  int y2 = cy + r/3;
  int x3 = cx + r/2;
  int y3 = cy - r/4;
  for (int off = -2; off <= 2; ++off) {
    tft.drawLine(x1, y1 + off, x2, y2 + off, ST77XX_WHITE);
    tft.drawLine(x2, y2 + off, x3, y3 + off, ST77XX_WHITE);
  }
}

static void drawCrossIcon(int cx, int cy, int r) {
  tft.fillCircle(cx, cy, r, ST77XX_RED);
  int off = r * 3 / 4;
  for (int o = -2; o <= 2; ++o) {
    tft.drawLine(cx - off + o, cy - off, cx + off + o, cy + off, ST77XX_WHITE);
    tft.drawLine(cx - off + o, cy + off, cx + off + o, cy - off, ST77XX_WHITE);
  }
}

static void drawCenteredText(const String &txt, int y, uint8_t size, uint16_t color = ST77XX_WHITE) {
  tft.setTextSize(size);
  tft.setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(txt, 0, y, &x1, &y1, &w, &h);
  int x = (tft.width() - w) / 2;
  tft.setCursor(x, y);
  tft.print(txt);
}

static void clearContentArea() {
  tft.fillRect(0, 22, tft.width(), tft.height() - 22, ST77XX_BLACK);
}

static void drawHeader() {
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.fillRect(0, 0, tft.width(), 22, ST77XX_BLACK);
  drawCenteredText("CONTROL DE ACCESO LAB", 4, 1, ST77XX_WHITE);
  tft.drawFastHLine(0, 20, tft.width(), ST77XX_WHITE);
}

// ----------------- API pública -----------------
void displayInit() {
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  pinMode(RGB_R_PIN, OUTPUT);
  pinMode(RGB_G_PIN, OUTPUT);

  drawHeader();
  ledOff();

  showWaitingMessage();
}

void showWaitingMessage() {
  drawHeader();
  clearContentArea();
  drawCenteredText("Bienvenido", 28, 2, ST77XX_WHITE);
  drawCenteredText("Esperando tarjeta...", 60, 1, ST77XX_WHITE);
  ledOff();
}

void showAccessGranted(const String &name, const String &materia, const String &uid) {
  tft.fillScreen(ST77XX_BLACK);

  int cx = tft.width() / 2;
  int cy = 36;
  int r = std::min(tft.width(), tft.height()) / 6;
  drawCheckIcon(cx, cy, r);

  int baseY = cy + r + 6;
  drawCenteredText("ACCESO CONCEDIDO", baseY, 1, ST77XX_WHITE);

  if (name.length()) drawCenteredText(name, baseY + 18, 1, ST77XX_WHITE);
  if (materia.length()) drawCenteredText(materia, baseY + 30, 1, ST77XX_WHITE);
  if (uid.length()) drawCenteredText(uid, baseY + 42, 1, ST77XX_WHITE);

  ledGreenOn();

  unsigned long start = millis();
  while (millis() - start < ACCESS_SCREEN_MS) {
    delay(10);
  }

  showWaitingMessage();
}

void showAccessDenied(const String &reason, const String &uid) {
  tft.fillScreen(ST77XX_BLACK);

  int cx = tft.width() / 2;
  int cy = 36;
  int r = std::min(tft.width(), tft.height()) / 6;
  drawCrossIcon(cx, cy, r);

  int baseY = cy + r + 6;
  drawCenteredText("ACCESO DENEGADO", baseY, 1, ST77XX_WHITE);

  if (reason.length()) drawCenteredText(reason, baseY + 18, 1, ST77XX_WHITE);
  else drawCenteredText("Tarjeta no reconocida", baseY + 18, 1, ST77XX_WHITE);

  if (uid.length()) drawCenteredText(uid, baseY + 36, 1, ST77XX_WHITE);

  ledRedOn();

  unsigned long start = millis();
  while (millis() - start < ACCESS_SCREEN_MS) {
    delay(10);
  }

  showWaitingMessage();
}

// ----------------- Dibujar QR en la pantalla -----------------
void showQRCodeOnDisplay(const String &url, int pixelBoxSize) {
  using qrcodegen::QrCode;

  // Evitamos usar el token LOW (que IntelliSense interpreta como macro).
  // En su lugar pasamos el valor 0 casteado a la enumeración Ecc.
  QrCode qr = QrCode::encodeText(url.c_str(), static_cast<qrcodegen::QrCode::Ecc>(0));
  int s = qr.getSize();

  int maxBox = pixelBoxSize;
  int modulePx = maxBox / s;
  if (modulePx <= 0) modulePx = 1;
  int totalPx = modulePx * s;

  tft.fillScreen(ST77XX_BLACK);
  drawHeader();

  int left = (tft.width() - totalPx) / 2;
  int top  = (tft.height() - totalPx) / 2 + 8;
  if (left < 0) left = 0;
  if (top < 22) top = 22;

  tft.fillRect(left - 4, top - 4, totalPx + 8, totalPx + 8, ST77XX_WHITE);

  for (int y = 0; y < s; ++y) {
    for (int x = 0; x < s; ++x) {
      bool dark = qr.getModule(x, y);
      int px = left + x * modulePx;
      int py = top  + y * modulePx;
      if (dark) tft.fillRect(px, py, modulePx, modulePx, ST77XX_BLACK);
    }
  }

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(6, tft.height() - 14);
  tft.print("Escanee el QR para registrarse");
}

// ----------------- LEDs -----------------
void ledOff() {
  digitalWrite(RGB_R_PIN, HIGH);
  digitalWrite(RGB_G_PIN, HIGH);
}

void ledRedOn() {
  digitalWrite(RGB_R_PIN, LOW);
  digitalWrite(RGB_G_PIN, HIGH);
}

void ledGreenOn() {
  digitalWrite(RGB_R_PIN, HIGH);
  digitalWrite(RGB_G_PIN, LOW);
}
