#pragma once

#include <Arduino.h>

#include <lvgl.h>
#include <stdint.h>

enum class AppKey : uint8_t { kLeft, kMiddle, kRight };

// A built-in mini app. |root| is the 480x416 content container provided by the
// launcher; |tick| and |key| are only called while the app is on screen.
struct AppEntry {
  const char* id;
  const char* title;
  const char* monogram;
  const char* subtitle;
  uint32_t accent;
  void (*build)(lv_obj_t* root);
  void (*tick)(lv_obj_t* root, uint32_t now_ms);
  void (*key)(lv_obj_t* root, AppKey key, uint32_t held_ms);
};

const AppEntry* appTable();
int appCount();

// Indices of apps handled directly by the launcher instead of a page builder.
constexpr int kAppXiaozhi = 0;
constexpr int kAppDashboard = 1;
