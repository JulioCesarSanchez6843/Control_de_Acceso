#pragma once
// Configuración global (constantes, pines, rutas de archivos, tiempos)
// Reemplaza los valores si quieres cambiarlos desde aquí.

#include <Arduino.h>

// --- WiFi / Timezone ---
extern const char* WIFI_SSID;
extern const char* WIFI_PASS;
extern const char* TZ;

// --- Pines ---
extern const int RST_PIN;
extern const int SS_PIN;
extern const int TFT_CS;
extern const int TFT_DC;
extern const int TFT_RST;
extern const int SERVO_PIN;
extern const int RGB_R_PIN;
extern const int RGB_G_PIN;
extern const int BUZZER_PIN;

// --- Archivos SPIFFS (rutas) ---
extern const char* USERS_FILE;   // "/users.csv"
extern const char* ATT_FILE;     // "/attendance.csv"
extern const char* DENIED_FILE;  // "/denied.csv"
extern const char* SCHEDULES_FILE; // "/schedules.csv"
extern const char* NOTIF_FILE;   // "/notifications.csv"
extern const char* COURSES_FILE; // "/courses.csv"

// --- Timings ---
extern const unsigned long DISPLAY_MS;
extern const unsigned long POLL_INTERVAL;
extern const unsigned long CAPTURE_DEBOUNCE_MS;

// --- Horarios / grilla ---
extern const String DAYS[6];         // {"LUN","MAR","MIE","JUE","VIE","SAB"}
extern const int SLOT_STARTS[];      // ej. {7,9,11,13,15,17}
extern const int SLOT_COUNT;         // 6 (numero de franjas)
