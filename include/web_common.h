#pragma once
// Utilidades comunes para generación de HTML (cabecera/pie), y prototipos
// que usan en múltiples módulos web.

#include <Arduino.h>
#include <WebServer.h>

// Generación de HTML
String htmlHeader(const char* title);
String htmlFooter();

// Helpers para rutas web generales (se declaran aquí si quieres centralizarlos)
void handleRoot();
void handleStatus();

// Exporta la función que registra las rutas (definida en web_routes.cpp)
void registerRoutes();
