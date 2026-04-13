/**
 * wifi_provisioning.cpp — State Machine, NVS und WiFi-Logik
 *
 * Fix gegenüber v1:
 * - WiFi.STA.begin() wird aufgerufen damit Scan funktioniert
 * - AP-Name ist jetzt AWARE-XXXXXX
 * - Display zeigt Provisioning-Schritte an
 */

#include "wifi_provisioning.h"
#include "html_content.h"
#include <esp_task_wdt.h>

static const uint16_t DNS_PORT = 53;

// ============================================================
// Konstruktor
// ============================================================

WiFiProvisioning::WiFiProvisioning()
    : _state(ProvisioningState::BOOT),
      _stateEnteredAt(0),
      _server(80),
      _connectSuccess(false),
      _connectDone(false),
      _reconnectAttempts(0),
      _lastReconnectAt(0),
      _resetPressedAt(0),
      _resetActive(false),
      _onComplete(nullptr) {}

// ============================================================
// Öffentliche API
// ============================================================

void WiFiProvisioning::begin() {
  DBG_PRINTLN(F("[Prov] Begin"));

  // Display initialisieren
  _display.begin();
  _display.showBoot();

  // Reset-Pin konfigurieren
  pinMode(RESET_PIN, INPUT_PULLUP);

  // Watchdog aktivieren
  esp_task_wdt_config_t wdtConfig = {
    .timeout_ms = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = (1 << 0),
    .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&wdtConfig);
  esp_task_wdt_add(NULL);

  delay(500);  // Boot-Screen kurz anzeigen

  _changeState(ProvisioningState::CHECK_NVS);
}

void WiFiProvisioning::loop() {
  esp_task_wdt_reset();
  _checkResetButton();

  switch (_state) {
    case ProvisioningState::BOOT:
      _handleStateBoot();
      break;
    case ProvisioningState::CHECK_NVS:
      _handleStateCheckNvs();
      break;
    case ProvisioningState::PROVISIONING_MODE:
      _handleStateProvisioningMode();
      break;
    case ProvisioningState::CONNECTING_NEW:
      _handleStateConnectingNew();
      break;
    case ProvisioningState::CONNECTING_SAVED:
      _handleStateConnectingSaved();
      break;
    case ProvisioningState::NORMAL_OPERATION:
      _handleStateNormalOperation();
      break;
    case ProvisioningState::CONNECTION_LOST:
      _handleStateConnectionLost();
      break;
  }
}

bool WiFiProvisioning::isProvisioned() {
  return _loadCredentials();
}

void WiFiProvisioning::resetCredentials() {
  DBG_PRINTLN(F("[Prov] Factory Reset!"));
  _display.showFactoryReset();
  _clearCredentials();
  delay(1500);
  ESP.restart();
}

void WiFiProvisioning::setOnCompleteCallback(std::function<void(String ip)> cb) {
  _onComplete = cb;
}

ProvisioningState WiFiProvisioning::getState() const {
  return _state;
}

// ============================================================
// State-Handler
// ============================================================

void WiFiProvisioning::_handleStateBoot() {
  _changeState(ProvisioningState::CHECK_NVS);
}

void WiFiProvisioning::_handleStateCheckNvs() {
  if (_loadCredentials()) {
    DBG_PRINTF("[Prov] Gespeicherte SSID: %s\n", _savedSsid.c_str());
    _display.showConnecting(_savedSsid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(_savedSsid.c_str(), _savedPass.c_str());
    _changeState(ProvisioningState::CONNECTING_SAVED);
  } else {
    DBG_PRINTLN(F("[Prov] Keine Credentials — starte Provisioning"));
    _startProvisioningMode();
    _changeState(ProvisioningState::PROVISIONING_MODE);
  }
}

void WiFiProvisioning::_handleStateProvisioningMode() {
  _dns.processNextRequest();
  _server.handleClient();
}

void WiFiProvisioning::_handleStateConnectingNew() {
  _dns.processNextRequest();
  _server.handleClient();

  unsigned long elapsed = millis() - _stateEnteredAt;

  if (WiFi.status() == WL_CONNECTED) {
    _assignedIp = WiFi.localIP().toString();
    _connectSuccess = true;
    _connectDone = true;
    DBG_PRINTF("[Prov] Verbunden! IP: %s\n", _assignedIp.c_str());

    _saveCredentials(_pendingSsid, _pendingPass);
    _display.showConnected(_pendingSsid, _assignedIp);

    delay(500);
    _stopProvisioningMode();
    WiFi.mode(WIFI_STA);
    _changeState(ProvisioningState::NORMAL_OPERATION);

    if (_onComplete) {
      _onComplete(_assignedIp);
    }

  } else if (elapsed > CONNECT_TIMEOUT_MS) {
    DBG_PRINTLN(F("[Prov] Timeout"));
    WiFi.disconnect();
    _connectSuccess = false;
    _connectDone = true;
    _connectError = "timeout";
    _display.showConnectionFailed("timeout");
    _changeState(ProvisioningState::PROVISIONING_MODE);

  } else if (WiFi.status() == WL_CONNECT_FAILED) {
    DBG_PRINTLN(F("[Prov] Falsches Passwort?"));
    WiFi.disconnect();
    _connectSuccess = false;
    _connectDone = true;
    _connectError = "wrong_password";
    _display.showConnectionFailed("wrong_password");
    _changeState(ProvisioningState::PROVISIONING_MODE);
  }
}

void WiFiProvisioning::_handleStateConnectingSaved() {
  unsigned long elapsed = millis() - _stateEnteredAt;

  if (WiFi.status() == WL_CONNECTED) {
    _assignedIp = WiFi.localIP().toString();
    DBG_PRINTF("[Prov] Verbunden (gespeichert)! IP: %s\n", _assignedIp.c_str());
    _display.showConnected(_savedSsid, _assignedIp);
    _changeState(ProvisioningState::NORMAL_OPERATION);
    _reconnectAttempts = 0;

    if (_onComplete) {
      _onComplete(_assignedIp);
    }

  } else if (elapsed > SAVED_CONNECT_TIMEOUT_MS) {
    DBG_PRINTLN(F("[Prov] Gespeicherte Credentials: Timeout"));
    WiFi.disconnect();
    _startProvisioningMode();
    _changeState(ProvisioningState::PROVISIONING_MODE);
  }
}

void WiFiProvisioning::_handleStateNormalOperation() {
  if (WiFi.status() != WL_CONNECTED) {
    DBG_PRINTLN(F("[Prov] Verbindung verloren!"));
    _reconnectAttempts = 0;
    _lastReconnectAt = 0;
    _changeState(ProvisioningState::CONNECTION_LOST);
  }
}

void WiFiProvisioning::_handleStateConnectionLost() {
  if (WiFi.status() == WL_CONNECTED) {
    _assignedIp = WiFi.localIP().toString();
    _reconnectAttempts = 0;
    _display.showConnected(_savedSsid, _assignedIp);
    _changeState(ProvisioningState::NORMAL_OPERATION);
    return;
  }

  unsigned long now = millis();
  if (_reconnectAttempts >= RECONNECT_ATTEMPTS) {
    DBG_PRINTLN(F("[Prov] Max Reconnect — starte Provisioning"));
    WiFi.disconnect();
    _startProvisioningMode();
    _changeState(ProvisioningState::PROVISIONING_MODE);
    return;
  }

  if (now - _lastReconnectAt >= RECONNECT_INTERVAL_MS) {
    _reconnectAttempts++;
    _lastReconnectAt = now;
    DBG_PRINTF("[Prov] Reconnect %d/%d\n", _reconnectAttempts, RECONNECT_ATTEMPTS);
    _display.showReconnecting(_reconnectAttempts, RECONNECT_ATTEMPTS);
    WiFi.reconnect();
  }
}

// ============================================================
// Provisioning-Modus Start/Stop
// ============================================================

void WiFiProvisioning::_startProvisioningMode() {
  // AP+STA Modus zuerst setzen (damit MAC-Adresse verfügbar ist)
  WiFi.mode(WIFI_AP_STA);

  // STA-Interface explizit initialisieren (nötig damit Scan funktioniert!)
  WiFi.STA.begin();

  // AP-Name generieren (braucht WiFi.mode() für korrekte MAC)
  _apSsid = _generateApSsid();
  DBG_PRINTF("[Prov] Starte AP: %s\n", _apSsid.c_str());

  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(_apSsid.c_str(), AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CLIENTS);

  // DNS-Server — Captive Portal
  _dns.start(DNS_PORT, "*", AP_IP);

  // WebServer
  _setupWebServerRoutes();
  _server.begin();

  // Display: Zeige Anweisungen für den User
  _display.showProvisioningAP(_apSsid, AP_PASSWORD);

  DBG_PRINTF("[Prov] AP aktiv. IP: %s\n", WiFi.softAPIP().toString().c_str());
}

void WiFiProvisioning::_stopProvisioningMode() {
  DBG_PRINTLN(F("[Prov] Provisioning beenden"));
  _server.stop();
  _dns.stop();
  WiFi.softAPdisconnect(true);
}

void WiFiProvisioning::_setupWebServerRoutes() {
  // --- Captive Portal Detection ---
  // Android
  _server.on("/generate_204", HTTP_GET, [this]() { handleGenerate204(); });
  _server.on("/gen_204", HTTP_GET, [this]() { handleGenerate204(); });

  // Apple
  _server.on("/hotspot-detect.html", HTTP_GET, [this]() { handleHotspotDetect(); });
  _server.on("/library/test/success.html", HTTP_GET, [this]() { handleHotspotDetect(); });
  _server.on("/captive-portal/api/auth", HTTP_GET, [this]() { handleHotspotDetect(); });

  // Windows
  _server.on("/connecttest.txt", HTTP_GET, [this]() { handleConnectTest(); });
  _server.on("/ncsi.txt", HTTP_GET, [this]() { handleNcsiTxt(); });
  _server.on("/redirect", HTTP_GET, [this]() { handleNotFound(); });

  // Firefox
  _server.on("/canonical.html", HTTP_GET, [this]() { handleHotspotDetect(); });
  _server.on("/success.txt", HTTP_GET, [this]() { handleHotspotDetect(); });

  // --- Funktionale Endpunkte ---
  _server.on("/", HTTP_GET, [this]() { handleRoot(); });
  _server.on("/scan", HTTP_GET, [this]() { handleScan(); });
  _server.on("/connect", HTTP_POST, [this]() { handleConnect(); });
  _server.on("/status", HTTP_GET, [this]() { handleStatus(); });

  // Alle anderen → Redirect
  _server.onNotFound([this]() { handleNotFound(); });
}

// ============================================================
// NVS-Operationen
// ============================================================

bool WiFiProvisioning::_loadCredentials() {
  // Beim ersten Boot existiert der Namespace noch nicht (NOT_FOUND ist normal)
  if (!_prefs.begin(NVS_NAMESPACE, true)) {
    _prefs.end();
    _savedSsid = "";
    _savedPass = "";
    return false;
  }
  _savedSsid = _prefs.getString(NVS_KEY_SSID, "");
  _savedPass = _prefs.getString(NVS_KEY_PASS, "");
  _prefs.end();
  return _savedSsid.length() > 0;
}

void WiFiProvisioning::_saveCredentials(const String& ssid, const String& pass) {
  DBG_PRINTF("[Prov] Speichere: %s\n", ssid.c_str());
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.putString(NVS_KEY_SSID, ssid);
  _prefs.putString(NVS_KEY_PASS, pass);
  _prefs.end();
}

void WiFiProvisioning::_clearCredentials() {
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.clear();
  _prefs.end();
}

// ============================================================
// Hilfsfunktionen
// ============================================================

void WiFiProvisioning::_changeState(ProvisioningState newState) {
  _state = newState;
  _stateEnteredAt = millis();

  #ifdef PROV_DEBUG
  const char* names[] = {
    "BOOT", "CHECK_NVS", "PROVISIONING_MODE", "CONNECTING_NEW",
    "CONNECTING_SAVED", "NORMAL_OPERATION", "CONNECTION_LOST"
  };
  DBG_PRINTF("[Prov] State -> %s\n", names[static_cast<int>(newState)]);
  #endif
}

void WiFiProvisioning::_checkResetButton() {
  bool pressed = (digitalRead(RESET_PIN) == LOW);

  if (pressed && !_resetActive) {
    _resetActive = true;
    _resetPressedAt = millis();
  } else if (pressed && _resetActive) {
    if (millis() - _resetPressedAt >= RESET_HOLD_TIME_MS) {
      resetCredentials();
    }
  } else if (!pressed && _resetActive) {
    _resetActive = false;
  }
}

String WiFiProvisioning::_generateApSsid() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String("AWARE-") + suffix;
}
