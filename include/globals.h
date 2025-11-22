#pragma once
// globals.h - Declaraciones globales, tipos y prototipos compartidos

#include <Arduino.h>
#include <WebServer.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <vector>
#include <FS.h>

#if defined(ARDUINO_ARCH_ESP32)
  #include <ESP32Servo.h>
#else
  #include <Servo.h>
#endif

// ---------------- CONFIG ----------------
extern const char* WIFI_SSID;
extern const char* WIFI_PASS;
extern const char* TZ;
// -----------------------------------------

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
// ----------------------------------------

// ---------------- FILES SPIFFS ----------------
extern const char* USERS_FILE;
extern const char* ATT_FILE;
extern const char* DENIED_FILE;
extern const char* SCHEDULES_FILE;
extern const char* NOTIF_FILE;
extern const char* COURSES_FILE;
extern const char* CAPTURE_QUEUE_FILE;
// -----------------------------------------------

// Timing constants
extern const unsigned long DISPLAY_MS;
extern const unsigned long POLL_INTERVAL;
extern const unsigned long CAPTURE_DEBOUNCE_MS;

// Days & slots
extern const String DAYS[6];
extern const int SLOT_STARTS[];
extern const int SLOT_COUNT;

// ---------------- Objetos globales ----------------
extern WebServer server;
extern MFRC522 mfrc522;
extern Adafruit_ST7735 tft;
extern Servo puerta;

// Capture mode globals
extern volatile bool captureMode;
extern volatile bool captureBatchMode;
extern String captureUID;
extern String captureName;
extern String captureAccount;
extern unsigned long captureDetectedAt;

// Variables para captura batch
extern std::vector<String> capturedUIDs;
extern bool isCapturing;
extern bool isBatchCapture;

// ---------------- Self-register session type ----------------
struct SelfRegSession {
  String token;
  String uid;
  unsigned long createdAtMs;
  unsigned long ttlMs;
  String materia;
};
extern std::vector<SelfRegSession> selfRegSessions;

// Estado de self-register mostrado en display (bloqueo mientras alumno completa)
extern volatile bool awaitingSelfRegister;
extern unsigned long awaitingSinceMs;
extern unsigned long SELF_REG_TIMEOUT_MS;
extern String currentSelfRegToken;
extern String currentSelfRegUID;

// *** NUEVA VARIABLE PARA BLOQUEO DE RFID DURANTE REGISTRO ***
extern volatile bool blockRFIDForSelfReg;

// ---------------- Tipos ----------------
struct Course {
  String materia;
  String profesor;
  String created_at;
};

struct ScheduleEntry {
  String materia;
  String day;
  String start;
  String end;
};

// ---------------- Prototipos utilitarios ----------------

// time_utils
String nowISO(); // devuelve "YYYY-MM-DD HH:MM:SS"

// UID helpers
String uidBytesToString(byte *uid, byte len);

// schedules / current schedule
std::vector<ScheduleEntry> loadSchedules();
String currentScheduledMateria();
bool slotOccupied(const String &day, const String &start, const String &materiaFilter = String());
void addScheduleSlot(const String &materia, const String &day, const String &start, const String &end);

// csv parsing
std::vector<String> parseQuotedCSVLine(const String &line);

// files utils (deben devolver bool)
bool appendLineToFile(const char* path, const String &line);
bool writeAllLines(const char* path, const std::vector<String> &lines);
void initFiles();

// courses
std::vector<Course> loadCourses();
bool courseExists(const String &materia);
void addCourse(const String &materia, const String &prof);
void writeCourses(const std::vector<Course> &list);

// users
String findAnyUserByUID(const String &uid);
bool existsUserUidMateria(const String &uid, const String &materia);
bool existsUserAccountMateria(const String &account, const String &materia);
std::vector<String> usersForMateria(const String &materia);

// notifications
void addNotification(const String &uid, const String &name, const String &account, const String &note);
std::vector<String> readNotifications(int limit = 200);
int notifCount();
void clearNotifications();

// display / leds
void ledOff();
void ledRedOn();
void ledGreenOn();

// Mostrar QR en pantalla
void showQRCodeOnDisplay(const String &url, int pixelBoxSize);

// Nueva función para cancelar captura y volver a pantalla normal
void cancelCaptureAndReturnToNormal();