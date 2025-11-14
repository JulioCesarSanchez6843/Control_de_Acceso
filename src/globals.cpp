// src/globals.cpp
#include "config.h"
#include "globals.h"

#include <WebServer.h>
#include <MFRC522.h>
#include <Adafruit_ST7735.h>

#if defined(ARDUINO_ARCH_ESP32)
  #include <ESP32Servo.h>
#else
  #include <Servo.h>
#endif

// --- Config values (definiciones) ---
const char* WIFI_SSID = "Totalplay-2.4G-1cc8";
const char* WIFI_PASS = "pHh5XfaynxccRz5H";
const char* TZ = "America/Mexico_City";

// --- Pins ---
const int RST_PIN   = 22;
const int SS_PIN    = 21;
const int TFT_CS    = 5;
const int TFT_DC    = 2;
const int TFT_RST   = 4;
const int SERVO_PIN = 15;
const int RGB_R_PIN = 25;
const int RGB_G_PIN = 26;
const int BUZZER_PIN = 0; // Si no usas buzzer, deja 0 o ajusta al pin correcto

// --- Files (SPIFFS paths) ---
const char* USERS_FILE     = "/users.csv";
const char* ATT_FILE       = "/attendance.csv";
const char* DENIED_FILE    = "/denied.csv";
const char* SCHEDULES_FILE = "/schedules.csv";
const char* NOTIF_FILE     = "/notifications.csv";
const char* COURSES_FILE   = "/courses.csv";

// --- Timings ---
const unsigned long DISPLAY_MS = 4000UL;
const unsigned long POLL_INTERVAL = 150UL;
const unsigned long CAPTURE_DEBOUNCE_MS = 3000UL;

// --- Days, slots ---
const String DAYS[6] = {"LUN","MAR","MIE","JUE","VIE","SAB"};
const int SLOT_STARTS[] = {7,9,11,13,15,17};
const int SLOT_COUNT = 6;

// --- Objetos globales instanciados ---
WebServer server(80);
// MFRC522 constructor: SDA/SS pin, RST pin (ajustado a tu wiring)
MFRC522 mfrc522(SS_PIN, RST_PIN);
// Adafruit_ST7735 constructor (CS, DC, RST). Asegúrate coincide con tu librería.
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
// Servo objeto (ESP32 o AVR dependiendo de la placa)
Servo puerta;

// --- Capture mode globals ---
volatile bool captureMode = false;
String captureUID = "";
String captureName = "";
String captureAccount = "";
unsigned long captureDetectedAt = 0;
