# WiFi Provisioning Captive Portal — ESP32-C5

Captive-Portal-basiertes WiFi-Provisioning-System fuer IoT-Geraete.
Der Enduser verbindet sich per Smartphone mit dem Geraet, waehlt sein WLAN
im Browser aus und das Geraet speichert die Credentials dauerhaft im NVS.

---

## Hardware

| Komponente | Details |
|------------|---------|
| MCU | ESP32-C5 DevKitC-1 (RISC-V, 240 MHz, WiFi 6) |
| Display | ST7789 1.54" TFT 240x240, Hardware-SPI |
| Framework | Arduino (PlatformIO, pioarduino develop-Branch) |
| Board | `esp32-c5-devkitc1-n8r4` (8 MB Flash, 4 MB PSRAM) |

### Display-Verkabelung

| Display-Pin | GPIO |
|-------------|------|
| SCK/CLK | GPIO6 (FSPICLK) |
| MOSI/SDA | GPIO7 (FSPID) |
| CS | GPIO0 |
| RST/RES | GPIO1 |
| DC/CMD | GPIO26 |
| BL/BLK | GPIO25 |
| VCC | 3V3 |
| GND | GND |

---

## Funktionsweise

### Erststart (kein WLAN gespeichert)

1. ESP32 startet im `WIFI_AP_STA` Dual-Mode
2. Access Point wird aufgespannt: `AWARE-XXXXXX` (MAC-Suffix), Passwort `12345678`
3. DNS-Server leitet alle Anfragen auf `192.168.4.1` um (Captive Portal)
4. HTTP-Webserver auf Port 80 liefert das Konfigurations-Interface
5. Display zeigt 3-Schritte-Anleitung (WLAN-Name, Passwort, Browser-URL)

### Provisioning-Ablauf (User-Sicht)

1. Smartphone mit `AWARE-XXXXXX` verbinden (Passwort: `12345678`)
2. Browser oeffnen → `192.168.4.1`
3. "Netzwerke scannen" klicken
4. WLAN auswaehlen, Passwort eingeben, "Verbinden"
5. Geraet speichert Credentials und wechselt in den Normalbetrieb

### Folgestarts (WLAN gespeichert)

1. ESP32 liest Credentials aus NVS
2. Verbindet sich direkt im STA-only-Modus
3. Falls nach 30s keine Verbindung: zurueck in Provisioning-Modus
4. Bei Verbindungsverlust: 5 Reconnect-Versuche, dann Provisioning

### Factory Reset

GPIO 9 (konfigurierbar) fuer 5 Sekunden auf LOW halten → NVS wird geloescht, Neustart in Provisioning-Modus.

---

## State Machine

```
BOOT → CHECK_NVS
         ├─ Credentials vorhanden → CONNECTING_SAVED → NORMAL_OPERATION
         └─ Keine Credentials    → PROVISIONING_MODE
                                      ↓
                                   CONNECTING_NEW
                                    ├─ Erfolg → NORMAL_OPERATION
                                    └─ Fehler → PROVISIONING_MODE

NORMAL_OPERATION → (Verbindungsverlust) → CONNECTION_LOST
                                            ├─ Reconnect OK → NORMAL_OPERATION
                                            └─ Max Versuche → PROVISIONING_MODE
```

---

## Captive Portal Detection

| Plattform | Endpunkt | Antwort |
|-----------|----------|---------|
| Android | `/generate_204`, `/gen_204` | 302 Redirect |
| Apple (iOS/macOS) | `/hotspot-detect.html`, `/library/test/success.html` | Volle HTML-Seite |
| Windows | `/connecttest.txt`, `/ncsi.txt` | 302 Redirect |
| Firefox | `/canonical.html`, `/success.txt` | Volle HTML-Seite |
| Alle anderen Pfade | `/*` | 302 Redirect auf `/` |

---

## HTTP-API

| Methode | Pfad | Beschreibung |
|---------|------|-------------|
| GET | `/` | Web-Interface (HTML/CSS/JS inline) |
| GET | `/scan` | JSON-Array der gefundenen WLANs: `[{"ssid":"...","rssi":-45,"enc":4}]` |
| POST | `/connect` | Verbindungsversuch. Body: `ssid=...&pass=...` (URL-encoded) |
| GET | `/status` | Polling-Endpunkt: `{"status":"connecting"}`, `{"status":"ok","ip":"..."}`, `{"status":"error","reason":"..."}` |

---

## Projektstruktur

```
WiFiProvisioning/
├── platformio.ini              # Build-Konfiguration (pioarduino, ESP32-C5)
├── include/
│   └── config.h                # Alle konfigurierbaren Konstanten
├── src/
│   ├── main.cpp                # Setup, Loop, Callback-Integration
│   ├── wifi_provisioning.h     # Klassen-Header, State-Enum
│   ├── wifi_provisioning.cpp   # State Machine, NVS, WiFi-Logik
│   ├── web_server_handlers.cpp # HTTP-Handler, Captive Portal, Scan, Connect
│   ├── html_content.h          # HTML/CSS/JS als PROGMEM-String (~6 KB)
│   ├── display.h               # TFT Display Abstraktion
│   └── display.cpp             # Display-Screens fuer jeden State
```

---

## Konfiguration (config.h)

| Define | Default | Beschreibung |
|--------|---------|-------------|
| `DEVICE_NAME` | `"AWARE"` | Angezeigt im Web-Interface und auf dem Display |
| `AP_PASSWORD` | `"12345678"` | WPA2-Passwort fuer den AP (min. 8 Zeichen) |
| `RESET_PIN` | `9` | GPIO fuer Factory Reset |
| `RESET_HOLD_TIME_MS` | `5000` | Haltezeit fuer Reset |
| `CONNECT_TIMEOUT_MS` | `15000` | Timeout neuer Verbindungsversuch |
| `SAVED_CONNECT_TIMEOUT_MS` | `30000` | Timeout gespeicherte Credentials |
| `RECONNECT_ATTEMPTS` | `5` | Max Reconnect-Versuche |
| `WDT_TIMEOUT_S` | `30` | Watchdog-Timeout |
| `TFT_BL_BRIGHTNESS` | `30` | Display-Helligkeit (0-255) |

---

## Eigene Logik integrieren

In `main.cpp`:

```cpp
void onProvisioningComplete(String ip) {
  // Wird aufgerufen wenn WiFi verbunden ist.
  // Hier eigene Initialisierung:
  // - MQTT-Client starten
  // - Sensoren initialisieren
  // - OTA-Updates aktivieren
}

void loop() {
  provisioning.loop();  // Immer aufrufen!

  if (provisioning.getState() == ProvisioningState::NORMAL_OPERATION) {
    // Eigene Loop-Logik hier
  }
}
```

---

## Build & Flash

```
# PlatformIO CLI
pio run                    # Kompilieren
pio run -t upload          # Flashen
pio device monitor         # Serial Monitor (115200 Baud)
```

Oder in VS Code: PlatformIO Sidebar → Build / Upload / Monitor.

Upload-Baudrate: 921600 (konfiguriert in `platformio.ini`).

---

## Bekannte Hinweise

- **WiFi-Scan blockiert 3-10s**: Synchroner Scan, Watchdog wird vorher gefuettert. Waehrend des Scans antwortet der Webserver nicht.
- **AP+STA Kanal-Constraint**: ESP32 betreibt AP und STA auf demselben Kanal. Beim Verbindungsaufbau kann der AP kurzzeitig den Kanal wechseln.
- **NVS Wear-Leveling**: ESP32 NVS hat eingebautes Wear-Leveling. Credentials werden nur bei erfolgreichem Connect geschrieben.
- **Debug-Flag**: Heisst `PROV_DEBUG` (nicht `DEBUG_SERIAL`), da Adafruit BusIO `DEBUG_SERIAL` intern als Serial-Objekt verwendet.
- **PlatformIO Platform**: Verwendet pioarduino `develop`-Branch, da der offizielle `espressif32` den ESP32-C5 noch nicht unterstuetzt.
