// gnss.h — u-blox F9P via I2C (Wire1) auf separatem Bus.
// Rolle bestimmt Mess-Set (UBX/NMEA/RTCM3) und Datenpfad.

#ifndef GNSS_H
#define GNSS_H

#include <Arduino.h>
#include "app_state.h"

namespace Gnss {
  bool begin();              // Wire1 init + Modul-Init (Rolle aus WifiProv::role())
  void task(void* arg);      // Drain checkUblox -> g_gnssOutStream;
                             // Rover: g_rtcmInStream -> pushRawData

  // Live-Status (vom Task geschrieben)
  uint8_t  siv();            // Anzahl Satelliten
  uint8_t  fixType();        // 0..5
  bool     posValid();
  int32_t  latDeg7();        // Grad * 1e7
  int32_t  lonDeg7();        // Grad * 1e7
  int32_t  altMm();          // mm MSL
  bool     timeValid();
  bool     dateValid();
}

#endif // GNSS_H
