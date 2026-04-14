// app_state.h — globale System-States und Event-Typen fuer Inter-Task-Kommunikation

#ifndef APP_STATE_H
#define APP_STATE_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/stream_buffer.h>

enum AppState : uint8_t {
  STATE_BOOT = 0,
  STATE_SPLASH,                  // Logo-Sequenz (synchron in setup())
  STATE_SD_INIT,                 // SD-Mount-Status (3 s Einblendung)
  STATE_GNSS_INIT,               // u-blox F9P Detect + Config (synchron)
  STATE_CHECK_NVS,
  STATE_PROV_AP,                 // QR1: AP-Credentials
  STATE_PROV_URL,                // QR2: Portal-URL (Client ist auf AP)
  STATE_PROV_TRANSITION,         // "Connected! Press button" zwischen QR1/QR2
  STATE_CONNECTING,              // STA-Verbindung laeuft (neue Creds)
  STATE_CONNECTING_SAVED,        // STA-Verbindung mit gespeicherten Creds
  STATE_CONNECTED,               // "Verbunden!" Screen (kurz nach Connect)
  STATE_NORMAL_OPERATION,        // GNSS/App-Run
  STATE_CONNECTION_FAILED,       // nach Fehlschlag (mit Reason)
  STATE_RECONNECTING,            // Wiederverbindungsversuche
  STATE_FACTORY_RESET,           // Screen kurz vor Reboot
  STATE_ERROR,
};

enum ConnectFail : uint8_t {
  FAIL_NONE = 0,
  FAIL_WRONG_PASSWORD,
  FAIL_TIMEOUT,
  FAIL_OTHER,
};

enum Role : uint8_t {
  ROLE_IOT_LOGGER_SD  = 0,   // UBX auf SD, stuendlicher Upload
  ROLE_BASE_NTRIP     = 1,   // RTCM3 via NTRIP als Source
  ROLE_ROVER_NTRIP    = 2,   // RTCM3 via NTRIP als Sink
  ROLE_IOT_LOGGER_TCP = 3,   // UBX realtime via plain TCP
};

enum EventType : uint8_t {
  EVT_NONE = 0,
  EVT_BUTTON_SHORT,
  EVT_BUTTON_LONG,               // Factory-Reset Trigger (>=10s)
  EVT_WIFI_CLIENT_JOINED,        // Client ist auf AP verbunden -> QR2
  EVT_WIFI_CONNECT_REQUEST,      // vom Web-Handler: neue Creds eingereicht
  EVT_WIFI_CONNECTED,
  EVT_WIFI_DISCONNECTED,
  EVT_STATE_CHANGED,             // payload = neuer AppState
};

struct AppEvent {
  EventType type;
  uint32_t  payload;             // EVT_STATE_CHANGED: neuer AppState
};

// Globals (definiert in main.cpp)
extern volatile AppState  g_state;
extern volatile Role      g_role;
extern QueueHandle_t      g_eventQueue;     // Events -> wifi/state-handler
extern QueueHandle_t      g_displayQueue;   // AppEvent (meist STATE_CHANGED) -> display
extern StreamBufferHandle_t g_gnssOutStream; // GNSS -> SD oder NTRIP oder TCP
extern StreamBufferHandle_t g_rtcmInStream;  // NTRIP Sink -> GNSS pushRawData

#endif // APP_STATE_H
