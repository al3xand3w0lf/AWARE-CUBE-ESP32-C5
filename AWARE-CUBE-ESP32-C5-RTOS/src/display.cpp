// display.cpp — ST7789 240x240. Rendering portiert aus legacy src/display.cpp.
// Task dispatcht AppState-Changes aus g_displayQueue an die passenden show*.

#include "display.h"
#include "config.h"
#include "wifi_prov.h"
#include "sd_storage.h"

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <qrcode.h>

namespace Display {

static Adafruit_ST7789 s_tft(TFT_CS, TFT_DC, TFT_RST);

// ----- low-level helpers -------------------------------------------------

static void clearScreen() {
  s_tft.fillScreen(COL_BG);
}

static void drawCentered(const char* text, int y, uint16_t color, uint8_t size) {
  s_tft.setTextSize(size);
  s_tft.setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  s_tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  s_tft.setCursor((240 - w) / 2, y);
  s_tft.print(text);
}

static void drawQr(const char* payload, uint8_t version, int pxPer, int y) {
  QRCode qr;
  uint8_t buf[qrcode_getBufferSize(16)];          // genug fuer bis Version 16
  qrcode_initText(&qr, buf, version, ECC_LOW, payload);

  const int modules = qr.size;
  const int qrPx    = modules * pxPer;
  const int qrX     = (240 - qrPx) / 2;

  s_tft.fillRect(qrX - pxPer, y - pxPer,
                 qrPx + 2 * pxPer, qrPx + 2 * pxPer, 0xFFFF /* white */);

  for (int yy = 0; yy < modules; yy++) {
    for (int xx = 0; xx < modules; xx++) {
      if (qrcode_getModule(&qr, xx, yy)) {
        s_tft.fillRect(qrX + xx * pxPer, y + yy * pxPer, pxPer, pxPer, 0);
      }
    }
  }
}

// ----- public API --------------------------------------------------------

bool begin() {
  ledcAttach(TFT_BL, 5000, 8);
  ledcWrite(TFT_BL, TFT_BL_BRIGHTNESS);

  // Hardware-SPI auf native FSPI-Pins (shared mit SD-Karte)
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);

  s_tft.init(240, 240);
  s_tft.setSPISpeed(40000000);
  s_tft.setRotation(0);
  clearScreen();

  DBG_PRINTLN("[Display] Initialisiert (240x240 ST7789)");
  return true;
}

void showBoot() {
  clearScreen();
  drawCentered(DEVICE_NAME, 80, COL_ACCENT, 4);
  drawCentered("Starting...", 130, COL_DIMMED, 2);
}

void showSdInit(bool ok, float sizeMB) {
  clearScreen();
  drawCentered("SD Karte", 60, COL_ACCENT, 3);
  if (ok) {
    drawCentered("OK", 110, COL_SUCCESS, 4);
    char buf[24];
    if (sizeMB >= 1024.0f) snprintf(buf, sizeof(buf), "%.1f GB", sizeMB / 1024.0f);
    else                   snprintf(buf, sizeof(buf), "%.0f MB", sizeMB);
    drawCentered(buf, 165, COL_TEXT, 2);
  } else {
    drawCentered("Keine Karte", 120, COL_ERROR, 2);
    drawCentered("erkannt", 150, COL_ERROR, 2);
  }
}

void showProvisioningAP(const String& apName, const String& password) {
  clearScreen();
  drawCentered("Connect to WiFi", 2,  COL_ACCENT, 2);
  drawCentered("Point your camera", 26, COL_TEXT, 2);
  drawCentered("at the code",       46, COL_TEXT, 2);

  String payload = "WIFI:T:WPA;S:" + apName + ";P:" + password + ";;";
  drawQr(payload.c_str(), /*version*/ 4, /*pxPer*/ 5, /*y*/ 68);
}

void showProvisioningUrl(const String& url) {
  clearScreen();
  drawCentered("Almost done",       2,  COL_SUCCESS, 2);
  drawCentered("Scan again to pick", 26, COL_TEXT, 2);
  drawCentered("your home WiFi",    46, COL_TEXT, 2);
  drawQr(url.c_str(), /*version*/ 3, /*pxPer*/ 5, /*y*/ 78);
}

void showTransitionLookAtDevice() {
  clearScreen();
  drawCentered("Connected!",    50,  COL_SUCCESS, 3);
  drawCentered("Press button", 120, COL_ACCENT, 2);
  drawCentered("for next step",148, COL_ACCENT, 2);
}

void showConnecting(const String& ssid) {
  clearScreen();
  drawCentered("Verbinde...", 60, COL_WARN, 2);

  s_tft.setTextColor(COL_TITLE);
  s_tft.setTextSize(2);
  int16_t x1, y1; uint16_t w, h;
  s_tft.getTextBounds(ssid.c_str(), 0, 0, &x1, &y1, &w, &h);
  s_tft.setCursor((240 - w) / 2, 100);
  s_tft.print(ssid);

  drawCentered("Bitte warten", 150, COL_DIMMED, 1);
}

void showConnected(const String& ssid, const String& ip) {
  clearScreen();
  drawCentered("Verbunden!", 40, COL_SUCCESS, 3);
  s_tft.drawFastHLine(20, 72, 200, COL_DIMMED);

  s_tft.setTextColor(COL_TEXT); s_tft.setTextSize(1);
  s_tft.setCursor(10, 90);  s_tft.print("WLAN:");

  s_tft.setTextColor(COL_TITLE); s_tft.setTextSize(2);
  s_tft.setCursor(10, 106); s_tft.print(ssid);

  s_tft.setTextColor(COL_TEXT); s_tft.setTextSize(1);
  s_tft.setCursor(10, 136); s_tft.print("IP-Adresse:");

  s_tft.setTextColor(COL_ACCENT); s_tft.setTextSize(2);
  s_tft.setCursor(10, 152); s_tft.print(ip);

  drawCentered(DEVICE_NAME, 220, COL_DIMMED, 1);
}

void showConnectionFailed(ConnectFail reason) {
  clearScreen();
  drawCentered("Fehler!", 50, COL_ERROR, 3);

  s_tft.setTextColor(COL_TEXT); s_tft.setTextSize(1);
  s_tft.setCursor(10, 100);
  switch (reason) {
    case FAIL_WRONG_PASSWORD: s_tft.print("Falsches Passwort."); break;
    case FAIL_TIMEOUT:        s_tft.print("Netzwerk nicht erreichbar."); break;
    default:                  s_tft.print("Verbindung fehlgeschlagen.");
  }
  drawCentered("Erneut versuchen", 140, COL_WARN, 2);
  drawCentered("im Browser",       165, COL_WARN, 2);
}

void showReconnecting(int attempt, int maxAttempts) {
  clearScreen();
  drawCentered("Verbindung", 50, COL_WARN, 2);
  drawCentered("verloren",   75, COL_WARN, 2);
  char buf[32];
  snprintf(buf, sizeof(buf), "Versuch %d/%d", attempt, maxAttempts);
  drawCentered(buf, 130, COL_TEXT, 2);
}

void showFactoryReset() {
  clearScreen();
  drawCentered("Factory",    70,  COL_ERROR, 3);
  drawCentered("Reset",     105,  COL_ERROR, 3);
  drawCentered("Neustart...",160, COL_DIMMED, 2);
}

void pulseBacklight(int pulses) {
  for (int i = 0; i < pulses; i++) {
    ledcWrite(TFT_BL, 255);
    vTaskDelay(pdMS_TO_TICKS(150));
    ledcWrite(TFT_BL, TFT_BL_BRIGHTNESS);
    vTaskDelay(pdMS_TO_TICKS(150));
  }
}

// ----- Task ---------------------------------------------------------------

static void render(AppState s) {
  switch (s) {
    case STATE_BOOT:
      showBoot();
      break;
    case STATE_SD_INIT:
      showSdInit(SdStorage::isMounted(), SdStorage::sizeMB());
      break;
    case STATE_PROV_AP:
      showProvisioningAP(WifiProv::apSsid(), WifiProv::apPassword());
      break;
    case STATE_PROV_URL:
      showProvisioningUrl(WifiProv::portalUrl());
      break;
    case STATE_PROV_TRANSITION:
      showTransitionLookAtDevice();
      break;
    case STATE_CONNECTING:
    case STATE_CONNECTING_SAVED:
      showConnecting(WifiProv::staSsid());
      break;
    case STATE_CONNECTED:
    case STATE_NORMAL_OPERATION:
      showConnected(WifiProv::staSsid(), WifiProv::staIp());
      break;
    case STATE_CONNECTION_FAILED:
      showConnectionFailed(WifiProv::lastFailReason());
      break;
    case STATE_RECONNECTING:
      showReconnecting(WifiProv::reconnectAttempt(), WifiProv::reconnectMax());
      break;
    case STATE_FACTORY_RESET:
      showFactoryReset();
      break;
    default:
      break;
  }
}

void task(void*) {
  showBoot();
  AppEvent evt;
  for (;;) {
    if (xQueueReceive(g_displayQueue, &evt, pdMS_TO_TICKS(200)) == pdTRUE) {
      if (evt.type == EVT_STATE_CHANGED) {
        render((AppState)evt.payload);
      }
    }
  }
}

} // namespace Display
