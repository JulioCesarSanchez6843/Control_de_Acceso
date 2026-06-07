#pragma once
// Funciones relacionadas con lectura RFID y lógica de autorización.
// Versión online-only: sin SPIFFS, sin cola local, sin sincronización pendiente.

#include <Arduino.h>

String uidBytesToString(byte *uid, byte len);
String nowISO();                // obtiene timestamp local "YYYY-MM-DD HH:MM:SS"
String currentScheduledMateria();
void rfidLoopHandler();         // función que debe llamarse periódicamente para procesar tarjetas.