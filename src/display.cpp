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

// Dibuja texto centrado horizontalmente en la Y especificada.
// size corresponde a setTextSize (1..n). Mantener tamaño pequeño para QR-screen.
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
  // letras un poco más pequeñas que antes para dar aire
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
// Cambios principales:
//  - NO dibujar header (quitamos "CONTROL DE ACCESO LAB") para ganar espacio.
//  - Ajustes de tamaño y posicionamiento para evitar que el texto quede encima.
void showQRCodeOnDisplay(const String &url, int pixelBoxSize) {
  using qrcodegen::QrCode;

  // Generar QR (usamos valor entero 0 para ECC LOW si el enum choca con macros)
  QrCode qr = QrCode::encodeText(url.c_str(), static_cast<qrcodegen::QrCode::Ecc>(0));
  int s = qr.getSize(); // módulos por lado

  // Calcular escala (tam módulo en px)
  int maxBox = pixelBoxSize;
  // si pixelBoxSize es mayor que pantalla, ajustamos al 80% del menor lado
  int screenMin = std::min(tft.width(), tft.height());
  if (maxBox > screenMin) {
    maxBox = (screenMin * 80) / 100; // 80% del menor lado
  }

  int modulePx = maxBox / s;
  if (modulePx <= 0) modulePx = 1;
  int totalPx = modulePx * s;

  // limpiar pantalla completa (NO header) para tener todo el espacio
  tft.fillScreen(ST77XX_BLACK);

  // Centrar el QR en toda la pantalla (más espacio al top porque quitamos header)
  int left = (tft.width() - totalPx) / 2;
  int top  = (tft.height() - totalPx) / 2;
  if (left < 0) left = 0;
  if (top < 0) top = 0;

  // Fondo blanco para el QR con pequeño padding
  const int pad = 3;
  int bgLeft = left - pad;
  int bgTop  = top  - pad;
  int bgW    = totalPx + 2 * pad;
  int bgH    = totalPx + 2 * pad;

  // Asegurar que el fondo no sobresalga de la pantalla
  if (bgLeft < 0) { bgLeft = 0; }
  if (bgTop  < 0) { bgTop = 0; }
  if (bgLeft + bgW > tft.width())  bgW = tft.width() - bgLeft;
  if (bgTop  + bgH > tft.height()) bgH = tft.height() - bgTop;

  tft.fillRect(bgLeft, bgTop, bgW, bgH, ST77XX_WHITE);

  // dibujar módulos (negros sobre fondo blanco)
  for (int y = 0; y < s; ++y) {
    for (int x = 0; x < s; ++x) {
      bool dark = qr.getModule(x, y);
      int px = left + x * modulePx;
      int py = top  + y * modulePx;
      if (dark) tft.fillRect(px, py, modulePx, modulePx, ST77XX_BLACK);
    }
  }

  // Texto indicativo: usamos texto más pequeño y lo colocamos abajo, centrado
  // para evitar solapamiento con el QR.
  String hint = "Escanee el QR para registrarse";
  // Si queda poco espacio debajo, colocamos texto arriba.
  int spaceBelow = tft.height() - (top + totalPx);
  int textY;
  if (spaceBelow > 18) {
    textY = top + totalPx + 4; // texto debajo del QR
  } else {
    // poco espacio, colocarlo en la parte superior con margen
    textY = 4;
  }

  tft.setTextSize(1); // tamaño pequeño para que quepa
  tft.setTextColor(ST77XX_WHITE);
  // imprimir centrado manualmente
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(hint, 0, textY, &x1, &y1, &w, &h);
  int tx = (tft.width() - w) / 2;
  if (tx < 0) tx = 0;
  tft.setCursor(tx, textY);
  tft.print(hint);

  // NOTA: No bloqueamos aquí — quien muestra el QR debe gestionar awaiting flags.
}

// Mostrar banner pequeño informando bloqueo por auto-registro (overlay sobre pantalla actual).
void showSelfRegisterBanner(const String &uid) {
  // dibujar rectángulo pequeño en la parte superior (sin borrar QR)
  int h = 20;
  tft.fillRect(0, 0, tft.width(), h, ST77XX_BLACK);
  tft.drawFastHLine(0, h-1, tft.width(), ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  String t = "Usuario registrando... No leer nuevas tarjetas";
  // recortar si muy largo
  if (t.length() > 40) t = t.substring(0, 40);
  int16_t x1,y1; uint16_t w,hb;
  tft.getTextBounds(t,0,2,&x1,&y1,&w,&hb);
  int x = (tft.width()-w)/2; if (x<0) x=0;
  tft.setCursor(x, 2);
  tft.print(t);

  // si se proporciona UID mostrarlo a la derecha pequeño
  if (uid.length()) {
    String s = uid;
    if (s.length() > 12) s = s.substring(0,12);
    tft.setCursor(2, 2); tft.print(s);
  }
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
