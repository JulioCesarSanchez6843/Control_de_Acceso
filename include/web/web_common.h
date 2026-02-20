#pragma once
#include <Arduino.h>

// ====================
// Declaraciones públicas del módulo web_common
// ====================

// Conteo de notificaciones no leídas
int unreadNotifCount();

// HTML base
String htmlHeader(const char* title);
String htmlFooter();

// Rutas principales
void handleRoot();
void handleStatus();
