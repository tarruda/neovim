// Bridge used for communication between a builtin UI thread and nvim core
#ifndef NVIM_UI_BRIDGE_H
#define NVIM_UI_BRIDGE_H

#include <uv.h>

#include "nvim/ui.h"
#include "nvim/event/defs.h"

typedef struct ui_bridge_data UIBridgeData;
typedef void(*ui_main_fn)(UIBridgeData *bridge, UI *ui);
struct ui_bridge_data {
  UI bridge;  // actual UI passed to ui_attach
  UI *ui;     // UI pointer that will have it's callback called in
              // another thread
  event_scheduler scheduler;
  uv_thread_t ui_thread;
  ui_main_fn ui_main;
  uv_mutex_t mutex;
  uv_cond_t cond;
};


#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "ui_bridge.h.generated.h"
#endif
#endif  // NVIM_UI_BRIDGE_H
