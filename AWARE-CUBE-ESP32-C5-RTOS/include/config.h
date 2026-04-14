// config.h — zentrale Konfiguration (Pins, Timeouts, NVS-Keys)
// Single source of truth; nicht in Modulen hardcoden.

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>

// --- Geraete-Identifikation ---
#define DEVICE_NAME       "AWARE"
#define AP_PASSWORD       "12345678"

// --- Netzwerk ---
#define AP_CHANNEL        1
#define AP_MAX_CLIENTS    4
#define AP_IP             IPAddress(192, 168, 4, 1)
#define AP_GATEWAY        IPAddress(192, 168, 4, 1)
#define AP_SUBNET         IPAddress(255, 255, 255, 0)

// --- Timeouts (ms) ---
#define CONNECT_TIMEOUT_MS        15000
#define SAVED_CONNECT_TIMEOUT_MS  30000
#define AP_SHUTDOWN_DELAY_MS       3000

// --- Button (GPIO24, aktiv HIGH, interner Pull-Down) ---
#define BUTTON_PIN            24
#define BUTTON_SHORT_MAX_MS  1000
#define BUTTON_LONG_HOLD_MS 10000
#define BUTTON_DEBOUNCE_MS     50

// --- NVS ---
#define NVS_NAMESPACE     "wifi"
#define NVS_KEY_SSID      "ssid"
#define NVS_KEY_PASS      "pass"

// --- Reconnect ---
#define RECONNECT_ATTEMPTS    5
#define RECONNECT_INTERVAL_MS 10000

// --- Watchdog ---
#define WDT_TIMEOUT_S     30

// --- Display ST7789 240x240 (shared SPI) ---
#define TFT_CS             0
#define TFT_RST            1
#define TFT_DC            26
#define TFT_BL            25
#define TFT_SCLK           6
#define TFT_MOSI           7
#define TFT_MISO           2
#define TFT_BL_BRIGHTNESS 30

// --- SD-Karte (shared SPI mit Display) ---
#define SD_CS_PIN          3
#define SD_SPI_MHZ         4

// --- GNSS u-blox F9P (separater I2C-Bus Wire1) ---
#define GNSS_I2C_SDA       5
#define GNSS_I2C_SCL       4
#define GNSS_I2C_HZ   100000     // Fast-Mode
#define GNSS_I2C_ADDR    0x42

// --- Stream-Buffer-Groessen (Byte) ---
#define GNSS_OUT_STREAM_BYTES  32768  // GNSS -> SD/NTRIP/TCP (~32s bei 1kB/s)
#define RTCM_IN_STREAM_BYTES    8192  // NTRIP Sink -> GNSS pushRawData

// --- GNSS Laufzeit-Tunables ---
#define GNSS_NAV_RATE_HZ           1
#define GNSS_FILE_BUFFER_BYTES  16384   // interner ring-buffer der u-blox-lib
#define GNSS_STALL_TIMEOUT_MS  (5 * 60 * 1000UL)
#define GNSS_MAX_REINIT            3

// --- Base-Station Survey-In (Rolle 1) ---
#define BASE_SURVEYIN_DURATION_S  300
#define BASE_SURVEYIN_ACCURACY_M  2.0f

// --- NTRIP Client ---
#define NTRIP_RECONNECT_INIT_MS    1000
#define NTRIP_RECONNECT_MAX_MS    30000
#define NTRIP_HANDSHAKE_TIMEOUT_MS 5000
#define NTRIP_SEND_CHUNK_BYTES      512

// --- TCP Realtime Stream (Rolle 3) ---
#define TCP_RECONNECT_INIT_MS    1000
#define TCP_RECONNECT_MAX_MS    30000
#define TCP_SEND_CHUNK_BYTES      512
#define FIRMWARE_VERSION         "1.0.0"

// --- Task-Konfiguration (Core / Prio / Stack) ---
// ESP32-C5 ist Single-Core (RISC-V). Alle Tasks auf Core 0.
#define TASK_CORE_APP      0
#define TASK_CORE_NET      0

#define TASK_PRIO_WIFI     5
#define TASK_PRIO_GNSS     6
#define TASK_PRIO_DISPLAY  4
#define TASK_PRIO_NTRIP    4
#define TASK_PRIO_TCP      4
#define TASK_PRIO_UPLOAD   2
#define TASK_PRIO_BUTTON   3
#define TASK_PRIO_SD       2

#define TASK_STACK_WIFI    6144
#define TASK_STACK_GNSS    8192
#define TASK_STACK_DISPLAY 4096
#define TASK_STACK_NTRIP   6144
#define TASK_STACK_TCP     4096
#define TASK_STACK_UPLOAD  6144
#define TASK_STACK_BUTTON  2048
#define TASK_STACK_SD      4096

// --- Debug ---
#ifdef PROV_DEBUG
  #define DBG_PRINT(...)    Serial.print(__VA_ARGS__)
  #define DBG_PRINTLN(...)  Serial.println(__VA_ARGS__)
  #define DBG_PRINTF(...)   Serial.printf(__VA_ARGS__)
#else
  #define DBG_PRINT(...)
  #define DBG_PRINTLN(...)
  #define DBG_PRINTF(...)
#endif

#endif // CONFIG_H
