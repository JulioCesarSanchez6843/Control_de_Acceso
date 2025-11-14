// src/main.cpp
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
static const unsigned long NTP_TIMEOUT_MS = 30UL * 1000UL; // 30 segundos
static const unsigned long NTP_POLL_MS    = 500UL;        // poll cada 500 ms

static bool systemTimeReasonable() {
  time_t now = time(nullptr);
  // 1577836800 = 2020-01-01T00:00:00Z -> considera razonable si > 2020
  return now > 1577836800;
}

static void waitForNtpSyncOrTimeout() {
  unsigned long t0 = millis();
  Serial.printf("Esperando sincronización NTP (timeout %lus) ...\n", NTP_TIMEOUT_MS / 1000UL);
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

  const char *tz = getenv("TZ");
  Serial.print("getenv(\"TZ\"): ");
  Serial.println(tz ? tz : "NULL");
  Serial.print("WiFi status: ");
  Serial.println(WiFi.status());
}

void connectWiFiWithTimeout(unsigned long timeout_ms = 20000UL) {
  Serial.printf("Conectando WiFi a '%s' (timeout %lus)...\n", WIFI_SSID, timeout_ms/1000UL);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeout_ms) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(String("WiFi OK - IP: ") + WiFi.localIP().toString());
  } else {
    Serial.println("WARN: No conectado a WiFi. NTP no funcionará sin conexión.");
  }
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
  connectWiFiWithTimeout(30000UL); // 30s

  // timezone + NTP
  Serial.println("Configurando TZ y NTP...");

  // POSIX fallback TZ (asegura que se aplique el offset en ESP32)
  // GMT-6 fija UTC-6. Cambia a otra cadena POSIX si necesitas reglas DST.
  const char *posixTZ = "GMT-6"; // <-- usa esto para forzar UTC-6

  // Primero intenta usar la variable TZ definida en globals (puede ser IANA)
  // pero como fallback aplicamos posixTZ que sí funciona en la mayoría de builds ESP.
  // Intento 1: usar TZ (puede ser "America/Mexico_City")
  configTzTime(TZ, "pool.ntp.org", "time.nist.gov");
  // Forzar variable de entorno POSIX si la anterior no aplica correctamente
  setenv("TZ", posixTZ, 1);
  tzset();

  // Esperar explícitamente hasta que NTP sincronice o timeout
  waitForNtpSyncOrTimeout();

  // Si no sincronizó razonablemente, reintentar usando directamente posixTZ en configTzTime
  if (!systemTimeReasonable()) {
    Serial.println("Reintentando configTzTime con cadena POSIX (fallback)...");
    configTzTime(posixTZ, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", posixTZ, 1);
    tzset();
    waitForNtpSyncOrTimeout();
  }

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

  // web (registrar rutas)
  registerRoutes();

  // RUTA DEBUG: set time epoch via web (solo para pruebas) - quita en producción
  server.on("/debug_set_time", HTTP_GET, [](){
    if (!server.hasArg("epoch")) { server.send(400,"text/plain","epoch required"); return; }
    uint32_t e = (uint32_t) server.arg("epoch").toInt();
    struct timeval tv; tv.tv_sec = (time_t)e; tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    // re-aplicar tz
    setenv("TZ", "GMT-6", 1); tzset();
    server.send(200,"text/plain", String("Time set to: ") + nowISO());
  });

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
