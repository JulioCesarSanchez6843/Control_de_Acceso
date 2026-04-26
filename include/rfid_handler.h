#pragma once
// Funciones relacionadas con lectura RFID y lógica de autorización.

#include <Arduino.h>

String uidBytesToString(byte *uid, byte len);
String nowISO(); // obtiene timestamp local "YYYY-MM-DD HH:MM:SS"
String currentScheduledMateria();

// Procesamiento periódico de tarjetas RFID
void rfidLoopHandler();

// Sincroniza los registros pendientes en SPIFFS hacia Oracle/FastAPI
void syncPendingToServer();

void syncPendingToServerForce(); // solo para boot, sin throttle