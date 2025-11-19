// src/time_utils.h
#pragma once
#include <Arduino.h>

String nowISO();
String uidBytesToString(byte *uid, byte len);
String currentScheduledMateria();
void setTimeFromEpoch(uint32_t epoch_seconds);
uint32_t getEpochNow();
void printLocalTimeToSerial();
