// gnss.h — u-blox F9P via I2C (Wire1) auf separatem Bus.
// Rolle bestimmt Mess-Set (UBX/NMEA/RTCM3) und Datenpfad.

#ifndef GNSS_H
#define GNSS_H

#include <Arduino.h>
#include "app_state.h"

namespace Gnss {
  // Callback fuer Boot-Progress (Display zeichnet Fortschrittsscreen)
  enum class InitStep : uint8_t {
    I2C_UP,        // Wire.begin ok
    DETECT_OK,     // F9P antwortet
    DETECT_FAIL,   // F9P nicht gefunden
    CONFIG_OK,     // Rolle konfiguriert
    CONFIG_FAIL,
  };
  typedef void (*InitProgressCb)(InitStep step);

  bool begin(InitProgressCb cb = nullptr);   // Wire init + Modul-Init
  void task(void* arg);

  // Live-Status (vom Task geschrieben)
  uint8_t  siv();
  uint8_t  fixType();
  bool     posValid();
  int32_t  latDeg7();
  int32_t  lonDeg7();
  int32_t  altMm();
  bool     timeValid();
  bool     dateValid();

  // Zeit/Datum (aus pollLiveStatus gecached)
  uint16_t year();
  uint8_t  month();
  uint8_t  day();
  uint8_t  hour();
  uint8_t  minute();
  uint8_t  second();
}

#endif // GNSS_H
