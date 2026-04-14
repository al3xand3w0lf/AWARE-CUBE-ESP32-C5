// tcp_stream.cpp — Plain-TCP UBX Realtime-Sink fuer Rolle 3.
// Konsumiert g_gnssOutStream, schreibt in 512-B-Chunks auf den TCP-Socket.
// Reconnect-Backoff 1s -> 30s.

#include "tcp_stream.h"
#include "config.h"
#include "app_state.h"
#include "wifi_prov.h"

#include <WiFi.h>
#include <esp_task_wdt.h>

namespace TcpStream {

static WiFiClient s_client;
static bool       s_enabled    = false;
static bool       s_connected  = false;
static uint32_t   s_lastTryMs  = 0;
static uint32_t   s_backoffMs  = TCP_RECONNECT_INIT_MS;
static uint32_t   s_bytes      = 0;

static char     s_host[96] = {0};
static uint16_t s_port     = 0;

// Erwartet "host:port". Bei Fehler false.
static bool parseHostPort(const char* url, char* hostOut, size_t hostCap,
                          uint16_t& portOut) {
  if (!url || !*url) return false;
  const char* colon = strrchr(url, ':');
  if (!colon || colon == url) return false;
  size_t hlen = colon - url;
  if (hlen >= hostCap) return false;
  memcpy(hostOut, url, hlen);
  hostOut[hlen] = '\0';
  int p = atoi(colon + 1);
  if (p <= 0 || p > 65535) return false;
  portOut = (uint16_t)p;
  return true;
}

bool begin() {
  s_enabled = (WifiProv::role() == ROLE_IOT_LOGGER_TCP);
  if (!s_enabled) return true;
  if (!parseHostPort(WifiProv::uploadUrl(), s_host, sizeof(s_host), s_port)) {
    DBG_PRINTLN("[TCP] host:port invalid — task idles");
  }
  return true;
}

bool     isStreaming() { return s_connected; }
uint32_t bytesSent()   { return s_bytes; }

void task(void*) {
  esp_task_wdt_add(nullptr);
  uint8_t buf[TCP_SEND_CHUNK_BYTES];

  for (;;) {
    esp_task_wdt_reset();

    if (!s_enabled || s_port == 0) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (WiFi.status() != WL_CONNECTED) {
      s_connected = false;
      s_client.stop();
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    if (!s_client.connected()) {
      s_connected = false;
      if (millis() - s_lastTryMs < s_backoffMs) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      s_lastTryMs = millis();
      DBG_PRINTF("[TCP] connect %s:%u\n", s_host, s_port);
      if (s_client.connect(s_host, s_port)) {
        DBG_PRINTLN("[TCP] connected");
        s_connected = true;
        s_backoffMs = TCP_RECONNECT_INIT_MS;
      } else {
        DBG_PRINTLN("[TCP] connect FAILED");
        s_backoffMs = (s_backoffMs * 2 < TCP_RECONNECT_MAX_MS)
                        ? s_backoffMs * 2 : TCP_RECONNECT_MAX_MS;
        continue;
      }
    }

    size_t n = xStreamBufferReceive(g_gnssOutStream, buf, sizeof(buf),
                                    pdMS_TO_TICKS(100));
    if (n > 0) {
      size_t w = s_client.write(buf, n);
      s_bytes += w;
      if (w != n) {
        DBG_PRINTLN("[TCP] short write — drop connection");
        s_client.stop();
        s_connected = false;
      }
    }
  }
}

} // namespace TcpStream
