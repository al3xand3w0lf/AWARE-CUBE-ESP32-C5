/**
 * sd_card.cpp — SD-Karten-Init auf shared SPI-Bus.
 * Voraussetzung: SPI.begin(...) wurde bereits vom Display aufgerufen.
 */

#include "sd_card.h"
#include "config.h"
#include <SPI.h>

bool SdCard::begin() {
  DBG_PRINTLN(F("[SD] Initialisiere SD-Karte (shared SPI)..."));

  SdSpiConfig cfg(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(SD_SPI_MHZ), &SPI);
  if (!_sd.begin(cfg)) {
    DBG_PRINTLN(F("[SD] FEHLER: Karte nicht erkannt"));
    _ready = false;
    return false;
  }

  _ready = true;
  DBG_PRINTF("[SD] OK — Volume: %.1f MB\n", sizeMB());
  return true;
}

float SdCard::sizeMB() {
  if (!_ready) return 0.0f;
  return _sd.vol()->sectorsPerCluster() * _sd.vol()->clusterCount() / 2048.0f;
}

void SdCard::listRoot() {
  if (!_ready) {
    DBG_PRINTLN(F("[SD] Nicht bereit"));
    return;
  }
  DBG_PRINTLN(F("[SD] Root-Verzeichnis:"));
  _sd.ls(&Serial, LS_R | LS_SIZE);
}
