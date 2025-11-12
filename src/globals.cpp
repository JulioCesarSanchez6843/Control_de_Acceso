// src/globals.cpp
#include <Arduino.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#if defined(ARDUINO_ARCH_ESP32)
  #include <ESP32Servo.h>
#else
  #include <Servo.h>
#endif
#include <WebServer.h>

#include "globals.h" // declara externs y prototipos

// ---------------- Config (mueve credenciales a un archivo .gitignored si vas a commitear) ----------------
const char* WIFI_SSID = "Totalplay-2.4G-1cc8";
const char* WIFI_PASS = "pHh5XfaynxccRz5H";
const char* TZ = "America/Mexico_City";

// ---------------- Pines ----------------
// Nota: evita usar el mismo pin para dos cosas. Ajusta BUZZER_PIN si tu hardware lo requiere.
const int RST_PIN   = 22;
const int SS_PIN    = 21;
const int TFT_CS    = 5;
const int TFT_DC    = 2;   // pin de datos del TFT
const int TFT_RST   = 4;
const int SERVO_PIN = 15;
const int RGB_R_PIN = 25;
const int RGB_G_PIN = 26;
const int BUZZER_PIN= 13;  // <-- CORREGIDO: antes estaba en 2 (conflicto con TFT_DC). Cámbialo si tu buzzer está cableado a otro pin.

// ---------------- Paths de archivos en SPIFFS ----------------
const char* USERS_FILE     = "/users.csv";
const char* ATT_FILE       = "/attendance.csv";
const char* DENIED_FILE    = "/denied.csv";
const char* SCHEDULES_FILE = "/schedules.csv";
const char* NOTIF_FILE     = "/notifications.csv";
const char* COURSES_FILE   = "/courses.csv";

// ---------------- Objetos globales (definidos aquí, declarado como extern en globals.h) ----------------
// Decláralos extern en include/globals.h para usarlos desde otros módulos.
MFRC522 mfrc522(SS_PIN, RST_PIN);
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
Servo puerta;
WebServer server(80);

// si usas otras variables globales (por ejemplo captureMode, captureUID, etc.)
// defínelas también aquí o en otro archivo .cpp y decláralas extern en globals.h.
