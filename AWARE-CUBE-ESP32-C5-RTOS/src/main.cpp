// AWARE Cube — ESP32-C5 (FreeRTOS variant)
// setup() initialisiert Hardware + Queues/Streams, startet pinned Tasks. loop() schlaeft.

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "app_state.h"
#include "display.h"
#include "button.h"
#include "wifi_prov.h"
#include "sd_storage.h"
#include "gnss.h"
#include "ntrip.h"
#include "tcp_stream.h"
#include "uploader.h"

volatile AppState        g_state          = STATE_BOOT;
volatile Role            g_role           = ROLE_IOT_LOGGER_SD;
QueueHandle_t            g_eventQueue     = nullptr;
QueueHandle_t            g_displayQueue   = nullptr;
StreamBufferHandle_t     g_gnssOutStream  = nullptr;
StreamBufferHandle_t     g_rtcmInStream   = nullptr;

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 1500) { delay(10); }
  DBG_PRINTLN("# AWARE Cube (RTOS) boot");

  g_eventQueue    = xQueueCreate(16, sizeof(AppEvent));
  g_displayQueue  = xQueueCreate(8,  sizeof(AppEvent));
  g_gnssOutStream = xStreamBufferCreate(GNSS_OUT_STREAM_BYTES, 64);
  g_rtcmInStream  = xStreamBufferCreate(RTCM_IN_STREAM_BYTES,  64);

  // Display zuerst: SPI wird hier aufgesetzt. SD nutzt denselben Bus.
  if (!Display::begin())   DBG_PRINTLN("# display init FAILED");
  if (!SdStorage::begin()) DBG_PRINTLN("# sd init FAILED (optional)");
  if (!Button::begin())    DBG_PRINTLN("# button init FAILED");
  if (!WifiProv::begin())  DBG_PRINTLN("# wifi init FAILED");
  if (!Gnss::begin())      DBG_PRINTLN("# gnss init FAILED (will retry in task)");
  Ntrip::begin();
  TcpStream::begin();
  Uploader::begin();

  esp_task_wdt_config_t wdt_cfg = {
    .timeout_ms    = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  esp_task_wdt_reconfigure(&wdt_cfg);

  xTaskCreatePinnedToCore(WifiProv::task,  "wifi",    TASK_STACK_WIFI,
                          nullptr, TASK_PRIO_WIFI,    nullptr, TASK_CORE_NET);
  xTaskCreatePinnedToCore(Display::task,   "display", TASK_STACK_DISPLAY,
                          nullptr, TASK_PRIO_DISPLAY, nullptr, TASK_CORE_APP);
  xTaskCreatePinnedToCore(Button::task,    "button",  TASK_STACK_BUTTON,
                          nullptr, TASK_PRIO_BUTTON,  nullptr, TASK_CORE_NET);
  xTaskCreatePinnedToCore(SdStorage::task, "sd",      TASK_STACK_SD,
                          nullptr, TASK_PRIO_SD,      nullptr, TASK_CORE_NET);
  xTaskCreatePinnedToCore(Gnss::task,      "gnss",    TASK_STACK_GNSS,
                          nullptr, TASK_PRIO_GNSS,    nullptr, TASK_CORE_APP);
  xTaskCreatePinnedToCore(Ntrip::task,     "ntrip",   TASK_STACK_NTRIP,
                          nullptr, TASK_PRIO_NTRIP,   nullptr, TASK_CORE_NET);
  xTaskCreatePinnedToCore(TcpStream::task, "tcp",     TASK_STACK_TCP,
                          nullptr, TASK_PRIO_TCP,     nullptr, TASK_CORE_NET);
  xTaskCreatePinnedToCore(Uploader::task,  "upload",  TASK_STACK_UPLOAD,
                          nullptr, TASK_PRIO_UPLOAD,  nullptr, TASK_CORE_NET);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
