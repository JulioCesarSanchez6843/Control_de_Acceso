// src/display.cpp
#include "display.h"
#include "globals.h"
#include "config.h"

#include <SPIFFS.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <algorithm> // std::min

// Evitar choque de macros LOW/HIGH con qrcodegen.hpp (si usas qrcodegen).
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

static const unsigned long ACCESS_SCREEN_MS = 4000UL; // 4s

// ----------------- Helpers gráficos -----------------
static void drawCheckIcon(int cx, int cy, int r) {
  tft.fillCircle(cx, cy, r, ST77XX_GREEN);
  int x1 = cx - r/2;
  int y1 = cy;
  int x2 = cx - r/8;
  int y2 = cy + r/3;
  int x3 = cx + r/2;
  int y3 = cy - r/4;
  for (int off = -1; off <= 1; ++off) {
    tft.drawLine(x1, y1 + off, x2, y2 + off, ST77XX_WHITE);
    tft.drawLine(x2, y2 + off, x3, y3 + off, ST77XX_WHITE);
  }
}

static void drawCrossIcon(int cx, int cy, int r) {
  tft.fillCircle(cx, cy, r, ST77XX_RED);
  int off = r * 3 / 4;
  for (int o = -1; o <= 1; ++o) {
    tft.drawLine(cx - off + o, cy - off, cx + off + o, cy + off, ST77XX_WHITE);
    tft.drawLine(cx - off + o, cy + off, cx + off + o, cy - off, ST77XX_WHITE);
  }
}

// Texto centrado (tamaño reducido por defecto para evitar solapamiento)
static void drawCenteredText(const String &txt, int y, uint8_t size, uint16_t color = ST77XX_WHITE) {
  tft.setTextSize(size);
  tft.setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(txt, 0, y, &x1, &y1, &w, &h);
  int x = (tft.width() - w) / 2;
  if (x < 0) x = 0;
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
  // header reducido para ganar espacio
  drawCenteredText("CONTROL DE ACCESO", 4, 1, ST77XX_WHITE);
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
  // Letras más pequeñas para dar más aire y evitar solapamiento
  drawCenteredText("Bienvenido", 28, 1, ST77XX_WHITE);
  drawCenteredText("Esperando tarjeta...", 56, 1, ST77XX_WHITE);
  ledOff();
}

void showAccessGranted(const String &name, const String &materia, const String &uid) {
  tft.fillScreen(ST77XX_BLACK);

  int cx = tft.width() / 2;
  int cy = 34;
  int r = std::min(tft.width(), tft.height()) / 7;
  drawCheckIcon(cx, cy, r);

  int baseY = cy + r + 4;
  drawCenteredText("ACCESO CONCEDIDO", baseY, 1, ST77XX_WHITE);

  if (name.length()) drawCenteredText(name, baseY + 16, 1, ST77XX_WHITE);
  if (materia.length()) drawCenteredText(materia, baseY + 28, 1, ST77XX_WHITE);
  if (uid.length()) drawCenteredText(uid, baseY + 40, 1, ST77XX_WHITE);

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
  int cy = 34;
  int r = std::min(tft.width(), tft.height()) / 7;
  drawCrossIcon(cx, cy, r);

  int baseY = cy + r + 4;
  drawCenteredText("ACCESO DENEGADO", baseY, 1, ST77XX_WHITE);

  if (reason.length()) drawCenteredText(reason, baseY + 16, 1, ST77XX_WHITE);
  else drawCenteredText("Tarjeta no reconocida", baseY + 16, 1, ST77XX_WHITE);

  if (uid.length()) drawCenteredText(uid, baseY + 32, 1, ST77XX_WHITE);

  ledRedOn();

  unsigned long start = millis();
  while (millis() - start < ACCESS_SCREEN_MS) {
    delay(10);
  }

  showWaitingMessage();
}

// ----------------- Dibujar QR en la pantalla -----------------
// Dibuja QR en pantalla sin header para maximizar espacio.
// pixelBoxSize es sugerido; lo limitamos para evitar solapamiento.
void showQRCodeOnDisplay(const String &url, int pixelBoxSize) {
  using qrcodegen::QrCode;
  // genera QR con ECC LOW (enum puede chocar con macros)
  QrCode qr = QrCode::encodeText(url.c_str(), static_cast<qrcodegen::QrCode::Ecc>(0));
  int s = qr.getSize();

  // Limitar tamaño del QR para que no cubra toda la pantalla
  int screenMin = std::min(tft.width(), tft.height());
  int allowed = (screenMin * 60) / 100; // usar 60% del lado menor por defecto
  int maxBox = std::min(pixelBoxSize, allowed);

  int modulePx = maxBox / s;
  if (modulePx <= 0) modulePx = 1;
  int totalPx = modulePx * s;

  // limpiar pantalla completa (NO header) para tener todo el espacio
  tft.fillScreen(ST77XX_BLACK);

  int left = (tft.width() - totalPx) / 2;
  int top  = (tft.height() - totalPx) / 2;
  if (left < 0) left = 0;
  if (top < 0) top = 0;

  const int pad = 3;
  int bgLeft = left - pad;
  int bgTop  = top  - pad;
  int bgW    = totalPx + 2 * pad;
  int bgH    = totalPx + 2 * pad;

  if (bgLeft < 0) bgLeft = 0;
  if (bgTop < 0) bgTop = 0;
  if (bgLeft + bgW > tft.width())  bgW = tft.width() - bgLeft;
  if (bgTop  + bgH > tft.height()) bgH = tft.height() - bgTop;

  tft.fillRect(bgLeft, bgTop, bgW, bgH, ST77XX_WHITE);

  for (int y = 0; y < s; ++y) {
    for (int x = 0; x < s; ++x) {
      bool dark = qr.getModule(x, y);
      int px = left + x * modulePx;
      int py = top  + y * modulePx;
      if (dark) tft.fillRect(px, py, modulePx, modulePx, ST77XX_BLACK);
    }
  }

  // Mensaje pequeño, fuente reducida, preferiblemente arriba si espacio debajo es poco
  String hint = "Escanee el QR para registrarse";
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  int16_t x1, y1; uint16_t w, h;
  int textY;
  int spaceBelow = tft.height() - (top + totalPx);
  if (spaceBelow > 18) textY = top + totalPx + 2;
  else textY = 2;
  tft.getTextBounds(hint, 0, textY, &x1, &y1, &w, &h);
  int tx = (tft.width() - w) / 2; if (tx < 0) tx = 0;
  tft.setCursor(tx, textY);
  tft.print(hint);
}

// Mostrar banner pequeño indicando bloqueo por auto-registro (overlay sobre pantalla actual).
void showSelfRegisterBanner(const String &uid) {
  // rectángulo en la parte superior pequeño
  int h = 18;
  tft.fillRect(0, 0, tft.width(), h, ST77XX_BLACK);
  tft.drawFastHLine(0, h-1, tft.width(), ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  String t = "Registrando usuario... No pasar otra tarjeta";
  if (t.length() > 48) t = t.substring(0, 48);
  int16_t x1,y1; uint16_t w,hb;
  tft.getTextBounds(t,0,2,&x1,&y1,&w,&hb);
  int x = (tft.width()-w)/2; if (x<0) x=0;
  tft.setCursor(x, 2);
  tft.print(t);

  if (uid.length()) {
    String s = uid;
    if (s.length() > 12) s = s.substring(0,12);
    tft.setCursor(2, 2); tft.print(s);
  }
}

// ----------------- Indicador modo captura -----------------
// batch == true -> muestra "Modo: Batch" (si paused=true muestra "(PAUSADO)")
// batch == false -> muestra "Modo: Individual"
void showCaptureMode(bool batch, bool paused) {
  // dibuja un banner pequeño bajo el header (no borra todo)
  int bannerH = 18;
  int y = 22; // justo debajo del header area
  tft.fillRect(0, y, tft.width(), bannerH, ST77XX_BLACK);
  tft.drawFastHLine(0, y + bannerH - 1, tft.width(), ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  String txt;
  if (batch) {
    txt = "Modo Captura: BATCH";
    if (paused) txt += " (PAUSADO)";
    else txt += " (ACTIVO)";
  } else {
    txt = "Modo Captura: INDIVIDUAL";
  }

  // recortar si muy largo
  if (txt.length() > 48) txt = txt.substring(0, 48);
  int16_t x1,y1; uint16_t w,h;
  tft.getTextBounds(txt,0,y+2,&x1,&y1,&w,&h);
  int x = (tft.width() - w) / 2;
  if (x < 0) x = 0;
  tft.setCursor(x, y + 2);
  tft.print(txt);
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
