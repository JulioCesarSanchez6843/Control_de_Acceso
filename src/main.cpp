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

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Iniciando ESP32 Registro Asistencia - Materias + Horarios + Historial");

  if (!SPIFFS.begin(true)) Serial.println("Error mount SPIFFS");
  initFiles();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Conectando WiFi");
  int tries=0;
  while (WiFi.status()!=WL_CONNECTED && tries<40) { delay(250); Serial.print("."); tries++; }
  if (WiFi.status()==WL_CONNECTED) Serial.println("\nIP: " + WiFi.localIP().toString());
  else Serial.println("\nWiFi no conectado. Aun asi app local funcionara.");

  configTzTime(TZ,"pool.ntp.org","time.nist.gov");

  SPI.begin();
  mfrc522.PCD_Init();

  displayInit();
  puerta.attach(SERVO_PIN);
  puerta.write(0);

  // register web routes
  registerRoutes();
  server.begin();
  Serial.println("Web server iniciado.");
}

unsigned long lastPoll = 0;
void loop() {
  server.handleClient();
  if (millis() - lastPoll > POLL_INTERVAL) {
    lastPoll = millis();
    rfidLoopHandler();
  }
}
