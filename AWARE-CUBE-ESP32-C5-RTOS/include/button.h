// button.h — GPIO24 (active HIGH), ISR + Debounce, short/long/10s-hold

#ifndef BUTTON_H
#define BUTTON_H

#include "app_state.h"

namespace Button {
  bool begin();              // Pin-Mode + ISR attach
  void task(void* arg);      // debounce + Event-Klassifikation -> g_eventQueue
}

#endif // BUTTON_H
