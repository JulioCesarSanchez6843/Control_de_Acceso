// src/main.cpp  (reemplaza tu fichero main actual)
#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <time.h>

#include "config.h"
#include "globals.h"
#include "display.h"
#include "files_utils.h"
#include "rfid_handler.h"
#include "web/web_routes.h"

// cuánto esperar (ms) a que NTP sincronice antes de seguir (ajusta si quieres)
static const unsigned long NTP_TIMEOUT_MS = 20UL * 1000UL; // 20 segundos
static const unsigned long NTP_POLL_MS    = 500UL;        // poll cada 500 ms

static bool systemTimeReasonable() {
  time_t now = time(nullptr);
  // 1577836800 = 2020-01-01T00:00:00Z
  return now > 1577836800;
}

static void waitForNtpSyncOrTimeout() {
  unsigned long t0 = millis();
  Serial.print("Esperando sincronización NTP (timeout ");
  Serial.print(NTP_TIMEOUT_MS / 1000);
  Serial.println("s) ...");
  while (!systemTimeReasonable() && (millis() - t0) < NTP_TIMEOUT_MS) {
    delay(NTP_POLL_MS);
    Serial.print(".");
  }
  Serial.println();
  if (systemTimeReasonable()) {
    Serial.println("Hora sincronizada via NTP.");
  } else {
    Serial.println("WARN: Timeout NTP. Hora no sincronizada — se usará la hora del sistema (posible incorrecta).");
  }
}

static void printTimeInfo() {
  struct tm t;
  if (getLocalTime(&t)) {
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    Serial.print("Hora local (nowISO): ");
    Serial.println(buf);
  } else {
    Serial.println("getLocalTime() falló.");
  }
  time_t epoch = time(nullptr);
  Serial.print("Epoch (UTC): ");
  Serial.println((unsigned long)epoch);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("Iniciando ESP32 Registro Asistencia - Materias + Horarios (TZ fix)");

  // SPIFFS
  Serial.println("Montando SPIFFS...");
  if (!SPIFFS.begin(true)) {
    Serial.println("ERR: SPIFFS.begin() falló. Se continuará pero archivos pueden faltar.");
  } else {
    Serial.println("SPIFFS montado OK.");
  }

  initFiles();
  Serial.println("initFiles() -> OK.");

  // WiFi
  Serial.println("Conectando WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Conectando");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(250);
    Serial.print(".");
    tries++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(String("WiFi OK - IP: ") + WiFi.localIP().toString());
  } else {
    Serial.println("WARN: No conectado a WiFi. NTP no funcionará sin conexión.");
  }

  // timezone + NTP
  Serial.println("Configurando TZ y NTP...");
  // Intentamos usar la TZ "oficial" (si el sistema la soporta)
  configTzTime(TZ, "pool.ntp.org", "time.nist.gov");
  // FORZAR un TZ simple POSIX en caso de que la base tz no exista o no se aplique:
  // 'GMT-6' significa UTC-6 (Mexico City generalmente UTC-6; ajusta si necesitas otro offset)
  setenv("TZ", "GMT-6", 1);
  tzset();

  // Esperar explícitamente hasta que NTP sincronice o timeout
  waitForNtpSyncOrTimeout();

  // Imprimir hora (verificación)
  printTimeInfo();

  // Inicializaciones hardware
  Serial.println("Iniciando SPI...");
  SPI.begin();

  Serial.println("Inicializando lector RFID (MFRC522)...");
  mfrc522.PCD_Init();
  Serial.println("MFRC522 inicializado.");

  Serial.println("Inicializando display...");
  displayInit();
  Serial.println("displayInit() OK.");

  Serial.println("Configurando servo puerta...");
  puerta.attach(SERVO_PIN);
  puerta.write(0);

  // web
  registerRoutes();
  server.begin();
  Serial.println("Web server iniciado.");

  Serial.println("Setup completo - entrando a loop.");
}

unsigned long lastPoll = 0;
void loop() {
  server.handleClient();

  if (millis() - lastPoll > POLL_INTERVAL) {
    lastPoll = millis();
    rfidLoopHandler();
  }
}
