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

// cuánto esperar (ms) a que NTP sincronice antes de seguir 
static const unsigned long NTP_TIMEOUT_MS = 30UL * 1000UL; // 30 segundos
static const unsigned long NTP_POLL_MS    = 500UL;        // poll cada 500 ms

// Devuelve true si la hora del sistema es posterior al 1-ene-2020 (evita epoch por defecto).
static bool systemTimeReasonable() {
  time_t now = time(nullptr);
  return now > 1577836800;
}

// Espera hasta que NTP sincronice o hasta agotar el timeout; imprime progreso y advertencias.
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

// Imprime hora local, epoch UTC, variable TZ y estado WiFi para diagnóstico.
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

// Intenta conectar WiFi hasta timeout_ms; informa IP si tiene éxito o advierte si falla.
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
  // Inicializa Serial y mensajes de arranque.
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("Iniciando ESP32 Registro Asistencia - Materias + Horarios (TZ fix)");

  // Monta SPIFFS; si falla continúa pero puede faltar archivos.
  Serial.println("Montando SPIFFS...");
  if (!SPIFFS.begin(true)) {
    Serial.println("ERR: SPIFFS.begin() falló. Se continuará pero archivos pueden faltar.");
  } else {
    Serial.println("SPIFFS montado OK.");
  }

  // Inicializa archivos de aplicación (estructura/archivos por defecto).
  initFiles();
  Serial.println("initFiles() -> OK.");

  // Conecta WiFi con timeout prolongado (30s).
  connectWiFiWithTimeout(30000UL); // 30s

  // Configura zona horaria y NTP; aplica fallback POSIX si es necesario.
  Serial.println("Configurando TZ y NTP...");
  const char *posixTZ = "GMT-6"; // fallback POSIX para UTC-6

  // Primero intenta usar TZ definido en globals (IANA), luego fuerza POSIX.
  configTzTime(TZ, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", posixTZ, 1);
  tzset();

  // Espera sincronización NTP o timeout.
  waitForNtpSyncOrTimeout();

  // Si no se sincroniza razonablemente, reintenta usando posixTZ directamente.
  if (!systemTimeReasonable()) {
    Serial.println("Reintentando configTzTime con cadena POSIX (fallback)...");
    configTzTime(posixTZ, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", posixTZ, 1);
    tzset();
    waitForNtpSyncOrTimeout();
  }

  // Imprime información de tiempo para verificación.
  printTimeInfo();

  // Inicializa periféricos: SPI, lector RFID, display y servo.
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

  // Registra rutas web y arranca servidor HTTP.
  registerRoutes();

  // Ruta de depuración para ajustar epoch vía HTTP (solo pruebas).
  server.on("/debug_set_time", HTTP_GET, [](){
    if (!server.hasArg("epoch")) { server.send(400,"text/plain","epoch required"); return; }
    uint32_t e = (uint32_t) server.arg("epoch").toInt();
    struct timeval tv; tv.tv_sec = (time_t)e; tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    setenv("TZ", "GMT-6", 1); tzset();
    server.send(200,"text/plain", String("Time set to: ") + nowISO());
  });

  server.begin();
  Serial.println("Web server iniciado.");
  Serial.println("Setup completo - entrando a loop.");
}

unsigned long lastPoll = 0;

void loop() {
  // Atiende peticiones HTTP entrantes.
  server.handleClient();

  // Ejecuta el handler de RFID periódicamente según POLL_INTERVAL.
  if (millis() - lastPoll > POLL_INTERVAL) {
    lastPoll = millis();
    rfidLoopHandler();
  }
}
