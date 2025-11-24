#pragma once
// Declaraciones públicas de utilidades de archivos usadas por el proyecto.
#include <vector>
#include <Arduino.h>
#include <FS.h>

// Parsea una línea CSV con campos entre comillas: "a","b c","d"
std::vector<String> parseQuotedCSVLine(const String &line);

// Añade una línea al final del archivo. Devuelve true si tuvo éxito.
bool appendLineToFile(const char *path, const String &line);

// Escribe todas las líneas (sobrescribe el archivo). Devuelve true si tuvo éxito.
bool writeAllLines(const char *path, const std::vector<String> &lines);

// Inicializa archivos (crea cabeceras si no existen)
void initFiles();
