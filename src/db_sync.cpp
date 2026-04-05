// src/db_sync.cpp
#include "db_sync.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

static const char* SERVER_URL = "http://192.168.100.8:8000";


// --------------------------------------------------
// Generar fecha actual (YYYY-MM-DD HH:MM:SS)
// --------------------------------------------------
static String nowISO() {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
  return String(buf);
}


// --------------------------------------------------
// Ping al servidor
// --------------------------------------------------
bool pingServer() {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("DB_SYNC: sin WiFi, pingServer() = false");
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  String url = String(SERVER_URL) + "/ping";

  Serial.print("DB_SYNC ping -> ");
  Serial.println(url);

  if (!http.begin(client, url)) {
    Serial.println("DB_SYNC: http.begin() falló en ping");
    return false;
  }

  int code = http.GET();
  String body = http.getString();

  Serial.printf("DB_SYNC ping HTTP code: %d\n", code);
  Serial.print("DB_SYNC ping body: ");
  Serial.println(body);

  http.end();

  return (code == 200);
}



// --------------------------------------------------
// Enviar asistencia
// --------------------------------------------------
bool sendAsistencia(
    String timestamp,
    String rfid_uid,
    String name,
    String account,
    String materia,
    String mode
) {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("DB_SYNC: sin WiFi, no se puede enviar asistencia");
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  String url = String(SERVER_URL) + "/asistencia";

  Serial.print("DB_SYNC POST -> ");
  Serial.println(url);

  if (!http.begin(client, url)) {
    Serial.println("DB_SYNC: http.begin() falló en asistencia");
    return false;
  }

  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;

  doc["timestamp"] = timestamp;
  doc["rfid_uid"] = rfid_uid;
  doc["name"] = name;
  doc["account"] = account;
  doc["materia"] = materia;
  doc["mode"] = mode;

  String json;
  serializeJson(doc, json);

  Serial.print("DB_SYNC JSON asistencia: ");
  Serial.println(json);

  int httpCode = http.POST(json);

  String response = http.getString();

  Serial.printf("DB_SYNC asistencia HTTP code: %d\n", httpCode);
  Serial.print("DB_SYNC respuesta: ");
  Serial.println(response);

  http.end();

  return (httpCode == 200);
}



// --------------------------------------------------
// Registrar alumno en servidor
// --------------------------------------------------
bool sendAlumnoRegistro(
    String uid,
    String nombre,
    String cuenta,
    String materia,
    String created_at
) {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("DB_SYNC: sin WiFi para registrar alumno");
    return false;
  }

  if (created_at.length() == 0) {
    created_at = nowISO();
  }

  WiFiClient client;
  HTTPClient http;

  String url = String(SERVER_URL) + "/alumno";

  Serial.print("DB_SYNC POST -> ");
  Serial.println(url);

  if (!http.begin(client, url)) {
    Serial.println("DB_SYNC: http.begin() falló alumno");
    return false;
  }

  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;

  doc["rfid_uid"] = uid;
  doc["name"] = nombre;
  doc["account"] = cuenta;
  doc["materia"] = materia;
  doc["created_at"] = created_at;

  String json;
  serializeJson(doc, json);

  Serial.print("DB_SYNC JSON alumno: ");
  Serial.println(json);

  int httpCode = http.POST(json);

  String response = http.getString();

  Serial.printf("Alumno registro HTTP code: %d\n", httpCode);
  Serial.print("Alumno respuesta: ");
  Serial.println(response);

  http.end();

  return (httpCode == 200 || httpCode == 409);
}



// --------------------------------------------------
// Registrar profesor en servidor
// --------------------------------------------------
bool sendProfesorRegistro(
    String uid,
    String nombre,
    String cuenta,
    String created_at
) {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("DB_SYNC: sin WiFi para registrar profesor");
    return false;
  }

  if (created_at.length() == 0) {
    created_at = nowISO();
  }

  WiFiClient client;
  HTTPClient http;

  String url = String(SERVER_URL) + "/profesor";

  Serial.print("DB_SYNC POST -> ");
  Serial.println(url);

  if (!http.begin(client, url)) {
    Serial.println("DB_SYNC: http.begin() falló profesor");
    return false;
  }

  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;

  doc["rfid_uid"] = uid;
  doc["name"] = nombre;
  doc["account"] = cuenta;
  doc["created_at"] = created_at;

  String json;
  serializeJson(doc, json);

  Serial.print("DB_SYNC JSON profesor: ");
  Serial.println(json);

  int httpCode = http.POST(json);

  String response = http.getString();

  Serial.printf("Profesor registro HTTP code: %d\n", httpCode);
  Serial.print("Profesor respuesta: ");
  Serial.println(response);

  http.end();

  return (httpCode == 200 || httpCode == 409);
}