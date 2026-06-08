// src/main.cpp
// ============================================================
// MODO: siempre ONLINE
// SPIFFS = almacenamiento primario rápido + respaldo de BD
// Oracle = réplica/backup asíncrona (envío inmediato o diferido)
// No existe modoLocal: si la BD no responde, los datos
// quedan en SPIFFS y se sincronizan en cuanto vuelve.
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <time.h>
#include <sys/time.h>

#if defined(ARDUINO_ARCH_ESP32)
  #include <ESP32Servo.h>
#else
  #include <Servo.h>
#endif

#include <ESPmDNS.h>

#include "config.h"
#include "globals.h"
#include "display.h"
#include "files_utils.h"
#include "rfid_handler.h"
#include "web/web_routes.h"
#include "db_sync.h"

// ============================================================
// Tiempos de espera
// ============================================================
static const unsigned long WIFI_TIMEOUT_MS = 60UL * 1000UL;
static const unsigned long NTP_TIMEOUT_MS  = 30UL * 1000UL;
static const unsigned long NTP_POLL_MS     = 500UL;

// ============================================================
// Helpers de pantalla de arranque (texto que no se corta)
// ============================================================

// Calcula cuántos caracteres caben en maxW píxeles con fuente size
static int _bootCharsPerLine(uint8_t size, int maxW) {
  return maxW / (6 * size);
}

// Dibuja texto centrado con wrapping; devuelve la Y final
static int _bootDrawWrapped(const String &txt, int y,
                             uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color);

  const int margin = 6;
  int cpl    = _bootCharsPerLine(size, tft.width() - 2 * margin);
  int lineH  = 9 * size;
  int len    = txt.length();
  int offset = 0;

  while (offset < len) {
    int take = min(cpl, len - offset);
    int cut  = offset + take;

    if (cut < len && txt.charAt(cut) != ' ') {
      int sp = txt.lastIndexOf(' ', cut - 1);
      if (sp > offset) cut = sp;
    }

    String line = txt.substring(offset, cut);
    line.trim();

    int16_t x1, y1; uint16_t w, h;
    tft.getTextBounds(line, 0, y, &x1, &y1, &w, &h);
    int cx = (tft.width() - (int)w) / 2;
    if (cx < margin) cx = margin;

    tft.setCursor(cx, y);
    tft.print(line);
    y += lineH;

    offset = cut;
    if (offset < len && txt.charAt(offset) == ' ') offset++;
  }
  return y;
}

// Pantalla de arranque: header fijo + caja con título y subtítulo
// Usa siempre fuente 1 (6×8 px) para que el texto no se corte.
static void showBootScreen(const String &line1,
                           const String &line2 = String(),
                           uint16_t color = ST77XX_WHITE) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);

  // ── Header ────────────────────────────────────────────────
  tft.fillRect(0, 0, tft.width(), 22, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds("CONTROL DE ACCESO", 0, 4, &x1, &y1, &w, &h);
  tft.setCursor((tft.width() - w) / 2, 4);
  tft.print("CONTROL DE ACCESO");
  tft.drawFastHLine(0, 20, tft.width(), ST77XX_WHITE);

  // ── Caja decorativa ───────────────────────────────────────
  int boxX = 5, boxY = 26;
  int boxW = tft.width() - 10;
  int boxH = 44;

  tft.drawRoundRect(boxX, boxY, boxW, boxH, 5, color);
  tft.fillRoundRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2, 5, ST77XX_BLACK);

  // ── Línea 1: título principal (fuente 1, con wrapping) ────
  _bootDrawWrapped(line1, boxY + 7, 1, color);

  // ── Línea 2: subtítulo (fuente 1, con wrapping) ───────────
  if (line2.length()) {
    _bootDrawWrapped(line2, boxY + 22, 1, ST77XX_WHITE);
  }

  // ── Icono de espera debajo de la caja ─────────────────────
  int iconY = boxY + boxH + 22;
  if (iconY + 12 < tft.height()) {
    // drawWaitIcon está en display.cpp; no está disponible aquí,
    // así que dibujamos un indicador sencillo con puntos
    int cx = tft.width() / 2;
    tft.fillCircle(cx - 10, iconY, 3, color);
    tft.fillCircle(cx,      iconY, 3, color);
    tft.fillCircle(cx + 10, iconY, 3, color);
  }
}

// Muestra advertencia amarilla (sin detener el sistema)
static void showBootWarning(const String &msg) {
  showBootScreen("AVISO", msg, ST77XX_YELLOW);
  Serial.println(msg);
  delay(2500);
}

// ============================================================
// NTP / hora
// ============================================================
static bool systemTimeReasonable() {
  time_t now = time(nullptr);
  return now > 1577836800; // 1-ene-2020
}

static void waitForNtpSyncOrTimeout() {
  unsigned long t0 = millis();
  Serial.printf("Esperando NTP (timeout %lus)...\n", NTP_TIMEOUT_MS / 1000UL);

  while (!systemTimeReasonable() && (millis() - t0) < NTP_TIMEOUT_MS) {
    delay(NTP_POLL_MS);
    Serial.print(".");
  }
  Serial.println();

  if (systemTimeReasonable()) {
    Serial.println("Hora sincronizada via NTP.");
  } else {
    Serial.println("WARN: Timeout NTP. Hora no sincronizada.");
  }
}

static void printTimeInfo() {
  struct tm t;
  if (getLocalTime(&t)) {
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    Serial.print("Hora local: ");
    Serial.println(buf);
  } else {
    Serial.println("getLocalTime() fallo.");
  }

  time_t epoch = time(nullptr);
  Serial.print("Epoch (UTC): ");
  Serial.println((unsigned long)epoch);

  const char *tz = getenv("TZ");
  Serial.print("getenv(TZ): ");
  Serial.println(tz ? tz : "NULL");

  Serial.print("WiFi status: ");
  Serial.println((int)WiFi.status());
}

// ============================================================
// Eventos WiFi
// ============================================================
static void wifiEvent(WiFiEvent_t event) {
  Serial.print("WiFi event: ");
  Serial.println((int)event);

  switch (event) {
    case SYSTEM_EVENT_STA_START:        Serial.println("  STA_START");        break;
    case SYSTEM_EVENT_STA_CONNECTED:    Serial.println("  STA_CONNECTED");    break;
    case SYSTEM_EVENT_STA_GOT_IP:       Serial.println("  STA_GOT_IP");       break;
    case SYSTEM_EVENT_STA_DISCONNECTED: Serial.println("  STA_DISCONNECTED"); break;
    default:                            Serial.println("  (otro evento)");    break;
  }
}

// ============================================================
// WiFi
// ============================================================
static bool connectWiFiWithTimeout(unsigned long timeout_ms = WIFI_TIMEOUT_MS) {
  Serial.printf("Conectando a '%s' (timeout %lus)...\n",
                WIFI_SSID, timeout_ms / 1000UL);

  WiFi.onEvent(wifiEvent);

  Serial.println("Escaneando redes...");
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    Serial.println("  Sin redes visibles.");
  } else {
    Serial.printf("  %d redes:\n", n);
    for (int i = 0; i < n; ++i) {
      Serial.printf("   %02d: '%s'  RSSI=%d dBm  CH=%d  %s\n",
                    i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                    WiFi.channel(i),
                    (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "OPEN" : "ENC");
    }
  }
  WiFi.scanDelete();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(true);
  delay(250);

  if (strlen(WIFI_SSID) == 0) {
    Serial.println("WARN: WIFI_SSID vacio.");
    return false;
  }

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long t0 = millis();
  wl_status_t lastStatus = WL_IDLE_STATUS;

  while ((millis() - t0) < timeout_ms) {
    wl_status_t st = WiFi.status();
    if (st != lastStatus) {
      Serial.printf("  WiFi.status(): %d\n", (int)st);
      lastStatus = st;
    }
    if (st == WL_CONNECTED) {
      Serial.println("Conectado. IP: " + WiFi.localIP().toString());
      Serial.print("  RSSI: "); Serial.println(WiFi.RSSI());
      Serial.print("  MAC:  "); Serial.println(WiFi.macAddress());
      return true;
    }
    if (st == WL_CONNECT_FAILED) {
      Serial.println("  WL_CONNECT_FAILED.");
      break;
    }
    delay(300);
  }

  Serial.println("Timeout WiFi.");
  Serial.printf("Estado final: %d\n", (int)WiFi.status());
  return false;
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("Iniciando ESP32 - Control de Acceso (ONLINE + SPIFFS)");

  // ── SPIFFS ─────────────────────────────────────────────────
  Serial.println("Montando SPIFFS...");
  if (!SPIFFS.begin(true)) {
    Serial.println("ERR: SPIFFS.begin() fallo.");
  } else {
    Serial.println("SPIFFS OK.");
  }

  initFiles();
  Serial.println("initFiles() OK.");

  // ── Pantalla ───────────────────────────────────────────────
  displayInit();
  showBootScreen("Conectando a WiFi", "Espere hasta 60 s", ST77XX_CYAN);

  // ── WiFi ───────────────────────────────────────────────────
  if (!connectWiFiWithTimeout(WIFI_TIMEOUT_MS)) {
    showBootScreen("ERROR DE RED", "Sin conexion WiFi", ST77XX_RED);
    Serial.println("Sistema detenido: sin WiFi.");
    while (true) {
      updateDisplay();
      delay(1000);
    }
  }

  // Mostrar IP (siempre cabe en una línea con fuente 1)
  showBootScreen("WiFi conectado", WiFi.localIP().toString(), ST77XX_GREEN);
  delay(1000);

  // ── mDNS ───────────────────────────────────────────────────
  if (MDNS.begin("control-acceso")) {
    Serial.println("mDNS: http://control-acceso.local");
  } else {
    Serial.println("WARN: mDNS fallo.");
  }

  // ── NTP / Hora ─────────────────────────────────────────────
  Serial.println("Configurando TZ y NTP...");
  // Mexico elimino el horario de verano en 2023. Todo el año es UTC-6 (CST fijo).
  // El ESP32 NO tiene tzdata, por eso usamos la cadena POSIX directamente.
  const char *posixTZ = "CST6";

  configTzTime(posixTZ, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", posixTZ, 1);
  tzset();

  waitForNtpSyncOrTimeout();

  if (!systemTimeReasonable()) {
    Serial.println("Reintentando NTP con fallback...");
    configTzTime(posixTZ, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", posixTZ, 1);
    tzset();
    waitForNtpSyncOrTimeout();
  }

  printTimeInfo();

  // ── Servidor BD ────────────────────────────────────────────
  showBootScreen("Verificando servidor BD", "Intentando conexion", ST77XX_YELLOW);

  bool serverOnline = pingServer();
  if (serverOnline) {
    showBootScreen("Servidor BD OK", "Sincronizando pendientes", ST77XX_GREEN);
    delay(800);
    syncPendingToServer();
  } else {
    showBootWarning("BD no responde ahora");
    Serial.println("WARN: BD no disponible. Se sincronizara despues.");
  }

  // ── SPI / RFID ─────────────────────────────────────────────
  Serial.println("Iniciando SPI...");
  SPI.begin();

  Serial.println("Inicializando MFRC522...");
  mfrc522.PCD_Init();
  Serial.println("MFRC522 OK.");

  // ── Servo ──────────────────────────────────────────────────
  Serial.printf("Servo en pin %d\n", SERVO_PIN);
  puerta.attach(SERVO_PIN);
  puerta.write(0);
  Serial.println("Servo OK. Pos=0.");

  // ── Rutas web ──────────────────────────────────────────────
  registerRoutes();

  server.on("/debug_set_time", HTTP_GET, []() {
    if (!server.hasArg("epoch")) {
      server.send(400, "text/plain", "epoch required");
      return;
    }
    uint32_t e = (uint32_t)server.arg("epoch").toInt();
    struct timeval tv;
    tv.tv_sec  = (time_t)e;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    setenv("TZ", "CST6", 1);
    tzset();
    server.send(200, "text/plain", String("Time set to: ") + nowISO());
  });

  server.begin();
  Serial.println("Web server iniciado.");

  showWaitingMessage();
  Serial.println("Setup completo. Entrando a loop.");
}

// ============================================================
// Loop
// ============================================================
unsigned long lastPoll = 0;

void loop() {
  server.handleClient();
  updateDisplay();

  if (millis() - lastPoll > POLL_INTERVAL) {
    lastPoll = millis();
    rfidLoopHandler();
  }
}