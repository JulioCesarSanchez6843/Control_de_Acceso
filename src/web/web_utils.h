#pragma once
#include <Arduino.h>

String csvEscape(const String &s);
String htmlEscape(const String &s);
String jsonEscape(const String &s);
String hhmm(int h);
