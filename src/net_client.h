#pragma once

#include <Arduino.h>

#include "app_state.h"

struct NetActionResult {
  char message[49];
};

bool netBegin();
bool netOnline();
void netReconnect();
void netRequestRefresh();
void netRequestOpenDashboard();
bool netTakeState(AppState& state);
bool netTakeActionResult(NetActionResult& result);
