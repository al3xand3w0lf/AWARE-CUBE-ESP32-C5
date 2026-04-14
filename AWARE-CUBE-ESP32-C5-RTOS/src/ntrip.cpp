// ntrip.cpp — NTRIP Client (Source + Sink), adapted from 155 AWARE.
//
// Source (Base):  SOURCE <pass> /<mp> HTTP-style Handshake. Caster antwortet
//                 "ICY 200 OK". Danach: g_gnssOutStream -> client.write().
// Sink   (Rover): GET /<mp> HTTP/1.0 + "Ntrip-Version: Ntrip/2.0" + Basic
//                 Auth. Caster antwortet "ICY 200 OK" oder "HTTP/1.x 200".
//                 Danach: client.read() -> g_rtcmInStream.
//
// Reconnect-Backoff 1s -> 30s. Handshake-Timeout 5s. Bei "Bad Password" /
// "Mount Point Not Found" in ERROR-State (60s Pause) um Caster nicht zu
// ueberfluten.

#include "ntrip.h"
#include "config.h"
#include "app_state.h"
#include "wifi_prov.h"

#include <WiFi.h>
#include <esp_task_wdt.h>
#include <mbedtls/base64.h>

namespace Ntrip {

enum State : uint8_t {
  ST_DISABLED = 0,
  ST_WAIT_WIFI,
  ST_DISCONNECTED,
  ST_CONNECTING,
  ST_HANDSHAKE,
  ST_STREAMING,
  ST_ERROR,
};

static WiFiClient s_client;
static State      s_state          = ST_DISABLED;
static uint32_t   s_reconnectMs    = NTRIP_RECONNECT_INIT_MS;
static uint32_t   s_lastTryMs      = 0;
static uint32_t   s_handshakeMs    = 0;
static uint32_t   s_errorEnteredMs = 0;
static uint32_t   s_bytesTotal     = 0;
static bool       s_isSource       = false;   // true=Base, false=Rover

static void setState(State s) { s_state = s; }

static void backoff() {
  s_lastTryMs = millis();
  s_reconnectMs = (s_reconnectMs * 2 < NTRIP_RECONNECT_MAX_MS)
                    ? s_reconnectMs * 2 : NTRIP_RECONNECT_MAX_MS;
}

static bool sendSourceRequest() {
  char req[256];
  int n = snprintf(req, sizeof(req),
                   "SOURCE %s /%s\r\nSource-Agent: NTRIP AWARE-ESP32/%s\r\n\r\n",
                   WifiProv::ntripPassword(), WifiProv::ntripMountpoint(),
                   FIRMWARE_VERSION);
  DBG_PRINTF("[NTRIP] SOURCE -> %s:%u /%s\n",
             WifiProv::ntripHost(), WifiProv::ntripPort(),
             WifiProv::ntripMountpoint());
  return s_client.write((const uint8_t*)req, n) == (size_t)n;
}

static bool sendRoverRequest() {
  // Basic-Auth Header bauen
  char userpass[128];
  snprintf(userpass, sizeof(userpass), "%s:%s",
           WifiProv::ntripUser(), WifiProv::ntripPassword());
  unsigned char b64[192];
  size_t b64Len = 0;
  mbedtls_base64_encode(b64, sizeof(b64), &b64Len,
                        (const unsigned char*)userpass, strlen(userpass));
  b64[b64Len] = '\0';

  char req[512];
  int n = snprintf(req, sizeof(req),
                   "GET /%s HTTP/1.0\r\n"
                   "Host: %s\r\n"
                   "Ntrip-Version: Ntrip/2.0\r\n"
                   "User-Agent: NTRIP AWARE-ESP32/%s\r\n"
                   "Authorization: Basic %s\r\n"
                   "Connection: close\r\n\r\n",
                   WifiProv::ntripMountpoint(), WifiProv::ntripHost(),
                   FIRMWARE_VERSION, (const char*)b64);
  DBG_PRINTF("[NTRIP] GET %s:%u /%s\n",
             WifiProv::ntripHost(), WifiProv::ntripPort(),
             WifiProv::ntripMountpoint());
  return s_client.write((const uint8_t*)req, n) == (size_t)n;
}

// Handshake: warte auf "ICY 200" oder "HTTP/1.x 200". Bei Fehler false.
// Returned true nur wenn eine Antwort erkannt wurde (OK oder reject).
// outFatal = true bei Auth-Fehler -> ERROR-State.
static bool readHandshakeResponse(bool& outOk, bool& outFatal) {
  if (!s_client.available()) return false;
  char resp[256];
  int idx = 0;
  while (s_client.available() && idx < (int)sizeof(resp) - 1) {
    resp[idx++] = s_client.read();
  }
  resp[idx] = '\0';
  DBG_PRINTF("[NTRIP] resp: %s\n", resp);

  bool ok = (strstr(resp, "ICY 200") != nullptr) ||
            (strstr(resp, " 200 ")   != nullptr);
  bool fatal = (strstr(resp, "401") != nullptr) ||
               (strstr(resp, "Bad Password") != nullptr) ||
               (strstr(resp, "Mount Point") != nullptr) ||
               (strstr(resp, "Unauthorized") != nullptr);
  outOk    = ok;
  outFatal = !ok && fatal;
  return true;
}

// --------------------------------------------------------------------- public

bool begin() {
  Role r = WifiProv::role();
  if (r == ROLE_BASE_NTRIP) {
    s_isSource = true;
    setState(ST_WAIT_WIFI);
  } else if (r == ROLE_ROVER_NTRIP) {
    s_isSource = false;
    setState(ST_WAIT_WIFI);
  } else {
    setState(ST_DISABLED);
  }
  s_reconnectMs = NTRIP_RECONNECT_INIT_MS;
  return true;
}

bool     isStreaming()      { return s_state == ST_STREAMING; }
uint32_t bytesTransferred() { return s_bytesTotal; }

void task(void*) {
  esp_task_wdt_add(nullptr);
  uint8_t buf[NTRIP_SEND_CHUNK_BYTES];

  for (;;) {
    esp_task_wdt_reset();

    if (s_state == ST_DISABLED) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    switch (s_state) {
      case ST_WAIT_WIFI:
        if (WiFi.status() == WL_CONNECTED) {
          setState(ST_DISCONNECTED);
          s_lastTryMs = 0;
        }
        break;

      case ST_DISCONNECTED: {
        if (WiFi.status() != WL_CONNECTED) { setState(ST_WAIT_WIFI); break; }
        if (millis() - s_lastTryMs < s_reconnectMs) break;

        const char* host = WifiProv::ntripHost();
        uint16_t    port = WifiProv::ntripPort();
        if (!host || !*host || port == 0) { s_lastTryMs = millis(); break; }

        DBG_PRINTF("[NTRIP] TCP connect %s:%u\n", host, port);
        if (s_client.connect(host, port)) {
          setState(ST_CONNECTING);
        } else {
          DBG_PRINTLN("[NTRIP] TCP connect FAILED");
          backoff();
        }
        break;
      }

      case ST_CONNECTING: {
        bool ok = s_isSource ? sendSourceRequest() : sendRoverRequest();
        if (!ok) { s_client.stop(); backoff(); setState(ST_DISCONNECTED); break; }
        s_handshakeMs = millis();
        setState(ST_HANDSHAKE);
        break;
      }

      case ST_HANDSHAKE: {
        if (millis() - s_handshakeMs > NTRIP_HANDSHAKE_TIMEOUT_MS) {
          DBG_PRINTLN("[NTRIP] handshake timeout");
          s_client.stop(); backoff(); setState(ST_DISCONNECTED); break;
        }
        bool ok = false, fatal = false;
        if (readHandshakeResponse(ok, fatal)) {
          if (ok) {
            DBG_PRINTLN("[NTRIP] streaming started");
            s_reconnectMs = NTRIP_RECONNECT_INIT_MS;
            setState(ST_STREAMING);
          } else if (fatal) {
            s_client.stop();
            s_errorEnteredMs = millis();
            setState(ST_ERROR);
          } else {
            s_client.stop(); backoff(); setState(ST_DISCONNECTED);
          }
        }
        break;
      }

      case ST_STREAMING: {
        if (!s_client.connected() || WiFi.status() != WL_CONNECTED) {
          DBG_PRINTLN("[NTRIP] connection lost");
          s_client.stop();
          s_lastTryMs = millis();
          setState(ST_DISCONNECTED);
          break;
        }
        if (s_isSource) {
          // Base: drain g_gnssOutStream -> caster
          size_t n = xStreamBufferReceive(g_gnssOutStream, buf, sizeof(buf),
                                          pdMS_TO_TICKS(50));
          if (n > 0) {
            size_t w = s_client.write(buf, n);
            s_bytesTotal += w;
          }
        } else {
          // Rover: caster -> g_rtcmInStream
          int avail = s_client.available();
          if (avail > 0) {
            int toRead = avail > (int)sizeof(buf) ? sizeof(buf) : avail;
            int r = s_client.read(buf, toRead);
            if (r > 0) {
              xStreamBufferSend(g_rtcmInStream, buf, r, pdMS_TO_TICKS(20));
              s_bytesTotal += r;
            }
          } else {
            vTaskDelay(pdMS_TO_TICKS(20));
          }
        }
        break;
      }

      case ST_ERROR:
        if (millis() - s_errorEnteredMs > 60000) {
          DBG_PRINTLN("[NTRIP] retry after error");
          s_reconnectMs = NTRIP_RECONNECT_INIT_MS;
          s_lastTryMs = 0;
          setState(ST_DISCONNECTED);
        }
        break;

      default: break;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

} // namespace Ntrip
