// src/db_sync.cpp
#include "db_sync.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static const char* SERVER_URL = "http://192.168.100.8:8000";

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

  Serial.print("DB_SYNC JSON: ");
  Serial.println(json);

  int httpCode = http.POST(json);
  String response = http.getString();

  Serial.printf("DB_SYNC asistencia HTTP code: %d\n", httpCode);
  Serial.print("DB_SYNC respuesta: ");
  Serial.println(response);

  http.end();

  return (httpCode == 200);
}