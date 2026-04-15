// display.cpp — ST7789 240x240. Boot-Splash (Logos) + state-driven Screens.

#include "display.h"
#include "config.h"
#include "wifi_prov.h"
#include "sd_storage.h"
#include "gnss.h"
#include "ntrip.h"

#include "wolf_logo.h"
#include "space_geodesy_logo.h"
#include "eth_spacegeodesy_logo.h"

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SPI.h>
#include <qrcode.h>

namespace Display {

static Adafruit_ST7789       s_tft(TFT_CS, TFT_DC, TFT_RST);
static U8G2_FOR_ADAFRUIT_GFX u8f;

// Schriftarten — klassische bdf-Bitmap-Fonts (Retro-Terminal/Pixel-Look)
// S1  : Footer, Sub-Zeilen (Datum, SSID)
// S2  : Labels und Wert-Felder
// S2B : fett — Header-Titel
// S3  : grosse Status-Meldungen ("Verbunden!", "Fehler!")
// S4  : Boot-Titel (DEVICE_NAME)
#define FONT_S1  u8g2_font_6x10_tf
#define FONT_S2  u8g2_font_8x13_tf
#define FONT_S2B u8g2_font_8x13B_tf
#define FONT_S3  u8g2_font_9x15B_tf
#define FONT_S4  u8g2_font_10x20_tf

// Hilfsfunktion: y = obere Textkante (Adafruit-Konvention).
// u8g2 erwartet die Baseline -> Offset = getFontAscent().
static void u8text(const uint8_t* font, uint16_t color, int x, int y, const char* text) {
  u8f.setFont(font);
  u8f.setForegroundColor(color);
  u8f.setCursor(x, y + u8f.getFontAscent());
  u8f.print(text);
}

// ----- low-level helpers -------------------------------------------------

static void clearScreen() {
  s_tft.fillScreen(COL_BG);
}

static void drawCentered(const char* text, int y, uint16_t color, uint8_t size) {
  const uint8_t* font;
  switch (size) {
    case 1:  font = FONT_S1;  break;
    case 2:  font = FONT_S2;  break;
    case 3:  font = FONT_S3;  break;
    default: font = FONT_S4;  break;
  }
  u8f.setFont(font);
  u8f.setForegroundColor(color);
  uint16_t w = u8f.getUTF8Width(text);
  u8f.setCursor((240 - (int)w) / 2, y + u8f.getFontAscent());
  u8f.print(text);
}

static void drawQr(const char* payload, uint8_t version, int pxPer, int y) {
  QRCode qr;
  uint8_t buf[qrcode_getBufferSize(16)];
  qrcode_initText(&qr, buf, version, ECC_LOW, payload);

  const int modules = qr.size;
  const int qrPx    = modules * pxPer;
  const int qrX     = (240 - qrPx) / 2;

  s_tft.fillRect(qrX - pxPer, y - pxPer,
                 qrPx + 2 * pxPer, qrPx + 2 * pxPer, 0xFFFF);

  for (int yy = 0; yy < modules; yy++) {
    for (int xx = 0; xx < modules; xx++) {
      if (qrcode_getModule(&qr, xx, yy)) {
        s_tft.fillRect(qrX + xx * pxPer, y + yy * pxPer, pxPer, pxPer, 0);
      }
    }
  }
}

// Logo-Splash: 128x128 Bitmap zentriert auf 240x240
static void drawLogo(const uint8_t* bmp, int w, int h, uint16_t color) {
  clearScreen();
  s_tft.drawBitmap((240 - w) / 2, (240 - h) / 2, bmp, w, h, color);
}

// ----- public API --------------------------------------------------------

bool begin() {
  ledcAttach(TFT_BL, 5000, 8);
  ledcWrite(TFT_BL, TFT_BL_BRIGHTNESS);

  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);

  s_tft.init(240, 240);
  s_tft.setSPISpeed(40000000);
  s_tft.setRotation(0);
  clearScreen();

  u8f.begin(s_tft);
  u8f.setFontMode(1);   // transparent: Hintergrundpixel werden nicht ueberschrieben

  DBG_PRINTLN("[Display] Initialisiert (240x240 ST7789)");
  return true;
}

void showBoot() {
  clearScreen();
  drawCentered(DEVICE_NAME, 80, COL_ACCENT, 4);
  drawCentered("Starting...", 130, COL_DIMMED, 2);
}

// Kurze Intro-Animation: konzentrische Rechtecke rein, dann raus
static void introRectAnim() {
  clearScreen();
  for (int i = 0; i < 120; i += 6) {
    s_tft.drawRect(i, i, 240 - 2 * i, 240 - 2 * i, COL_ACCENT);
    delay(15);
  }
  delay(200);
  clearScreen();
}

void showSplashSequence() {
  introRectAnim();
  drawLogo(eth_spagegeodesy_logo_bmp, ETHSPACEG_LOGO_WIDTH, ETHSPACEG_LOGO_HEIGHT, COL_TITLE);
  delay(1200);
  drawLogo(spagegeodesy_logo_bmp, SPACEG_LOGO_WIDTH, SPACEG_LOGO_HEIGHT, COL_TITLE);
  delay(1200);
  drawLogo(wolf_logo_bmp, WOLF_LOGO_WIDTH, WOLF_LOGO_HEIGHT, COL_ACCENT);
  delay(1200);
}

// ======================================================================
//  Einheitliches Layout fuer Boot- und Run-Bildschirme
// ======================================================================
//  Header: "AWARE <mode>" Titelzeile, horizontale Linie darunter.
//  Zeilen: Label (links, FONT_S2, DIMMED) + Wert (rechts ab VAL_X,
//          auto-shrink auf FONT_S1 wenn >22 Zeichen).
//  Footer: DEVICE_NAME unten, DIMMED, FONT_S1.

static constexpr int TITLE_Y   = 6;
static constexpr int HLINE_Y   = 28;
static constexpr int VAL_X     = 60;
static constexpr int VAL_W     = 240 - VAL_X - 4;
static constexpr int ROW_H_BIG = 13;   // Hoehe von FONT_S2 (8x13)
static constexpr int Y_ROW0    = 40;
static constexpr int ROW_STEP  = 28;
static constexpr int FOOTER_Y  = 226;

static int rowY(int i) { return Y_ROW0 + i * ROW_STEP; }

static void drawHeader(const char* title) {
  u8f.setFont(FONT_S2B);
  u8f.setForegroundColor(COL_ACCENT);
  u8f.setCursor(6, TITLE_Y + u8f.getFontAscent());
  u8f.print("AWARE ");
  u8f.setForegroundColor(COL_TITLE);
  u8f.print(title);
  s_tft.drawFastHLine(0, HLINE_Y, 240, COL_DIMMED);
}

static void drawFooter() {
  drawCentered(DEVICE_NAME, FOOTER_Y, COL_DIMMED, 1);
}

static void drawLabel(int y, const char* text) {
  u8text(FONT_S2, COL_DIMMED, 6, y, text);
}

// Wertfeld ueberzeichnen. Bei >22 Zeichen auto-shrink auf FONT_S1.
static void drawValue(int y, const char* text, uint16_t color) {
  s_tft.fillRect(VAL_X, y, VAL_W, ROW_H_BIG, COL_BG);
  const uint8_t* font = (strlen(text) > 22) ? FONT_S1 : FONT_S2;
  u8text(font, color, VAL_X, y, text);
}

// ----- Boot-Screens -------------------------------------------------------

void showSdInit(bool ok, float sizeMB) {
  clearScreen();
  drawHeader("BOOT");
  drawLabel(rowY(0), "SD");
  if (ok) {
    char buf[24];
    if (sizeMB >= 1024.0f) snprintf(buf, sizeof(buf), "%.1fGB", sizeMB / 1024.0f);
    else                   snprintf(buf, sizeof(buf), "%.0fMB OK", sizeMB);
    drawValue(rowY(0), buf, COL_SUCCESS);
  } else {
    drawValue(rowY(0), "keine Karte", COL_ERROR);
  }
  drawFooter();
}

// --- GNSS Init Progress Screen (gleiches Raster) -------------------------

static GnssStatus s_gnssLineStatus[3] = {
  GnssStatus::PENDING, GnssStatus::PENDING, GnssStatus::PENDING
};

static void drawGnssInitScreen() {
  clearScreen();
  drawHeader("GNSS");

  const char* labels[3] = { "I2C", "Modul", "Config" };
  for (int i = 0; i < 3; i++) {
    drawLabel(rowY(i), labels[i]);

    const char* badge;
    uint16_t    col;
    switch (s_gnssLineStatus[i]) {
      case GnssStatus::OK:   badge = "OK";   col = COL_SUCCESS; break;
      case GnssStatus::FAIL: badge = "FAIL"; col = COL_ERROR;   break;
      default:               badge = "...";  col = COL_DIMMED;  break;
    }
    drawValue(rowY(i), badge, col);
  }
  drawFooter();
}

void showGnssInitReset() {
  for (int i = 0; i < 3; i++) s_gnssLineStatus[i] = GnssStatus::PENDING;
  drawGnssInitScreen();
}

void showGnssInitUpdate(GnssLine line, GnssStatus st) {
  s_gnssLineStatus[(uint8_t)line] = st;
  drawGnssInitScreen();
}

// --- Provisioning screens -------------------------------------------------

void showProvisioningAP(const String& apName, const String& password) {
  clearScreen();
  drawCentered("Connect to WiFi",   2,  COL_ACCENT, 2);
  drawCentered("Point your camera", 26, COL_TEXT,   2);
  drawCentered("at the code",       46, COL_TEXT,   2);

  String payload = "WIFI:T:WPA;S:" + apName + ";P:" + password + ";;";
  drawQr(payload.c_str(), 4, 5, 68);
}

void showProvisioningUrl(const String& url) {
  clearScreen();
  drawCentered("Almost done",        2,  COL_SUCCESS, 2);
  drawCentered("Scan again to pick", 26, COL_TEXT,    2);
  drawCentered("your home WiFi",     46, COL_TEXT,    2);
  drawQr(url.c_str(), 3, 5, 78);
}

void showTransitionLookAtDevice() {
  clearScreen();
  drawCentered("Connected!",    50,  COL_SUCCESS, 3);
  drawCentered("Press button",  120, COL_ACCENT,  2);
  drawCentered("for next step", 148, COL_ACCENT,  2);
}

void showConnecting(const String& ssid) {
  clearScreen();
  drawCentered("Verbinde...", 60, COL_WARN, 2);

  u8f.setFont(FONT_S2);
  u8f.setForegroundColor(COL_TITLE);
  uint16_t w = u8f.getUTF8Width(ssid.c_str());
  u8f.setCursor((240 - (int)w) / 2, 100 + u8f.getFontAscent());
  u8f.print(ssid.c_str());

  drawCentered("Bitte warten", 150, COL_DIMMED, 1);
}

void showConnected(const String& ssid, const String& ip) {
  clearScreen();
  drawCentered("Verbunden!", 40, COL_SUCCESS, 3);
  s_tft.drawFastHLine(20, 72, 200, COL_DIMMED);

  u8text(FONT_S1, COL_TEXT,   10, 90,  "WLAN:");
  u8text(FONT_S2, COL_TITLE,  10, 106, ssid.c_str());
  u8text(FONT_S1, COL_TEXT,   10, 136, "IP-Adresse:");
  u8text(FONT_S2, COL_ACCENT, 10, 152, ip.c_str());

  drawCentered(DEVICE_NAME, 220, COL_DIMMED, 1);
}

void showConnectionFailed(ConnectFail reason) {
  clearScreen();
  drawCentered("Fehler!", 50, COL_ERROR, 3);

  u8f.setFont(FONT_S1);
  u8f.setForegroundColor(COL_TEXT);
  u8f.setCursor(10, 100 + u8f.getFontAscent());
  switch (reason) {
    case FAIL_WRONG_PASSWORD: u8f.print("Falsches Passwort.");        break;
    case FAIL_TIMEOUT:        u8f.print("Netzwerk nicht erreichbar."); break;
    default:                  u8f.print("Verbindung fehlgeschlagen.");
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
  drawCentered("Factory",     70,  COL_ERROR,  3);
  drawCentered("Reset",      105,  COL_ERROR,  3);
  drawCentered("Neustart...", 160, COL_DIMMED, 2);
}

// --- Normal Operation: Live-Status ---------------------------------------

static const char* roleShort(Role r) {
  switch (r) {
    case ROLE_IOT_LOGGER_SD:  return "LOG-SD";
    case ROLE_BASE_NTRIP:     return "BASE";
    case ROLE_ROVER_NTRIP:    return "ROVER";
    case ROLE_IOT_LOGGER_TCP: return "LOG-TCP";
  }
  return "?";
}

// Statischer Teil (Header + Labels + Footer) nur einmal zeichnen,
// nur Wertfelder bei 1-Hz-Refresh ueberzeichnen -> kein Flackern.
static bool s_normalStaticDrawn = false;

static void drawNormalStatic() {
  clearScreen();
  drawHeader(roleShort(g_role));
  drawLabel(rowY(0), "UTC");
  drawLabel(rowY(1), "GNSS");
  drawLabel(rowY(2), "SD");
  drawLabel(rowY(3), "WiFi");
  drawLabel(rowY(4), (g_role == ROLE_BASE_NTRIP || g_role == ROLE_ROVER_NTRIP)
                       ? "NTRIP" : "Role");
  drawFooter();
}

static void drawNormalDynamic() {
  char line[48];

  // Zeit (+ Datum als kleine Sub-Zeile)
  if (Gnss::timeValid()) {
    snprintf(line, sizeof(line), "%02u:%02u:%02u",
             Gnss::hour(), Gnss::minute(), Gnss::second());
    drawValue(rowY(0), line, COL_TEXT);
  } else {
    drawValue(rowY(0), "warte...", COL_WARN);
  }
  const int yDate = rowY(0) + ROW_H_BIG + 1;
  s_tft.fillRect(VAL_X, yDate, VAL_W, 10, COL_BG);
  if (Gnss::dateValid()) {
    snprintf(line, sizeof(line), "%04u-%02u-%02u",
             Gnss::year(), Gnss::month(), Gnss::day());
    u8text(FONT_S1, COL_DIMMED, VAL_X, yDate, line);
  }

  // GNSS Fix + SIV
  uint8_t fix = Gnss::fixType();
  if (fix >= 2) {
    snprintf(line, sizeof(line), "F%u SIV %u", fix, Gnss::siv());
    drawValue(rowY(1), line, COL_SUCCESS);
  } else {
    snprintf(line, sizeof(line), "no fix %u", Gnss::siv());
    drawValue(rowY(1), line, COL_WARN);
  }

  // SD
  if (SdStorage::isMounted()) {
    float mb = SdStorage::sizeMB();
    if (mb >= 1024.0f) snprintf(line, sizeof(line), "%.1fGB", mb / 1024.0f);
    else               snprintf(line, sizeof(line), "%.0fMB", mb);
    drawValue(rowY(2), line, COL_SUCCESS);
  } else {
    drawValue(rowY(2), "n/a", COL_ERROR);
  }

  // WiFi (IP)
  if (WifiProv::isConnected()) {
    drawValue(rowY(3), WifiProv::staIp(), COL_SUCCESS);
  } else {
    drawValue(rowY(3), "reconnect", COL_WARN);
  }

  // NTRIP / Role
  if (g_role == ROLE_BASE_NTRIP || g_role == ROLE_ROVER_NTRIP) {
    if (Ntrip::isStreaming()) {
      uint32_t kb = Ntrip::bytesTransferred() / 1024;
      snprintf(line, sizeof(line), "%luKB", (unsigned long)kb);
      drawValue(rowY(4), line, COL_SUCCESS);
    } else {
      drawValue(rowY(4), "warte", COL_WARN);
    }
  } else {
    drawValue(rowY(4), roleShort(g_role), COL_TEXT);
  }

  // SSID-Zeile unter Row4 (klein, DIMMED)
  const int ySsid = rowY(4) + ROW_H_BIG + 6;
  s_tft.fillRect(0, ySsid, 240, 10, COL_BG);
  u8f.setFont(FONT_S1);
  u8f.setForegroundColor(COL_DIMMED);
  u8f.setCursor(6, ySsid + u8f.getFontAscent());
  u8f.print("SSID: ");
  u8f.print(WifiProv::staSsid());
}

void showNormalOperation() {
  if (!s_normalStaticDrawn) {
    drawNormalStatic();
    s_normalStaticDrawn = true;
  }
  drawNormalDynamic();
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
  if (s != STATE_NORMAL_OPERATION) s_normalStaticDrawn = false;
  switch (s) {
    case STATE_BOOT:              showBoot(); break;
    case STATE_SD_INIT:           showSdInit(SdStorage::isMounted(), SdStorage::sizeMB()); break;
    case STATE_PROV_AP:           showProvisioningAP(WifiProv::apSsid(), WifiProv::apPassword()); break;
    case STATE_PROV_URL:          showProvisioningUrl(WifiProv::portalUrl()); break;
    case STATE_PROV_TRANSITION:   showTransitionLookAtDevice(); break;
    case STATE_CONNECTING:
    case STATE_CONNECTING_SAVED:  showConnecting(WifiProv::staSsid()); break;
    case STATE_CONNECTED:         showConnected(WifiProv::staSsid(), WifiProv::staIp()); break;
    case STATE_NORMAL_OPERATION:  showNormalOperation(); break;
    case STATE_CONNECTION_FAILED: showConnectionFailed(WifiProv::lastFailReason()); break;
    case STATE_RECONNECTING:      showReconnecting(WifiProv::reconnectAttempt(), WifiProv::reconnectMax()); break;
    case STATE_FACTORY_RESET:     showFactoryReset(); break;
    default: break;
  }
}

void task(void*) {
  AppEvent evt;
  AppState lastRendered = (AppState)g_state;
  uint32_t lastRefreshMs = 0;

  for (;;) {
    if (xQueueReceive(g_displayQueue, &evt, pdMS_TO_TICKS(200)) == pdTRUE) {
      if (evt.type == EVT_STATE_CHANGED) {
        lastRendered = (AppState)evt.payload;
        render(lastRendered);
        lastRefreshMs = millis();
      }
    }
    // Live-Refresh im Normal-Operation-Screen (1 Hz)
    if (lastRendered == STATE_NORMAL_OPERATION &&
        millis() - lastRefreshMs > 1000) {
      showNormalOperation();
      lastRefreshMs = millis();
    }
  }
}

} // namespace Display
