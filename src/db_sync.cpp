#include "db_sync.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

String SERVER_URL = "http://IP_DE_TU_PC:8000"; 
// CAMBIA esto por la IP de tu PC

bool pingServer() {

    HTTPClient http;

    http.begin(SERVER_URL + "/ping");

    int httpCode = http.GET();

    http.end();

    return (httpCode == 200);
}

bool sendAsistencia(
    String timestamp,
    String rfid_uid,
    String name,
    String account,
    String materia,
    String mode
) {

    HTTPClient http;

    http.begin(SERVER_URL + "/asistencia");
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

    int httpCode = http.POST(json);

    http.end();

    return (httpCode == 200);
}