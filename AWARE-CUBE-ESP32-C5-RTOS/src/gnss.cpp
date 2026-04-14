// gnss.cpp — u-blox F9P auf Wire (IO5 SDA / IO4 SCL, 400 kHz).
//
// Adaptiert aus 155 AWARE/src/gnss.cpp. Hauptunterschiede:
//   - Wire statt Wire (separater I2C-Bus vom Display)
//   - Alle Daten flow via StreamBuffer statt globaler Callbacks
//   - RTCM-Intercept via DevUBLOXGNSS::processRTCM-Override schreibt in
//     g_gnssOutStream, sofern Rolle == ROLE_BASE_NTRIP
//   - Rover-Modus liest g_rtcmInStream -> pushRawData()
//   - Keine Display/Logfile-Abhaengigkeiten in diesem Modul

#include "gnss.h"
#include "config.h"
#include "wifi_prov.h"

#include <Wire.h>
#include <SparkFun_u-blox_GNSS_v3.h>
#include <esp_task_wdt.h>

namespace Gnss {

// ------------------------------------------------------------------- state

static SFE_UBLOX_GNSS s_gnss;
static volatile uint8_t  s_siv       = 0;
static volatile uint8_t  s_fixType   = 0;
static volatile bool     s_posValid  = false;
static volatile int32_t  s_lat       = 0;
static volatile int32_t  s_lon       = 0;
static volatile int32_t  s_alt       = 0;
static volatile bool     s_timeValid = false;
static volatile bool     s_dateValid = false;

static uint32_t s_lastDataMs   = 0;
static uint8_t  s_reinitCount  = 0;

// Rolle beim Start gecached (wird zur Boot-Zeit uebernommen; Role-Switch
// erfordert laut Plan Reboot).
static Role s_role = ROLE_IOT_LOGGER_SD;

// ---------------------------------------------------------------- helpers

static bool enableRTCMMessage(uint32_t key, uint8_t rate, const char* name) {
  bool ok = s_gnss.newCfgValset(VAL_LAYER_RAM);
  ok &= s_gnss.addCfgValset(key, rate);
  ok &= s_gnss.sendCfgValset();
  DBG_PRINTF("  RTCM %s: %s\n", name, ok ? "OK" : "FAIL");
  return ok;
}

static bool enableRtcmOutput() {
  DBG_PRINTLN("[GNSS] Enabling RTCM3 output...");

  // NMEA auf I2C deaktivieren (Bus-Traffic reduzieren)
  bool ok = s_gnss.newCfgValset(VAL_LAYER_RAM);
  ok &= s_gnss.addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_GLL_I2C, 0);
  ok &= s_gnss.addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_GSA_I2C, 0);
  ok &= s_gnss.addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_GSV_I2C, 0);
  ok &= s_gnss.addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_GST_I2C, 0);
  ok &= s_gnss.addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_RMC_I2C, 0);
  ok &= s_gnss.addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_VTG_I2C, 0);
  ok &= s_gnss.addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_GGA_I2C, 0);
  ok &= s_gnss.addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_ZDA_I2C, 0);
  ok &= s_gnss.sendCfgValset();

  // Proprietaere UBX-RTCM-Leaks aus
  s_gnss.newCfgValset(VAL_LAYER_RAM);
  s_gnss.addCfgValset(UBLOX_CFG_MSGOUT_UBX_RXM_RTCM_I2C, 0);
  s_gnss.sendCfgValset();

  // MSM7-Set einzeln aktivieren (batched valsets koennen Eintraege droppen)
  bool allOk = true;
  allOk &= enableRTCMMessage(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1005_I2C, 10, "1005");
  allOk &= enableRTCMMessage(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1077_I2C,  1, "1077");
  allOk &= enableRTCMMessage(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1087_I2C,  1, "1087");
  allOk &= enableRTCMMessage(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1097_I2C,  1, "1097");
  allOk &= enableRTCMMessage(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1127_I2C,  1, "1127");
  allOk &= enableRTCMMessage(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1230_I2C, 10, "1230");
  return allOk;
}

static bool configureBaseSurveyIn() {
  bool ok = s_gnss.enableSurveyMode(BASE_SURVEYIN_DURATION_S,
                                    BASE_SURVEYIN_ACCURACY_M);
  DBG_PRINTF("[GNSS] Survey-in %ds / %.1fm: %s\n",
             BASE_SURVEYIN_DURATION_S, BASE_SURVEYIN_ACCURACY_M,
             ok ? "OK" : "FAIL");
  return ok;
}

// I2C-Recovery per Bit-Banging auf Wire-Pins.
static void i2cRecovery() {
  DBG_PRINTLN("[GNSS] I2C recovery (bit-bang)");
  Wire.end();
  delay(50);
  pinMode(GNSS_I2C_SDA, INPUT_PULLUP);
  pinMode(GNSS_I2C_SCL, OUTPUT);
  for (int i = 0; i < 9; i++) {
    digitalWrite(GNSS_I2C_SCL, LOW);  delayMicroseconds(5);
    digitalWrite(GNSS_I2C_SCL, HIGH); delayMicroseconds(5);
  }
  pinMode(GNSS_I2C_SDA, OUTPUT);
  digitalWrite(GNSS_I2C_SDA, LOW);   delayMicroseconds(5);
  digitalWrite(GNSS_I2C_SDA, HIGH);  delay(50);

  Wire.begin(GNSS_I2C_SDA, GNSS_I2C_SCL);
  Wire.setClock(GNSS_I2C_HZ);
  delay(100);
}

// -------------------------------------------------------------- init + config

static bool applyRoleConfig() {
  // Gemeinsamer Baseline
  s_gnss.setFileBufferSize(GNSS_FILE_BUFFER_BYTES);
  s_gnss.setNavigationFrequency(GNSS_NAV_RATE_HZ);

  switch (s_role) {
    case ROLE_IOT_LOGGER_SD:
    case ROLE_IOT_LOGGER_TCP:
      s_gnss.setI2COutput(COM_TYPE_UBX);
      s_gnss.logRXMRAWX();                 // RAWX in fileBuffer
      break;

    case ROLE_BASE_NTRIP:
      s_gnss.setI2COutput(COM_TYPE_UBX | COM_TYPE_NMEA | COM_TYPE_RTCM3);
      enableRtcmOutput();
      configureBaseSurveyIn();
      break;

    case ROLE_ROVER_NTRIP:
      s_gnss.setI2COutput(COM_TYPE_UBX | COM_TYPE_NMEA);
      // Corrections kommen ueber pushRawData() aus g_rtcmInStream
      break;
  }

  s_gnss.saveConfigSelective(VAL_CFG_SUBSEC_IOPORT);
  return true;
}

bool begin() {
  s_role = WifiProv::role();

  Wire.begin(GNSS_I2C_SDA, GNSS_I2C_SCL);
  Wire.setClock(GNSS_I2C_HZ);

  // Nicht blockierend: wenn der F9P noch nicht da ist, versucht der Task
  // spaeter erneut (Stall-Recovery).
  if (!s_gnss.begin(Wire, GNSS_I2C_ADDR)) {
    DBG_PRINTLN("[GNSS] begin() failed — will retry in task");
    return false;
  }
  DBG_PRINTLN("[GNSS] module detected");
  applyRoleConfig();
  s_lastDataMs = millis();
  return true;
}

// ------------------------------------------------------------------- getters

uint8_t  siv()        { return s_siv; }
uint8_t  fixType()    { return s_fixType; }
bool     posValid()   { return s_posValid; }
int32_t  latDeg7()    { return s_lat; }
int32_t  lonDeg7()    { return s_lon; }
int32_t  altMm()      { return s_alt; }
bool     timeValid() { return s_timeValid; }
bool     dateValid() { return s_dateValid; }

// ------------------------------------------------------------------- task

static void pollLiveStatus() {
  s_siv       = s_gnss.getSIV();
  s_fixType   = s_gnss.getFixType();
  s_timeValid = s_gnss.getTimeValid();
  s_dateValid = s_gnss.getDateValid();
  if (s_fixType >= 2) {
    s_lat      = s_gnss.getLatitude();
    s_lon      = s_gnss.getLongitude();
    s_alt      = s_gnss.getAltitudeMSL();
    s_posValid = true;
  }
}

void task(void*) {
  esp_task_wdt_add(nullptr);
  uint8_t drainBuf[512];
  uint8_t rtcmBuf[256];
  uint32_t lastStatusMs = 0;

  for (;;) {
    esp_task_wdt_reset();

    // Falls Modul noch nicht online: Recovery probieren.
    if (!s_gnss.isConnected()) {
      if (s_reinitCount >= GNSS_MAX_REINIT) {
        DBG_PRINTLN("[GNSS] max reinits — restart");
        ESP.restart();
      }
      i2cRecovery();
      if (s_gnss.begin(Wire, GNSS_I2C_ADDR)) {
        applyRoleConfig();
        s_reinitCount = 0;
        s_lastDataMs = millis();
      } else {
        s_reinitCount++;
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
    }

    // I2C-Drain — triggert processRTCM()-Override (fuer Base-Rolle) und
    // befuellt den internen fileBuffer (fuer Logger-Rollen).
    s_gnss.checkUblox();

    // Live-Status nur gelegentlich (teuer)
    uint32_t now = millis();
    if (now - lastStatusMs > 500) {
      pollLiveStatus();
      lastStatusMs = now;
    }

    // Logger-Rollen: fileBuffer -> g_gnssOutStream
    if (s_role == ROLE_IOT_LOGGER_SD || s_role == ROLE_IOT_LOGGER_TCP) {
      uint16_t avail = s_gnss.fileBufferAvailable();
      while (avail >= sizeof(drainBuf)) {
        s_gnss.extractFileBufferData(drainBuf, sizeof(drainBuf));
        xStreamBufferSend(g_gnssOutStream, drainBuf, sizeof(drainBuf),
                          pdMS_TO_TICKS(20));
        s_lastDataMs = now;
        avail -= sizeof(drainBuf);
      }
    } else if (s_role == ROLE_BASE_NTRIP) {
      // RTCM-Bytes kommen via processRTCM()-Override direkt in den
      // Stream. Nur "was raus geht" als Heartbeat zaehlen.
      if (xStreamBufferBytesAvailable(g_gnssOutStream) > 0)
        s_lastDataMs = now;
    }

    // Rover: RTCM-Corrections von NTRIP in das Modul pushen.
    if (s_role == ROLE_ROVER_NTRIP && g_rtcmInStream) {
      size_t n = xStreamBufferReceive(g_rtcmInStream, rtcmBuf, sizeof(rtcmBuf), 0);
      if (n > 0) {
        s_gnss.pushRawData(rtcmBuf, n);
        s_lastDataMs = now;
      }
    }

    // Stall-Erkennung
    if (now - s_lastDataMs > GNSS_STALL_TIMEOUT_MS) {
      DBG_PRINTLN("[GNSS] stall detected");
      s_reinitCount++;
      i2cRecovery();
      if (s_gnss.begin(Wire, GNSS_I2C_ADDR)) {
        applyRoleConfig();
        s_lastDataMs = now;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

} // namespace Gnss

// ---------------------------------------------------------------- RTCM hook
//
// Weak-override der u-blox-Lib. Wird pro RTCM-Byte aus checkUblox() aufgerufen.
// Im Base-Modus streamen wir gefilterte Frames in g_gnssOutStream (-> NTRIP).
// Frame-Parser aus legacy gnss.cpp (155 AWARE) uebernommen.

static bool rtcm_isAllowedType(uint16_t t) {
  return t == 1005 || t == 1077 || t == 1087 ||
         t == 1097 || t == 1127 || t == 1230;
}

void DevUBLOXGNSS::processRTCM(uint8_t b) {
  if (Gnss::siv() == 255) return;          // noop; nutze Getter um link zu halten
  // (Echter Guard: nur im Base-Modus weiterleiten)
  if (g_role != ROLE_BASE_NTRIP) return;
  if (g_gnssOutStream == nullptr) return;

  static enum { SYNC, LEN_HI, LEN_LO, TYPE_HI, TYPE_LO, PAYLOAD, CRC } st = SYNC;
  static uint16_t frameLen = 0, msgType = 0, remaining = 0;
  static bool     forward  = false;
  static uint8_t  hdr[5];
  static uint8_t  hdrIdx   = 0;

  switch (st) {
    case SYNC:
      if (b == 0xD3) { hdr[0] = b; hdrIdx = 1; st = LEN_HI; }
      break;
    case LEN_HI:
      hdr[hdrIdx++] = b;
      frameLen = (uint16_t)(b & 0x03) << 8;
      st = LEN_LO;
      break;
    case LEN_LO:
      hdr[hdrIdx++] = b;
      frameLen |= b;
      if (frameLen < 2 || frameLen > 1023) { st = SYNC; }
      else                                  { st = TYPE_HI; }
      break;
    case TYPE_HI:
      hdr[hdrIdx++] = b;
      msgType = (uint16_t)b << 4;
      st = TYPE_LO;
      break;
    case TYPE_LO:
      hdr[hdrIdx++] = b;
      msgType |= (b >> 4);
      forward = rtcm_isAllowedType(msgType);
      if (forward) {
        xStreamBufferSend(g_gnssOutStream, hdr, 5, 0);
      }
      remaining = frameLen - 2;
      st = (remaining > 0) ? PAYLOAD : (remaining = 3, CRC);
      break;
    case PAYLOAD:
      if (forward) xStreamBufferSend(g_gnssOutStream, &b, 1, 0);
      if (--remaining == 0) { remaining = 3; st = CRC; }
      break;
    case CRC:
      if (forward) xStreamBufferSend(g_gnssOutStream, &b, 1, 0);
      if (--remaining == 0) st = SYNC;
      break;
  }
}
