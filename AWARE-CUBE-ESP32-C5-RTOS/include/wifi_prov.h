// wifi_prov.h — Captive-Portal Provisioning, AP+STA, NVS, Reconnect.
// Rolle/NTRIP-Config ebenfalls in NVS (Namespace "aware").

#ifndef WIFI_PROV_H
#define WIFI_PROV_H

#include "app_state.h"

namespace WifiProv {
  bool begin();
  void task(void* arg);
  void factoryReset();                 // NVS-Namespaces leeren + reboot

  // Getter fuer Display/andere Tasks (sind thread-safe: einzige Writer ist
  // der WifiProv-Task; Reader lesen snapshot).
  const char* apSsid();                // "AWARE-AB12CD"
  const char* apPassword();
  const char* portalUrl();             // "http://192.168.4.1"
  const char* staSsid();               // aktuelle/versuchte STA-SSID
  const char* staIp();                 // "" falls nicht verbunden
  bool        isConnected();
  ConnectFail lastFailReason();
  int         reconnectAttempt();      // aktueller Versuch
  int         reconnectMax();          // RECONNECT_ATTEMPTS

  // Role + NTRIP-Config (aus NVS geladen)
  Role        role();
  const char* ntripHost();
  uint16_t    ntripPort();
  const char* ntripMountpoint();
  const char* ntripUser();
  const char* ntripPassword();
  const char* uploadUrl();
  const char* uploadKey();
}

#endif // WIFI_PROV_H
