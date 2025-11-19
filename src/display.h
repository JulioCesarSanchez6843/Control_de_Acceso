// src/display.h
#pragma once
#include <Arduino.h>

// Inicialización y pantallas comunes
void displayInit();
void showWaitingMessage();
void showAccessGranted(const String &name, const String &materia, const String &uid);
void showAccessDenied(const String &reason, const String &uid);

// LEDs
void ledOff();
void ledRedOn();
void ledGreenOn();

// Mostrar un QR en pantalla con la URL. pixelBoxSize es el tamaño total (en px) del QR (ej. 120).
void showQRCodeOnDisplay(const String &url, int pixelBoxSize);

// Banner que indica que hay un self-register en curso (bloqueo de lectura)
void showSelfRegisterBanner(const String &uid);
