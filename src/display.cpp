// src/display.cpp
#include "display.h"
#include "globals.h"
#include "config.h"

#include <SPIFFS.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <algorithm>

#pragma push_macro("LOW")
#pragma push_macro("HIGH")
#ifdef LOW
  #undef LOW
#endif
#ifdef HIGH
  #undef HIGH
#endif

#include "qrcodegen.hpp"

#pragma pop_macro("HIGH")
#pragma pop_macro("LOW")

// Duraciones de pantallas
static const unsigned long ACCESS_SCREEN_MS = 4000UL;
static const unsigned long TEMP_RED_MS = 3000UL;

// Estado para restauración de pantalla
static bool g_lastWasQR = false;
static String g_lastQRUrl = String();
static int g_lastQRSize = 0;

static bool g_lastWasCapture = false;
static bool g_lastCaptureBatch = false;
static String g_lastCaptureUID = String();

// Estado de mensajes rojos temporales
static bool g_showTempMessage = false;
static String g_tempMessage = "";
static unsigned long g_tempMessageStart = 0;
static unsigned long g_tempMessageDuration = 0;
static bool g_tempMessageActive = false;

// ---------------------------------------------------------
// Funciones de dibujo básicas
// ---------------------------------------------------------
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

// Reloj de arena dibujado con dos triángulos unidos (estilo icono enviado)
static void drawWaitIcon(int cx, int cy, int r) {
  // r controla el "radio" aproximado; ajustamos puntos relativos
  uint16_t frameColor = ST77XX_WHITE;
  uint16_t sandColor  = ST77XX_YELLOW;
  uint16_t glassColor = ST77XX_BLUE; // opcional para relleno del vidrio si se quisiera

  // coordenadas principales
  int topY = cy - r;       // línea superior del triángulo superior
  int bottomY = cy + r;    // línea inferior del triángulo inferior
  int left = cx - r;
  int right = cx + r;
  int centerX = cx;
  int centerY = cy;

  // limpiar área del icono (evita solapamientos)
  tft.fillRect(left - 2, topY - 2, (right - left) + 5, (bottomY - topY) + 5, ST77XX_BLACK);

  // Dibujar líneas de marco superior e inferior (como en la imagen)
  tft.drawLine(left, topY - 2, right, topY - 2, frameColor);     // barra superior
  tft.drawLine(left, bottomY + 2, right, bottomY + 2, frameColor); // barra inferior

  // Dibujar triángulos (contorno)
  tft.drawTriangle(left, topY, right, topY, centerX, centerY, frameColor);     // triángulo superior (vértices: left-top, right-top, center)
  tft.drawTriangle(left, bottomY, right, bottomY, centerX, centerY, frameColor); // triángulo inferior (vértices: left-bottom, right-bottom, center)

  // Relleno "vidrio" opcional muy sutil (comentar si no se quiere)
  // tft.fillTriangle(left+1, topY+1, right-1, topY+1, centerX, centerY-1, glassColor);
  // tft.fillTriangle(left+1, bottomY-1, right-1, bottomY-1, centerX, centerY+1, glassColor);

  // Rellenar "arena" superior (pequeña porción en triángulo superior)
  int sandTopLeftX = cx - r/3;
  int sandTopRightX = cx + r/3;
  int sandTopY = topY + (r/3);
  tft.fillTriangle(sandTopLeftX, sandTopY, sandTopRightX, sandTopY, centerX, centerY - (r/6), sandColor);

  // Rellenar "arena" acumulada abajo
  int sandBotLeftX = cx - r/2;
  int sandBotRightX = cx + r/2;
  int sandBotY = bottomY;
  tft.fillTriangle(sandBotLeftX, sandBotY, sandBotRightX, sandBotY, centerX, centerY + (r/2), sandColor);

  // Línea central fina que sugiere paso de arena
  tft.drawFastVLine(centerX, centerY - (r/8), (r/4) + 1, frameColor);
}

// --------------------------------------------------------------------------------
// resto del archivo (sin cambios de lógica salvo posicionamiento del icono/UID)
// --------------------------------------------------------------------------------

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
  drawCenteredText("CONTROL DE ACCESO", 4, 1, ST77XX_WHITE);
  tft.drawFastHLine(0, 20, tft.width(), ST77XX_WHITE);
}

// Dibuja mensaje rojo temporal
static void drawTemporaryRedMessageNow(const String &msg) {
  int wpad = 8;
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  int16_t x1,y1; uint16_t w,h;
  tft.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  int boxW = w + 2*wpad;
  int boxH = h + 8;
  int left = (tft.width() - boxW) / 2;
  int top = (tft.height() - boxH) / 2;

  if (left < 0) left = 0;
  if (top < 0) top = 0;

  tft.fillRect(left, top, boxW, boxH, ST77XX_RED);
  tft.drawRect(left, top, boxW, boxH, ST77XX_WHITE);
  tft.setCursor(left + wpad, top + 4);
  tft.print(msg);
}

// ---------------------------------------------------------
// Inicialización
// ---------------------------------------------------------
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

// Pantalla principal
void showWaitingMessage() {
  g_lastWasQR = false;
  g_lastQRUrl = String();
  g_lastWasCapture = false;
  g_lastCaptureUID = String();

  g_showTempMessage = false;
  g_tempMessageActive = false;

  drawHeader();
  clearContentArea();
  drawCenteredText("Bienvenido", 28, 1);
  drawCenteredText("Esperando tarjeta...", 56, 1);
  ledOff();
}

// ---------------------------------------------------------
// Acceso concedido
// ---------------------------------------------------------
void showAccessGranted(const String &name, const String &materia, const String &uid) {
  g_showTempMessage = false;
  g_tempMessageActive = false;

  g_lastWasQR = false;
  g_lastWasCapture = false;
  g_lastCaptureUID = String();

  tft.fillScreen(ST77XX_BLACK);

  int cx = tft.width() / 2;
  int cy = 34;
  int r = std::min(tft.width(), tft.height()) / 7;
  drawCheckIcon(cx, cy, r);

  int baseY = cy + r + 4;
  drawCenteredText("ACCESO CONCEDIDO", baseY, 1);

  if (name.length()) drawCenteredText(name, baseY + 16, 1);
  if (materia.length()) drawCenteredText(materia, baseY + 28, 1);
  if (uid.length()) drawCenteredText(uid, baseY + 40, 1);

  ledGreenOn();

  unsigned long start = millis();
  while (millis() - start < ACCESS_SCREEN_MS) delay(10);

  showWaitingMessage();
}

// ---------------------------------------------------------
// Acceso denegado
// ---------------------------------------------------------
void showAccessDenied(const String &reason, const String &uid) {
  g_showTempMessage = false;
  g_tempMessageActive = false;

  g_lastWasQR = false;
  g_lastWasCapture = false;
  g_lastCaptureUID = String();

  tft.fillScreen(ST77XX_BLACK);

  int cx = tft.width() / 2;
  int cy = 34;
  int r = std::min(tft.width(), tft.height()) / 7;
  drawCrossIcon(cx, cy, r);

  int baseY = cy + r + 4;
  drawCenteredText("ACCESO DENEGADO", baseY, 1);

  if (reason.length()) drawCenteredText(reason, baseY + 16, 1);
  else drawCenteredText("Tarjeta no reconocida", baseY + 16, 1);

  if (uid.length()) drawCenteredText(uid, baseY + 32, 1);

  ledRedOn();

  unsigned long start = millis();
  while (millis() - start < ACCESS_SCREEN_MS) delay(10);

  showWaitingMessage();
}

// ---------------------------------------------------------
// Mostrar QR
// ---------------------------------------------------------
void showQRCodeOnDisplay(const String &url, int pixelBoxSize) {
  using qrcodegen::QrCode;
  QrCode qr = QrCode::encodeText(url.c_str(), static_cast<qrcodegen::QrCode::Ecc>(0));
  int s = qr.getSize();

  int screenMin = std::min(tft.width(), tft.height());
  int allowed = (screenMin * 52) / 100;
  int maxBox = std::min(pixelBoxSize, allowed);

  int modulePx = maxBox / s;
  if (modulePx <= 0) modulePx = 1;
  int totalPx = modulePx * s;

  g_lastWasQR = true;
  g_lastQRUrl = url;
  g_lastQRSize = pixelBoxSize;
  g_lastWasCapture = false;
  g_lastCaptureUID = String();

  g_showTempMessage = false;
  g_tempMessageActive = false;

  tft.fillScreen(ST77XX_BLACK);

  int left = (tft.width() - totalPx) / 2;
  int top  = (tft.height() - totalPx) / 2;

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
      if (qr.getModule(x, y)) {
        int px = left + x * modulePx;
        int py = top  + y * modulePx;
        tft.fillRect(px, py, modulePx, modulePx, ST77XX_BLACK);
      }
    }
  }

  String hint = "Escanee el QR para registrarse";
  int textY = top + totalPx + 2;
  drawCenteredText(hint, textY, 1);

  String title = "Registrando nuevo usuario";
  drawCenteredText(title, textY + 14, 1);

  showSelfRegisterBanner(String());
}

void showSelfRegisterBanner(const String &) {
  int h = 18;
  tft.fillRect(0, 0, tft.width(), h, ST77XX_BLACK);
  tft.drawFastHLine(0, h-1, tft.width(), ST77XX_WHITE);
  drawCenteredText("Registrando nuevo usuario... No pasar tarjeta", 2, 1);
}

// ---------------------------------------------------------
// Modo captura
// ---------------------------------------------------------
void showCaptureMode(bool batch, bool paused) {
  int bannerH = 18;
  int y = 22;

  tft.fillRect(0, y, tft.width(), bannerH, ST77XX_BLACK);
  tft.drawFastHLine(0, y + bannerH - 1, tft.width(), ST77XX_WHITE);

  String txt;
  if (batch) {
    txt = paused ? "Modo Captura: BATCH (PAUSADO)" : "Modo Captura: BATCH (ACTIVO)";
  } else {
    txt = "Modo Captura: INDIVIDUAL";
  }

  drawCenteredText(txt, y + 2, 1);
}

// ---------------------------------------------------------
// Pantalla de captura en progreso
// ---------------------------------------------------------
void showCaptureInProgress(bool batch, const String &uid) {
  g_lastWasCapture = true;
  g_lastCaptureBatch = batch;
  g_lastCaptureUID = uid;
  g_lastWasQR = false;

  // Para BATCH mantenemos encabezado; para INDIVIDUAL quitamos encabezado para ganar espacio
  if (batch) {
    drawHeader();
    clearContentArea();
  } else {
    // limpiar explícitamente el encabezado y el contenido para evitar solape
    tft.fillRect(0, 0, tft.width(), 22, ST77XX_BLACK);
    clearContentArea();
  }

  if (batch) {
    drawCenteredText("CAPTURA EN LOTE", 28, 2);
    tft.setCursor(8, 56);
    tft.print("Acerca varias tarjetas.");
    tft.setCursor(8, 70);
    tft.print("Cada UID se pondra en cola.");
  } else {
    // Texto resumido para modo individual, ubicado más arriba para evitar solape
    drawCenteredText("MODO CAPTURA INDIVIDUAL", 28, 1);
    drawCenteredText("Espere al administrador", 44, 1);

    // Ícono (reloj de arena de dos triángulos) en la posición entre texto y UID
    int cx = tft.width() / 2;
    int icon_cy = (88 + (tft.height() - 52)) / 2; // posición entre texto y área de UID
    int r = 12; // tamaño del icono
    drawWaitIcon(cx, icon_cy, r);
  }

  // Mostrar UID en proceso (si se dio) en la parte más baja de la pantalla
  if (uid.length()) {
    String uu = uid;
    if (uu.length() > 16) uu = uu.substring(0, 16);
    if (uu.length() > 8) {
      String r1 = uu.substring(0, uu.length()/2);
      String r2 = uu.substring(uu.length()/2);
      // dos líneas colocadas muy abajo
      drawCenteredText(r1, tft.height() - 18, 1);
      drawCenteredText(r2, tft.height() - 8, 1);
    } else {
      // una línea colocada muy abajo
      drawCenteredText("UID: " + uu, tft.height() - 8, 1);
    }
  }

  // Mantener animación "Esperando tarjeta..." solo para BATCH
  if (batch) {
    unsigned long m = millis();
    int dots = (m / 400) % 4;
    String dotsStr = "";
    for (int i = 0; i < dots; ++i) dotsStr += ".";
    drawCenteredText("Esperando tarjeta" + dotsStr, tft.height() - 18, 1);
  }
}

// ---------------------------------------------------------
// Mensajes rojos temporales
// ---------------------------------------------------------
void showTemporaryRedMessage(const String &msg, unsigned long durationMs) {
  if (durationMs == 0) durationMs = TEMP_RED_MS;
  if (g_tempMessageActive) return;

  g_showTempMessage = true;
  g_tempMessage = msg;
  g_tempMessageStart = millis();
  g_tempMessageDuration = durationMs;
  g_tempMessageActive = true;

  drawTemporaryRedMessageNow(msg);
}

bool isTemporaryMessageActive() {
  return g_tempMessageActive;
}

// ---------------------------------------------------------
// Actualización de pantalla
// ---------------------------------------------------------
void updateDisplay() {
  if (g_tempMessageActive) {
    if (millis() - g_tempMessageStart >= g_tempMessageDuration) {
      g_showTempMessage = false;
      g_tempMessageActive = false;

      if (g_lastWasQR && g_lastQRUrl.length() > 0) {
        showQRCodeOnDisplay(g_lastQRUrl, g_lastQRSize > 0 ? g_lastQRSize : (std::min(tft.width(), tft.height())*52/100));
      } else if (g_lastWasCapture) {
        showCaptureInProgress(g_lastCaptureBatch, g_lastCaptureUID);
        showCaptureMode(g_lastCaptureBatch, false);
      } else {
        showWaitingMessage();
      }
    }
  }
}

// ---------------------------------------------------------
// Cancelar captura
// ---------------------------------------------------------
void cancelCaptureAndReturnToNormal() {
  g_lastWasQR = false;
  g_lastQRUrl = String();
  g_lastWasCapture = false;
  g_lastCaptureBatch = false;
  g_lastCaptureUID = String();
  g_showTempMessage = false;
  g_tempMessageActive = false;

  showWaitingMessage();
}

// ---------------------------------------------------------
// LEDs
// ---------------------------------------------------------
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
