#pragma once
#include <Arduino.h>

// --------------------------------------------------
// Conexión / salud del servidor
// --------------------------------------------------
bool pingServer();

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
);

String getAsistenciaById(int asistencia_id);
String listAsistencias(const String& rfid_uid = String(), const String& materia = String());
bool updateAsistenciaById(
    int asistencia_id,
    String timestamp,
    String rfid_uid,
    String name,
    String account,
    String materia,
    String mode
);
bool deleteAsistenciaById(int asistencia_id);

// --------------------------------------------------
// ALUMNOS
// --------------------------------------------------
// created_at es opcional; si no se manda, el ESP lo genera
bool sendAlumnoRegistro(
    String uid,
    String nombre,
    String cuenta,
    String materia = String(),
    String created_at = String()
);

String getAlumnoById(int alumno_id);
String listAlumnos(const String& materia = String());
bool updateAlumnoById(
    int alumno_id,
    String uid,
    String nombre,
    String cuenta,
    String materia = String(),
    String created_at = String()
);
bool deleteAlumnoById(int alumno_id);

// --------------------------------------------------
// PROFESORES
// --------------------------------------------------
// account + created_at también opcionales por compatibilidad
bool sendProfesorRegistro(
    String uid,
    String nombre,
    String cuenta,
    String created_at = String()
);

String getProfesorByUid(const String& uid);
String listProfesores();
bool updateProfesorByUid(
    const String& oldUid,
    const String& newUid,
    const String& nombre,
    const String& cuenta,
    const String& created_at = String()
);
bool deleteProfesorByUid(const String& uid, bool cascade = false);

// --------------------------------------------------
// MATERIAS
// --------------------------------------------------
bool sendMateriaRegistro(
    String materia,
    String profesor,
    String created_at = String()
);

String getMateriaByName(const String& materia);
String listMaterias();
bool updateMateriaByName(
    const String& oldMateria,
    const String& newMateria,
    const String& profesor,
    const String& created_at = String()
);
bool deleteMateriaByName(const String& materia, bool cascade = true);

// --------------------------------------------------
// PROFESOR_MATERIA
// --------------------------------------------------
bool sendProfesorMateriaRegistro(
    String uid,
    String materia,
    String created_at = String()
);

String getProfesorMateriaById(int pm_id);
String listProfesorMateria();
bool updateProfesorMateriaById(
    int pm_id,
    String uid,
    String materia,
    String created_at = String()
);
bool deleteProfesorMateriaById(int pm_id);

// --------------------------------------------------
// HORARIOS
// --------------------------------------------------
bool sendHorarioRegistro(
    String materia,
    String profesor,
    String dia,
    String hora_inicio,
    String hora_fin,
    String created_at = String()
);

String getHorarioById(int horario_id);
String listHorarios(const String& materia = String());
bool updateHorarioById(
    int horario_id,
    String materia,
    String profesor,
    String dia,
    String hora_inicio,
    String hora_fin,
    String created_at = String()
);
bool deleteHorarioById(int horario_id);

// --------------------------------------------------
// ACCESOS DENEGADOS
// --------------------------------------------------
bool sendAccesoDenegadoRegistro(
    String timestamp,
    String rfid_uid,
    String note = String()
);

String getAccesoDenegadoById(int id);
String listAccesosDenegados();
bool updateAccesoDenegadoById(
    int id,
    String timestamp,
    String rfid_uid,
    String note = String()
);
bool deleteAccesoDenegadoById(int id);

// --------------------------------------------------
// NOTIFICACIONES
// --------------------------------------------------
bool sendNotificacionRegistro(
    String timestamp,
    String rfid_uid,
    String name,
    String account,
    String note
);

String getNotificacionById(int notif_id);
String listNotificaciones(bool solo_no_leidas = true);
bool updateNotificacionById(
    int notif_id,
    String timestamp,
    String rfid_uid,
    String name,
    String account,
    String note,
    int leida
);
bool marcarNotificacionLeida(int notif_id);
bool deleteNotificacionById(int notif_id);