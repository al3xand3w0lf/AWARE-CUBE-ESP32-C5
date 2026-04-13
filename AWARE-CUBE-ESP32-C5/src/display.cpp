/**
 * display.cpp — TFT Display für Provisioning-Status
 *
 * Zeigt auf dem 240x240 ST7789 Display die aktuellen Schritte an,
 * die der User durchführen muss um das Gerät mit WiFi zu verbinden.
 */

#include "display.h"
#include <qrcode.h>

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

  _drawCentered("Connect to WiFi", 2, COL_ACCENT, 2);
  _drawCentered("Point your camera", 26, COL_TEXT, 2);
  _drawCentered("at the code", 46, COL_TEXT, 2);

  String payload = "WIFI:T:WPA;S:" + apName + ";P:" + password + ";;";

  const uint8_t qrVersion = 4;                 // 33x33 Module
  QRCode qr;
  uint8_t qrData[qrcode_getBufferSize(qrVersion)];
  qrcode_initText(&qr, qrData, qrVersion, ECC_LOW, payload.c_str());

  const int modules  = qr.size;                 // 33
  const int pxPer    = 5;                       // 33*5 = 165 px (~20% kleiner)
  const int qrPx     = modules * pxPer;
  const int qrX      = (240 - qrPx) / 2;
  const int qrY      = 68;

  _tft.fillRect(qrX - pxPer, qrY - pxPer,
                qrPx + 2 * pxPer, qrPx + 2 * pxPer, ST77XX_WHITE);

  for (int y = 0; y < modules; y++) {
    for (int x = 0; x < modules; x++) {
      if (qrcode_getModule(&qr, x, y)) {
        _tft.fillRect(qrX + x * pxPer, qrY + y * pxPer, pxPer, pxPer, ST77XX_BLACK);
      }
    }
  }
}

void Display::showProvisioningUrl(const String& url) {
  _clear();

  _drawCentered("Almost done", 2, COL_SUCCESS, 2);
  _drawCentered("Scan again to pick", 26, COL_TEXT, 2);
  _drawCentered("your home WiFi", 46, COL_TEXT, 2);

  const uint8_t qrVersion = 3;                 // 29x29 reicht fuer kurze URL
  QRCode qr;
  uint8_t qrData[qrcode_getBufferSize(qrVersion)];
  qrcode_initText(&qr, qrData, qrVersion, ECC_LOW, url.c_str());

  const int modules = qr.size;                  // 29
  const int pxPer   = 5;                        // 29*5 = 145 px
  const int qrPx    = modules * pxPer;
  const int qrX     = (240 - qrPx) / 2;
  const int qrY     = 78;

  _tft.fillRect(qrX - pxPer, qrY - pxPer,
                qrPx + 2 * pxPer, qrPx + 2 * pxPer, ST77XX_WHITE);

  for (int y = 0; y < modules; y++) {
    for (int x = 0; x < modules; x++) {
      if (qrcode_getModule(&qr, x, y)) {
        _tft.fillRect(qrX + x * pxPer, qrY + y * pxPer, pxPer, pxPer, ST77XX_BLACK);
      }
    }
  }
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

void Display::showSdInit(bool ok, float sizeMB) {
  _clear();
  _drawCentered("SD Karte", 60, COL_ACCENT, 3);

  if (ok) {
    _drawCentered("OK", 110, COL_SUCCESS, 4);
    char buf[24];
    if (sizeMB >= 1024.0f) {
      snprintf(buf, sizeof(buf), "%.1f GB", sizeMB / 1024.0f);
    } else {
      snprintf(buf, sizeof(buf), "%.0f MB", sizeMB);
    }
    _drawCentered(buf, 165, COL_TEXT, 2);
  } else {
    _drawCentered("Keine Karte", 120, COL_ERROR, 2);
    _drawCentered("erkannt", 150, COL_ERROR, 2);
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
