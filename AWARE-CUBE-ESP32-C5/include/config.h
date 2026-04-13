/**
 * config.h — Zentrale Konfiguration für WiFi Provisioning
 *
 * Alle anpassbaren Parameter an einer Stelle gesammelt.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- Geräte-Identifikation ---
#define DEVICE_NAME       "AWARE"             // Angezeigt im Web-Interface und als AP-Prefix
#define AP_PASSWORD       "12345678"           // AP-Passwort (min. 8 Zeichen für WPA2)

// --- Netzwerk ---
#define AP_CHANNEL        1                   // AP-Kanal (wird ggf. vom STA überschrieben)
#define AP_MAX_CLIENTS    4                   // Maximale gleichzeitige AP-Clients
#define AP_IP             IPAddress(192, 168, 4, 1)
#define AP_GATEWAY        IPAddress(192, 168, 4, 1)
#define AP_SUBNET         IPAddress(255, 255, 255, 0)

// --- Timeouts (Millisekunden) ---
#define CONNECT_TIMEOUT_MS      15000         // Timeout für neuen Verbindungsversuch
#define SAVED_CONNECT_TIMEOUT_MS 30000        // Timeout für gespeicherte Credentials
#define AP_SHUTDOWN_DELAY_MS    3000          // Verzögerung bevor AP abgeschaltet wird

// --- Reset-Button ---
#define RESET_PIN         9                   // GPIO für Factory-Reset
#define RESET_HOLD_TIME_MS 5000               // Haltezeit für Reset (5 Sekunden)

// --- NVS ---
#define NVS_NAMESPACE     "wifi"              // NVS-Namespace für Credentials
#define NVS_KEY_SSID      "ssid"
#define NVS_KEY_PASS      "pass"

// --- Reconnect ---
#define RECONNECT_ATTEMPTS 5                  // Versuche bevor zurück in Provisioning
#define RECONNECT_INTERVAL_MS 10000           // Abstand zwischen Reconnect-Versuchen

// --- Watchdog ---
#define WDT_TIMEOUT_S     30                  // Watchdog-Timeout in Sekunden (Scan braucht bis 10s)

// --- Display: ST7789 240x240 Hardware-SPI (ESP32-C5) ---
#define TFT_CS            0
#define TFT_RST           1
#define TFT_DC           26
#define TFT_BL           25
#define TFT_SCLK          6                   // SPI Clock (nativer FSPICLK) — shared mit SD
#define TFT_MOSI          7                   // SPI MOSI (nativer FSPID)   — shared mit SD
#define TFT_MISO          2                   // SPI MISO (nativer FSPIQ)   — nur von SD benoetigt
#define TFT_BL_BRIGHTNESS 30                  // Helligkeit 0-255

// --- SD-Karte: shared SPI mit ST7789 (SCK=6, MOSI=7, MISO=2) ---
#define SD_CS_PIN         3                   // Chip-Select der SD-Karte
#define SD_SPI_MHZ        4                   // SPI-Speed fuer SD (shared bus)

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
