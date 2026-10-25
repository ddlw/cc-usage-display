#pragma once

#include <Arduino.h>

#include <lvgl.h>
#include <stdint.h>

// Shared look & feel helpers for the launcher and the built-in apps.
namespace kit {

constexpr uint32_t kBg = 0x030706;
constexpr uint32_t kPanel = 0x0B1210;
constexpr uint32_t kPanelAlt = 0x11201B;
constexpr uint32_t kTrack = 0x1A231E;
constexpr uint32_t kGreen = 0xA8FF4F;
constexpr uint32_t kWhite = 0xF4F7ED;
constexpr uint32_t kMuted = 0x718079;
constexpr uint32_t kDivider = 0x1E2A25;
constexpr uint32_t kAmber = 0xFFB02E;
constexpr uint32_t kRed = 0xFF5F57;
constexpr uint32_t kBlue = 0x55E6D2;
constexpr uint32_t kViolet = 0xA5B4FC;
constexpr uint32_t kInk = 0x07110D;

inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font,
                uint32_t color);
lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h,
                uint32_t bg = kPanel, int radius = 10);
// Card with a caption, a big value and an optional note.
lv_obj_t* card(lv_obj_t* parent, int x, int y, int w, int h, const char* caption,
               uint32_t caption_color, lv_obj_t** value_out,
               const lv_font_t* value_font = nullptr, const char* note = nullptr);
// Flat button with a centered label, returns the button.
lv_obj_t* button(lv_obj_t* parent, int x, int y, int w, int h, const char* text,
                 uint32_t bg, uint32_t fg, lv_event_cb_t cb, void* user_data);
lv_obj_t* textButton(lv_obj_t* parent, int x, int y, int w, int h,
                     const char* text, uint32_t fg, lv_event_cb_t cb,
                     void* user_data);
void sectionTitle(lv_obj_t* parent, int x, int y, const char* text);

}  // namespace kit