// src/display.cpp
#include "globals.h"
#include "display.h"

// Inicializa pantalla (tft está definido en globals.cpp)
void displayInit() {
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
}

void ledOff() {
  digitalWrite(RGB_R_PIN, HIGH);
  digitalWrite(RGB_G_PIN, HIGH);
}

void ledRedOn() {
  digitalWrite(RGB_R_PIN, LOW);
  digitalWrite(RGB_G_PIN, HIGH);
}

void ledGreenOn() {
  digitalWrite(RGB_R_PIN, HIGH);
  digitalWrite(RGB_G_PIN, LOW);
}

// Implementaciones SIN valores por defecto (los defaults van en display.h)
void showMessage(const char *title, const String &line1, const String &line2, uint16_t color, unsigned long ms) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0,6);
  tft.setTextColor(color);
  tft.setTextSize(1);
  tft.println(title);
  tft.setTextColor(ST77XX_WHITE);
  if (line1.length()) tft.println(line1);
  if (line2.length()) tft.println(line2);
  delay(ms);
}

// showGranted: abre la puerta brevemente
void showGranted(const String &name, const String &materia, const String &uid, unsigned long ms) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0,6);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(1);
  tft.println("ACCESO CONCEDIDO");
  tft.setTextColor(ST77XX_WHITE);
  tft.println(name);
  tft.println(materia);
  tft.println(uid);
  ledGreenOn();
  puerta.write(90);
  delay(ms);
  puerta.write(0);
  ledOff();
}

void showDenied(const String &uid, const String &note, unsigned long ms) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0,6);
  tft.setTextColor(ST77XX_RED);
  tft.setTextSize(1);
  tft.println("ACCESO DENEGADO");
  tft.setTextColor(ST77XX_WHITE);
  tft.println(uid);
  if (note.length()) tft.println(note);
  ledRedOn();
  delay(ms);
  ledOff();
}
