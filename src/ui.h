#pragma once

#include <Arduino.h>

#include <lvgl.h>

#include "app_state.h"

enum class UiAction : uint8_t {
  kOpenDashboard,
  kRefresh,
  kReconnect,
  kGoHome,
};

using UiActionHandler = void (*)(UiAction action);

// Builds the CC usage dashboard inside |parent| (480x480), which stays owned by
// the caller. The toast overlay is attached to the active screen so it remains
// visible while the dashboard is hidden.
void uiCreate(lv_obj_t* parent, UiActionHandler action_handler);
void uiSetVisible(bool visible);
lv_obj_t* uiRoot();

void uiUpdate(const AppState& state, uint32_t elapsed_seconds);
void uiUpdateBattery(int percent, bool charging);
void uiCyclePage();
void uiShowQuickActions();
void uiHideQuickActions();
void uiShowToast(const char* text);
bool uiModalVisible();