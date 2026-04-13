/**
 * display.cpp — TFT Display für Provisioning-Status
 *
 * Zeigt auf dem 240x240 ST7789 Display die aktuellen Schritte an,
 * die der User durchführen muss um das Gerät mit WiFi zu verbinden.
 */

#include "display.h"

Display::Display()
    : _tft(TFT_CS, TFT_DC, TFT_RST) {}

void Display::begin() {
  // Backlight per PWM
  ledcAttach(TFT_BL, 5000, 8);
  ledcWrite(TFT_BL, TFT_BL_BRIGHTNESS);

  // Hardware-SPI auf die nativen FSPI-Pins remappen (shared mit SD-Karte)
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);

  _tft.init(240, 240);
  _tft.setSPISpeed(40000000);
  _tft.setRotation(0);
  _clear();

  DBG_PRINTLN(F("[Display] Initialisiert (240x240 ST7789)"));
}

// ============================================================
// Provisioning-Bildschirme
// ============================================================

void Display::showBoot() {
  _clear();
  _drawCentered(DEVICE_NAME, 80, COL_ACCENT, 4);
  _drawCentered("Starting...", 130, COL_DIMMED, 2);
}

void Display::showProvisioningAP(const String& apName, const String& password) {
  _clear();

  // Titel
  _drawCentered("WiFi Setup", 8, COL_ACCENT, 2);

  // Trennlinie
  _tft.drawFastHLine(20, 30, 200, COL_DIMMED);

  // Schritt 1
  _tft.setTextColor(COL_WARN);
  _tft.setTextSize(1);
  _tft.setCursor(10, 42);
  _tft.print("1. WLAN verbinden:");

  _tft.setTextColor(COL_TITLE);
  _tft.setTextSize(2);
  _tft.setCursor(10, 58);
  _tft.print(apName);

  // Passwort
  _tft.setTextColor(COL_WARN);
  _tft.setTextSize(1);
  _tft.setCursor(10, 84);
  _tft.print("Passwort:");

  _tft.setTextColor(COL_TITLE);
  _tft.setTextSize(2);
  _tft.setCursor(10, 100);
  _tft.print(password);

  // Trennlinie
  _tft.drawFastHLine(20, 124, 200, COL_DIMMED);

  // Schritt 2
  _tft.setTextColor(COL_WARN);
  _tft.setTextSize(1);
  _tft.setCursor(10, 136);
  _tft.print("2. Browser oeffnen:");

  _tft.setTextColor(COL_ACCENT);
  _tft.setTextSize(2);
  _tft.setCursor(10, 152);
  _tft.print("192.168.4.1");

  // Trennlinie
  _tft.drawFastHLine(20, 178, 200, COL_DIMMED);

  // Schritt 3
  _tft.setTextColor(COL_WARN);
  _tft.setTextSize(1);
  _tft.setCursor(10, 190);
  _tft.print("3. Netzwerk scannen");

  _tft.setTextColor(COL_TEXT);
  _tft.setTextSize(1);
  _tft.setCursor(10, 206);
  _tft.print("   und WLAN auswaehlen");

  // Unten: Geräte-Name
  _tft.setTextColor(COL_DIMMED);
  _tft.setTextSize(1);
  _tft.setCursor(10, 228);
  _tft.print(DEVICE_NAME);
}

void Display::showConnecting(const String& ssid) {
  _clear();
  _drawCentered("Verbinde...", 60, COL_WARN, 2);

  _tft.setTextColor(COL_TITLE);
  _tft.setTextSize(2);
  // SSID zentriert
  int16_t x1, y1;
  uint16_t w, h;
  _tft.getTextBounds(ssid.c_str(), 0, 0, &x1, &y1, &w, &h);
  _tft.setCursor((240 - w) / 2, 100);
  _tft.print(ssid);

  _drawCentered("Bitte warten", 150, COL_DIMMED, 1);
}

void Display::showConnected(const String& ssid, const String& ip) {
  _clear();
  _drawCentered("Verbunden!", 40, COL_SUCCESS, 3);

  _tft.drawFastHLine(20, 72, 200, COL_DIMMED);

  _tft.setTextColor(COL_TEXT);
  _tft.setTextSize(1);
  _tft.setCursor(10, 90);
  _tft.print("WLAN:");

  _tft.setTextColor(COL_TITLE);
  _tft.setTextSize(2);
  _tft.setCursor(10, 106);
  _tft.print(ssid);

  _tft.setTextColor(COL_TEXT);
  _tft.setTextSize(1);
  _tft.setCursor(10, 136);
  _tft.print("IP-Adresse:");

  _tft.setTextColor(COL_ACCENT);
  _tft.setTextSize(2);
  _tft.setCursor(10, 152);
  _tft.print(ip);

  _drawCentered(DEVICE_NAME, 220, COL_DIMMED, 1);
}

void Display::showConnectionFailed(const String& reason) {
  _clear();
  _drawCentered("Fehler!", 50, COL_ERROR, 3);

  _tft.setTextColor(COL_TEXT);
  _tft.setTextSize(1);
  _tft.setCursor(10, 100);

  if (reason == "wrong_password") {
    _tft.print("Falsches Passwort.");
  } else if (reason == "timeout") {
    _tft.print("Netzwerk nicht erreichbar.");
  } else {
    _tft.print("Verbindung fehlgeschlagen.");
  }

  _drawCentered("Erneut versuchen", 140, COL_WARN, 2);
  _drawCentered("im Browser", 165, COL_WARN, 2);
}

void Display::showReconnecting(int attempt, int maxAttempts) {
  _clear();
  _drawCentered("Verbindung", 50, COL_WARN, 2);
  _drawCentered("verloren", 75, COL_WARN, 2);

  char buf[32];
  snprintf(buf, sizeof(buf), "Versuch %d/%d", attempt, maxAttempts);
  _drawCentered(buf, 130, COL_TEXT, 2);
}

void Display::showFactoryReset() {
  _clear();
  _drawCentered("Factory", 70, COL_ERROR, 3);
  _drawCentered("Reset", 105, COL_ERROR, 3);
  _drawCentered("Neustart...", 160, COL_DIMMED, 2);
}

void Display::showSdStatus(bool ok, float sizeMB) {
  // Statuszeile unten (y=232..239) ueberschreiben
  _tft.fillRect(0, 232, 240, 8, COL_BG);
  _tft.setTextSize(1);
  _tft.setCursor(4, 232);

  if (ok) {
    _tft.setTextColor(COL_SUCCESS);
    _tft.print("SD:OK ");
    _tft.setTextColor(COL_TEXT);
    if (sizeMB >= 1024.0f) {
      _tft.printf("%.1f GB", sizeMB / 1024.0f);
    } else {
      _tft.printf("%.0f MB", sizeMB);
    }
  } else {
    _tft.setTextColor(COL_ERROR);
    _tft.print("SD: keine Karte");
  }
}

// ============================================================
// Hilfsfunktionen
// ============================================================

void Display::_clear() {
  _tft.fillScreen(COL_BG);
}

void Display::_drawTitle(const char* title) {
  _drawCentered(title, 10, COL_ACCENT, 2);
  _tft.drawFastHLine(20, 32, 200, COL_DIMMED);
}

void Display::_drawCentered(const char* text, int y, uint16_t color, uint8_t size) {
  _tft.setTextSize(size);
  _tft.setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  _tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  _tft.setCursor((240 - w) / 2, y);
  _tft.print(text);
}

void Display::_drawWrapped(const char* text, int x, int y, int maxWidth, uint16_t color, uint8_t size) {
  _tft.setTextSize(size);
  _tft.setTextColor(color);
  _tft.setCursor(x, y);
  _tft.setTextWrap(true);
  _tft.print(text);
  _tft.setTextWrap(false);
}
