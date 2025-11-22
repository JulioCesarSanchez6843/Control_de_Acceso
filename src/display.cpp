// src/display.cpp
#include "display.h"
#include "globals.h"
#include "config.h"

#include <SPIFFS.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <algorithm> // std::min

// Guardar y quitar macros problemáticas antes de incluir qrcodegen.hpp
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

static const unsigned long ACCESS_SCREEN_MS = 4000UL; // 4s
static const unsigned long TEMP_RED_MS = 3000UL;      // 3s para mensajes rojos

// Estado interno para restauración de pantalla tras mensajes temporales
static bool g_lastWasQR = false;
static String g_lastQRUrl = String();
static int g_lastQRSize = 0;

static bool g_lastWasCapture = false;
static bool g_lastCaptureBatch = false;
static String g_lastCaptureUID = String();

// Estado para mensajes temporales no bloqueantes
static bool g_showTempMessage = false;
static String g_tempMessage = "";
static unsigned long g_tempMessageStart = 0;
static unsigned long g_tempMessageDuration = 0;
static bool g_tempMessageActive = false;

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
  drawCenteredText("CONTROL DE ACCESO", 4, 1, ST77XX_WHITE);
  tft.drawFastHLine(0, 20, tft.width(), ST77XX_WHITE);
}

// Función auxiliar para dibujar el mensaje rojo
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
  // actualizar estado
  g_lastWasQR = false;
  g_lastQRUrl = String();
  g_lastWasCapture = false;
  g_lastCaptureUID = String();
  g_showTempMessage = false;
  g_tempMessageActive = false;

  drawHeader();
  clearContentArea();
  drawCenteredText("Bienvenido", 28, 1, ST77XX_WHITE);
  drawCenteredText("Esperando tarjeta...", 56, 1, ST77XX_WHITE);
  ledOff();
}

void showAccessGranted(const String &name, const String &materia, const String &uid) {
  // limpiar estado activo (no QR ni captura)
  g_lastWasQR = false;
  g_lastWasCapture = false;
  g_lastCaptureUID = String();
  g_showTempMessage = false;
  g_tempMessageActive = false;

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
  // limpiar estado activo
  g_lastWasQR = false;
  g_lastWasCapture = false;
  g_lastCaptureUID = String();
  g_showTempMessage = false;
  g_tempMessageActive = false;

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
  QrCode qr = QrCode::encodeText(url.c_str(), static_cast<qrcodegen::QrCode::Ecc>(0));
  int s = qr.getSize();

  // Hacemos el QR un poco más pequeño para evitar solapamientos: usar el 52% del lado menor
  int screenMin = std::min(tft.width(), tft.height());
  int allowed = (screenMin * 52) / 100; // 52% del lado menor
  int maxBox = std::min(pixelBoxSize, allowed);

  int modulePx = maxBox / s;
  if (modulePx <= 0) modulePx = 1;
  int totalPx = modulePx * s;

  // marcar estado QR activo (para restauración luego)
  g_lastWasQR = true;
  g_lastQRUrl = url;
  g_lastQRSize = pixelBoxSize;
  g_lastWasCapture = false;
  g_lastCaptureUID = String();
  g_showTempMessage = false;
  g_tempMessageActive = false;

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

  // Mensaje más pequeño y colocado para no sobreponer el QR
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  String hint = "Escanee el QR para registrarse";
  int16_t x1, y1; uint16_t w, h;
  int spaceBelow = tft.height() - (top + totalPx);
  int textY;
  if (spaceBelow > 28) textY = top + totalPx + 2;
  else textY = 2;

  tft.getTextBounds(hint, 0, textY, &x1, &y1, &w, &h);
  int tx = (tft.width() - w) / 2; if (tx < 0) tx = 0;
  tft.setCursor(tx, textY);
  tft.print(hint);

  // Mostrar info adicional (titulo) en lugar del UID para evitar sobreimpresiones
  String title = "Registrando nuevo usuario";
  int titleY = textY + 14;
  if (titleY + 12 > tft.height()) titleY = textY - 12;
  drawCenteredText(title, titleY, 1, ST77XX_WHITE);

  // Mostrar banner superior pequeño para instrucción (no borra QR)
  showSelfRegisterBanner(String());
}

// Mostrar banner pequeño indicando bloqueo por auto-registro (overlay sobre pantalla actual).
// No imprime UID para evitar superposiciones sobre el QR.
void showSelfRegisterBanner(const String & /*uid_unused_for_qr*/) {
  int h = 18;
  tft.fillRect(0, 0, tft.width(), h, ST77XX_BLACK);
  tft.drawFastHLine(0, h-1, tft.width(), ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  String t = "Registrando nuevo usuario... No pasar tarjeta";
  if (t.length() > 48) t = t.substring(0, 48);
  int16_t x1,y1; uint16_t w,hb;
  tft.getTextBounds(t,0,2,&x1,&y1,&w,&hb);
  int x = (tft.width()-w)/2; if (x<0) x=0;
  tft.setCursor(x, 2);
  tft.print(t);
}

// ----------------- Indicador modo captura -----------------
void showCaptureMode(bool batch, bool paused) {
  int bannerH = 18;
  int y = 22;
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

  if (txt.length() > 48) txt = txt.substring(0, 48);
  int16_t x1,y1; uint16_t w,h;
  tft.getTextBounds(txt,0,y+2,&x1,&y1,&w,&h);
  int x = (tft.width() - w) / 2;
  if (x < 0) x = 0;
  tft.setCursor(x, y + 2);
  tft.print(txt);
}

// ----------------- Pantalla: captura en progreso -----------------
void showCaptureInProgress(bool batch, const String &uid) {
  // actualizar estado
  g_lastWasCapture = true;
  g_lastCaptureBatch = batch;
  g_lastCaptureUID = uid;
  g_lastWasQR = false;
  g_lastQRUrl = String();
  g_showTempMessage = false;
  g_tempMessageActive = false;

  drawHeader();
  clearContentArea();

  if (batch) {
    drawCenteredText("CAPTURA EN LOTE", 28, 2, ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    int left = 8;
    int y = 56;
    tft.setCursor(left, y);
    tft.print("Acerca varias tarjetas. Cada UID se pondrÃ¡ en la cola.");
    tft.setCursor(left, y + 14);
    tft.print("Revise la app / web para ver la lista y terminar.");
  } else {
    drawCenteredText("CAPTURA INDIVIDUAL", 28, 2, ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    int left = 8;
    int y = 56;
    tft.setCursor(left, y);
    tft.print("Acerca una tarjeta para generar el formulario.");
    tft.setCursor(left, y + 14);
    tft.print("Complete los datos en la web.");
  }

  // Mostrar UID en proceso (si se dio) en renglones cortos
  if (uid.length()) {
    String uu = uid;
    if (uu.length() > 16) uu = uu.substring(0, 16);
    if (uu.length() > 8) {
      String r1 = uu.substring(0, uu.length()/2);
      String r2 = uu.substring(uu.length()/2);
      drawCenteredText(r1, tft.height() - 44, 1, ST77XX_WHITE);
      drawCenteredText(r2, tft.height() - 32, 1, ST77XX_WHITE);
    } else {
      drawCenteredText("UID: " + uu, tft.height() - 38, 1, ST77XX_WHITE);
    }
  }

  unsigned long m = millis();
  int dots = (m / 400) % 4; // 0..3
  String dotsStr = "";
  for (int i = 0; i < dots; ++i) dotsStr += ".";
  String waiting = "Esperando tarjeta" + dotsStr;
  drawCenteredText(waiting, tft.height() - 18, 1, ST77XX_WHITE);
}

// ----------------- Mensajes temporales (rojo) NO BLOQUEANTE -----------------
void showTemporaryRedMessage(const String &msg, unsigned long durationMs) {
  if (durationMs == 0) durationMs = TEMP_RED_MS;
  
  // Configurar el mensaje temporal
  g_showTempMessage = true;
  g_tempMessage = msg;
  g_tempMessageStart = millis();
  g_tempMessageDuration = durationMs;
  g_tempMessageActive = true;
  
  // Dibujar el mensaje inmediatamente
  drawTemporaryRedMessageNow(msg);
}

// ----------------- Función de actualización no bloqueante -----------------
void updateDisplay() {
  // Manejar mensajes temporales
  if (g_showTempMessage && g_tempMessageActive) {
    unsigned long currentTime = millis();
    if (currentTime - g_tempMessageStart >= g_tempMessageDuration) {
      g_showTempMessage = false;
      g_tempMessageActive = false;
      
      // Restaurar pantalla previa según estado guardado
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

// ----------------- Cancelar captura y volver a normal -----------------
void cancelCaptureAndReturnToNormal() {
  // Limpiar todos los estados de captura y QR
  g_lastWasQR = false;
  g_lastQRUrl = String();
  g_lastWasCapture = false;
  g_lastCaptureBatch = false;
  g_lastCaptureUID = String();
  g_showTempMessage = false;
  g_tempMessageActive = false;
  
  // Volver directamente a la pantalla de bienvenido
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