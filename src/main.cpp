// src/main.cpp
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#if defined(ARDUINO_ARCH_ESP32)
  #include <ESP32Servo.h>
#else
  #include <Servo.h>
#endif

#include "config.h"
#include "globals.h"
#include "files_utils.h"
#include "display.h"
#include "rfid_handler.h"
#include "web_routes.h"

// IMPORTANT: these objects must be defined ONCE in your project (e.g. in globals.cpp).
// Here we only declare them as extern to avoid multiple-definition linker errors.
extern WebServer server;
extern MFRC522 mfrc522;
extern Adafruit_ST7735 tft;
extern Servo puerta;

// Capture-mode globals (we keep these local to main.cpp)
volatile bool captureMode = false;
String captureUID = "";
String captureName = "";
String captureAccount = "";
unsigned long captureDetectedAt = 0;

const unsigned long DISPLAY_MS = 4000UL;
const unsigned long POLL_INTERVAL = 150UL;
#ifndef CAPTURE_DEBOUNCE_MS
  const unsigned long CAPTURE_DEBOUNCE_MS = 3000UL;
#else
  const unsigned long CAPTURE_DEBOUNCE_MS = CAPTURE_DEBOUNCE_MS;
#endif

unsigned long lastPoll = 0;

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

  // hardware init
  pinMode(RGB_R_PIN, OUTPUT);
  pinMode(RGB_G_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  ledOff();

  displayInit();
  rfidInit();

  // register routes (use direct function pointers)
  server.on("/", handleRoot);
  server.on("/materias", handleMaterias);
  server.on("/materias/new", handleMateriasNew);
  server.on("/materias_add", HTTP_POST, handleMateriasAddPOST);
  server.on("/materias/edit", handleMateriasEditGET);
  server.on("/materias_edit", HTTP_POST, handleMateriasEditPOST);
  server.on("/materias_delete", HTTP_POST, handleMateriasDeletePOST);

  server.on("/students", handleStudentsForMateria);
  server.on("/students_all", handleStudentsAll);
  server.on("/student_remove_course", HTTP_POST, handleStudentRemoveCourse);
  server.on("/student_delete", HTTP_POST, handleStudentDelete);

  server.on("/capture", HTTP_GET, handleCapturePage);
  server.on("/capture_confirm", HTTP_POST, handleCaptureConfirm);
  server.on("/capture_poll", HTTP_GET, handleCapturePoll);
  server.on("/capture_stop", HTTP_GET, handleCaptureStopGET);

  server.on("/status", handleStatus);

  server.on("/schedules", HTTP_GET, handleSchedulesGrid);
  server.on("/schedules/edit", HTTP_GET, handleSchedulesEditGrid);
  server.on("/schedules_add_slot", HTTP_POST, handleSchedulesAddSlot);
  server.on("/schedules_del", HTTP_POST, handleSchedulesDel);

  server.on("/schedules_for", HTTP_GET, handleSchedulesForMateriaGET);
  server.on("/schedules_for_add", HTTP_POST, handleSchedulesForMateriaAddPOST);
  server.on("/schedules_for_del", HTTP_POST, handleSchedulesForMateriaDelPOST);

  server.on("/notifications", handleNotificationsPage);
  server.on("/notifications_clear", HTTP_POST, handleNotificationsClearPOST);

  server.on("/edit", handleEditGet);
  server.on("/edit_post", HTTP_POST, handleEditPost);
  server.on("/users.csv", handleUsersCSV);
  server.on("/attendance.csv", [](){
    if (!SPIFFS.exists(ATT_FILE)) { server.send(404,"text/plain","no att"); return; }
    File f = SPIFFS.open(ATT_FILE, FILE_READ);
    server.streamFile(f,"text/csv");
    f.close();
  });
  server.on("/notifications.csv", [](){
    if (!SPIFFS.exists(NOTIF_FILE)) { server.send(404,"text/plain","no"); return; }
    File f = SPIFFS.open(NOTIF_FILE, FILE_READ);
    server.streamFile(f,"text/csv");
    f.close();
  });

  server.on("/history", handleHistoryPage);
  server.on("/history.csv", handleHistoryCSV);
  server.on("/history_clear", HTTP_POST, handleHistoryClearPOST);
  server.on("/materia_history", handleMateriaHistoryGET);

  server.begin();
  Serial.println("Web server iniciado.");

  tft.fillScreen(ST77XX_BLACK); tft.setCursor(0,2); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1);
  tft.println("CONTROL DE ACCESO LAB");
  tft.println("---------------------");
  tft.println();
  tft.println("Esperando tarjeta...");
}

void loop() {
  server.handleClient();
  if (millis() - lastPoll > POLL_INTERVAL) {
    lastPoll = millis();
    rfidLoopHandler();
  }
}

