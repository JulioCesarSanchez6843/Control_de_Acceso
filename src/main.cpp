// src/main.cpp
#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
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
#include "rfid_handler.h"
#include "web/web_routes.h"
#include "db_sync.h"

// ============================================================
// Tiempos de espera
// ============================================================
static const unsigned long WIFI_TIMEOUT_MS    = 60UL * 1000UL;
static const unsigned long SERVER_TIMEOUT_MS  = 60UL * 1000UL;
static const unsigned long NTP_TIMEOUT_MS     = 30UL * 1000UL;
static const unsigned long NTP_POLL_MS        = 500UL;
static const unsigned long RECONNECT_EVERY_MS = 10000UL;

// ============================================================
// Estado de conectividad
// ============================================================
static bool wifiReady = false;
static bool serverReady = false;
static bool timeReady = false;
static unsigned long lastReconnectAttempt = 0;

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

static void showErrorScreen(const String &msg) {
  showBootScreen("SIN CONEXION", msg, ST77XX_RED);
  Serial.println(msg);
}

static void showOnlineScreen() {
  showBootScreen("SISTEMA LISTO", "Conexion activa", ST77XX_GREEN);
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
    timeReady = true;
  } else {
    Serial.println("WARN: Timeout NTP. Hora no sincronizada.");
    timeReady = false;
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
}

// ============================================================
// WiFi / servidor
// ============================================================
static bool connectWiFiWithTimeout(unsigned long timeout_ms = WIFI_TIMEOUT_MS) {
  if (WiFi.status() == WL_CONNECTED) {
    wifiReady = true;
    return true;
  }

  Serial.printf("Intentando conectar a '%s' (timeout %lus)...\n", WIFI_SSID, timeout_ms / 1000UL);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(true);
  delay(250);

  if (strlen(WIFI_SSID) == 0) {
    Serial.println("ERR: WIFI_SSID vacio.");
    wifiReady = false;
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
      wifiReady = true;
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
  wifiReady = false;
  return false;
}

static bool configureTimeIfWiFiReady() {
  if (!wifiReady || WiFi.status() != WL_CONNECTED) {
    timeReady = false;
    return false;
  }

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
  return timeReady;
}

static bool waitForServerWithTimeout(unsigned long timeout_ms = SERVER_TIMEOUT_MS) {
  if (!wifiReady || WiFi.status() != WL_CONNECTED) {
    serverReady = false;
    return false;
  }

  Serial.printf("Probando servidor FastAPI por hasta %lus...\n", timeout_ms / 1000UL);

  unsigned long t0 = millis();
  while ((millis() - t0) < timeout_ms) {
    if (pingServer()) {
      Serial.println("Servidor FastAPI responde OK.");
      serverReady = true;
      return true;
    }
    delay(2000);
  }

  Serial.println("Servidor FastAPI no respondio dentro del timeout.");
  serverReady = false;
  return false;
}

static void bringSystemOnlineIfPossible() {
  wifiReady = connectWiFiWithTimeout(WIFI_TIMEOUT_MS);

  if (wifiReady && !timeReady) {
    configureTimeIfWiFiReady();
  }

  if (wifiReady) {
    serverReady = waitForServerWithTimeout(SERVER_TIMEOUT_MS);
  } else {
    serverReady = false;
  }

  if (wifiReady && serverReady) {
    showOnlineScreen();
    delay(1000);
    showWaitingMessage();
  } else if (!wifiReady) {
    showErrorScreen("WiFi no disponible");
  } else {
    showErrorScreen("Servidor no responde");
  }
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("Iniciando ESP32 Registro Asistencia - flujo online");

  // Pantalla de arranque
  displayInit();
  showBootScreen("Iniciando sistema...", "Preparando RFID y red", ST77XX_CYAN);

  // Registrar eventos WiFi
  WiFi.onEvent(wifiEvent);

  // 1) Intento inicial de conectividad
  bringSystemOnlineIfPossible();

  // 2) SPI / RFID
  Serial.println("Iniciando SPI...");
  SPI.begin();

  Serial.println("Inicializando lector RFID (MFRC522)...");
  mfrc522.PCD_Init();
  Serial.println("MFRC522 inicializado.");

  // 3) Servo
  Serial.printf("Inicializando servo. Pin (SERVO_PIN) = %d\n", SERVO_PIN);
  puerta.attach(SERVO_PIN);
  puerta.write(0);
  Serial.println("Servo attach OK. Posicion inicial 0.");

  // 4) Rutas web del ESP32
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

  Serial.println((wifiReady && serverReady) ? "Sistema listo en linea." : "Sistema iniciado, esperando conexion.");
  Serial.println("Setup completo - entrando a loop.");
}

// ============================================================
// Loop
// ============================================================
unsigned long lastPoll = 0;

void loop() {
  server.handleClient();
  updateDisplay();

  // Reintento de conexion sin modo local ni guardado offline
  if (!wifiReady || !serverReady) {
    if (millis() - lastReconnectAttempt >= RECONNECT_EVERY_MS) {
      lastReconnectAttempt = millis();
      Serial.println("Reintentando conexion WiFi/servidor...");

      if (WiFi.status() == WL_CONNECTED) {
        wifiReady = true;
      } else {
        wifiReady = connectWiFiWithTimeout(15000UL);
      }

      if (wifiReady && !timeReady) {
        configureTimeIfWiFiReady();
      }

      if (wifiReady) {
        serverReady = waitForServerWithTimeout(15000UL);
      } else {
        serverReady = false;
      }

      if (wifiReady && serverReady) {
        showOnlineScreen();
        delay(800);
        showWaitingMessage();
      } else if (!wifiReady) {
        showErrorScreen("Reintentando WiFi...");
      } else {
        showErrorScreen("Reintentando servidor...");
      }
    }

    delay(10);
    return;
  }

  // Operacion normal SOLO online
  if (millis() - lastPoll > POLL_INTERVAL) {
    lastPoll = millis();
    rfidLoopHandler();
  }
}