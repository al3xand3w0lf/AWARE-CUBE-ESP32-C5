// button.cpp — GPIO24 (active HIGH, pull-down). ISR setzt Flag, Task klassifiziert.

#include "button.h"
#include "config.h"

namespace Button {

static volatile bool     s_edgeFlag = false;
static volatile uint32_t s_lastEdgeMs = 0;

static void IRAM_ATTR isr() {
  uint32_t now = millis();
  if ((now - s_lastEdgeMs) < BUTTON_DEBOUNCE_MS) return;
  s_lastEdgeMs = now;
  s_edgeFlag   = true;
}

bool begin() {
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), isr, CHANGE);
  return true;
}

void task(void*) {
  bool     pressed    = false;
  uint32_t pressStart = 0;
  bool     longFired  = false;

  for (;;) {
    int level = digitalRead(BUTTON_PIN);

    if (level == HIGH && !pressed) {
      pressed    = true;
      longFired  = false;
      pressStart = millis();
    } else if (level == HIGH && pressed) {
      if (!longFired && (millis() - pressStart) >= BUTTON_LONG_HOLD_MS) {
        AppEvent e{ EVT_BUTTON_LONG, 0 };
        xQueueSend(g_eventQueue, &e, 0);
        longFired = true;
      }
    } else if (level == LOW && pressed) {
      uint32_t held = millis() - pressStart;
      pressed = false;
      if (!longFired && held <= BUTTON_SHORT_MAX_MS) {
        AppEvent e{ EVT_BUTTON_SHORT, 0 };
        xQueueSend(g_eventQueue, &e, 0);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

} // namespace Button
