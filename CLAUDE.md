# CLAUDE.md

Guidance for Claude Code when working in this repository.

## Project

**AWARE Cube** — ESP32-C5 GNSS-IoT device. Captive-Portal WiFi provisioning,
ST7789 TFT, SD card (shared SPI), GPIO24 button, **u-blox F9P RTK** via I²C.
Two parallel PlatformIO projects:

| Directory | Status | Architecture |
|---|---|---|
| `AWARE-CUBE-ESP32-C5/` | Legacy reference | Arduino-loop; state machine in `wifi_provisioning.cpp`. |
| `AWARE-CUBE-ESP32-C5-RTOS/` | **Active** | FreeRTOS tasks, queues/stream-buffers. Modelled after `../../167 Aelos/aeolos`. |

## Roles (NVS `aware/role`, boot-time selected — switch requires reboot)

| ID | Name | GNSS output | Consumer of `g_gnssOutStream` |
|---|---|---|---|
| 0 | `ROLE_IOT_LOGGER_SD` | UBX + RAWX | `sd_writer` → SD, `uploader` POSTs files hourly |
| 1 | `ROLE_BASE_NTRIP` | RTCM3 (MSM7) | `ntrip` SOURCE mode → caster |
| 2 | `ROLE_ROVER_NTRIP` | UBX + NMEA | `ntrip` GET mode → `g_rtcmInStream` → `pushRawData()` |
| 3 | `ROLE_IOT_LOGGER_TCP` | UBX realtime | `tcp_stream` → plain-TCP socket |

## Build / Flash / Monitor

PlatformIO, Arduino framework, **pioarduino `#develop`** platform (stock
`espressif32` doesn't support C5). Board: `esp32-c5-devkitc1-n8r4`.

```bash
cd AWARE-CUBE-ESP32-C5-RTOS
pio run                         # compile
pio run -t upload               # flash @ 921600
pio device monitor              # serial @ 115200
```

Debug flag is `PROV_DEBUG`. **Do not rename to `DEBUG_SERIAL`** — clashes
with Adafruit BusIO internals.

## RTOS architecture

`main.cpp` runs `setup()` only; `loop()` sleeps. Tasks are pinned.

**ESP32-C5 is single-core** (RISC-V). Both `TASK_CORE_APP` and
`TASK_CORE_NET` are `0`. Do not use core 1 — `xTaskCreatePinnedToCore`
asserts.

| Task | Prio | Stack | Role |
|---|---|---|---|
| `gnss`    | 6 | 8 KB | F9P I²C drain, `checkUblox()`, fileBuffer → `g_gnssOutStream`, RTCM intercept via `DevUBLOXGNSS::processRTCM` override, rover `pushRawData` from `g_rtcmInStream`, I²C bit-bang recovery |
| `wifi`    | 5 | 6 KB | Provisioning SM, AP+STA, DNS, HTTP handlers, NVS |
| `ntrip`   | 4 | 6 KB | Dual-mode NTRIP (SOURCE for Base / GET+Basic-Auth for Rover), ICY/HTTP 200 handshake, 1→30 s backoff |
| `tcp`     | 4 | 4 KB | Plain-TCP UBX stream (role 3), host:port from `uploadUrl` |
| `display` | 4 | 4 KB | ST7789 rendering, one screen per state, QR codes |
| `button`  | 3 | 2 KB | GPIO24 ISR, short/long/10 s-hold classification |
| `sd`      | 2 | 4 KB | SdFat mount + drain `g_gnssOutStream` to rotating files |
| `upload`  | 2 | 6 KB | Scan `/ready4upload`, multipart POST, delete on 2xx (role 0) |

**IPC** (`include/app_state.h`):
- `g_state` — volatile `AppState`, read-mostly, single writer is `wifi`.
- `g_role` — volatile `Role`, set once at boot from NVS.
- `g_eventQueue` (16× `AppEvent`) — button + web handler events → wifi SM.
- `g_displayQueue` (8× `AppEvent`) — state changes → display.
- `g_gnssOutStream` (32 KB) — GNSS bytes → exactly one consumer (SD, NTRIP
  source, or TCP) depending on role.
- `g_rtcmInStream` (8 KB) — NTRIP RTCM bytes → GNSS `pushRawData` (rover).

Watchdog `WDT_TIMEOUT_S=30`. WiFi scan blocks up to 10 s → don't lower.
GNSS, NTRIP, TCP and uploader tasks call `esp_task_wdt_add/reset`.

## Hardware (do not change without discussion)

- **Shared SPI** (SCK=6, MOSI=7, MISO=2) — ST7789 + SD. `SPI.begin()` in
  `Display::begin()`; SD mounts on the same bus.
- **Display**: CS=0, RST=1, DC=26, BL=25 (PWM via `ledcAttach`,
  `TFT_BL_BRIGHTNESS=30`).
- **SD**: CS=3, `SD_SPI_MHZ=4` (shared bus is sensitive).
- **Button**: GPIO24, **active HIGH** with internal pull-down. Short (<1 s)
  = next info screen; long hold (≥10 s) = factory reset. Debounce 50 ms.
- **GNSS (u-blox F9P)**: I²C on **`Wire`** (HP_I2C) — SDA=5, SCL=4,
  400 kHz, addr 0x42. **Do not use `Wire1`**: that's LP_I2C on C5 and is
  pin-locked to GPIO#2 (SDA) / GPIO#1 (SCL).
- **AP+STA**: AP SSID `AWARE-XXXXXX` (MAC suffix), pass `12345678`, IP
  `192.168.4.1`. Captive-portal DNS catches all hostnames; handlers must
  cover Android/iOS/Windows/Firefox probes or detection breaks. RFC 8910
  DHCP option 114 is set via `esp_netif_dhcps_option`.

## GNSS specifics

- **Base role** enables RTCM3 MSM7 set: 1005, 1077, 1087, 1097, 1127, 1230.
  Survey-in 300 s / 2.0 m.
- **RTCM frame filter**: `DevUBLOXGNSS::processRTCM` weak-override parses
  the SYNC/LEN/TYPE/PAYLOAD/CRC state machine and only forwards allowed
  types into `g_gnssOutStream`. Guarded by `g_role == ROLE_BASE_NTRIP`.
- **Stall recovery**: no data for 5 min → bit-bang I²C recovery (9× SCL
  toggle + STOP), then re-`begin()`. After `GNSS_MAX_REINIT=3` failures
  `ESP.restart()`.
- **Logger roles** use UBX + `logRXMRAWX()` (internal fileBuffer 16 KB).

## NVS

- `wifi` namespace: `ssid`, `pass`. Credentials only written on **success**,
  not on form submit.
- `aware` namespace: `role`, `ntrip_host`, `ntrip_port`, `ntrip_mp`,
  `ntrip_user`, `ntrip_pass`, `upload_url` (host:port), `upload_key`.

Factory reset (button ≥10 s) clears both namespaces and reboots.

`uploadUrl` is reused as `host:port` for role 3 (plain TCP) — no URL
scheme/path, just endpoint.

## Conventions

- User-facing strings and comments: **German**. Identifiers and log tags:
  **English**.
- `include/config.h` is the single source of truth for pins/timeouts/task
  params — do not hardcode elsewhere.
- Module-per-concern with C-style namespaces (`Gnss::`, `Ntrip::`,
  `Display::`, `WifiProv::`, `Button::`, `SdStorage::`, `TcpStream::`,
  `Uploader::`). No classes unless a library forces it.
- No Arduino `String` in hot paths; `Serial.printf` for formatted output.
- App logic lives outside `wifi_prov.cpp` — use `g_eventQueue` /
  `g_displayQueue` instead of adding hooks inside the provisioning module.

## Toolchain pitfalls

1. pioarduino `#develop` is required for ESP32-C5. Stock `platform =
   espressif32` pins Arduino-ESP32 2.x and doesn't know C5.
2. ESP32-C5 is **single-core** — all tasks must pin to core 0.
3. `esp_task_wdt_init()` in IDF 5.x takes a config struct; use
   `esp_task_wdt_reconfigure(&cfg)` because the Arduino core already inits
   the TWDT.
4. `Wire1` on C5 is LP_I2C with fixed pins (SDA=GPIO2). For arbitrary pins
   use `Wire` (HP_I2C).
5. `sd_card.h` / `SdCard` name collides with SdFat typedef → use
   `SdStorage`.
6. SdFat emits a `#warning File not defined because __has_include(FS.h)` —
   harmless; we use `FsFile` throughout.
