// uploader.h — Stuendlicher HTTPS-Upload von /ready4upload auf SD
// (Rolle ROLE_IOT_LOGGER_SD). Trigger: Stundenwechsel + WiFi OK.

#ifndef UPLOADER_H
#define UPLOADER_H

#include <Arduino.h>

namespace Uploader {
  bool begin();
  void task(void* arg);
  uint32_t filesUploaded();
}

#endif // UPLOADER_H
