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
static const unsigned long TEMP_RED_MS      = 3000UL;

// Estado para restauración de pantalla
static bool   g_lastWasQR      = false;
static String g_lastQRUrl      = String();
static int    g_lastQRSize     = 0;

static bool   g_lastWasCapture   = false;
static bool   g_lastCaptureBatch = false;
static String g_lastCaptureUID   = String();

// Estado de mensajes rojos temporales
static bool          g_showTempMessage     = false;
static String        g_tempMessage         = "";
static unsigned long g_tempMessageStart    = 0;
static unsigned long g_tempMessageDuration = 0;
static bool          g_tempMessageActive   = false;

// ─────────────────────────────────────────────────────────────
// Helpers de texto: wrapping manual para pantallas pequeñas
// ─────────────────────────────────────────────────────────────

// Devuelve cuántos caracteres de 'txt' (tamaño size) caben en 'maxW' píxeles
static int charsPerLine(uint8_t size, int maxW) {
  int charW = 6 * size; // fuente Adafruit: 5px + 1px separación
  return maxW / charW;
}

// Dibuja texto con wrapping automático a partir de (x, y).
// Devuelve la Y tras la última línea escrita.
static int drawWrappedText(const String &txt, int x, int y,
                            uint8_t size, uint16_t color,
                            int maxW, int lineH) {
  tft.setTextSize(size);
  tft.setTextColor(color);

  int cpl    = charsPerLine(size, maxW - x);
  int len    = txt.length();
  int offset = 0;

  while (offset < len) {
    // Intentar ajustar en cpl caracteres, romper en espacio si es posible
    int take = min(cpl, len - offset);
    int cut  = offset + take;

    if (cut < len && txt.charAt(cut) != ' ') {
      // Buscar último espacio dentro del fragmento
      int sp = txt.lastIndexOf(' ', cut - 1);
      if (sp > offset) cut = sp;
    }

    String line = txt.substring(offset, cut);
    line.trim();

    // Centrar la línea en la pantalla
    tft.setTextSize(size);
    int16_t x1, y1; uint16_t w, h;
    tft.getTextBounds(line, 0, y, &x1, &y1, &w, &h);
    int cx = (tft.width() - (int)w) / 2;
    if (cx < x) cx = x;

    tft.setCursor(cx, y);
    tft.print(line);
    y += lineH;

    offset = cut;
    if (offset < len && txt.charAt(offset) == ' ') offset++; // saltar espacio
  }
  return y;
}

// ─────────────────────────────────────────────────────────────
// Texto centrado simple (una línea; trunca si no cabe)
// ─────────────────────────────────────────────────────────────
static void drawCenteredText(const String &txt, int y,
                              uint8_t size,
                              uint16_t color = ST77XX_WHITE) {
  tft.setTextSize(size);
  tft.setTextColor(color);

  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(txt, 0, y, &x1, &y1, &w, &h);
  int x = (tft.width() - (int)w) / 2;
  if (x < 0) x = 0;
  tft.setCursor(x, y);
  tft.print(txt);
}

// ─────────────────────────────────────────────────────────────
// Áreas básicas
// ─────────────────────────────────────────────────────────────
static void clearContentArea() {
  tft.fillRect(0, 22, tft.width(), tft.height() - 22, ST77XX_BLACK);
}

static void drawHeader() {
  tft.fillRect(0, 0, tft.width(), 22, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  drawCenteredText("CONTROL DE ACCESO", 4, 1, ST77XX_WHITE);
  tft.drawFastHLine(0, 20, tft.width(), ST77XX_WHITE);
}

// ─────────────────────────────────────────────────────────────
// Iconos
// ─────────────────────────────────────────────────────────────
static void drawCheckIcon(int cx, int cy, int r) {
  tft.fillCircle(cx, cy, r, ST77XX_GREEN);
  int x1 = cx - r / 2,  y1 = cy;
  int x2 = cx - r / 8,  y2 = cy + r / 3;
  int x3 = cx + r / 2,  y3 = cy - r / 4;
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

static void drawWaitIcon(int cx, int cy, int r) {
  uint16_t frameColor = ST77XX_WHITE;
  uint16_t sandColor  = ST77XX_YELLOW;

  int topY    = cy - r;
  int bottomY = cy + r;
  int left    = cx - r;
  int right   = cx + r;

  tft.fillRect(left - 2, topY - 2, (right - left) + 5, (bottomY - topY) + 5, ST77XX_BLACK);

  tft.drawLine(left, topY - 2, right, topY - 2, frameColor);
  tft.drawLine(left, bottomY + 2, right, bottomY + 2, frameColor);

  tft.drawTriangle(left, topY,    right, topY,    cx, cy, frameColor);
  tft.drawTriangle(left, bottomY, right, bottomY, cx, cy, frameColor);

  tft.fillTriangle(cx - r / 3, topY + r / 3,
                   cx + r / 3, topY + r / 3,
                   cx,         cy - r / 6,   sandColor);

  tft.fillTriangle(cx - r / 2, bottomY,
                   cx + r / 2, bottomY,
                   cx,         cy + r / 2,   sandColor);

  tft.drawFastVLine(cx, cy - r / 8, r / 4 + 1, frameColor);
}

// ─────────────────────────────────────────────────────────────
// Mensaje rojo temporal superpuesto
// ─────────────────────────────────────────────────────────────
static void drawTemporaryRedMessageNow(const String &msg) {
  const int wpad = 8;
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);

  // Si el mensaje no cabe en una línea, lo recortamos con "…"
  String display = msg;
  int maxTextW = tft.width() - 2 * wpad - 4;
  if ((int)w > maxTextW) {
    // Truncar por caracteres
    int cpl = charsPerLine(1, maxTextW - 6); // 6 px reservados para "…"
    display  = msg.substring(0, cpl) + "~";
    tft.getTextBounds(display, 0, 0, &x1, &y1, &w, &h);
  }

  int boxW = (int)w + 2 * wpad;
  int boxH = (int)h + 8;
  int left = (tft.width()  - boxW) / 2;
  int top  = (tft.height() - boxH) / 2;
  if (left < 0) left = 0;
  if (top  < 0) top  = 0;

  tft.fillRect(left, top, boxW, boxH, ST77XX_RED);
  tft.drawRect(left, top, boxW, boxH, ST77XX_WHITE);
  tft.setCursor(left + wpad, top + 4);
  tft.print(display);
}

// ─────────────────────────────────────────────────────────────
// Pantalla de arranque genérica
// ─────────────────────────────────────────────────────────────
void showBootScreen(const String &title, const String &subtitle, uint16_t color) {
  // Limpiar estado
  g_lastWasQR      = false; g_lastQRUrl = String(); g_lastQRSize = 0;
  g_lastWasCapture = false; g_lastCaptureBatch = false; g_lastCaptureUID = String();
  g_showTempMessage = false; g_tempMessageActive = false;

  tft.fillScreen(ST77XX_BLACK);
  drawHeader();
  clearContentArea();

  // Caja decorativa
  int boxX = 6, boxY = 26;
  int boxW = tft.width() - 12;
  int boxH = 46;

  tft.drawRoundRect(boxX, boxY, boxW, boxH, 5, color);
  tft.fillRoundRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2, 5, ST77XX_BLACK);

  // Título: wrapping dentro de la caja (margen 4 px a cada lado)
  int innerX  = boxX + 5;
  int innerW  = boxW - 10;
  int titleY  = boxY + 6;
  int lineH   = 10;

  drawWrappedText(title, innerX, titleY, 1, color, innerX + innerW, lineH);

  // Subtítulo debajo de la caja (wrapping también)
  if (subtitle.length()) {
    int subY = boxY + boxH + 4;
    drawWrappedText(subtitle, innerX, subY, 1, ST77XX_WHITE, innerX + innerW, lineH);
  }

  // Icono de espera
  int iconCX = tft.width() / 2;
  int iconCY = boxY + boxH + 30;
  if (iconCY + 12 < tft.height()) {
    drawWaitIcon(iconCX, iconCY, 10);
  }
}

// ─────────────────────────────────────────────────────────────
// Pantallas de arranque específicas
// ─────────────────────────────────────────────────────────────
void showBootWifiConnecting() {
  showBootScreen("Conectando a WiFi", "Espere hasta 60 s", ST77XX_CYAN);
}

void showBootServerConnecting() {
  showBootScreen("Conectando a Oracle", "Espere hasta 60 s", ST77XX_YELLOW);
}

void showBootConnected() {
  showBootScreen("Conectado", "Modo normal / BD prioridad", ST77XX_GREEN);
}

void showBootErrorScreen() {
  showBootScreen("Error de conexion", "Sin funcion de red", ST77XX_RED);
}

// ─────────────────────────────────────────────────────────────
// Inicialización
// ─────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────
// Pantalla principal (en espera)
// ─────────────────────────────────────────────────────────────
void showWaitingMessage() {
  g_lastWasQR      = false; g_lastQRUrl = String();
  g_lastWasCapture = false; g_lastCaptureUID = String();
  g_showTempMessage = false; g_tempMessageActive = false;

  drawHeader();
  clearContentArea();

  // Icono central de espera
  int cx = tft.width() / 2;
  int cy = tft.height() / 2 + 4;
  drawWaitIcon(cx, cy - 10, 14);

  drawCenteredText("Bienvenido", cy + 10, 1, ST77XX_WHITE);
  drawCenteredText("Acerque su tarjeta", cy + 22, 1, ST77XX_WHITE);

  ledOff();
}

// ─────────────────────────────────────────────────────────────
// Acceso concedido
// ─────────────────────────────────────────────────────────────
void showAccessGranted(const String &name, const String &materia, const String &uid) {
  g_showTempMessage = false; g_tempMessageActive = false;
  g_lastWasQR      = false;
  g_lastWasCapture = false; g_lastCaptureUID = String();

  tft.fillScreen(ST77XX_BLACK);

  int cx = tft.width() / 2;
  int cy = 30;
  int r  = std::min(tft.width(), tft.height()) / 8;
  drawCheckIcon(cx, cy, r);

  int y = cy + r + 6;
  drawCenteredText("ACCESO CONCEDIDO", y, 1, ST77XX_GREEN);
  y += 12;

  // Nombre puede ser largo: wrapping
  if (name.length()) {
    y = drawWrappedText(name, 4, y, 1, ST77XX_WHITE, tft.width() - 4, 10);
  }
  if (materia.length()) {
    y = drawWrappedText(materia, 4, y, 1, ST77XX_CYAN, tft.width() - 4, 10);
  }
  if (uid.length()) {
    drawCenteredText(uid, y, 1, ST77XX_WHITE);
  }

  ledGreenOn();

  unsigned long start = millis();
  while (millis() - start < ACCESS_SCREEN_MS) delay(10);

  showWaitingMessage();
}

// ─────────────────────────────────────────────────────────────
// Acceso denegado
// ─────────────────────────────────────────────────────────────
void showAccessDenied(const String &reason, const String &uid) {
  g_showTempMessage = false; g_tempMessageActive = false;
  g_lastWasQR      = false;
  g_lastWasCapture = false; g_lastCaptureUID = String();

  tft.fillScreen(ST77XX_BLACK);

  int cx = tft.width() / 2;
  int cy = 30;
  int r  = std::min(tft.width(), tft.height()) / 8;
  drawCrossIcon(cx, cy, r);

  int y = cy + r + 6;
  drawCenteredText("ACCESO DENEGADO", y, 1, ST77XX_RED);
  y += 12;

  String rsn = reason.length() ? reason : "Tarjeta no reconocida";
  y = drawWrappedText(rsn, 4, y, 1, ST77XX_WHITE, tft.width() - 4, 10);

  if (uid.length()) {
    drawCenteredText(uid, y + 2, 1, ST77XX_WHITE);
  }

  ledRedOn();

  unsigned long start = millis();
  while (millis() - start < ACCESS_SCREEN_MS) delay(10);

  showWaitingMessage();
}

// ─────────────────────────────────────────────────────────────
// Mostrar QR
// ─────────────────────────────────────────────────────────────
void showQRCodeOnDisplay(const String &url, int pixelBoxSize) {
  using qrcodegen::QrCode;
  QrCode qr = QrCode::encodeText(url.c_str(),
                  static_cast<qrcodegen::QrCode::Ecc>(0));
  int s = qr.getSize();

  int screenMin = std::min(tft.width(), tft.height());
  int allowed   = (screenMin * 52) / 100;
  int maxBox    = std::min(pixelBoxSize, allowed);

  int modulePx = maxBox / s;
  if (modulePx <= 0) modulePx = 1;
  int totalPx = modulePx * s;

  g_lastWasQR      = true;
  g_lastQRUrl      = url;
  g_lastQRSize     = pixelBoxSize;
  g_lastWasCapture = false;
  g_lastCaptureUID = String();
  g_showTempMessage   = false;
  g_tempMessageActive = false;

  tft.fillScreen(ST77XX_BLACK);

  // Banner superior
  showSelfRegisterBanner(String());

  int bannerH = 18;
  int availH  = tft.height() - bannerH - 20; // 20 px para textos inferiores
  int left = (tft.width() - totalPx) / 2;
  int top  = bannerH + (availH - totalPx) / 2;
  if (top < bannerH) top = bannerH;

  // Fondo blanco con padding
  const int pad = 3;
  int bgL = left - pad; if (bgL < 0) bgL = 0;
  int bgT = top  - pad; if (bgT < 0) bgT = 0;
  int bgW = totalPx + 2 * pad;
  int bgH = totalPx + 2 * pad;
  if (bgL + bgW > tft.width())  bgW = tft.width()  - bgL;
  if (bgT + bgH > tft.height()) bgH = tft.height() - bgT;

  tft.fillRect(bgL, bgT, bgW, bgH, ST77XX_WHITE);

  for (int y = 0; y < s; ++y) {
    for (int x = 0; x < s; ++x) {
      if (qr.getModule(x, y)) {
        tft.fillRect(left + x * modulePx, top + y * modulePx,
                     modulePx, modulePx, ST77XX_BLACK);
      }
    }
  }

  // Textos debajo del QR
  int textY = top + totalPx + pad + 2;
  if (textY + 18 < tft.height()) {
    drawCenteredText("Escanee para registrarse", textY,      1, ST77XX_WHITE);
    drawCenteredText("No pasar tarjeta ahora",  textY + 10, 1, ST77XX_YELLOW);
  }
}

void showSelfRegisterBanner(const String &) {
  int h = 18;
  tft.fillRect(0, 0, tft.width(), h, ST77XX_BLACK);
  tft.drawFastHLine(0, h - 1, tft.width(), ST77XX_WHITE);
  // Texto corto para que no se corte
  drawCenteredText("Registrando usuario", 3, 1, ST77XX_YELLOW);
}

// ─────────────────────────────────────────────────────────────
// Modo captura — banner
// ─────────────────────────────────────────────────────────────
void showCaptureMode(bool batch, bool paused) {
  int bannerH = 18;
  int y = 22;

  tft.fillRect(0, y, tft.width(), bannerH, ST77XX_BLACK);
  tft.drawFastHLine(0, y + bannerH - 1, tft.width(), ST77XX_WHITE);

  String txt;
  if (batch) {
    txt = paused ? "BATCH (PAUSADO)" : "BATCH (ACTIVO)";
  } else {
    txt = "CAPTURA INDIVIDUAL";
  }
  drawCenteredText(txt, y + 3, 1, ST77XX_CYAN);
}

// ─────────────────────────────────────────────────────────────
// Pantalla captura en progreso
// ─────────────────────────────────────────────────────────────
void showCaptureInProgress(bool batch, const String &uid) {
  g_lastWasCapture   = true;
  g_lastCaptureBatch = batch;
  g_lastCaptureUID   = uid;
  g_lastWasQR        = false;

  if (batch) {
    drawHeader();
    clearContentArea();
  } else {
    tft.fillRect(0, 0, tft.width(), 22, ST77XX_BLACK);
    clearContentArea();
  }

  if (batch) {
    drawCenteredText("CAPTURA EN LOTE", 28, 1, ST77XX_CYAN);

    // Instrucciones con wrapping
    drawWrappedText("Acerque varias tarjetas.", 4, 42, 1, ST77XX_WHITE,
                    tft.width() - 4, 10);
    drawWrappedText("Cada UID se pondra en cola.", 4, 54, 1, ST77XX_WHITE,
                    tft.width() - 4, 10);
  } else {
    drawCenteredText("CAPTURA INDIVIDUAL", 28, 1, ST77XX_CYAN);
    drawCenteredText("Espere al administrador", 42, 1, ST77XX_WHITE);

    int cx = tft.width() / 2;
    drawWaitIcon(cx, 72, 10);
  }

  // UID al fondo
  if (uid.length()) {
    String uu = uid.length() > 16 ? uid.substring(0, 16) : uid;
    if (uu.length() > 8) {
      String r1 = uu.substring(0, uu.length() / 2);
      String r2 = uu.substring(uu.length() / 2);
      drawCenteredText(r1, tft.height() - 18, 1, ST77XX_WHITE);
      drawCenteredText(r2, tft.height() - 8,  1, ST77XX_WHITE);
    } else {
      drawCenteredText("UID: " + uu, tft.height() - 8, 1, ST77XX_WHITE);
    }
  }

  if (batch) {
    int dots = (millis() / 400) % 4;
    String dotsStr = "";
    for (int i = 0; i < dots; ++i) dotsStr += ".";
    drawCenteredText("Esperando" + dotsStr, tft.height() - 18, 1, ST77XX_YELLOW);
  }
}

// ─────────────────────────────────────────────────────────────
// Mensajes rojos temporales
// ─────────────────────────────────────────────────────────────
void showTemporaryRedMessage(const String &msg, unsigned long durationMs) {
  if (durationMs == 0) durationMs = TEMP_RED_MS;
  if (g_tempMessageActive) return;

  g_showTempMessage     = true;
  g_tempMessage         = msg;
  g_tempMessageStart    = millis();
  g_tempMessageDuration = durationMs;
  g_tempMessageActive   = true;

  drawTemporaryRedMessageNow(msg);
}

bool isTemporaryMessageActive() {
  return g_tempMessageActive;
}

// ─────────────────────────────────────────────────────────────
// Actualización no bloqueante
// ─────────────────────────────────────────────────────────────
void updateDisplay() {
  if (g_tempMessageActive) {
    if (millis() - g_tempMessageStart >= g_tempMessageDuration) {
      g_showTempMessage   = false;
      g_tempMessageActive = false;

      if (g_lastWasQR && g_lastQRUrl.length() > 0) {
        showQRCodeOnDisplay(
          g_lastQRUrl,
          g_lastQRSize > 0 ? g_lastQRSize
                           : (std::min(tft.width(), tft.height()) * 52 / 100)
        );
      } else if (g_lastWasCapture) {
        showCaptureInProgress(g_lastCaptureBatch, g_lastCaptureUID);
        showCaptureMode(g_lastCaptureBatch, false);
      } else {
        showWaitingMessage();
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────
// Cancelar captura
// ─────────────────────────────────────────────────────────────
void cancelCaptureAndReturnToNormal() {
  g_lastWasQR         = false; g_lastQRUrl = String();
  g_lastWasCapture    = false; g_lastCaptureBatch = false; g_lastCaptureUID = String();
  g_showTempMessage   = false; g_tempMessageActive = false;
  showWaitingMessage();
}

// ─────────────────────────────────────────────────────────────
// LEDs
// ─────────────────────────────────────────────────────────────
void ledOff()     { digitalWrite(RGB_R_PIN, HIGH); digitalWrite(RGB_G_PIN, HIGH); }
void ledRedOn()   { digitalWrite(RGB_R_PIN, LOW);  digitalWrite(RGB_G_PIN, HIGH); }
void ledGreenOn() { digitalWrite(RGB_R_PIN, HIGH); digitalWrite(RGB_G_PIN, LOW);  }