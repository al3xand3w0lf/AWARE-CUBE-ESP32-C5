// ntrip.h — NTRIP Client, Dual-Mode:
//   Source (Base-Rolle):  drain g_gnssOutStream -> Caster
//   Sink   (Rover-Rolle): GET mountpoint -> g_rtcmInStream
// Rolle wird beim begin() aus WifiProv::role() gelesen.

#ifndef NTRIP_H
#define NTRIP_H

#include <Arduino.h>

namespace Ntrip {
  bool begin();
  void task(void* arg);
  bool isStreaming();
  uint32_t bytesTransferred();
}

#endif // NTRIP_H
