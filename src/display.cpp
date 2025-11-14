// src/display.cpp
// Todo lo relativo a la TFT: dibujos, iconos, textos y tiempos.
// Requiere que en display.h exista: extern Adafruit_ST7735 tft;

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
static void drawCheckIcon(int cx, int cy, int r) {
  // fondo circular verde
  tft.fillCircle(cx, cy, r, ST77XX_GREEN);

  // palomita: dos segmentos con grosor
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
  // fondo circular rojo
  tft.fillCircle(cx, cy, r, ST77XX_RED);

  int off = r * 3 / 4;
  for (int o = -2; o <= 2; ++o) {
    tft.drawLine(cx - off + o, cy - off, cx + off + o, cy + off, ST77XX_WHITE);
    tft.drawLine(cx - off + o, cy + off, cx + off + o, cy - off, ST77XX_WHITE);
  }
}

// Centrar texto horizontalmente en Y dado
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

// Limpia el área de contenido (deja la cabecera intacta)
static void clearContentArea() {
  // Reservamos 22px para cabecera
  tft.fillRect(0, 22, tft.width(), tft.height() - 22, ST77XX_BLACK);
}

// Dibuja cabecera (titular y línea)
static void drawHeader() {
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  // limpiar todo header
  tft.fillRect(0, 0, tft.width(), 22, ST77XX_BLACK);
  drawCenteredText("CONTROL DE ACCESO LAB", 4, 1, ST77XX_WHITE);
  tft.drawFastHLine(0, 20, tft.width(), ST77XX_WHITE);
}

// ----------------- API pública -----------------
void displayInit() {
  // Inicializa TFT y dibuja pantalla de bienvenida
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  pinMode(RGB_R_PIN, OUTPUT);
  pinMode(RGB_G_PIN, OUTPUT);

  drawHeader();
  ledOff();

  // Mostrar pantalla de bienvenida al arrancar
  showWaitingMessage();
}

// Bienvenida + "Esperando tarjeta..."
void showWaitingMessage() {
  // Limpiar contenido y redibujar cabecera para garantizar no overlays
  drawHeader();
  clearContentArea();

  // Texto "Bienvenido" y "Esperando tarjeta..."
  drawCenteredText("Bienvenido", 28, 2, ST77XX_WHITE);
  drawCenteredText("Esperando tarjeta...", 60, 1, ST77XX_WHITE);

  ledOff();
}

// Acceso concedido: icono, texto (título más pequeño para evitar overlap), y volver
void showAccessGranted(const String &name, const String &materia, const String &uid) {
  // Dibujo limpio
  tft.fillScreen(ST77XX_BLACK);

  // Icono
  int cx = tft.width() / 2;
  int cy = 36;
  int r = min(tft.width(), tft.height()) / 6;
  drawCheckIcon(cx, cy, r);

  // Mensajes: título más pequeño (size 1) para evitar solapamiento
  int baseY = cy + r + 6;
  drawCenteredText("ACCESO CONCEDIDO", baseY, 1, ST77XX_WHITE);

  // Nombre / materia / uid con tamaños moderados (más pequeños para evitar encimar)
  if (name.length()) drawCenteredText(name, baseY + 18, 1, ST77XX_WHITE);
  if (materia.length()) drawCenteredText(materia, baseY + 30, 1, ST77XX_WHITE);
  if (uid.length()) drawCenteredText(uid, baseY + 42, 1, ST77XX_WHITE);

  // LED verde
  ledGreenOn();

  // Mantener pantalla el tiempo definido
  unsigned long start = millis();
  while (millis() - start < ACCESS_SCREEN_MS) {
    delay(10);
  }

  // Volver a pantalla de bienvenida (completamente limpia)
  showWaitingMessage();
}

// Acceso denegado: icono tache, título más pequeño y volver
void showAccessDenied(const String &reason, const String &uid) {
  // Dibujo limpio
  tft.fillScreen(ST77XX_BLACK);

  // Icono
  int cx = tft.width() / 2;
  int cy = 36;
  int r = min(tft.width(), tft.height()) / 6;
  drawCrossIcon(cx, cy, r);

  int baseY = cy + r + 6;
  drawCenteredText("ACCESO DENEGADO", baseY, 1, ST77XX_WHITE);

  // Mensaje breve (si no hay reason mostrar default corto)
  if (reason.length()) drawCenteredText(reason, baseY + 18, 1, ST77XX_WHITE);
  else drawCenteredText("Tarjeta no reconocida", baseY + 18, 1, ST77XX_WHITE);

  if (uid.length()) drawCenteredText(uid, baseY + 36, 1, ST77XX_WHITE);

  ledRedOn();

  unsigned long start = millis();
  while (millis() - start < ACCESS_SCREEN_MS) {
    delay(10);
  }

  // Volver a pantalla de bienvenida
  showWaitingMessage();
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
