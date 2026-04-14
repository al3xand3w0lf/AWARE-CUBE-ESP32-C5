// wifi_prov.cpp — Provisioning-State-Machine als FreeRTOS-Task.
// Portiert aus legacy wifi_provisioning.cpp + web_server_handlers.cpp,
// angepasst an namespace-Style und Event-basierte IPC.
//
// Alle WebServer/DNS/WiFi-Calls passieren nur aus dem WifiProv-Task
// (single-threaded innerhalb dieses Moduls) — kein Mutex noetig.

#include "wifi_prov.h"
#include "config.h"
#include "html_content.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_netif.h>

namespace WifiProv {

// ---------------------------------------------------------------- state

static constexpr uint16_t DNS_PORT = 53;
static const char CAPTIVE_PORTAL_URI[] = "http://192.168.4.1/";
#define NVS_AWARE_NAMESPACE "aware"

static DNSServer    s_dns;
static WebServer    s_server(80);
static Preferences  s_prefs;

static char s_apSsid[32]  = "";
static char s_staSsidBuf[33] = "";     // exposed via staSsid()
static char s_staIpBuf[16]   = "";     // exposed via staIp()

static String s_savedSsid, s_savedPass;
static String s_pendingSsid, s_pendingPass;
static String s_assignedIp;
static ConnectFail s_failReason = FAIL_NONE;

// /status-Maschinenstatus
static bool s_connectDone    = false;
static bool s_connectSuccess = false;

// Reconnect
static int          s_reconnectAttempts = 0;
static unsigned long s_lastReconnectAt  = 0;

// Provisioning-Helper
static int  s_lastStationCount   = -1;
static bool s_awaitingButtonForQr2 = false;
static unsigned long s_stateEnteredAt = 0;

// Role + Upload/NTRIP (aus NVS)
static Role     s_role = ROLE_IOT_LOGGER_SD;
static String   s_ntripHost, s_ntripMp, s_ntripUser, s_ntripPass;
static uint16_t s_ntripPort = 2101;
static String   s_uploadUrl, s_uploadKey;

// Forward decls
static void  changeState(AppState s);
static void  startProvisioningMode();
static void  stopProvisioningMode();
static void  setupWebRoutes();
static bool  loadWifiCreds();
static void  saveWifiCreds(const String& ssid, const String& pass);
static void  clearAllNvs();
static void  loadAwareConfig();
static String generateApSsid();

// -------------------------------------------------------------- helpers

static void changeState(AppState s) {
  g_state = s;
  s_stateEnteredAt = millis();
  AppEvent e{ EVT_STATE_CHANGED, (uint32_t)s };
  xQueueSend(g_displayQueue, &e, 0);

  #ifdef PROV_DEBUG
  DBG_PRINTF("[Prov] State -> %u\n", (unsigned)s);
  #endif
}

static String generateApSsid() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(DEVICE_NAME "-") + suffix;
}

// -------------------------------------------------------------- NVS

static bool loadWifiCreds() {
  if (!s_prefs.begin(NVS_NAMESPACE, true)) {
    s_prefs.end();
    s_savedSsid = ""; s_savedPass = "";
    return false;
  }
  s_savedSsid = s_prefs.getString(NVS_KEY_SSID, "");
  s_savedPass = s_prefs.getString(NVS_KEY_PASS, "");
  s_prefs.end();
  return s_savedSsid.length() > 0;
}

static void saveWifiCreds(const String& ssid, const String& pass) {
  s_prefs.begin(NVS_NAMESPACE, false);
  s_prefs.putString(NVS_KEY_SSID, ssid);
  s_prefs.putString(NVS_KEY_PASS, pass);
  s_prefs.end();
}

static void clearAllNvs() {
  s_prefs.begin(NVS_NAMESPACE, false);        s_prefs.clear(); s_prefs.end();
  s_prefs.begin(NVS_AWARE_NAMESPACE, false);  s_prefs.clear(); s_prefs.end();
}

static void loadAwareConfig() {
  if (!s_prefs.begin(NVS_AWARE_NAMESPACE, true)) {
    s_prefs.end();
    s_role = ROLE_IOT_LOGGER_SD;
    return;
  }
  s_role       = (Role)s_prefs.getUChar("role", ROLE_IOT_LOGGER_SD);
  s_ntripHost  = s_prefs.getString("ntrip_host", "");
  s_ntripPort  = s_prefs.getUShort("ntrip_port", 2101);
  s_ntripMp    = s_prefs.getString("ntrip_mp",   "");
  s_ntripUser  = s_prefs.getString("ntrip_user", "");
  s_ntripPass  = s_prefs.getString("ntrip_pass", "");
  s_uploadUrl  = s_prefs.getString("upload_url", "");
  s_uploadKey  = s_prefs.getString("upload_key", "");
  s_prefs.end();
  g_role = s_role;
}

// -------------------------------------------------------------- HTTP handlers

#define LOG_PROBE(tag) DBG_PRINTF("[HTTP] " tag " uri=%s\n", s_server.uri().c_str())

static const char REDIRECT_HTML[] PROGMEM =
  "<!DOCTYPE html><html><head>"
  "<meta http-equiv=\"refresh\" content=\"0;url=http://192.168.4.1/\">"
  "<script>window.location.replace(\"http://192.168.4.1/\")</script>"
  "</head><body><a href=\"http://192.168.4.1/\">Weiter</a></body></html>";

static void sendRedirect() {
  s_server.sendHeader("Location", "http://192.168.4.1/");
  s_server.send(302, "text/html", FPSTR(REDIRECT_HTML));
}

static void handleRoot() {
  DBG_PRINTLN("[HTTP] GET /");
  String html = FPSTR(HTML_PAGE);
  html.replace("%DEVICE_NAME%", DEVICE_NAME);
  s_server.send(200, "text/html", html);
}

static void handleHotspotDetect() {
  LOG_PROBE("Apple/Firefox");
  String html = FPSTR(HTML_PAGE);
  html.replace("%DEVICE_NAME%", DEVICE_NAME);
  s_server.send(200, "text/html", html);
}

static void handleScan() {
  DBG_PRINTLN("[HTTP] GET /scan");
  if (g_state == STATE_CONNECTING) {
    s_server.send(503, "application/json", "[]");
    return;
  }
  esp_task_wdt_reset();
  int n = WiFi.scanNetworks();
  DBG_PRINTF("[HTTP] Scan: %d\n", n);

  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    String ssid = WiFi.SSID(i);
    ssid.replace("\\", "\\\\");
    ssid.replace("\"", "\\\"");
    json += "{\"ssid\":\""; json += ssid;
    json += "\",\"rssi\":"; json += String(WiFi.RSSI(i));
    json += ",\"enc\":";    json += String(WiFi.encryptionType(i));
    json += "}";
  }
  json += "]";
  WiFi.scanDelete();
  s_server.send(200, "application/json", json);
}

static void handleConnect() {
  DBG_PRINTLN("[HTTP] POST /connect");
  if (g_state == STATE_CONNECTING) {
    s_server.send(409, "application/json", "{\"status\":\"error\",\"reason\":\"busy\"}");
    return;
  }
  s_pendingSsid = s_server.arg("ssid");
  s_pendingPass = s_server.arg("pass");
  if (s_pendingSsid.length() == 0) {
    s_server.send(400, "application/json", "{\"status\":\"error\",\"reason\":\"no_ssid\"}");
    return;
  }

  s_connectSuccess = false;
  s_connectDone    = false;
  s_failReason     = FAIL_NONE;

  strlcpy(s_staSsidBuf, s_pendingSsid.c_str(), sizeof(s_staSsidBuf));
  WiFi.begin(s_pendingSsid.c_str(), s_pendingPass.c_str());

  s_server.send(200, "application/json", "{\"status\":\"connecting\"}");
  changeState(STATE_CONNECTING);
}

static void handleStatus() {
  String json;
  if (g_state == STATE_CONNECTING) {
    if (s_connectDone) {
      if (s_connectSuccess) json = "{\"status\":\"ok\",\"ip\":\"" + s_assignedIp + "\"}";
      else                  json = String("{\"status\":\"error\",\"reason\":\"") +
                                    (s_failReason == FAIL_WRONG_PASSWORD ? "wrong_password" :
                                     s_failReason == FAIL_TIMEOUT        ? "timeout" : "other") + "\"}";
    } else {
      json = "{\"status\":\"connecting\"}";
    }
  } else if (g_state == STATE_NORMAL_OPERATION || g_state == STATE_CONNECTED) {
    json = "{\"status\":\"ok\",\"ip\":\"" + s_assignedIp + "\"}";
  } else if (s_connectDone && !s_connectSuccess) {
    json = "{\"status\":\"error\"}";
  } else {
    json = "{\"status\":\"idle\"}";
  }
  s_server.send(200, "application/json", json);
}

// POST /config — setzt role + NTRIP/Upload-Felder in NVS und reagiert mit Reboot-Hint.
// HTML-UI folgt; dieser Endpoint ist API-only.
static void handleConfig() {
  DBG_PRINTLN("[HTTP] POST /config");
  s_prefs.begin(NVS_AWARE_NAMESPACE, false);
  if (s_server.hasArg("role"))       s_prefs.putUChar ("role",       (uint8_t)s_server.arg("role").toInt());
  if (s_server.hasArg("ntrip_host")) s_prefs.putString("ntrip_host", s_server.arg("ntrip_host"));
  if (s_server.hasArg("ntrip_port")) s_prefs.putUShort("ntrip_port", (uint16_t)s_server.arg("ntrip_port").toInt());
  if (s_server.hasArg("ntrip_mp"))   s_prefs.putString("ntrip_mp",   s_server.arg("ntrip_mp"));
  if (s_server.hasArg("ntrip_user")) s_prefs.putString("ntrip_user", s_server.arg("ntrip_user"));
  if (s_server.hasArg("ntrip_pass")) s_prefs.putString("ntrip_pass", s_server.arg("ntrip_pass"));
  if (s_server.hasArg("upload_url")) s_prefs.putString("upload_url", s_server.arg("upload_url"));
  if (s_server.hasArg("upload_key")) s_prefs.putString("upload_key", s_server.arg("upload_key"));
  s_prefs.end();
  s_server.send(200, "application/json", "{\"status\":\"ok\",\"note\":\"reboot required\"}");
}

static void setupWebRoutes() {
  const char* wantedHeaders[] = {"User-Agent"};
  s_server.collectHeaders(wantedHeaders, 1);

  // Captive-portal probes (do not remove — each platform needs its own)
  s_server.on("/generate_204",              HTTP_GET,  sendRedirect);
  s_server.on("/gen_204",                   HTTP_GET,  sendRedirect);
  s_server.on("/hotspot-detect.html",       HTTP_GET,  handleHotspotDetect);
  s_server.on("/library/test/success.html", HTTP_GET,  handleHotspotDetect);
  s_server.on("/captive-portal/api/auth",   HTTP_GET,  handleHotspotDetect);
  s_server.on("/connecttest.txt",           HTTP_GET,  sendRedirect);
  s_server.on("/ncsi.txt",                  HTTP_GET,  sendRedirect);
  s_server.on("/redirect",                  HTTP_GET,  sendRedirect);
  s_server.on("/canonical.html",            HTTP_GET,  handleHotspotDetect);
  s_server.on("/success.txt",               HTTP_GET,  handleHotspotDetect);

  s_server.on("/",        HTTP_GET,  handleRoot);
  s_server.on("/scan",    HTTP_GET,  handleScan);
  s_server.on("/connect", HTTP_POST, handleConnect);
  s_server.on("/status",  HTTP_GET,  handleStatus);
  s_server.on("/config",  HTTP_POST, handleConfig);

  s_server.onNotFound(sendRedirect);
}

// -------------------------------------------------------------- AP start/stop

static void startProvisioningMode() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.STA.begin();                               // needed for scan

  String ap = generateApSsid();
  strlcpy(s_apSsid, ap.c_str(), sizeof(s_apSsid));
  DBG_PRINTF("[Prov] AP: %s\n", s_apSsid);

  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(s_apSsid, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CLIENTS);

  // RFC 8910: DHCP Option 114 (captive portal URI)
  esp_netif_t* apNetif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (apNetif) {
    esp_netif_dhcps_stop(apNetif);
    esp_netif_dhcps_option(apNetif, ESP_NETIF_OP_SET,
                           ESP_NETIF_CAPTIVEPORTAL_URI,
                           (void*)CAPTIVE_PORTAL_URI,
                           sizeof(CAPTIVE_PORTAL_URI) - 1);
    esp_netif_dhcps_start(apNetif);
  }

  s_dns.start(DNS_PORT, "*", AP_IP);
  setupWebRoutes();
  s_server.begin();

  s_lastStationCount      = 0;
  s_awaitingButtonForQr2  = false;
  changeState(STATE_PROV_AP);
  DBG_PRINTF("[Prov] AP up. IP: %s\n", WiFi.softAPIP().toString().c_str());
}

static void stopProvisioningMode() {
  s_server.stop();
  s_dns.stop();
  WiFi.softAPdisconnect(true);
}

// -------------------------------------------------------------- public API

bool begin() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(s_apSsid, sizeof(s_apSsid), "%s-%02X%02X%02X",
           DEVICE_NAME, mac[3], mac[4], mac[5]);

  loadAwareConfig();
  DBG_PRINTF("[Prov] Role=%u\n", (unsigned)s_role);
  return true;
}

void factoryReset() {
  DBG_PRINTLN("[Prov] Factory Reset");
  changeState(STATE_FACTORY_RESET);
  clearAllNvs();
  vTaskDelay(pdMS_TO_TICKS(1500));
  ESP.restart();
}

// Getter
const char* apSsid()         { return s_apSsid; }
const char* apPassword()     { return AP_PASSWORD; }
const char* portalUrl()      { return "http://192.168.4.1"; }
const char* staSsid()        { return s_staSsidBuf; }
const char* staIp()          { return s_staIpBuf; }
bool        isConnected()    { return WiFi.status() == WL_CONNECTED; }
ConnectFail lastFailReason() { return s_failReason; }
int         reconnectAttempt(){ return s_reconnectAttempts; }
int         reconnectMax()   { return RECONNECT_ATTEMPTS; }
Role        role()           { return s_role; }
const char* ntripHost()      { return s_ntripHost.c_str(); }
uint16_t    ntripPort()      { return s_ntripPort; }
const char* ntripMountpoint(){ return s_ntripMp.c_str(); }
const char* ntripUser()      { return s_ntripUser.c_str(); }
const char* ntripPassword()  { return s_ntripPass.c_str(); }
const char* uploadUrl()      { return s_uploadUrl.c_str(); }
const char* uploadKey()      { return s_uploadKey.c_str(); }

// -------------------------------------------------------------- task loop

// Kurzdruck-Handler (frueher WiFiProvisioning::_onShortPress)
static void onShortPress() {
  if (s_awaitingButtonForQr2) {
    s_awaitingButtonForQr2 = false;
    // QR2 zeigt die AP-IP-basierte URL (nicht portalUrl(), falls DHCP mal
    // anders vergibt — hier bewusst softAPIP).
    static String url;
    url = String("http://") + WiFi.softAPIP().toString();
    // Display bekommt neuen State; portalUrl() kehrt weiterhin den Default;
    // das Display liest im STATE_PROV_URL den portalUrl() — der ist fest 192.168.4.1.
    changeState(STATE_PROV_URL);
    return;
  }
  // TODO: im Normalbetrieb naechsten Info-Screen weiterschalten
}

static void drainEvents() {
  AppEvent evt;
  while (xQueueReceive(g_eventQueue, &evt, 0) == pdTRUE) {
    switch (evt.type) {
      case EVT_BUTTON_LONG:  factoryReset();  return;
      case EVT_BUTTON_SHORT: onShortPress();  break;
      default: break;
    }
  }
}

void task(void*) {
  esp_task_wdt_add(nullptr);

  // BOOT -> CHECK_NVS
  changeState(STATE_CHECK_NVS);
  if (loadWifiCreds()) {
    DBG_PRINTF("[Prov] Saved SSID: %s\n", s_savedSsid.c_str());
    strlcpy(s_staSsidBuf, s_savedSsid.c_str(), sizeof(s_staSsidBuf));
    WiFi.mode(WIFI_STA);
    WiFi.begin(s_savedSsid.c_str(), s_savedPass.c_str());
    changeState(STATE_CONNECTING_SAVED);
  } else {
    startProvisioningMode();
  }

  for (;;) {
    esp_task_wdt_reset();
    drainEvents();

    switch (g_state) {

      case STATE_PROV_AP:
      case STATE_PROV_URL:
      case STATE_PROV_TRANSITION:
      case STATE_CONNECTING: {
        s_dns.processNextRequest();
        s_server.handleClient();

        // Station-Count-Watch nur in PROV_AP/URL
        if (g_state == STATE_PROV_AP || g_state == STATE_PROV_URL) {
          int stations = WiFi.softAPgetStationNum();
          if (stations != s_lastStationCount) {
            s_lastStationCount = stations;
            if (stations > 0) {
              DBG_PRINTF("[Prov] Client joined (%d) — transition screen\n", stations);
              s_awaitingButtonForQr2 = true;
              changeState(STATE_PROV_TRANSITION);
              // Attention: Backlight-Pulse aus diesem Task (blockierend ~900ms)
              // ist OK; der Display-Task rendert den Screen zwischenzeitlich.
              // Alternativ via extra Event — fuer jetzt: Task-seitiges pulse ist OK.
            } else {
              s_awaitingButtonForQr2 = false;
              changeState(STATE_PROV_AP);
            }
          }
        }

        if (g_state == STATE_CONNECTING) {
          // Connect-Handling
          if (WiFi.status() == WL_CONNECTED) {
            s_assignedIp = WiFi.localIP().toString();
            strlcpy(s_staIpBuf, s_assignedIp.c_str(), sizeof(s_staIpBuf));
            saveWifiCreds(s_pendingSsid, s_pendingPass);
            s_connectSuccess = true;
            s_connectDone    = true;
            changeState(STATE_CONNECTED);
            vTaskDelay(pdMS_TO_TICKS(500));
            stopProvisioningMode();
            WiFi.mode(WIFI_STA);
            changeState(STATE_NORMAL_OPERATION);
          } else if (millis() - s_stateEnteredAt > CONNECT_TIMEOUT_MS) {
            WiFi.disconnect();
            s_connectSuccess = false;
            s_connectDone    = true;
            s_failReason     = FAIL_TIMEOUT;
            changeState(STATE_CONNECTION_FAILED);
            vTaskDelay(pdMS_TO_TICKS(1500));
            changeState(STATE_PROV_AP);
          } else if (WiFi.status() == WL_CONNECT_FAILED) {
            WiFi.disconnect();
            s_connectSuccess = false;
            s_connectDone    = true;
            s_failReason     = FAIL_WRONG_PASSWORD;
            changeState(STATE_CONNECTION_FAILED);
            vTaskDelay(pdMS_TO_TICKS(1500));
            changeState(STATE_PROV_AP);
          }
        }
        break;
      }

      case STATE_CONNECTING_SAVED: {
        if (WiFi.status() == WL_CONNECTED) {
          s_assignedIp = WiFi.localIP().toString();
          strlcpy(s_staIpBuf, s_assignedIp.c_str(), sizeof(s_staIpBuf));
          s_reconnectAttempts = 0;
          changeState(STATE_NORMAL_OPERATION);
        } else if (millis() - s_stateEnteredAt > SAVED_CONNECT_TIMEOUT_MS) {
          WiFi.disconnect();
          startProvisioningMode();
        }
        break;
      }

      case STATE_NORMAL_OPERATION: {
        if (WiFi.status() != WL_CONNECTED) {
          s_reconnectAttempts = 0;
          s_lastReconnectAt   = 0;
          s_staIpBuf[0] = '\0';
          changeState(STATE_RECONNECTING);
        }
        break;
      }

      case STATE_RECONNECTING: {
        if (WiFi.status() == WL_CONNECTED) {
          s_assignedIp = WiFi.localIP().toString();
          strlcpy(s_staIpBuf, s_assignedIp.c_str(), sizeof(s_staIpBuf));
          s_reconnectAttempts = 0;
          changeState(STATE_NORMAL_OPERATION);
          break;
        }
        if (s_reconnectAttempts >= RECONNECT_ATTEMPTS) {
          WiFi.disconnect();
          startProvisioningMode();
          break;
        }
        unsigned long now = millis();
        if (now - s_lastReconnectAt >= RECONNECT_INTERVAL_MS) {
          s_reconnectAttempts++;
          s_lastReconnectAt = now;
          DBG_PRINTF("[Prov] Reconnect %d/%d\n", s_reconnectAttempts, RECONNECT_ATTEMPTS);
          WiFi.reconnect();
          // re-publish state damit Display-Counter aktualisiert
          AppEvent e{ EVT_STATE_CHANGED, STATE_RECONNECTING };
          xQueueSend(g_displayQueue, &e, 0);
        }
        break;
      }

      default:
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

} // namespace WifiProv
