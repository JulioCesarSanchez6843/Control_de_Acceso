#include "display.h"
#include "globals.h"
#include "config.h"
#include <SPIFFS.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// Duración (ms) de las pantallas de acceso concedido/denegado
static const unsigned long ACCESS_SCREEN_MS = 5000UL; // 5 segundos

// ----------------- Helpers gráficos -----------------

// Dibuja un icono de check en (cx,cy) con radio r.
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

// Dibuja un icono de cruz/tache en (cx,cy) con radio r.
static void drawCrossIcon(int cx, int cy, int r) {
  tft.fillCircle(cx, cy, r, ST77XX_RED);
  int off = r * 3 / 4;
  for (int o = -2; o <= 2; ++o) {
    tft.drawLine(cx - off + o, cy - off, cx + off + o, cy + off, ST77XX_WHITE);
    tft.drawLine(cx - off + o, cy + off, cx + off + o, cy - off, ST77XX_WHITE);
  }
}

// Dibuja texto centrado horizontalmente en la Y especificada.
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

// Limpia el área principal de contenido manteniendo la cabecera.
static void clearContentArea() {
  tft.fillRect(0, 22, tft.width(), tft.height() - 22, ST77XX_BLACK);
}

// Dibuja la cabecera superior (título y línea separadora).
static void drawHeader() {
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.fillRect(0, 0, tft.width(), 22, ST77XX_BLACK);
  drawCenteredText("CONTROL DE ACCESO LAB", 4, 1, ST77XX_WHITE);
  tft.drawFastHLine(0, 20, tft.width(), ST77XX_WHITE);
}

// ----------------- API pública -----------------

// Inicializa la TFT, pines RGB y muestra la pantalla de espera.
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

// Muestra la pantalla de bienvenida y el mensaje "Esperando tarjeta...".
void showWaitingMessage() {
  drawHeader();
  clearContentArea();
  drawCenteredText("Bienvenido", 28, 2, ST77XX_WHITE);
  drawCenteredText("Esperando tarjeta...", 60, 1, ST77XX_WHITE);
  ledOff();
}

// Muestra pantalla de acceso concedido con icono, datos y LED verde.
// Después del tiempo definido vuelve a la pantalla de espera.
void showAccessGranted(const String &name, const String &materia, const String &uid) {
  tft.fillScreen(ST77XX_BLACK);

  int cx = tft.width() / 2;
  int cy = 36;
  int r = min(tft.width(), tft.height()) / 6;
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

// Muestra pantalla de acceso denegado con icono, razón y LED rojo.
// Después del tiempo definido vuelve a la pantalla de espera.
void showAccessDenied(const String &reason, const String &uid) {
  tft.fillScreen(ST77XX_BLACK);

  int cx = tft.width() / 2;
  int cy = 36;
  int r = min(tft.width(), tft.height()) / 6;
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

// ----------------- LEDs -----------------

// Apaga ambos LEDs RGB (estado alto = apagado en hardware común).
void ledOff() {
  digitalWrite(RGB_R_PIN, HIGH);
  digitalWrite(RGB_G_PIN, HIGH);
}

// Enciende LED rojo (verde apagado).
void ledRedOn() {
  digitalWrite(RGB_R_PIN, LOW);
  digitalWrite(RGB_G_PIN, HIGH);
}

// Enciende LED verde (rojo apagado).
void ledGreenOn() {
  digitalWrite(RGB_R_PIN, HIGH);
  digitalWrite(RGB_G_PIN, LOW);
}
