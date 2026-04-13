/**
 * web_server_handlers.cpp — HTTP-Handler inkl. Captive Portal
 *
 * Fix gegenüber v1:
 * - Captive Portal: Liefert eigene HTML-Seite statt nur Redirects
 *   (zuverlässiger auf iOS 14+, Android 10+, Windows 10/11)
 * - WiFi Scan: Funktioniert jetzt dank WiFi.STA.begin() in _startProvisioningMode()
 */

#include "wifi_provisioning.h"
#include "html_content.h"
#include <esp_task_wdt.h>

// Diagnose-Helfer: loggt jede Captive-Portal-Probe inkl. User-Agent
#define LOG_PROBE(tag) \
  DBG_PRINTF("[HTTP] " tag " uri=%s ua=\"%s\"\n", \
             _server.uri().c_str(), \
             _server.hasHeader("User-Agent") ? _server.header("User-Agent").c_str() : "?")

// Kleine Redirect-Seite als Fallback (Meta-Refresh + JS)
static const char REDIRECT_HTML[] PROGMEM = R"(
<!DOCTYPE html><html><head>
<meta http-equiv="refresh" content="0;url=http://192.168.4.1/">
<script>window.location.replace("http://192.168.4.1/")</script>
</head><body><a href="http://192.168.4.1/">Weiter zur Einrichtung</a></body></html>
)";

// ============================================================
// Captive Portal Detection
// ============================================================

void WiFiProvisioning::handleGenerate204() {
  LOG_PROBE("Android(generate_204)");
  _server.sendHeader("Location", "http://192.168.4.1/");
  _server.send(302, "text/html", FPSTR(REDIRECT_HTML));
}

void WiFiProvisioning::handleHotspotDetect() {
  LOG_PROBE("Apple/Firefox(hotspot)");
  String html = FPSTR(HTML_PAGE);
  html.replace("%DEVICE_NAME%", DEVICE_NAME);
  _server.send(200, "text/html", html);
}

void WiFiProvisioning::handleConnectTest() {
  LOG_PROBE("Windows(connecttest)");
  _server.sendHeader("Location", "http://192.168.4.1/");
  _server.send(302, "text/html", FPSTR(REDIRECT_HTML));
}

void WiFiProvisioning::handleNcsiTxt() {
  LOG_PROBE("Windows(ncsi)");
  _server.sendHeader("Location", "http://192.168.4.1/");
  _server.send(302, "text/html", FPSTR(REDIRECT_HTML));
}

void WiFiProvisioning::handleNotFound() {
  LOG_PROBE("NotFound");
  _server.sendHeader("Location", "http://192.168.4.1/");
  _server.send(302, "text/html", FPSTR(REDIRECT_HTML));
}

// ============================================================
// Web-Interface
// ============================================================

void WiFiProvisioning::handleRoot() {
  DBG_PRINTLN(F("[HTTP] GET /"));
  String html = FPSTR(HTML_PAGE);
  html.replace("%DEVICE_NAME%", DEVICE_NAME);
  _server.send(200, "text/html", html);
}

// ============================================================
// WiFi-Scan
// ============================================================

void WiFiProvisioning::handleScan() {
  DBG_PRINTLN(F("[HTTP] GET /scan"));

  // Kein Scan während Verbindungsaufbau (Race Condition)
  if (_state == ProvisioningState::CONNECTING_NEW) {
    _server.send(503, "application/json", "[]");
    return;
  }

  // Watchdog füttern vor dem Scan (blockiert 3-10 Sekunden)
  esp_task_wdt_reset();

  // Synchroner Scan
  int n = WiFi.scanNetworks();

  DBG_PRINTF("[HTTP] Scan: %d Netzwerke\n", n);

  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"";
    String ssid = WiFi.SSID(i);
    ssid.replace("\\", "\\\\");
    ssid.replace("\"", "\\\"");
    json += ssid;
    json += "\",\"rssi\":";
    json += String(WiFi.RSSI(i));
    json += ",\"enc\":";
    json += String(WiFi.encryptionType(i));
    json += "}";
  }
  json += "]";

  WiFi.scanDelete();

  _server.send(200, "application/json", json);
}

// ============================================================
// Verbindungsversuch
// ============================================================

void WiFiProvisioning::handleConnect() {
  DBG_PRINTLN(F("[HTTP] POST /connect"));

  if (_state == ProvisioningState::CONNECTING_NEW) {
    _server.send(409, "application/json", "{\"status\":\"error\",\"reason\":\"busy\"}");
    return;
  }

  _pendingSsid = _server.arg("ssid");
  _pendingPass = _server.arg("pass");

  if (_pendingSsid.length() == 0) {
    _server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"no_ssid\"}");
    return;
  }

  DBG_PRINTF("[HTTP] Verbinde zu: %s\n", _pendingSsid.c_str());

  _connectSuccess = false;
  _connectDone = false;
  _connectError = "";

  // WiFi-Verbindung starten
  WiFi.begin(_pendingSsid.c_str(), _pendingPass.c_str());

  // Antwort: Client pollt /status
  _server.send(200, "application/json", "{\"status\":\"connecting\"}");

  // Display aktualisieren
  _display.showConnecting(_pendingSsid);

  _changeState(ProvisioningState::CONNECTING_NEW);
}

// ============================================================
// Status-Abfrage (Polling)
// ============================================================

void WiFiProvisioning::handleStatus() {
  String json;

  if (_state == ProvisioningState::CONNECTING_NEW) {
    if (_connectDone) {
      if (_connectSuccess) {
        json = "{\"status\":\"ok\",\"ip\":\"" + _assignedIp + "\"}";
      } else {
        json = "{\"status\":\"error\",\"reason\":\"" + _connectError + "\"}";
      }
    } else {
      json = "{\"status\":\"connecting\"}";
    }
  } else if (_state == ProvisioningState::NORMAL_OPERATION) {
    json = "{\"status\":\"ok\",\"ip\":\"" + _assignedIp + "\"}";
  } else if (_connectDone && !_connectSuccess) {
    json = "{\"status\":\"error\",\"reason\":\"" + _connectError + "\"}";
  } else {
    json = "{\"status\":\"idle\"}";
  }

  _server.send(200, "application/json", json);
}
