// uploader.cpp — Scannt /ready4upload auf SD, POSTet Dateien als multipart
// an WifiProv::uploadUrl() ("host:port" Plain-HTTP; TLS-Variante kann spaeter
// ergaenzt werden). Loescht Datei bei 2xx.
//
// Trigger: Stundenwechsel (via millis()-Fallback; GNSS-Zeit-basierter Trigger
// folgt in Integrationsphase) oder SD-Fuellgrad — aktuell periodisch alle
// UPLOAD_SCAN_INTERVAL_MS = 1 Stunde.

#include "uploader.h"
#include "config.h"
#include "app_state.h"
#include "wifi_prov.h"
#include "sd_storage.h"

#include <WiFi.h>
#include <esp_task_wdt.h>

namespace Uploader {

#define UPLOAD_SCAN_INTERVAL_MS (60UL * 60UL * 1000UL)
#define UPLOAD_DIR              "/ready4upload"
#define UPLOAD_ENDPOINT         "/upload"

static uint32_t s_filesUploaded = 0;
static char     s_host[96] = {0};
static uint16_t s_port     = 0;

static bool parseHostPort(const char* url, char* hostOut, size_t hostCap,
                          uint16_t& portOut) {
  if (!url || !*url) return false;
  const char* p = url;
  if (strncmp(p, "http://", 7) == 0)  p += 7;
  else if (strncmp(p, "https://", 8) == 0) p += 8;
  const char* slash = strchr(p, '/');
  const char* end   = slash ? slash : p + strlen(p);
  const char* colon = (const char*)memchr(p, ':', end - p);
  if (!colon) return false;
  size_t hlen = colon - p;
  if (hlen == 0 || hlen >= hostCap) return false;
  memcpy(hostOut, p, hlen); hostOut[hlen] = '\0';
  int port = atoi(colon + 1);
  if (port <= 0 || port > 65535) return false;
  portOut = (uint16_t)port;
  return true;
}

static bool uploadFile(const char* fileName, uint32_t fileSize) {
  WiFiClient client;
  if (!client.connect(s_host, s_port)) {
    DBG_PRINTF("[UPLOAD] connect %s:%u FAILED\n", s_host, s_port);
    return false;
  }

  char boundary[48];
  snprintf(boundary, sizeof(boundary), "----AwareBoundary%08X",
           (unsigned)esp_random());

  char fullPath[300];
  snprintf(fullPath, sizeof(fullPath), "%s/%s", UPLOAD_DIR, fileName);

  FsFile f;
  if (!f.open(fullPath, O_RDONLY)) {
    DBG_PRINTF("[UPLOAD] open %s FAILED\n", fullPath);
    client.stop();
    return false;
  }

  // Body-Teile bauen (fix-size strings, keine Arduino String im Hot-Path)
  char bodyStart[512];
  int bsLen = snprintf(bodyStart, sizeof(bodyStart),
    "--%s\r\nContent-Disposition: form-data; name=\"device_id\"\r\n\r\n%s\r\n"
    "--%s\r\n"
    "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
    "Content-Type: application/octet-stream\r\n\r\n",
    boundary, DEVICE_NAME, boundary, fileName);

  char bodyEnd[64];
  int beLen = snprintf(bodyEnd, sizeof(bodyEnd), "\r\n--%s--\r\n", boundary);

  uint32_t contentLength = bsLen + fileSize + beLen;

  // Headers
  client.printf("POST %s HTTP/1.1\r\n", UPLOAD_ENDPOINT);
  client.printf("Host: %s:%u\r\n", s_host, s_port);
  client.printf("X-API-Key: %s\r\n", WifiProv::uploadKey());
  client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary);
  client.printf("Content-Length: %lu\r\n", (unsigned long)contentLength);
  client.print ("Connection: close\r\n\r\n");

  client.write((const uint8_t*)bodyStart, bsLen);

  uint8_t buf[1024];
  while (f.available()) {
    int r = f.read(buf, sizeof(buf));
    if (r <= 0) break;
    client.write(buf, r);
    esp_task_wdt_reset();
  }
  client.write((const uint8_t*)bodyEnd, beLen);
  f.close();

  // Response (nur Status)
  uint32_t t0 = millis();
  while (client.available() == 0) {
    if (millis() - t0 > 10000) { client.stop(); return false; }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  int httpCode = 0;
  String line = client.readStringUntil('\n');
  if (line.startsWith("HTTP/")) {
    int sp = line.indexOf(' ');
    if (sp > 0) httpCode = line.substring(sp + 1, sp + 4).toInt();
  }
  while (client.available()) client.read();
  client.stop();

  DBG_PRINTF("[UPLOAD] %s -> HTTP %d\n", fileName, httpCode);
  bool ok = (httpCode >= 200 && httpCode < 300);
  if (ok) {
    if (SdStorage::fs().remove(fullPath)) s_filesUploaded++;
  }
  return ok;
}

static void scanAndUpload() {
  if (!SdStorage::isMounted()) return;
  FsFile dir;
  if (!dir.open(UPLOAD_DIR, O_RDONLY)) {
    DBG_PRINTLN("[UPLOAD] /ready4upload missing");
    return;
  }
  FsFile entry;
  while (entry.openNext(&dir, O_RDONLY)) {
    char name[64];
    entry.getName(name, sizeof(name));
    uint32_t sz = entry.size();
    entry.close();

    int nl = strlen(name);
    if (nl <= 4) continue;
    const char* ext = name + nl - 4;
    if (strcmp(ext, ".ubx") != 0 && strcmp(ext, ".txt") != 0) continue;
    if (sz == 0) {
      char full[300]; snprintf(full, sizeof(full), "%s/%s", UPLOAD_DIR, name);
      SdStorage::fs().remove(full);
      continue;
    }

    DBG_PRINTF("[UPLOAD] %s (%lu B)\n", name, (unsigned long)sz);
    if (!uploadFile(name, sz)) break;   // bei Fehler abbrechen, naechster Zyklus
    esp_task_wdt_reset();
  }
  dir.close();
}

bool begin() {
  if (WifiProv::role() != ROLE_IOT_LOGGER_SD) return true;
  if (!parseHostPort(WifiProv::uploadUrl(), s_host, sizeof(s_host), s_port)) {
    DBG_PRINTLN("[UPLOAD] uploadUrl invalid — task idles");
  }
  return true;
}

uint32_t filesUploaded() { return s_filesUploaded; }

void task(void*) {
  esp_task_wdt_add(nullptr);
  uint32_t lastScanMs = 0;

  for (;;) {
    esp_task_wdt_reset();

    if (WifiProv::role() != ROLE_IOT_LOGGER_SD || s_port == 0) {
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    if (WiFi.status() == WL_CONNECTED &&
        (lastScanMs == 0 || millis() - lastScanMs >= UPLOAD_SCAN_INTERVAL_MS)) {
      lastScanMs = millis();
      scanAndUpload();
    }

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

} // namespace Uploader
