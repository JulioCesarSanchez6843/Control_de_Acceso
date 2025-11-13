#include "display.h"
#include "globals.h"
#include "config.h"
#include <SPIFFS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

void displayInit() {
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0,2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.println("CONTROL DE ACCESO LAB");
  tft.println("---------------------");
  tft.println();
  tft.println("Esperando tarjeta...");
  pinMode(RGB_R_PIN, OUTPUT);
  pinMode(RGB_G_PIN, OUTPUT);
  ledOff();
}

void showWaitingMessage() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0,6);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.println("Esperando tarjeta...");
}

void showAccessGranted(const String &name, const String &materia, const String &uid) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0,6);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(1);
  tft.println("ACCESO CONCEDIDO");
  tft.setTextColor(ST77XX_WHITE);
  tft.println(name);
  tft.println(materia);
  tft.println(uid);
}

void showAccessDenied(const String &reason, const String &uid) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0,6);
  tft.setTextColor(ST77XX_RED);
  tft.setTextSize(1);
  tft.println("ACCESO DENEGADO");
  tft.setTextColor(ST77XX_WHITE);
  if (reason.length()) tft.println(reason);
  tft.println(uid);
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
