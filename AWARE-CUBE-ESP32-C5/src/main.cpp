/**
 * main.cpp — Einstiegspunkt für das WiFi Provisioning System
 *
 * Setup: Serial + Provisioning initialisieren
 * Loop:  Provisioning State Machine + eigene Anwendungslogik
 */

#include <Arduino.h>
#include "wifi_provisioning.h"
#include "sd_card.h"
#include "config.h"

WiFiProvisioning provisioning;
SdStorage sdCard;

// Wird aufgerufen wenn Provisioning erfolgreich abgeschlossen ist
void onProvisioningComplete(String ip) {
  DBG_PRINTF("[Main] Provisioning abgeschlossen! IP: %s\n", ip.c_str());
  DBG_PRINTLN(F("[Main] Normalbetrieb gestartet — hier eigene Logik einfügen"));

  // ==============================================
  // HIER eigene Initialisierung nach WiFi-Verbindung:
  // - MQTT-Client starten
  // - Sensoren initialisieren
  // - OTA-Updates aktivieren
  // - etc.
  // ==============================================
}

void setup() {
  #ifdef PROV_DEBUG
  Serial.begin(115200);
  delay(500);  // Kurz warten bis Serial bereit
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("  WiFi Provisioning — ESP32-C5"));
  Serial.println(F("========================================"));
  #endif

  provisioning.setOnCompleteCallback(onProvisioningComplete);

  // 1. Display + Boot-Screen (initialisiert auch SPI-Bus fuer SD)
  provisioning.initDisplay();
  delay(800);

  // 2. SD-Karte initialisieren und 3s Status anzeigen
  bool sdOk = sdCard.begin();
  if (sdOk) sdCard.listRoot();
  provisioning.display().showSdInit(sdOk, sdCard.sizeMB());
  delay(3000);

  // 3. WiFi-Provisioning starten (zeigt AP-Screen)
  provisioning.begin();
}

void loop() {
  // Provisioning State Machine (muss jeden Loop-Durchlauf aufgerufen werden)
  provisioning.loop();

  // ==============================================
  // HIER eigene Loop-Logik:
  // Nur ausführen wenn im Normalbetrieb!
  // ==============================================
  if (provisioning.getState() == ProvisioningState::NORMAL_OPERATION) {
    // Beispiel: Sensorwerte lesen, MQTT publishen, etc.
  }
}
