#pragma once
// globals.h - Declaraciones globales, tipos y prototipos compartidos

#include <Arduino.h>
#include <WebServer.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <vector>

class Servo;

// ---------------- CONFIG ----------------
extern const char* WIFI_SSID;
extern const char* WIFI_PASS;
extern const char* TZ;

// Configuración del backend
extern const char* API_BASE_URL;
extern const char* API_TOKEN;
// ---------------------------------------

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
// ---------------------------------------

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
// --------------------------------------------------

// Capture mode globals
extern volatile bool captureMode;
extern volatile bool captureBatchMode;
extern String captureUID;
extern String captureName;
extern String captureAccount;
extern unsigned long captureDetectedAt;

// Variables para captura batch
extern std::vector<String> capturedUIDs;
extern volatile bool isCapturing;
extern volatile bool isBatchCapture;

// ---------------- Self-register session type ----------------
struct SelfRegSession {
  String token;
  String uid;
  unsigned long createdAtMs;
  unsigned long ttlMs;
  String materia;
};

extern std::vector<SelfRegSession> selfRegSessions;

// Estado de self-register mostrado en display
extern volatile bool awaitingSelfRegister;
extern unsigned long awaitingSinceMs;
extern unsigned long SELF_REG_TIMEOUT_MS;
extern String currentSelfRegToken;
extern String currentSelfRegUID;
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

// courses
std::vector<Course> loadCourses();
bool courseExists(const String &materia);
void addCourse(const String &materia, const String &prof);

// users
String findAnyUserByUID(const String &uid);
bool existsUserUidMateria(const String &uid, const String &materia);
bool existsUserAccountMateria(const String &account, const String &materia);
std::vector<String> usersForMateria(const String &materia);

// teachers helpers
String findTeacherByUID(const String &uid);
bool teacherNameExists(const String &name);
std::vector<String> teachersForMateria(const String &materia);

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

// Cancelar captura y volver a pantalla normal
void cancelCaptureAndReturnToNormal();
bool isTemporaryMessageActive();