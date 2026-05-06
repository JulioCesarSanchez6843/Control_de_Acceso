#pragma once
// Funciones relacionadas con lectura RFID y lógica de autorización.
// Flujo 100% online: toda persistencia y consulta se hace vía backend/base de datos.

#include <Arduino.h>

String uidBytesToString(byte *uid, byte len);
String nowISO(); // obtiene timestamp local "YYYY-MM-DD HH:MM:SS"
String currentScheduledMateria();

// Procesamiento periódico de tarjetas RFID
void rfidLoopHandler();