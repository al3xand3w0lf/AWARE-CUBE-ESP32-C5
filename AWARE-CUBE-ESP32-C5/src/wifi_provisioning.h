/**
 * wifi_provisioning.h — Klassen-Header für WiFi Provisioning
 *
 * State Machine für den gesamten Provisioning-Lifecycle:
 * BOOT → CHECK_NVS → PROVISIONING_MODE / CONNECTING_SAVED → NORMAL_OPERATION
 */

#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <functional>
#include "config.h"
#include "display.h"

// --- State Machine ---
enum class ProvisioningState {
  BOOT,
  CHECK_NVS,
  PROVISIONING_MODE,
  CONNECTING_NEW,
  CONNECTING_SAVED,
  NORMAL_OPERATION,
  CONNECTION_LOST
};

class WiFiProvisioning {
public:
  WiFiProvisioning();

  void initDisplay();   // Display + Boot-Screen (fuer frueh-Init vor SD-Karte)
  void begin();
  void loop();
  bool isProvisioned();
  void resetCredentials();
  void setOnCompleteCallback(std::function<void(String ip)> cb);
  ProvisioningState getState() const;
  Display& display() { return _display; }

  // HTTP-Handler (von web_server_handlers.cpp)
  void handleRoot();
  void handleScan();
  void handleConnect();
  void handleNotFound();
  void handleGenerate204();
  void handleHotspotDetect();
  void handleConnectTest();
  void handleNcsiTxt();
  void handleStatus();

private:
  ProvisioningState _state;
  unsigned long _stateEnteredAt;

  DNSServer    _dns;
  WebServer    _server;
  Preferences  _prefs;
  Display      _display;

  // Credentials
  String _pendingSsid;
  String _pendingPass;
  String _savedSsid;
  String _savedPass;

  // Verbindungsstatus
  bool _connectSuccess;
  bool _connectDone;
  String _assignedIp;
  String _connectError;

  // Reconnect
  int _reconnectAttempts;
  unsigned long _lastReconnectAt;

  // Button (Interrupt-gesteuert, Polling-Pfad nur bei aktivem Druck)
  static void IRAM_ATTR _buttonIsr();
  static volatile bool _buttonEvent;
  unsigned long _buttonPressedAt;
  unsigned long _releaseCandidateAt = 0;  // Zeitpunkt ersten LOW-Reads fuer Debounce
  bool _buttonActive;
  void _handleButton();
  void _onShortPress();

  // AP-Name (gespeichert für Display)
  String _apSsid;

  // Callback
  std::function<void(String ip)> _onComplete;

  bool _displayInited = false;
  int  _lastStationCount = -1;  // fuer Umschaltung WiFi-QR <-> URL-QR
  bool _awaitingButtonForQr2 = false;  // Transition-Screen wartet auf Tastendruck

  // Interne Methoden
  void _changeState(ProvisioningState newState);
  void _startProvisioningMode();
  void _stopProvisioningMode();
  void _setupWebServerRoutes();
  bool _loadCredentials();
  void _saveCredentials(const String& ssid, const String& pass);
  void _clearCredentials();
  String _generateApSsid();

  // State-Handler
  void _handleStateBoot();
  void _handleStateCheckNvs();
  void _handleStateProvisioningMode();
  void _handleStateConnectingNew();
  void _handleStateConnectingSaved();
  void _handleStateNormalOperation();
  void _handleStateConnectionLost();
};

#endif // WIFI_PROVISIONING_H
