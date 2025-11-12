#pragma once

// Core Arduino + platform includes
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
#include "time.h"
#include <vector>

// ---------------- CONFIG (puedes ajustarlos) ----------------
extern const char* WIFI_SSID;
extern const char* WIFI_PASS;
extern const char* TZ;

// ---------------- PINES ----------------
extern const int RST_PIN;
extern const int SS_PIN;
extern const int TFT_CS;
extern const int TFT_DC;
extern const int TFT_RST;
extern const int SERVO_PIN;
extern const int RGB_R_PIN;
extern const int RGB_G_PIN;
extern const int BUZZER_PIN;

// ---------------- FILES ----------------
extern const char* USERS_FILE;
extern const char* ATT_FILE;
extern const char* DENIED_FILE;
extern const char* SCHEDULES_FILE;
extern const char* NOTIF_FILE;
extern const char* COURSES_FILE;

// ---------------- Global object declarations (extern) ----------------
// Decláralas como extern aqui y crea las instancias en main.cpp
extern WebServer server;
extern MFRC522 mfrc522;
extern Adafruit_ST7735 tft;
extern Servo puerta;

// ---------------- Capture / runtime flags ----------------
extern volatile bool captureMode;
extern String captureUID;
extern String captureName;
extern String captureAccount;
extern unsigned long captureDetectedAt;

// ---------------- forward decls (si los llama otro .cpp) ----------------
// Si exponen funciones globales, decláralas aquí (no es obligatorio)
