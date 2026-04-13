/**
 * sd_card.h — SD-Karte auf shared SPI-Bus (SCK=6, MOSI=7, MISO=2, CS=3)
 */

#ifndef SD_CARD_H
#define SD_CARD_H

#include <Arduino.h>
#include <SdFat.h>

class SdStorage {
public:
  bool begin();               // true wenn Karte initialisiert
  void listRoot();            // Root-Verzeichnis auf Serial ausgeben
  float sizeMB();             // Kartengroesse in MB
  bool isReady() const { return _ready; }
  SdFat& fs() { return _sd; }

private:
  SdFat _sd;
  bool  _ready = false;
};

#endif
