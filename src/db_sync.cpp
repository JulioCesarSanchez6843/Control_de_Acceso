// src/db_sync.cpp
#include "db_sync.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

static const char* SERVER_URL = "http://192.168.100.8:8000";
static const uint32_t HTTP_TIMEOUT_MS = 15000;

// --------------------------------------------------
// Utilidades
// --------------------------------------------------
static bool wifiReady() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("DB_SYNC: sin WiFi");
    return false;
  }
  return true;
}

static String nowISO() {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
  return String(buf);
}

static String urlEncode(const String& str) {
  String encoded;
  encoded.reserve(str.length() * 3);

  for (size_t i = 0; i < str.length(); ++i) {
    char c = str[i];
    bool safe =
      (c >= 'A' && c <= 'Z') ||
      (c >= 'a' && c <= 'z') ||
      (c >= '0' && c <= '9') ||
      c == '-' || c == '_' || c == '.' || c == '~';

    if (safe) {
      encoded += c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }

  return encoded;
}

static String makeUrl(const String& path) {
  return String(SERVER_URL) + path;
}

static bool requestHttp(
    const String& method,
    const String& url,
    const String& payload,
    int& httpCode,
    String& response
) {
  if (!wifiReady()) return false;

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);

  Serial.print("DB_SYNC ");
  Serial.print(method);
  Serial.print(" -> ");
  Serial.println(url);

  if (!http.begin(client, url)) {
    Serial.println("DB_SYNC: http.begin() falló");
    return false;
  }

  if (method == "POST" || method == "PUT" || method == "DELETE") {
    http.addHeader("Content-Type", "application/json");
  }

  if (method == "GET") {
    httpCode = http.GET();
  } else if (method == "POST") {
    httpCode = http.POST(payload);
  } else if (method == "PUT") {
    httpCode = http.PUT(payload);
  } else if (method == "DELETE") {
    httpCode = http.sendRequest("DELETE");
  } else {
    Serial.println("DB_SYNC: método HTTP no soportado");
    http.end();
    return false;
  }

  response = http.getString();

  Serial.printf("DB_SYNC HTTP code: %d\n", httpCode);
  if (response.length()) {
    Serial.print("DB_SYNC response: ");
    Serial.println(response);
  }

  http.end();
  return (httpCode > 0);
}

static String buildJson(const JsonDocument& doc) {
  String json;
  serializeJson(doc, json);
  return json;
}

static bool isSuccessCode(int code) {
  return code >= 200 && code < 300;
}

// --------------------------------------------------
// Ping al servidor
// --------------------------------------------------
bool pingServer() {
  int code = 0;
  String body;
  if (!requestHttp("GET", makeUrl("/ping"), "", code, body)) {
    return false;
  }
  return (code == 200);
}

// --------------------------------------------------
// ASISTENCIAS
// --------------------------------------------------
bool sendAsistencia(
    String timestamp,
    String rfid_uid,
    String name,
    String account,
    String materia,
    String mode
) {
  DynamicJsonDocument doc(512);
  doc["timestamp"] = timestamp;
  doc["rfid_uid"] = rfid_uid;
  doc["name"] = name;
  doc["account"] = account;
  if (materia.length()) doc["materia"] = materia;
  doc["mode"] = mode;

  String json = buildJson(doc);

  int code = 0;
  String response;
  if (!requestHttp("POST", makeUrl("/asistencia"), json, code, response)) {
    return false;
  }

  return isSuccessCode(code);
}

String getAsistenciaById(int asistencia_id) {
  int code = 0;
  String body;
  if (!requestHttp("GET", makeUrl("/asistencia/" + String(asistencia_id)), "", code, body)) {
    return String();
  }
  if (!isSuccessCode(code)) return String();
  return body;
}

String listAsistencias(const String& rfid_uid, const String& materia) {
  String url = makeUrl("/asistencias");
  bool first = true;

  if (rfid_uid.length()) {
    url += first ? "?" : "&";
    url += "rfid_uid=" + urlEncode(rfid_uid);
    first = false;
  }
  if (materia.length()) {
    url += first ? "?" : "&";
    url += "materia=" + urlEncode(materia);
  }

  int code = 0;
  String body;
  if (!requestHttp("GET", url, "", code, body)) return String();
  if (!isSuccessCode(code)) return String();
  return body;
}

bool updateAsistenciaById(
    int asistencia_id,
    String timestamp,
    String rfid_uid,
    String name,
    String account,
    String materia,
    String mode
) {
  DynamicJsonDocument doc(512);
  doc["timestamp"] = timestamp;
  doc["rfid_uid"] = rfid_uid;
  doc["name"] = name;
  doc["account"] = account;
  if (materia.length()) doc["materia"] = materia;
  doc["mode"] = mode;

  String json = buildJson(doc);

  int code = 0;
  String response;
  if (!requestHttp("PUT", makeUrl("/asistencia/" + String(asistencia_id)), json, code, response)) {
    return false;
  }
  return isSuccessCode(code);
}

bool deleteAsistenciaById(int asistencia_id) {
  int code = 0;
  String response;
  if (!requestHttp("DELETE", makeUrl("/asistencia/" + String(asistencia_id)), "", code, response)) {
    return false;
  }
  return isSuccessCode(code);
}

// --------------------------------------------------
// ALUMNOS
// --------------------------------------------------
bool sendAlumnoRegistro(
    String uid,
    String nombre,
    String cuenta,
    String materia,
    String created_at
) {
  if (created_at.length() == 0) {
    created_at = nowISO();
  }

  DynamicJsonDocument doc(512);
  doc["rfid_uid"] = uid;
  doc["name"] = nombre;
  doc["account"] = cuenta;
  if (materia.length()) doc["materia"] = materia;
  doc["created_at"] = created_at;

  String json = buildJson(doc);

  int code = 0;
  String response;
  if (!requestHttp("POST", makeUrl("/alumno"), json, code, response)) {
    return false;
  }

  if (code == 409) {
    Serial.println("DB_SYNC: alumno duplicado en Oracle (409)");
    return false;
  }

  return isSuccessCode(code);
}

String getAlumnoById(int alumno_id) {
  int code = 0;
  String body;
  if (!requestHttp("GET", makeUrl("/alumno/" + String(alumno_id)), "", code, body)) {
    return String();
  }
  if (!isSuccessCode(code)) return String();
  return body;
}

String listAlumnos(const String& materia) {
  String url = makeUrl("/alumnos");
  if (materia.length()) {
    url += "?materia=" + urlEncode(materia);
  }

  int code = 0;
  String body;
  if (!requestHttp("GET", url, "", code, body)) return String();
  if (!isSuccessCode(code)) return String();
  return body;
}

bool updateAlumnoById(
    int alumno_id,
    String uid,
    String nombre,
    String cuenta,
    String materia,
    String created_at
) {
  DynamicJsonDocument doc(512);
  doc["rfid_uid"] = uid;
  doc["name"] = nombre;
  doc["account"] = cuenta;
  if (materia.length()) doc["materia"] = materia;
  doc["created_at"] = (created_at.length() ? created_at : nowISO());

  String json = buildJson(doc);

  int code = 0;
  String response;
  if (!requestHttp("PUT", makeUrl("/alumno/" + String(alumno_id)), json, code, response)) {
    return false;
  }

  return isSuccessCode(code);
}

bool deleteAlumnoById(int alumno_id) {
  int code = 0;
  String response;
  if (!requestHttp("DELETE", makeUrl("/alumno/" + String(alumno_id)), "", code, response)) {
    return false;
  }
  return isSuccessCode(code);
}

// --------------------------------------------------
// PROFESORES
// --------------------------------------------------
bool sendProfesorRegistro(
    String uid,
    String nombre,
    String cuenta,
    String created_at
) {
  if (created_at.length() == 0) {
    created_at = nowISO();
  }

  DynamicJsonDocument doc(512);
  doc["rfid_uid"] = uid;
  doc["name"] = nombre;
  doc["account"] = cuenta;
  doc["created_at"] = created_at;

  String json = buildJson(doc);

  int code = 0;
  String response;
  if (!requestHttp("POST", makeUrl("/profesor"), json, code, response)) {
    return false;
  }

  if (code == 409) {
    Serial.println("DB_SYNC: profesor duplicado en Oracle (409)");
    return false;
  }

  return isSuccessCode(code);
}

String getProfesorByUid(const String& uid) {
  int code = 0;
  String body;
  if (!requestHttp("GET", makeUrl("/profesor/" + urlEncode(uid)), "", code, body)) {
    return String();
  }
  if (!isSuccessCode(code)) return String();
  return body;
}

String listProfesores() {
  int code = 0;
  String body;
  if (!requestHttp("GET", makeUrl("/profesores"), "", code, body)) return String();
  if (!isSuccessCode(code)) return String();
  return body;
}

bool updateProfesorByUid(
    const String& oldUid,
    const String& newUid,
    const String& nombre,
    const String& cuenta,
    const String& created_at
) {
  DynamicJsonDocument doc(512);
  doc["rfid_uid"] = newUid.length() ? newUid : oldUid;
  doc["name"] = nombre;
  doc["account"] = cuenta;
  doc["created_at"] = (created_at.length() ? created_at : nowISO());

  String json = buildJson(doc);

  int code = 0;
  String response;
  if (!requestHttp("PUT", makeUrl("/profesor/" + urlEncode(oldUid)), json, code, response)) {
    return false;
  }

  return isSuccessCode(code);
}

bool deleteProfesorByUid(const String& uid, bool cascade) {
  String url = makeUrl("/profesor/" + urlEncode(uid));
  url += cascade ? "?cascade=true" : "?cascade=false";

  int code = 0;
  String response;
  if (!requestHttp("DELETE", url, "", code, response)) {
    return false;
  }
  return isSuccessCode(code);
}

// --------------------------------------------------
// MATERIAS
// --------------------------------------------------
bool sendMateriaRegistro(
    String materia,
    String profesor,
    String created_at
) {
  if (created_at.length() == 0) {
    created_at = nowISO();
  }

  DynamicJsonDocument doc(512);
  doc["materia"] = materia;
  doc["profesor"] = profesor;
  doc["created_at"] = created_at;

  String json = buildJson(doc);

  int code = 0;
  String response;
  if (!requestHttp("POST", makeUrl("/materia"), json, code, response)) {
    return false;
  }

  if (code == 409) {
    Serial.println("DB_SYNC: materia duplicada en Oracle (409)");
    return false;
  }

  return isSuccessCode(code);
}

String getMateriaByName(const String& materia) {
  int code = 0;
  String body;
  if (!requestHttp("GET", makeUrl("/materia/" + urlEncode(materia)), "", code, body)) {
    return String();
  }
  if (!isSuccessCode(code)) return String();
  return body;
}

String listMaterias() {
  int code = 0;
  String body;
  if (!requestHttp("GET", makeUrl("/materias"), "", code, body)) return String();
  if (!isSuccessCode(code)) return String();
  return body;
}

bool updateMateriaByName(
    const String& oldMateria,
    const String& newMateria,
    const String& profesor,
    const String& created_at
) {
  DynamicJsonDocument doc(512);
  doc["materia"] = newMateria.length() ? newMateria : oldMateria;
  doc["profesor"] = profesor;
  doc["created_at"] = (created_at.length() ? created_at : nowISO());

  String json = buildJson(doc);

  int code = 0;
  String response;
  if (!requestHttp("PUT", makeUrl("/materia/" + urlEncode(oldMateria)), json, code, response)) {
    return false;
  }

  return isSuccessCode(code);
}

bool deleteMateriaByName(const String& materia, bool cascade) {
  String url = makeUrl("/materia/" + urlEncode(materia));
  url += cascade ? "?cascade=true" : "?cascade=false";

  int code = 0;
  String response;
  if (!requestHttp("DELETE", url, "", code, response)) {
    return false;
  }
  return isSuccessCode(code);
}

// --------------------------------------------------
// PROFESOR_MATERIA
// --------------------------------------------------
bool sendProfesorMateriaRegistro(
    String uid,
    String materia,
    String created_at
) {
  if (created_at.length() == 0) {
    created_at = nowISO();
  }

  DynamicJsonDocument doc(512);
  doc["rfid_uid"] = uid;
  doc["materia"] = materia;
  doc["created_at"] = created_at;

  String json = buildJson(doc);

  int code = 0;
  String response;
  if (!requestHttp("POST", makeUrl("/profesor_materia"), json, code, response)) {
    return false;
  }

  if (code == 409) {
    Serial.println("DB_SYNC: relación profesor-materia duplicada (409)");
    return false;
  }

  return isSuccessCode(code);
}

String getProfesorMateriaById(int pm_id) {
  int code = 0;
  String body;
  if (!requestHttp("GET", makeUrl("/profesor_materia/" + String(pm_id)), "", code, body)) {
    return String();
  }
  if (!isSuccessCode(code)) return String();
  return body;
}

String listProfesorMateria() {
  int code = 0;
  String body;
  if (!requestHttp("GET", makeUrl("/profesor_materia"), "", code, body)) return String();
  if (!isSuccessCode(code)) return String();
  return body;
}

bool updateProfesorMateriaById(
    int pm_id,
    String uid,
    String materia,
    String created_at
) {
  DynamicJsonDocument doc(512);
  doc["rfid_uid"] = uid;
  doc["materia"] = materia;
  doc["created_at"] = (created_at.length() ? created_at : nowISO());

  String json = buildJson(doc);

  int code = 0;
  String response;
  if (!requestHttp("PUT", makeUrl("/profesor_materia/" + String(pm_id)), json, code, response)) {
    return false;
  }

  return isSuccessCode(code);
}

bool deleteProfesorMateriaById(int pm_id) {
  int code = 0;
  String response;
  if (!requestHttp("DELETE", makeUrl("/profesor_materia/" + String(pm_id)), "", code, response)) {
    return false;
  }
  return isSuccessCode(code);
}

// --------------------------------------------------
// HORARIOS
// --------------------------------------------------
bool sendHorarioRegistro(
    String materia,
    String profesor,
    String dia,
    String hora_inicio,
    String hora_fin,
    String created_at
) {
  if (created_at.length() == 0) {
    created_at = nowISO();
  }

  DynamicJsonDocument doc(512);
  doc["materia"] = materia;
  if (profesor.length()) doc["profesor"] = profesor;
  doc["dia"] = dia;
  doc["hora_inicio"] = hora_inicio;
  doc["hora_fin"] = hora_fin;
  doc["created_at"] = created_at;

  String json = buildJson(doc);

  int code = 0;
  String response;
  if (!requestHttp("POST", makeUrl("/horario"), json, code, response)) {
    return false;
  }

  if (code == 409) {
    Serial.println("DB_SYNC: horario duplicado o slot ocupado (409)");
    return false;
  }

  return isSuccessCode(code);
}

String getHorarioById(int horario_id) {
  int code = 0;
  String body;
  if (!requestHttp("GET", makeUrl("/horario/" + String(horario_id)), "", code, body)) {
    return String();
  }
  if (!isSuccessCode(code)) return String();
  return body;
}

String listHorarios(const String& materia) {
  String url = makeUrl("/horarios");
  if (materia.length()) {
    url += "?materia=" + urlEncode(materia);
  }

  int code = 0;
  String body;
  if (!requestHttp("GET", url, "", code, body)) return String();
  if (!isSuccessCode(code)) return String();
  return body;
}

bool updateHorarioById(
    int horario_id,
    String materia,
    String profesor,
    String dia,
    String hora_inicio,
    String hora_fin,
    String created_at
) {
  DynamicJsonDocument doc(512);
  doc["materia"] = materia;
  if (profesor.length()) doc["profesor"] = profesor;
  doc["dia"] = dia;
  doc["hora_inicio"] = hora_inicio;
  doc["hora_fin"] = hora_fin;
  doc["created_at"] = (created_at.length() ? created_at : nowISO());

  String json = buildJson(doc);

  int code = 0;
  String response;
  if (!requestHttp("PUT", makeUrl("/horario/" + String(horario_id)), json, code, response)) {
    return false;
  }

  return isSuccessCode(code);
}

bool deleteHorarioById(int horario_id) {
  int code = 0;
  String response;
  if (!requestHttp("DELETE", makeUrl("/horario/" + String(horario_id)), "", code, response)) {
    return false;
  }
  return isSuccessCode(code);
}

// --------------------------------------------------
// ACCESOS DENEGADOS
// --------------------------------------------------
bool sendAccesoDenegadoRegistro(
    String timestamp,
    String rfid_uid,
    String note
) {
  DynamicJsonDocument doc(512);
  doc["timestamp"] = timestamp;
  doc["rfid_uid"] = rfid_uid;
  if (note.length()) doc["note"] = note;

  String json = buildJson(doc);

  int code = 0;
  String response;
  if (!requestHttp("POST", makeUrl("/acceso_denegado"), json, code, response)) {
    return false;
  }

  return isSuccessCode(code);
}

String getAccesoDenegadoById(int id) {
  int code = 0;
  String body;
  if (!requestHttp("GET", makeUrl("/acceso_denegado/" + String(id)), "", code, body)) {
    return String();
  }
  if (!isSuccessCode(code)) return String();
  return body;
}

String listAccesosDenegados() {
  int code = 0;
  String body;
  if (!requestHttp("GET", makeUrl("/accesos_denegados"), "", code, body)) return String();
  if (!isSuccessCode(code)) return String();
  return body;
}

bool updateAccesoDenegadoById(
    int id,
    String timestamp,
    String rfid_uid,
    String note
) {
  DynamicJsonDocument doc(512);
  doc["timestamp"] = timestamp;
  doc["rfid_uid"] = rfid_uid;
  if (note.length()) doc["note"] = note;

  String json = buildJson(doc);

  int code = 0;
  String response;
  if (!requestHttp("PUT", makeUrl("/acceso_denegado/" + String(id)), json, code, response)) {
    return false;
  }

  return isSuccessCode(code);
}

bool deleteAccesoDenegadoById(int id) {
  int code = 0;
  String response;
  if (!requestHttp("DELETE", makeUrl("/acceso_denegado/" + String(id)), "", code, response)) {
    return false;
  }
  return isSuccessCode(code);
}

// --------------------------------------------------
// NOTIFICACIONES
// --------------------------------------------------
bool sendNotificacionRegistro(
    String timestamp,
    String rfid_uid,
    String name,
    String account,
    String note
) {
  DynamicJsonDocument doc(1024);
  doc["timestamp"] = timestamp;
  doc["rfid_uid"] = rfid_uid;
  if (name.length()) doc["name"] = name;
  if (account.length()) doc["account"] = account;
  doc["note"] = note;

  String json = buildJson(doc);

  int code = 0;
  String response;
  if (!requestHttp("POST", makeUrl("/notificacion"), json, code, response)) {
    return false;
  }

  return isSuccessCode(code);
}

String getNotificacionById(int notif_id) {
  int code = 0;
  String body;
  if (!requestHttp("GET", makeUrl("/notificacion/" + String(notif_id)), "", code, body)) {
    return String();
  }
  if (!isSuccessCode(code)) return String();
  return body;
}

String listNotificaciones(bool solo_no_leidas) {
  String url = makeUrl("/notificaciones");
  url += solo_no_leidas ? "?solo_no_leidas=true" : "?solo_no_leidas=false";

  int code = 0;
  String body;
  if (!requestHttp("GET", url, "", code, body)) return String();
  if (!isSuccessCode(code)) return String();
  return body;
}

bool updateNotificacionById(
    int notif_id,
    String timestamp,
    String rfid_uid,
    String name,
    String account,
    String note,
    int leida
) {
  DynamicJsonDocument doc(1024);
  doc["timestamp"] = timestamp;
  doc["rfid_uid"] = rfid_uid;
  if (name.length()) doc["name"] = name;
  if (account.length()) doc["account"] = account;
  doc["note"] = note;
  doc["leida"] = leida;

  String json = buildJson(doc);

  int code = 0;
  String response;
  if (!requestHttp("PUT", makeUrl("/notificacion/" + String(notif_id)), json, code, response)) {
    return false;
  }

  return isSuccessCode(code);
}

bool marcarNotificacionLeida(int notif_id) {
  int code = 0;
  String response;
  if (!requestHttp("PUT", makeUrl("/notificacion/" + String(notif_id) + "/leida"), "", code, response)) {
    return false;
  }
  return isSuccessCode(code);
}

bool deleteNotificacionById(int notif_id) {
  int code = 0;
  String response;
  if (!requestHttp("DELETE", makeUrl("/notificacion/" + String(notif_id)), "", code, response)) {
    return false;
  }
  return isSuccessCode(code);
}