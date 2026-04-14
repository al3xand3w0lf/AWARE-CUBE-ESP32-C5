// sd_storage.cpp — SdFat-Mount auf shared SPI (Display hat SPI.begin() bereits
// gemacht). Task-Loop ist Stub; wird in Phase 4 mit g_gnssOutStream-Drain und
// stuendlicher Filerotation gefuellt.

#include "sd_storage.h"
#include "config.h"
#include "app_state.h"
#include "wifi_prov.h"

#include <SPI.h>

namespace SdStorage {

static SdFat s_sd;
static bool  s_mounted = false;

bool begin() {
  DBG_PRINTLN("[SD] Init (shared SPI)...");
  SdSpiConfig cfg(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(SD_SPI_MHZ), &SPI);
  if (!s_sd.begin(cfg)) {
    DBG_PRINTLN("[SD] FAIL: card not detected");
    s_mounted = false;
    return false;
  }
  s_mounted = true;
  DBG_PRINTF("[SD] OK %.1f MB\n", sizeMB());
  return true;
}

bool  isMounted() { return s_mounted; }

float sizeMB() {
  if (!s_mounted) return 0.0f;
  return s_sd.vol()->sectorsPerCluster() * s_sd.vol()->clusterCount() / 2048.0f;
}

void listRoot() {
  if (!s_mounted) return;
  DBG_PRINTLN("[SD] /:");
  s_sd.ls(&Serial, LS_R | LS_SIZE);
}

SdFat& fs() { return s_sd; }

bool appendLine(const char* path, const char* line) {
  if (!s_mounted) return false;
  FsFile f;
  if (!f.open(path, O_WRONLY | O_CREAT | O_APPEND)) return false;
  size_t n = f.println(line);
  f.close();
  return n > 0;
}

bool writeBlock(const char* path, const uint8_t* data, size_t n) {
  if (!s_mounted) return false;
  FsFile f;
  if (!f.open(path, O_WRONLY | O_CREAT | O_APPEND)) return false;
  size_t w = f.write(data, n);
  f.sync();                // flush pro Block
  f.close();
  return w == n;
}

// ------------------------------------------------------------------ task

void task(void*) {
  // Nur aktiv in Rollen, die auf SD schreiben. Andere Rollen: schlafen.
  uint8_t buf[512];

  for (;;) {
    Role r = WifiProv::role();
    bool sdMode = (r == ROLE_IOT_LOGGER_SD) || (r == ROLE_ROVER_NTRIP);
    if (!sdMode || !s_mounted || g_gnssOutStream == nullptr) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    // Block-weise Drain. In Phase 4 wird File-Rotation (stuendlich) eingebaut;
    // vorerst schreiben wir in ein einziges Fallback-File.
    size_t n = xStreamBufferReceive(g_gnssOutStream, buf, sizeof(buf),
                                    pdMS_TO_TICKS(200));
    if (n > 0) {
      writeBlock("/aware_log.ubx", buf, n);
    }
    // Watchdog (wir sind auf core 1, aber TWDT wurde reconfigured -> feed)
    // -- Rolle erst in Phase 4 mit TWDT-Add fuer diesen Task.
  }
}

} // namespace SdStorage
