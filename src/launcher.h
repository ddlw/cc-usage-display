#pragma once

#include <Arduino.h>

#include <lvgl.h>
#include <stdint.h>

#include "apps.h"

// Home screen + mini app switching.
void launcherCreate();
void launcherShowHome();
void launcherOpenApp(int index);
void launcherCloseApp();
bool launcherAtHome();
int launcherCurrentApp();

void launcherTick(uint32_t now_ms);
void launcherKey(AppKey key, uint32_t held_ms);