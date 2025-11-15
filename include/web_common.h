#pragma once
#include <Arduino.h>
#include <WebServer.h>

// Generación de HTML
String htmlHeader(const char* title);
String htmlFooter();

// Helpers para rutas web
void handleRoot();
void handleStatus();
void registerRoutes();

// Declaraciones de validación 
bool existsUserUidMateria(const String& uid, const String& materia);
bool existsUserAccountMateria(const String& account, const String& materia);
