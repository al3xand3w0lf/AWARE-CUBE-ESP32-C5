// sd_storage.h — SdFat auf shared SPI (mit Display). Typedef heisst SdStorage,
// weil SdFat den Namen 'SdCard' intern belegt.

#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <Arduino.h>
#include <SdFat.h>

namespace SdStorage {
  bool  begin();                                  // Mount (nach Display::begin)
  bool  isMounted();
  float sizeMB();
  void  listRoot();                               // Root -> Serial
  SdFat& fs();                                    // Zugriff fuer GNSS-Logger/Uploader

  // Thread-unsafe: nur vom sd_writer-Task aufrufen (oder unter externer Sync).
  bool appendLine(const char* path, const char* line);
  bool writeBlock(const char* path, const uint8_t* data, size_t n);

  void task(void* arg);                           // drain g_gnssOutStream -> Files
}

#endif // SD_STORAGE_H
