#include "web_utils.h"

String csvEscape(const String &s) {
  String out;
  out.reserve(s.length() + 4);
  for (size_t i = 0; i < (size_t)s.length(); ++i) {
    char c = s.charAt(i);
    if (c == '"') { out += "\"\""; } else out += c;
  }
  return out;
}

String htmlEscape(const String &s) {
  String o; o.reserve(s.length() + 8);
  for (size_t i=0;i<s.length();++i) {
    char c = s.charAt(i);
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else if (c == '\'') o += "&#39;";
    else o += c;
  }
  return o;
}

String jsonEscape(const String &s) {
  String o; o.reserve(s.length() + 8);
  for (size_t i=0;i<s.length();++i) {
    char c = s.charAt(i);
    if (c == '\\' || c == '"') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else o += c;
  }
  return o;
}

String hhmm(int h) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:00", h);
  return String(buf);
}
