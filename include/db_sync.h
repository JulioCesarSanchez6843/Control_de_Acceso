#pragma once
#include <Arduino.h>

bool pingServer();

bool sendAsistencia(
    String timestamp,
    String rfid_uid,
    String name,
    String account,
    String materia,
    String mode
);

// created_at es opcional; si no se manda, el ESP lo genera
bool sendAlumnoRegistro(
    String uid,
    String nombre,
    String cuenta,
    String materia = String(),
    String created_at = String()
);

// account + created_at también opcionales por compatibilidad
bool sendProfesorRegistro(
    String uid,
    String nombre,
    String cuenta,
    String created_at = String()
);

// Registrar materia/curso en servidor
bool sendMateriaRegistro(
    String materia,
    String profesor,
    String created_at = String()
);