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
static const unsigned long WIFI_TIMEOUT_MS  = 60UL * 1000UL;
static const unsigned long NTP_TIMEOUT_MS   = 30UL * 1000UL;
static const unsigned long NTP_POLL_MS      = 500UL;

// ============================================================
// Utilidades de pantalla de arranque
// ============================================================
static void drawCenteredTextMain(const String &txt, int y, uint8_t size, uint16_t color = ST77XX_WHITE) {
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

static void showBootScreen(const String &line1, const String &line2 = String(), uint16_t color = ST77XX_WHITE) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(8, 6);
  tft.print("CONTROL DE ACCESO");
  tft.drawFastHLine(0, 20, tft.width(), ST77XX_WHITE);

  drawCenteredTextMain(line1, 42, 2, color);

  if (line2.length()) {
    drawCenteredTextMain(line2, 68, 1, ST77XX_WHITE);
  }
}

// Muestra error en pantalla pero NO detiene el sistema
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
  Serial.printf("Esperando sincronizacion NTP (timeout %lus)...\n", NTP_TIMEOUT_MS / 1000UL);

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
    Serial.print("Hora local (getLocalTime): ");
    Serial.println(buf);
  } else {
    Serial.println("getLocalTime() fallo.");
  }

  time_t epoch = time(nullptr);
  Serial.print("Epoch (UTC): ");
  Serial.println((unsigned long)epoch);

  const char *tz = getenv("TZ");
  Serial.print("getenv(\"TZ\"): ");
  Serial.println(tz ? tz : "NULL");

  Serial.print("WiFi status (numeric): ");
  Serial.println((int)WiFi.status());
}

// ============================================================
// Eventos WiFi
// ============================================================
static void wifiEvent(WiFiEvent_t event) {
  Serial.print("WiFi event: ");
  Serial.println((int)event);

  switch (event) {
    case SYSTEM_EVENT_STA_START:        Serial.println("  -> SYSTEM_EVENT_STA_START"); break;
    case SYSTEM_EVENT_STA_CONNECTED:    Serial.println("  -> SYSTEM_EVENT_STA_CONNECTED"); break;
    case SYSTEM_EVENT_STA_GOT_IP:       Serial.println("  -> SYSTEM_EVENT_STA_GOT_IP"); break;
    case SYSTEM_EVENT_STA_DISCONNECTED: Serial.println("  -> SYSTEM_EVENT_STA_DISCONNECTED"); break;
    default:                            Serial.println("  -> (otro evento)"); break;
  }
}

// ============================================================
// WiFi
// ============================================================
static bool connectWiFiWithTimeout(unsigned long timeout_ms = WIFI_TIMEOUT_MS) {
  Serial.printf("Intentando conectar a '%s' (timeout %lus)...\n", WIFI_SSID, timeout_ms / 1000UL);

  WiFi.onEvent(wifiEvent);

  Serial.println("Escaneando redes WiFi visibles...");
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    Serial.println("  No se encontraron redes.");
  } else {
    Serial.printf("  %d redes encontradas:\n", n);
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      int ch = WiFi.channel(i);
      wifi_auth_mode_t auth = WiFi.encryptionType(i);
      const char* enc = (auth == WIFI_AUTH_OPEN) ? "OPEN" : "ENCRYPTED";
      Serial.printf("   %02d: SSID='%s'  RSSI=%d dBm  CH=%d  %s\n",
                    i + 1, ssid.c_str(), rssi, ch, enc);
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
      Serial.printf("  WiFi.status() cambio: %d\n", (int)st);
      lastStatus = st;
    }

    if (st == WL_CONNECTED) {
      Serial.println(String("Conectado. IP: ") + WiFi.localIP().toString());
      Serial.print("  RSSI: ");
      Serial.println(WiFi.RSSI());
      Serial.print("  MAC: ");
      Serial.println(WiFi.macAddress());
      return true;
    }

    if (st == WL_CONNECT_FAILED) {
      Serial.println("  WL_CONNECT_FAILED. Abortando intento.");
      break;
    }

    delay(300);
  }

  Serial.println("Timeout de conexion WiFi.");
  Serial.printf("Estado final WiFi.status() = %d\n", (int)WiFi.status());
  return false;
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("Iniciando ESP32 Control de Acceso - MODO ONLINE con respaldo SPIFFS");

  // ─── SPIFFS ─────────────────────────────────────────────
  Serial.println("Montando SPIFFS...");
  if (!SPIFFS.begin(true)) {
    Serial.println("ERR: SPIFFS.begin() fallo. Se continuara, pero faltaran archivos si no existen.");
  } else {
    Serial.println("SPIFFS montado OK.");
  }

  initFiles();
  Serial.println("initFiles() -> OK.");

  // ─── Pantalla ────────────────────────────────────────────
  displayInit();
  showBootScreen("Conectando a WiFi...", "Espere hasta 60 s", ST77XX_CYAN);

  // ─── WiFi (OBLIGATORIO: sin WiFi no hay nada) ────────────
  if (!connectWiFiWithTimeout(WIFI_TIMEOUT_MS)) {
    // Sin WiFi no podemos ni NTP ni DB; mostramos error y nos detenemos
    showBootScreen("ERROR DE RED", "Sin conexion WiFi", ST77XX_RED);
    Serial.println("Sistema detenido: sin conexion WiFi.");
    while (true) {
      updateDisplay();
      delay(1000);
    }
  }

  showBootScreen("WiFi conectado", WiFi.localIP().toString(), ST77XX_GREEN);
  delay(1000);

  // ─── mDNS ────────────────────────────────────────────────
  if (MDNS.begin("control-acceso")) {
    Serial.println("mDNS iniciado: http://control-acceso.local");
  } else {
    Serial.println("WARN: No se pudo iniciar mDNS");
  }

  // ─── NTP / Hora ──────────────────────────────────────────
  Serial.println("Configurando TZ y NTP...");
  const char *posixTZ = "GMT-6";

  configTzTime(TZ, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", TZ, 1);
  tzset();

  waitForNtpSyncOrTimeout();

  if (!systemTimeReasonable()) {
    Serial.println("Reintentando configTzTime con cadena POSIX (fallback)...");
    configTzTime(posixTZ, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", posixTZ, 1);
    tzset();
    waitForNtpSyncOrTimeout();
  }

  printTimeInfo();

  // ─── Servidor FastAPI / Oracle (opcional en arranque) ────
  // El sistema arranca siempre. Si el servidor no responde ahora,
  // los datos se guardan en SPIFFS y se sincronizan cuando vuelva.
  showBootScreen("Verificando servidor BD...", "Intentando conexion", ST77XX_YELLOW);

  bool serverOnline = pingServer();
  if (serverOnline) {
    showBootScreen("Servidor BD conectado", "Sincronizando pendientes", ST77XX_GREEN);
    delay(800);
    // Subir cualquier dato pendiente en SPIFFS que aún no llegó a la BD
    syncPendingToServer();
  } else {
    showBootWarning("BD no responde ahora");
    Serial.println("WARN: Servidor BD no disponible en arranque. Se sincronizara mas adelante.");
    // No se detiene — modoLocal eliminado, SPIFFS cubre el respaldo
  }

  // ─── SPI / RFID ──────────────────────────────────────────
  Serial.println("Iniciando SPI...");
  SPI.begin();

  Serial.println("Inicializando lector RFID (MFRC522)...");
  mfrc522.PCD_Init();
  Serial.println("MFRC522 inicializado.");

  // ─── Servo ───────────────────────────────────────────────
  Serial.printf("Inicializando servo. Pin (SERVO_PIN) = %d\n", SERVO_PIN);
  puerta.attach(SERVO_PIN);
  puerta.write(0);
  Serial.println("Servo attach OK. Posicion inicial 0.");

  // ─── Rutas web ───────────────────────────────────────────
  registerRoutes();

  server.on("/debug_set_time", HTTP_GET, []() {
    if (!server.hasArg("epoch")) {
      server.send(400, "text/plain", "epoch required");
      return;
    }

    uint32_t e = (uint32_t)server.arg("epoch").toInt();
    struct timeval tv;
    tv.tv_sec = (time_t)e;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    setenv("TZ", TZ, 1);
    tzset();

    server.send(200, "text/plain", String("Time set to: ") + nowISO());
  });

  server.begin();
  Serial.println("Web server iniciado.");

  showWaitingMessage();

  Serial.println("Setup completo - Sistema listo (SPIFFS + Oracle como respaldo).");
  Serial.println("Entrando a loop.");
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