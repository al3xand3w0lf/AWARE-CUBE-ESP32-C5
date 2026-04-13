/**
 * display.h — TFT Display Abstraktion für Provisioning-Status
 *
 * ST7789 240x240, Hardware-SPI auf ESP32-C5
 * Zeigt dem User die Schritte zur WiFi-Einrichtung.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include "config.h"

// Farben
#define COL_BG        ST77XX_BLACK
#define COL_TITLE     ST77XX_WHITE
#define COL_TEXT      0xBDF7    // Hellgrau
#define COL_ACCENT    0x07FF    // Cyan
#define COL_SUCCESS   0x07E0    // Grün
#define COL_ERROR     0xF800    // Rot
#define COL_WARN      0xFFE0    // Gelb
#define COL_DIMMED    0x7BEF    // Dunkelgrau

class Display {
public:
  Display();
  void begin();

  // Provisioning-Bildschirme
  void showBoot();
  void showProvisioningAP(const String& apName, const String& password);
  void showProvisioningUrl(const String& url);  // Nach WLAN-Connect: QR mit Portal-URL
  void showConnecting(const String& ssid);
  void showConnected(const String& ssid, const String& ip);
  void showConnectionFailed(const String& reason);
  void showReconnecting(int attempt, int maxAttempts);
  void showFactoryReset();

  // Vollbild-Screen fuer SD-Init (wird fuer ~3s zwischen Boot und WiFi-Setup gezeigt)
  void showSdInit(bool ok, float sizeMB);

private:
  Adafruit_ST7789 _tft;

  void _clear();
  void _drawTitle(const char* title);
  void _drawCentered(const char* text, int y, uint16_t color, uint8_t size);
  void _drawWrapped(const char* text, int x, int y, int maxWidth, uint16_t color, uint8_t size);
};

#endif // DISPLAY_H
