#ifndef DB_SYNC_H
#define DB_SYNC_H

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

#endif