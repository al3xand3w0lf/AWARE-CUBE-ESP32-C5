// display.h — ST7789 240x240. Task dispatched auf AppState.
// Rendering-Routinen portiert aus legacy src/display.cpp (klassen-basiert ->
// namespace + file-static Adafruit_ST7789).

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include "app_state.h"

// Farben (wie legacy)
#define COL_BG        0x0000  // ST77XX_BLACK
#define COL_TITLE     0xFFFF  // ST77XX_WHITE
#define COL_TEXT      0xBDF7
#define COL_ACCENT    0x07FF
#define COL_SUCCESS   0x07E0
#define COL_ERROR     0xF800
#define COL_WARN      0xFFE0
#define COL_DIMMED    0x7BEF

namespace Display {
  bool begin();
  void task(void* arg);

  // Low-level Screens
  void showBoot();
  void showSdInit(bool ok, float sizeMB);
  void showProvisioningAP(const String& apName, const String& password);
  void showProvisioningUrl(const String& url);
  void showTransitionLookAtDevice();
  void showConnecting(const String& ssid);
  void showConnected(const String& ssid, const String& ip);
  void showConnectionFailed(ConnectFail reason);
  void showReconnecting(int attempt, int maxAttempts);
  void showFactoryReset();

  void pulseBacklight(int pulses = 3);    // blockierend ~pulses*300ms
}

#endif // DISPLAY_H
