#include <Arduino.h>

#include "apps.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <WiFi.h>

#include "app_state.h"
#include "hw_imu.h"
#include "hw_rtc.h"
#include "net_client.h"
#include "sys.h"
#include "timeutil.h"
#include "ui_kit.h"
#include "ui.h"

namespace {

using kit::c;
using kit::label;
using kit::panel;

constexpr int kWidth = 480;
constexpr int kHeight = 416;

// ---------------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------------

lv_obj_t* clock_time_label = nullptr;
lv_obj_t* clock_date_label = nullptr;
lv_obj_t* clock_zone_label = nullptr;
lv_obj_t* clock_source_label = nullptr;
lv_obj_t* clock_arc = nullptr;
lv_obj_t* clock_arc_label = nullptr;
bool clock_use_24h = true;
uint64_t clock_last_render = 0;

void clockRender() {
  const uint64_t now = sys::nowUnix();
  if (!sys::timeValid()) {
    clock_last_render = 0;
    lv_label_set_text(clock_time_label, "--:--");
    lv_label_set_text(clock_date_label, "NO TIME SYNC");
    lv_label_set_text(clock_source_label, "NO SYNC");
    lv_obj_set_style_text_color(clock_source_label, c(kit::kAmber), LV_PART_MAIN);
    lv_arc_set_value(clock_arc, 0);
    lv_label_set_text(clock_arc_label, "--");
    return;
  }
  clock_last_render = now;  timeutil::DateTime local;
  timeutil::fromUnixWithOffset(
      now, static_cast<int16_t>(sys::localOffsetMinutes()), local);

  if (clock_use_24h) {
    lv_label_set_text_fmt(clock_time_label, "%02u:%02u", local.hour, local.minute);
  } else {
    unsigned hour = local.hour % 12u;
    if (hour == 0) {
      hour = 12;
    }
    lv_label_set_text_fmt(clock_time_label, "%02u:%02u %s", hour, local.minute,
                          local.hour < 12u ? "AM" : "PM");
  }
  lv_label_set_text_fmt(clock_date_label, "%s %02u %s %d",
                        timeutil::weekdayShort(local.weekday), local.day,
                        timeutil::monthShort(local.month), local.year);
  lv_label_set_text_fmt(clock_zone_label, "UTC%+d:%02d",
                        sys::localOffsetMinutes() / 60,
                        abs(static_cast<int>(sys::localOffsetMinutes()) % 60));
  lv_arc_set_value(clock_arc, static_cast<int16_t>(local.second));
  lv_label_set_text_fmt(clock_arc_label, "%02u", local.second);
}

void clockBuild(lv_obj_t* root) {
  lv_obj_t* hero = panel(root, 0, 0, kWidth, 152);
  clock_time_label = label(hero, "--:--", &lv_font_montserrat_48, kit::kGreen);
  lv_obj_align(clock_time_label, LV_ALIGN_TOP_MID, 0, 16);
  clock_date_label = label(hero, "---", &lv_font_montserrat_18, kit::kWhite);
  lv_obj_align(clock_date_label, LV_ALIGN_TOP_MID, 0, 82);
  lv_obj_t* hint = label(hero, "RTC BACKED  -  RIGHT KEY: 12/24H", &lv_font_montserrat_12,
                         kit::kMuted);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);

  lv_obj_t* seconds = panel(root, 0, 164, 232, 252);
  kit::sectionTitle(seconds, 14, 12, "SECONDS");
  clock_arc = lv_arc_create(seconds);
  lv_obj_set_pos(clock_arc, 26, 40);
  lv_obj_set_size(clock_arc, 180, 180);
  lv_arc_set_rotation(clock_arc, 270);
  lv_arc_set_bg_angles(clock_arc, 0, 360);
  lv_arc_set_range(clock_arc, 0, 59);
  lv_arc_set_value(clock_arc, 0);
  lv_obj_remove_style(clock_arc, nullptr, LV_PART_KNOB);
  lv_obj_set_style_bg_opa(clock_arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_width(clock_arc, 12, LV_PART_MAIN);
  lv_obj_set_style_arc_width(clock_arc, 12, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(clock_arc, c(kit::kDivider), LV_PART_MAIN);
  lv_obj_set_style_arc_color(clock_arc, c(kit::kGreen), LV_PART_INDICATOR);
  clock_arc_label = label(clock_arc, "00", &lv_font_montserrat_32, kit::kWhite);
  lv_obj_center(clock_arc_label);
  lv_obj_add_flag(clock_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(clock_arc, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* source = panel(root, 248, 164, 232, 120);
  kit::sectionTitle(source, 14, 12, "TIME SOURCE");
  clock_source_label = label(source, "--", &lv_font_montserrat_26, kit::kBlue);
  lv_obj_set_pos(clock_source_label, 14, 34);
  lv_obj_t* source_note = label(source, "NETWORK + PCF85063", &lv_font_montserrat_12,
                                kit::kMuted);
  lv_obj_set_pos(source_note, 14, 74);
  lv_obj_t* zone = panel(root, 248, 296, 232, 120);
  kit::sectionTitle(zone, 14, 12, "LOCAL ZONE");
  clock_zone_label = label(zone, "--", &lv_font_montserrat_26, kit::kViolet);
  lv_obj_set_pos(clock_zone_label, 14, 34);
  lv_obj_t* zone_note = label(zone, "FROM COMPANION", &lv_font_montserrat_12,
                              kit::kMuted);
  lv_obj_set_pos(zone_note, 14, 74);

  clockRender();
}

void clockTick(lv_obj_t*, uint32_t) {
  const uint64_t now = sys::nowUnix();
  if (now == clock_last_render) {
    return;
  }
  clockRender();
  if (sys::timeValid()) {
    lv_label_set_text(clock_source_label, sys::rtcReady() ? "RTC+NET" : "NETWORK");
    lv_obj_set_style_text_color(clock_source_label, c(kit::kBlue), LV_PART_MAIN);
  } else {
    lv_label_set_text(clock_source_label, "NO SYNC");
    lv_obj_set_style_text_color(clock_source_label, c(kit::kAmber), LV_PART_MAIN);
  }
}

void clockKey(lv_obj_t*, AppKey key, uint32_t) {
  if (key == AppKey::kRight || key == AppKey::kMiddle) {
    clock_use_24h = !clock_use_24h;
    clock_last_render = 0;
    clockTick(nullptr, 0);
  }
}

// ---------------------------------------------------------------------------
// Calendar
// ---------------------------------------------------------------------------

lv_obj_t* cal_root = nullptr;
lv_obj_t* cal_month_label = nullptr;
lv_obj_t* cal_days = nullptr;
int cal_year = 1970;
unsigned cal_month = 1;
bool cal_started = false;

void calendarFill() {
  if (cal_days == nullptr) {
    return;
  }
  lv_obj_clean(cal_days);
  const int first_weekday =
      static_cast<int>((timeutil::daysFromCivil(cal_year, cal_month, 1) + 4) % 7);
  const int total = timeutil::daysInMonth(cal_year, cal_month);

  timeutil::DateTime today;
  const bool have_today = sys::timeValid();
  if (have_today) {
    timeutil::fromUnixWithOffset(sys::nowUnix(), static_cast<int16_t>(sys::localOffsetMinutes()),
                                 today);
  }

  int cell = 0;
  for (int slot = 0; slot < 42; ++slot) {
    const int day = slot - first_weekday + 1;
    if (day < 1 || day > total) {
      continue;
    }
    const int col = slot % 7;
    const int row = slot / 7;
    const int x = 4 + col * 62;
    const int y = row * 46;
    const bool is_today = have_today && today.year == cal_year &&
                          today.month == cal_month &&
                          static_cast<int>(today.day) == day;
    if (is_today) {
      lv_obj_t* mark = panel(cal_days, x, y, 56, 40, kit::kGreen, 8);
      lv_obj_t* text = label(mark, "", &lv_font_montserrat_18, kit::kInk);
      lv_label_set_text_fmt(text, "%d", day);
      lv_obj_center(text);
    } else {
      lv_obj_t* text = label(cal_days, "", &lv_font_montserrat_18, kit::kWhite);
      lv_label_set_text_fmt(text, "%d", day);
      lv_obj_set_pos(text, x + 20, y + 8);
    }
    ++cell;
  }
  (void)cell;
  if (cal_month_label != nullptr) {
    lv_label_set_text_fmt(cal_month_label, "%s %d", timeutil::monthShort(cal_month),
                          cal_year);
  }
}

void calendarPrev(lv_event_t*) {
  if (cal_month == 1) {
    cal_month = 12;
    --cal_year;
  } else {
    --cal_month;
  }
  calendarFill();
}

void calendarNext(lv_event_t*) {
  if (cal_month == 12) {
    cal_month = 1;
    ++cal_year;
  } else {
    ++cal_month;
  }
  calendarFill();
}

void calendarBuild(lv_obj_t* root) {
  cal_root = root;
  if (!cal_started) {
    cal_started = true;
    timeutil::DateTime local;
    if (sys::timeValid()) {
      timeutil::fromUnixWithOffset(sys::nowUnix(),
                                   static_cast<int16_t>(sys::localOffsetMinutes()), local);
    } else {
      timeutil::toUtc(0, local);
    }
    cal_year = local.year;
    cal_month = local.month;
  }

  lv_obj_t* head = panel(root, 0, 0, kWidth, 56);
  kit::button(head, 8, 10, 40, 36, LV_SYMBOL_LEFT, kit::kPanelAlt, kit::kGreen,
              calendarPrev, nullptr);
  kit::button(head, kWidth - 48, 10, 40, 36, LV_SYMBOL_RIGHT, kit::kPanelAlt,
              kit::kGreen, calendarNext, nullptr);
  cal_month_label = label(head, "----", &lv_font_montserrat_18, kit::kWhite);
  lv_obj_align(cal_month_label, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* week = panel(root, 0, 64, kWidth, 30, kit::kBg);
  static const char* kDays[7] = {"S", "M", "T", "W", "T", "F", "S"};
  for (int i = 0; i < 7; ++i) {
    lv_obj_t* text = label(week, kDays[i], &lv_font_montserrat_14,
                           i == 0 || i == 6 ? kit::kAmber : kit::kMuted);
    lv_obj_set_pos(text, 4 + i * 62 + 24, 4);
  }

  cal_days = panel(root, 0, 98, kWidth, 288, kit::kPanel);
  calendarFill();
  lv_obj_t* hint = label(root, "TAP ARROWS OR RIGHT KEY: NEXT MONTH",
                         &lv_font_montserrat_12, kit::kMuted);
  lv_obj_set_pos(hint, 14, 394);
}

void calendarTick(lv_obj_t*, uint32_t) {}

void calendarKey(lv_obj_t*, AppKey key, uint32_t) {
  if (key == AppKey::kRight) {
    calendarNext(nullptr);
  } else if (key == AppKey::kMiddle) {
    calendarPrev(nullptr);
  }
}

// ---------------------------------------------------------------------------
// Level (bubble) using the QMI8658 accelerometer
// ---------------------------------------------------------------------------

lv_obj_t* level_bubble = nullptr;
lv_obj_t* level_pitch_label = nullptr;
lv_obj_t* level_roll_label = nullptr;
lv_obj_t* level_tilt_label = nullptr;
float level_offset_x = 0.0f;
float level_offset_y = 0.0f;
bool level_tare_pending = false;

void levelTick(lv_obj_t*, uint32_t) {
  if (level_bubble == nullptr) {
    return;
  }
  int16_t ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
  if (!imu::ready() || !imu::readRaw(ax, ay, az, gx, gy, gz)) {
    lv_label_set_text(level_tilt_label, "IMU OFFLINE");
    lv_obj_set_style_text_color(level_tilt_label, c(kit::kRed), LV_PART_MAIN);
    return;
  }
  const float scale = imu::accelCountsPerG();
  const float fx = ax / scale;
  const float fy = ay / scale;
  const float fz = az / scale;

  if (level_tare_pending) {
    level_tare_pending = false;
    level_offset_x = fx;
    level_offset_y = fy;
  }

  const float dx = fx - level_offset_x;
  const float dy = fy - level_offset_y;
  const float roll = atan2f(dx, fz) * 57.29578f;
  const float pitch = atan2f(dy, fz) * 57.29578f;

  const float limit = 110.0f;
  float px = dx * 110.0f * 2.2f;
  float py = dy * 110.0f * 2.2f;
  if (px > limit) px = limit;
  if (px < -limit) px = -limit;
  if (py > limit) py = limit;
  if (py < -limit) py = -limit;
  lv_obj_set_pos(level_bubble, static_cast<int>(150.0f + px) - 22,
                 static_cast<int>(150.0f + py) - 22);

  lv_label_set_text_fmt(level_pitch_label, "%+.1f", pitch);
  lv_label_set_text_fmt(level_roll_label, "%+.1f", roll);
  const float total_tilt = sqrtf(dx * dx + dy * dy);
  if (total_tilt < 0.03f) {
    lv_label_set_text(level_tilt_label, "LEVEL");
    lv_obj_set_style_text_color(level_tilt_label, c(kit::kGreen), LV_PART_MAIN);
  } else if (total_tilt < 0.25f) {
    lv_label_set_text(level_tilt_label, "NEAR LEVEL");
    lv_obj_set_style_text_color(level_tilt_label, c(kit::kBlue), LV_PART_MAIN);
  } else {
    lv_label_set_text(level_tilt_label, "TILTED");
    lv_obj_set_style_text_color(level_tilt_label, c(kit::kAmber), LV_PART_MAIN);
  }
}

void levelBuild(lv_obj_t* root) {
  lv_obj_t* dial = panel(root, 0, 0, 300, 300, kit::kPanelAlt, LV_RADIUS_CIRCLE);
  lv_obj_t* ring = panel(dial, 20, 20, 260, 260, kit::kPanel, LV_RADIUS_CIRCLE);
  (void)ring;
  lv_obj_t* hline = panel(dial, 20, 149, 260, 2, kit::kDivider, 0);
  lv_obj_t* vline = panel(dial, 149, 20, 2, 260, kit::kDivider, 0);
  (void)hline;
  (void)vline;
  lv_obj_t* inner = panel(dial, 105, 105, 90, 90, kit::kBg, LV_RADIUS_CIRCLE);
  (void)inner;
  level_bubble = lv_obj_create(dial);
  lv_obj_set_size(level_bubble, 44, 44);
  lv_obj_set_style_radius(level_bubble, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(level_bubble, c(kit::kGreen), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(level_bubble, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(level_bubble, 0, LV_PART_MAIN);
  lv_obj_set_pos(level_bubble, 128, 128);

  lv_obj_t* pitch_card = panel(root, 312, 0, 168, 92);
  kit::sectionTitle(pitch_card, 14, 10, "PITCH deg");
  level_pitch_label = label(pitch_card, "--", &lv_font_montserrat_26, kit::kWhite);
  lv_obj_set_pos(level_pitch_label, 14, 32);

  lv_obj_t* roll_card = panel(root, 312, 104, 168, 92);
  kit::sectionTitle(roll_card, 14, 10, "ROLL deg");
  level_roll_label = label(roll_card, "--", &lv_font_montserrat_26, kit::kWhite);
  lv_obj_set_pos(level_roll_label, 14, 32);

  lv_obj_t* state_card = panel(root, 312, 208, 168, 92);
  kit::sectionTitle(state_card, 14, 10, "STATE");
  level_tilt_label = label(state_card, "IMU?", &lv_font_montserrat_18, kit::kAmber);
  lv_obj_set_pos(level_tilt_label, 14, 36);

  lv_obj_t* hint = label(root, "RIGHT KEY: RE-TARE", &lv_font_montserrat_12, kit::kMuted);
  lv_obj_set_pos(hint, 14, 310);
  if (!imu::ready()) {
    lv_obj_t* warn = label(root, "QMI8658 NOT DETECTED", &lv_font_montserrat_18,
                           kit::kRed);
    lv_obj_set_pos(warn, 14, 336);
  }
}

void levelKey(lv_obj_t*, AppKey key, uint32_t) {
  if (key == AppKey::kRight || key == AppKey::kMiddle) {
    level_tare_pending = true;
  }
}

// ---------------------------------------------------------------------------
// Motion / attitude
// ---------------------------------------------------------------------------

lv_obj_t* motion_values[6] = {};
lv_obj_t* motion_heading_label = nullptr;
lv_obj_t* motion_orient_label = nullptr;
lv_obj_t* motion_mag_label = nullptr;
uint32_t motion_last_ms = 0;
float motion_yaw = 0.0f;

void motionTick(lv_obj_t*, uint32_t now) {
  if (motion_heading_label == nullptr) {
    return;
  }
  const uint32_t delta = now - motion_last_ms;
  if (delta < 80) {
    return;
  }
  motion_last_ms = now;
  int16_t ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
  if (!imu::ready() || !imu::readRaw(ax, ay, az, gx, gy, gz)) {
    lv_label_set_text(motion_orient_label, "IMU OFFLINE");
    lv_obj_set_style_text_color(motion_orient_label, c(kit::kRed), LV_PART_MAIN);
    return;
  }
  const float a_scale = imu::accelCountsPerG();
  const float g_scale = imu::gyroCountsPerDps();
  const float values[6] = {ax / a_scale, ay / a_scale, az / a_scale,
                           gx / g_scale, gy / g_scale, gz / g_scale};
  for (int i = 0; i < 6; ++i) {
    lv_label_set_text_fmt(motion_values[i], "%+.2f", values[i]);
  }
  const float mag = sqrtf(values[0] * values[0] + values[1] * values[1] +
                          values[2] * values[2]);
  lv_label_set_text_fmt(motion_mag_label, "%.2f g", mag);

  motion_yaw += values[5] * (delta / 1000.0f);
  if (motion_yaw > 180.0f) motion_yaw -= 360.0f;
  if (motion_yaw < -180.0f) motion_yaw += 360.0f;
  const float heading = motion_yaw < 0 ? motion_yaw + 360.0f : motion_yaw;
  lv_label_set_text_fmt(motion_heading_label, "%03d deg", static_cast<int>(heading));

  const float nx = values[0];
  const float ny = values[1];
  const float nz = values[2];
  const char* orientation = "MOVING";
  if (nz > 0.75f) {
    orientation = "FACE UP";
  } else if (nz < -0.75f) {
    orientation = "FACE DOWN";
  } else if (nx > 0.75f) {
    orientation = "LANDSCAPE L";
  } else if (nx < -0.75f) {
    orientation = "LANDSCAPE R";
  } else if (ny > 0.75f) {
    orientation = "PORTRAIT 1";
  } else if (ny < -0.75f) {
    orientation = "PORTRAIT 2";
  }
  lv_label_set_text(motion_orient_label, orientation);
  lv_obj_set_style_text_color(motion_orient_label, c(kit::kGreen), LV_PART_MAIN);
}

void motionBuild(lv_obj_t* root) {
  static const char* kNames[6] = {"ACCEL X", "ACCEL Y", "ACCEL Z",
                                  "GYRO X",  "GYRO Y",  "GYRO Z"};
  static const uint32_t kColors[6] = {kit::kGreen, kit::kGreen, kit::kGreen,
                                      kit::kBlue,  kit::kBlue,  kit::kBlue};
  for (int i = 0; i < 6; ++i) {
    const int col = i % 3;
    const int row = i / 3;
    const int x = col * 160 + 2;
    const int y = row * 100;
    lv_obj_t* box = panel(root, x, y, 154, 92);
    kit::sectionTitle(box, 12, 10, kNames[i]);
    motion_values[i] = label(box, "--", &lv_font_montserrat_24, kColors[i]);
    lv_obj_set_pos(motion_values[i], 12, 32);
  }

  lv_obj_t* heading = panel(root, 2, 204, 235, 100);
  kit::sectionTitle(heading, 14, 10, "GYRO HEADING");
  motion_heading_label = label(heading, "000 deg", &lv_font_montserrat_32,
                               kit::kViolet);
  lv_obj_set_pos(motion_heading_label, 14, 34);
  lv_obj_t* note = label(heading, "RELATIVE, DRIFTS", &lv_font_montserrat_12,
                         kit::kMuted);
  lv_obj_set_pos(note, 14, 76);

  lv_obj_t* orient = panel(root, 245, 204, 233, 100);
  kit::sectionTitle(orient, 14, 10, "ORIENTATION");
  motion_orient_label = label(orient, "---", &lv_font_montserrat_24, kit::kWhite);
  lv_obj_set_pos(motion_orient_label, 14, 34);
  motion_mag_label = label(orient, "-- g", &lv_font_montserrat_14, kit::kMuted);
  lv_obj_set_pos(motion_mag_label, 14, 70);

  lv_obj_t* hint = label(root, "RIGHT KEY: RESET HEADING", &lv_font_montserrat_12,
                         kit::kMuted);
  lv_obj_set_pos(hint, 14, 314);
  if (!imu::ready()) {
    lv_obj_t* warn = label(root, "QMI8658 NOT DETECTED", &lv_font_montserrat_18,
                           kit::kRed);
    lv_obj_set_pos(warn, 14, 340);
  }
}

void motionKey(lv_obj_t*, AppKey key, uint32_t) {
  if (key == AppKey::kRight || key == AppKey::kMiddle) {
    motion_yaw = 0.0f;
  }
}
// ---------------------------------------------------------------------------
// Gallery - procedural art, no image assets needed
// ---------------------------------------------------------------------------

lv_obj_t* gallery_art = nullptr;
lv_obj_t* gallery_name = nullptr;
lv_obj_t* gallery_hint = nullptr;
int gallery_style = 0;
uint32_t gallery_seed = 0x1234567u;

constexpr int kArtWidth = 480;
constexpr int kArtHeight = 344;

uint32_t galleryRand() {
  gallery_seed = gallery_seed * 1664525u + 1013904223u;
  return gallery_seed >> 8;
}

int galleryRandRange(int lo, int hi) {
  if (hi <= lo) {
    return lo;
  }
  return lo + static_cast<int>(galleryRand() % static_cast<uint32_t>(hi - lo + 1));
}

lv_obj_t* galleryRect(int x, int y, int w, int h, lv_color_t color, int radius) {
  lv_obj_t* obj = panel(gallery_art, x, y, w, h, kit::kPanel, radius);
  lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
  return obj;
}

const char* kGalleryStyles[] = {"SPECTRUM", "SUNSET", "CHECKER", "BULLSEYE",
                                "STARFIELD", "TRICOLOR", "NEON GRID", "PASTEL"};
constexpr int kGalleryStyleCount = 8;

void galleryBuildArt() {
  if (gallery_art == nullptr) {
    return;
  }
  lv_obj_clean(gallery_art);
  switch (gallery_style) {
    case 0: {
      for (int i = 0; i < 30; ++i) {
        galleryRect(i * 10, 0, 10, kArtHeight,
                    lv_color_hsv_to_rgb(static_cast<uint16_t>(i * 360 / 30), 88, 96), 0);
      }
      break;
    }
    case 1: {
      for (int i = 0; i < 20; ++i) {
        const lv_color_t color = lv_color_mix(lv_color_hex(0xFF9A3C),
                                              lv_color_hex(0x141B4D),
                                              static_cast<uint8_t>(i * 255 / 19));
        galleryRect(0, i * 17, kArtWidth, 17, color, 0);
      }
      break;
    }
    case 2: {
      for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 5; ++col) {
          const uint16_t hue = static_cast<uint16_t>((row * 5 + col) * 360 / 15);
          galleryRect(col * 80, row * 86, 80, 86,
                      lv_color_hsv_to_rgb(hue, 70, (row + col) % 2 == 0 ? 92 : 55), 0);
        }
      }
      break;
    }
    case 3: {
      for (int i = 0; i < 12; ++i) {
        const int inset = i * 14;
        galleryRect(inset, inset, kArtWidth - inset * 2, kArtHeight - inset * 2,
                    lv_color_hsv_to_rgb(static_cast<uint16_t>(i * 20), 80,
                                        i % 2 == 0 ? 95 : 40),
                    0);
      }
      break;
    }
    case 4: {
      galleryRect(0, 0, kArtWidth, kArtHeight, lv_color_hex(0x05070D), 0);
      gallery_seed = 0x9E3779B9u;
      for (int i = 0; i < 70; ++i) {
        const int x = galleryRandRange(2, kArtWidth - 8);
        const int y = galleryRandRange(2, kArtHeight - 8);
        const int size = galleryRandRange(2, 5);
        galleryRect(x, y, size, size,
                    lv_color_hsv_to_rgb(static_cast<uint16_t>(galleryRandRange(0, 359)), 55,
                                        galleryRandRange(60, 100)),
                    LV_RADIUS_CIRCLE);
      }
      break;
    }
    case 5: {
      galleryRect(0, 0, 160, kArtHeight, lv_color_hex(0x27C6D8), 0);
      galleryRect(160, 0, 160, kArtHeight, lv_color_hex(0x3C6BFF), 0);
      galleryRect(320, 0, 160, kArtHeight, lv_color_hex(0x8A5CF6), 0);
      galleryRect(60, 60, 360, kArtHeight - 120, lv_color_hex(0x0B1210), 16);
      galleryRect(90, 90, 300, kArtHeight - 180, lv_color_hex(0xF4F7ED), 12);
      break;
    }
    case 6: {
      galleryRect(0, 0, kArtWidth, kArtHeight, lv_color_hex(0x04070A), 0);
      for (int i = 0; i < 12; ++i) {
        galleryRect(i * 40, 0, 2, kArtHeight,
                    lv_color_hsv_to_rgb(static_cast<uint16_t>(i * 30), 85, 90), 0);
      }
      for (int i = 0; i < 9; ++i) {
        galleryRect(0, i * 40, kArtWidth, 2,
                    lv_color_hsv_to_rgb(static_cast<uint16_t>(180 + i * 20), 85, 90), 0);
      }
      break;
    }
    default: {
      for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 5; ++col) {
          const uint16_t hue = static_cast<uint16_t>((row * 5 + col) * 360 / 15);
          galleryRect(col * 96 + 8, row * 112 + 10, 80, 92,
                      lv_color_hsv_to_rgb(hue, 42, 96), 14);
        }
      }
      break;
    }
  }
  if (gallery_name != nullptr) {
    lv_label_set_text(gallery_name, kGalleryStyles[gallery_style]);
  }
  if (gallery_hint != nullptr) {
    lv_label_set_text_fmt(gallery_hint, "STYLE %d/%d  -  TAP OR RIGHT KEY: NEXT",
                          gallery_style + 1, kGalleryStyleCount);
  }
}

void galleryBuild(lv_obj_t* root) {
  gallery_name = label(root, kGalleryStyles[gallery_style], &lv_font_montserrat_18,
                       kit::kViolet);
  lv_obj_set_pos(gallery_name, 14, 6);
  gallery_art = panel(root, 0, 36, kArtWidth, kArtHeight, kit::kPanel, 10);
  lv_obj_add_flag(gallery_art, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(
      gallery_art,
      [](lv_event_t*) {
        gallery_style = (gallery_style + 1) % kGalleryStyleCount;
        galleryBuildArt();
      },
      LV_EVENT_CLICKED, nullptr);
  lv_obj_t* note =
      label(root, "", &lv_font_montserrat_12, kit::kMuted);
  gallery_hint = note;
  lv_obj_set_pos(note, 14, 388);
  galleryBuildArt();
}

void galleryTick(lv_obj_t*, uint32_t) {}

void galleryKey(lv_obj_t*, AppKey key, uint32_t) {
  if (key == AppKey::kRight || key == AppKey::kMiddle) {
    gallery_style = (gallery_style + 1) % kGalleryStyleCount;
    galleryBuildArt();
  }
}

// ---------------------------------------------------------------------------
// Stopwatch
// ---------------------------------------------------------------------------

lv_obj_t* sw_time = nullptr;
lv_obj_t* sw_state = nullptr;
lv_obj_t* sw_lap_labels[3] = {};
lv_obj_t* sw_main_button = nullptr;
lv_obj_t* sw_main_label = nullptr;
bool sw_running = false;
uint32_t sw_accumulated = 0;
uint32_t sw_started_at = 0;
uint32_t sw_laps[3] = {};
int sw_lap_count = 0;

uint32_t swElapsed() {
  if (!sw_running) {
    return sw_accumulated;
  }
  return sw_accumulated + (millis() - sw_started_at);
}

void swRender() {
  if (sw_time == nullptr) {
    return;
  }
  const uint32_t ms = swElapsed();
  lv_label_set_text_fmt(sw_time, "%02lu:%02lu.%02lu",
                        static_cast<unsigned long>(ms / 60000u),
                        static_cast<unsigned long>((ms / 1000u) % 60u),
                        static_cast<unsigned long>((ms / 10u) % 100u));
  lv_label_set_text(sw_state, sw_running ? "RUNNING" : (ms > 0 ? "PAUSED" : "READY"));
  lv_obj_set_style_text_color(sw_state,
                              c(sw_running ? kit::kGreen
                                           : (ms > 0 ? kit::kAmber : kit::kMuted)),
                              LV_PART_MAIN);
}

void swToggle() {
  if (sw_running) {
    sw_accumulated = swElapsed();
    sw_running = false;
  } else {
    sw_started_at = millis();
    sw_running = true;
  }
  if (sw_main_label != nullptr) {
    lv_label_set_text(sw_main_label, sw_running ? "STOP" : "START");
  }
  swRender();
}

void swLap() {
  if (!sw_running && swElapsed() == 0) {
    return;
  }
  for (int i = 2; i > 0; --i) {
    sw_laps[i] = sw_laps[i - 1];
  }
  sw_laps[0] = swElapsed();
  if (sw_lap_count < 3) {
    ++sw_lap_count;
  }
  for (int i = 0; i < 3; ++i) {
    if (sw_lap_labels[i] == nullptr) {
      continue;
    }
    if (i < sw_lap_count) {
      const uint32_t ms = sw_laps[i];
      lv_label_set_text_fmt(sw_lap_labels[i], "LAP %d   %02lu:%02lu.%02lu", i + 1,
                            static_cast<unsigned long>(ms / 60000u),
                            static_cast<unsigned long>((ms / 1000u) % 60u),
                            static_cast<unsigned long>((ms / 10u) % 100u));
      lv_obj_clear_flag(sw_lap_labels[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(sw_lap_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void swReset() {
  sw_running = false;
  sw_accumulated = 0;
  sw_lap_count = 0;
  for (int i = 0; i < 3; ++i) {
    sw_laps[i] = 0;
    if (sw_lap_labels[i] != nullptr) {
      lv_obj_add_flag(sw_lap_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (sw_main_label != nullptr) {
    lv_label_set_text(sw_main_label, "START");
  }
  swRender();
}

void swBuild(lv_obj_t* root) {
  lv_obj_t* hero = panel(root, 0, 0, kWidth, 150);
  sw_time = label(hero, "00:00.00", &lv_font_montserrat_48, kit::kWhite);
  lv_obj_align(sw_time, LV_ALIGN_TOP_MID, 0, 26);
  sw_state = label(hero, "READY", &lv_font_montserrat_14, kit::kMuted);
  lv_obj_align(sw_state, LV_ALIGN_BOTTOM_MID, 0, -14);

  for (int i = 0; i < 3; ++i) {
    sw_lap_labels[i] = label(root, "", &lv_font_montserrat_14, kit::kBlue);
    lv_obj_set_pos(sw_lap_labels[i], 24, 164 + i * 26);
    lv_obj_add_flag(sw_lap_labels[i], LV_OBJ_FLAG_HIDDEN);
  }

  sw_main_button = kit::button(root, 24, 254, 200, 62, "START", kit::kGreen,
                               kit::kInk, [](lv_event_t*) { swToggle(); }, nullptr);
  sw_main_label = lv_obj_get_child(sw_main_button, 0);
  kit::button(root, 240, 254, 100, 62, "LAP", kit::kPanelAlt, kit::kWhite,
              [](lv_event_t*) { swLap(); }, nullptr);
  kit::button(root, 356, 254, 100, 62, "RESET", kit::kPanelAlt, kit::kMuted,
              [](lv_event_t*) { swReset(); }, nullptr);
  kit::button(root, 24, 330, 432, 56, "RIGHT KEY: START / STOP", kit::kPanelAlt,
              kit::kMuted, nullptr, nullptr);
  swRender();
}

void swTick(lv_obj_t*, uint32_t now) {
  if (sw_time == nullptr) {
    return;
  }
  if (sw_running) {
    swRender();
  } else {
    static uint32_t last = 0;
    if (now - last > 500) {
      last = now;
      swRender();
    }
  }
}

void swKey(lv_obj_t*, AppKey key, uint32_t) {
  if (key == AppKey::kRight) {
    swToggle();
  } else if (key == AppKey::kMiddle) {
    swLap();
  }
}

// ---------------------------------------------------------------------------
// System self test
// ---------------------------------------------------------------------------

constexpr int kTestRows = 11;
lv_obj_t* test_name[kTestRows] = {};
lv_obj_t* test_value[kTestRows] = {};
lv_obj_t* test_dot[kTestRows] = {};
lv_obj_t* test_summary = nullptr;

struct TestOutcome {
  const char* name;
  bool ok;
  char detail[40];
};

void testRun() {
  TestOutcome rows[kTestRows] = {};
  int count = 0;
  auto add = [&](const char* name, bool ok, const char* detail) {
    if (count >= kTestRows) {
      return;
    }
    rows[count].name = name;
    rows[count].ok = ok;
    snprintf(rows[count].detail, sizeof(rows[count].detail), "%s", detail);
    ++count;
  };

  add("DISPLAY", true, "CO5300 480x480");

  Wire.beginTransmission(0x5A);
  const bool touch_ok = Wire.endTransmission() == 0;
  add("TOUCH", touch_ok, touch_ok ? "CST9xx ACK 0x5A" : "NO ACK 0x5A");

  add("PMU", sys::powerReady(), sys::powerReady() ? "AXP2101 OK" : "NO AXP2101");
  add("RTC", sys::rtcReady(), sys::rtcReady() ? "PCF85063 OK" : "NO PCF85063");

  char imu_detail[40] = {};
  if (imu::ready()) {
    snprintf(imu_detail, sizeof(imu_detail), "QMI8658 @0x%02X", imu::address());
  } else {
    snprintf(imu_detail, sizeof(imu_detail), "NO QMI8658");
  }
  add("IMU", imu::ready(), imu_detail);

  add("WIFI", sys::online(), sys::online() ? "LINK UP" : "DISCONNECTED");
  const bool linked = sys::state().data_valid;
  add("COMPANION", linked, linked ? "USAGE DATA OK" : "NO DATA YET");
  add("TIME", sys::timeValid(), sys::timeValid() ? "SYNCED" : "NOT SYNCED");

  char flash_detail[40] = {};
  const uint32_t flash_mb = ESP.getFlashChipSize() / (1024u * 1024u);
  snprintf(flash_detail, sizeof(flash_detail), "%lu MB NOR", static_cast<unsigned long>(flash_mb));
  add("FLASH", flash_mb >= 16, flash_detail);

  const esp_partition_t* ai_slot = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
  char slot_detail[40] = {};
  snprintf(slot_detail, sizeof(slot_detail), "OTA1 %s  RUN %s",
           ai_slot != nullptr ? "PRESENT" : "MISSING", sys::runningSlotLabel());
  add("AI SLOT", ai_slot != nullptr, slot_detail);

  char heap_detail[40] = {};
  const uint32_t heap = ESP.getFreeHeap() / 1024u;
  snprintf(heap_detail, sizeof(heap_detail), "%lu KB FREE", static_cast<unsigned long>(heap));
  add("HEAP", heap > 40, heap_detail);

  int failures = 0;
  for (int i = 0; i < kTestRows; ++i) {
    const bool present = i < count;
    if (test_name[i] != nullptr) {
      lv_label_set_text(test_name[i], present ? rows[i].name : "--");
    }
    if (test_value[i] != nullptr) {
      lv_label_set_text(test_value[i], present ? rows[i].detail : "");
    }
    if (test_dot[i] != nullptr) {
      const bool ok = present && rows[i].ok;
      lv_obj_set_style_bg_color(test_dot[i], c(present ? (ok ? kit::kGreen : kit::kRed)
                                                       : kit::kMuted),
                                LV_PART_MAIN);
    }
    if (present && !rows[i].ok) {
      ++failures;
    }
  }
  if (test_summary != nullptr) {
    if (failures == 0) {
      lv_label_set_text(test_summary, "ALL CHECKS PASSED");
      lv_obj_set_style_text_color(test_summary, c(kit::kGreen), LV_PART_MAIN);
    } else {
      lv_label_set_text_fmt(test_summary, "%d CHECK(S) FAILED", failures);
      lv_obj_set_style_text_color(test_summary, c(kit::kAmber), LV_PART_MAIN);
    }
  }
}

void testBuild(lv_obj_t* root) {
  for (int i = 0; i < kTestRows; ++i) {
    const int x = (i % 2) * 240 + 8;
    const int y = 6 + (i / 2) * 32;
    lv_obj_t* dot = lv_obj_create(root);
    lv_obj_set_pos(dot, x, y + 6);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot, c(kit::kMuted), LV_PART_MAIN);
    test_dot[i] = dot;
    test_name[i] = label(root, "--", &lv_font_montserrat_14, kit::kWhite);
    lv_obj_set_pos(test_name[i], x + 18, y);
    test_value[i] = label(root, "", &lv_font_montserrat_12, kit::kMuted);
    lv_obj_set_pos(test_value[i], x + 108, y + 2);
  }
  test_summary = label(root, "", &lv_font_montserrat_18, kit::kWhite);
  lv_obj_set_pos(test_summary, 12, 216);
  kit::button(root, 8, 250, 220, 60, "RUN AGAIN", kit::kPanelAlt, kit::kGreen,
              [](lv_event_t*) { testRun(); }, nullptr);
  kit::button(root, 244, 250, 228, 60, "BOOT AI SLOT", kit::kPanelAlt, kit::kViolet,
              [](lv_event_t*) { sys::bootSlot(1); }, nullptr);
  lv_obj_t* note = label(root, "AI SLOT = OTA_1 (XIAOZHI).  IN XIAOZHI, HOLD\n"
                               "THE LEFT BUTTON TO COME BACK TO THIS LAUNCHER.",
                         &lv_font_montserrat_12, kit::kMuted);
  lv_obj_set_pos(note, 12, 324);
  testRun();
}

void testTick(lv_obj_t*, uint32_t) {}

void testKey(lv_obj_t*, AppKey key, uint32_t) {
  if (key == AppKey::kRight) {
    testRun();
  }
}

// ---------------------------------------------------------------------------
// Power detail
// ---------------------------------------------------------------------------

lv_obj_t* power_cards[9] = {};
lv_obj_t* power_percent = nullptr;
lv_obj_t* power_state = nullptr;
lv_obj_t* power_meta = nullptr;

void powerRefresh() {
  if (power_cards[0] == nullptr) {
    return;
  }
  if (!sys::powerReady()) {
    lv_label_set_text(power_meta, "AXP2101 NOT RESPONDING");
    lv_obj_set_style_text_color(power_meta, c(kit::kRed), LV_PART_MAIN);
    return;
  }
  int percent = sys::batteryPercent();
  if (percent < 0) {
    percent = 0;
  }
  lv_label_set_text_fmt(power_percent, "%d%%", percent);
  lv_label_set_text(power_state, sys::charging() ? "CHARGING" : (sys::vbusPresent() ? "ADAPTER" : "BATTERY"));
  lv_obj_set_style_text_color(power_state,
                              c(sys::charging() ? kit::kGreen
                                                : (sys::vbusPresent() ? kit::kBlue : kit::kMuted)),
                              LV_PART_MAIN);

  lv_obj_t* values[9] = {power_cards[0], power_cards[1], power_cards[2],
                         power_cards[3], power_cards[4], power_cards[5],
                         power_cards[6], power_cards[7], power_cards[8]};
  lv_label_set_text_fmt(values[0], "%u mV", sys::batteryVoltageMv());
  lv_label_set_text_fmt(values[1], "%u mV", sys::vbusVoltageMv());
  lv_label_set_text_fmt(values[2], "%u mV", sys::systemVoltageMv());
  lv_label_set_text_fmt(values[3], "%d%%", sys::batteryPercent());
  lv_label_set_text_fmt(values[4], "%u mA", sys::vbusCurrentLimitMa());
  lv_label_set_text_fmt(values[5], "%.1f C", sys::pmuTemperatureC());
  lv_label_set_text_fmt(values[6], "%u mV", sys::buttonBatteryVoltageMv());
  lv_label_set_text_fmt(values[7], "%s",
                        sys::charging() ? "ENABLED"
                                        : (sys::vbusPresent() ? "VBUS IN" : "IDLE"));
  lv_label_set_text_fmt(values[8], "%lu s",
                        static_cast<unsigned long>(sys::uptimeSeconds()));
}

void powerBuild(lv_obj_t* root) {
  lv_obj_t* hero = panel(root, 0, 0, kWidth, 118);
  power_percent = label(hero, "--%", &lv_font_montserrat_48, kit::kGreen);
  lv_obj_set_pos(power_percent, 22, 20);
  power_state = label(hero, "---", &lv_font_montserrat_18, kit::kWhite);
  lv_obj_set_pos(power_state, 200, 32);
  power_meta = label(hero, "AXP2101", &lv_font_montserrat_12, kit::kMuted);
  lv_obj_set_pos(power_meta, 200, 62);

  static const char* kCaptions[9] = {"BATTERY", "VBUS",    "SYSTEM", "BAT LEVEL",
                                     "VBUS LIMIT", "PMU TEMP", "BTN BAT", "CHARGER",
                                     "UPTIME"};
  for (int i = 0; i < 9; ++i) {
    const int col = i % 3;
    const int row = i / 3;
    lv_obj_t* box = panel(root, col * 160 + 2, 130 + row * 96, 154, 88);
    kit::sectionTitle(box, 12, 9, kCaptions[i]);
    power_cards[i] = label(box, "--", &lv_font_montserrat_18, kit::kWhite);
    lv_obj_set_pos(power_cards[i], 12, 32);
  }
  powerRefresh();
}

void powerTick(lv_obj_t*, uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 1000) {
    return;
  }
  last = now;
  powerRefresh();
}

void powerKey(lv_obj_t*, AppKey key, uint32_t) {
  if (key == AppKey::kRight) {
    powerRefresh();
  }
}

// ---------------------------------------------------------------------------
// Network / companion status
// ---------------------------------------------------------------------------

lv_obj_t* net_status = nullptr;
lv_obj_t* net_cards[6] = {};

void netRefresh() {
  if (net_status == nullptr) {
    return;
  }
  const AppState& state = sys::state();
  if (sys::online()) {
    lv_label_set_text(net_status, "WIFI ONLINE");
    lv_obj_set_style_text_color(net_status, c(kit::kGreen), LV_PART_MAIN);
  } else {
    lv_label_set_text(net_status, "WIFI OFFLINE");
    lv_obj_set_style_text_color(net_status, c(kit::kRed), LV_PART_MAIN);
  }
  lv_label_set_text(net_cards[0], WiFi.SSID().c_str());
  lv_label_set_text(net_cards[1], sys::localIp().c_str());
  const int rssi = sys::wifiRssi();
  lv_label_set_text_fmt(net_cards[2], "%d dBm", rssi);
  lv_obj_set_style_text_color(net_cards[2],
                              c(rssi > -60 ? kit::kGreen
                                           : (rssi > -75 ? kit::kAmber : kit::kRed)),
                              LV_PART_MAIN);
  lv_label_set_text(net_cards[3], connectionLabel(state.connection));
  lv_label_set_text_fmt(net_cards[4], "%lu s", static_cast<unsigned long>(sys::stateAgeSeconds()));
  lv_label_set_text(net_cards[5], state.data_valid ? "VALID" : "NO DATA");
  lv_obj_set_style_text_color(net_cards[5],
                              c(state.data_valid ? kit::kGreen : kit::kAmber),
                              LV_PART_MAIN);
}

void netBuild(lv_obj_t* root) {
  lv_obj_t* hero = panel(root, 0, 0, kWidth, 78);
  net_status = label(hero, "---", &lv_font_montserrat_26, kit::kWhite);
  lv_obj_set_pos(net_status, 22, 24);

  static const char* kCaptions[6] = {"SSID", "IP ADDRESS", "SIGNAL",
                                     "COMPANION", "SYNC AGE", "DATA"};
  for (int i = 0; i < 6; ++i) {
    const int col = i % 2;
    const int row = i / 2;
    lv_obj_t* box = panel(root, col * 240 + 2, 88 + row * 88, 232, 80);
    kit::sectionTitle(box, 12, 9, kCaptions[i]);
    net_cards[i] = label(box, "--", &lv_font_montserrat_18, kit::kWhite);
    lv_obj_set_pos(net_cards[i], 12, 32);
  }
  kit::button(root, 8, 356, 228, 52, "RECONNECT WIFI", kit::kPanelAlt, kit::kGreen,
              [](lv_event_t*) {
                netReconnect();
                sys::toast("RECONNECT QUEUED");
              },
              nullptr);
  kit::button(root, 244, 356, 228, 52, "REFRESH USAGE", kit::kPanelAlt, kit::kBlue,
              [](lv_event_t*) {
                netRequestRefresh();
                sys::toast("REFRESH QUEUED");
              },
              nullptr);
  netRefresh();
}

void netTick(lv_obj_t*, uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 1000) {
    return;
  }
  last = now;
  netRefresh();
}

void netKey(lv_obj_t*, AppKey key, uint32_t) {
  if (key == AppKey::kRight) {
    netRefresh();
  }
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

lv_obj_t* set_brightness_label = nullptr;
lv_obj_t* set_timeout_label = nullptr;
lv_obj_t* set_clock_label = nullptr;
lv_obj_t* set_rtc_label = nullptr;
lv_obj_t* set_about_label = nullptr;
int set_timeout_index = 0;

void settingsRefresh() {
  if (set_brightness_label != nullptr) {
    lv_label_set_text_fmt(set_brightness_label, "%u%%",
                          static_cast<unsigned>(sys::brightness() * 100u / 255u));
  }
  if (set_timeout_label != nullptr) {
    const uint32_t ms = sys::screenTimeoutMs();
    if (ms == 0) {
      lv_label_set_text(set_timeout_label, "NEVER");
    } else {
      lv_label_set_text_fmt(set_timeout_label, "%lu s",
                            static_cast<unsigned long>(ms / 1000u));
    }
  }
  if (set_clock_label != nullptr) {
    if (sys::timeValid()) {
      timeutil::DateTime local;
      timeutil::fromUnixWithOffset(sys::nowUnix(),
                                   static_cast<int16_t>(sys::localOffsetMinutes()), local);
      lv_label_set_text_fmt(set_clock_label, "%04d-%02u-%02u %02u:%02u:%02u", local.year,
                            local.month, local.day, local.hour, local.minute, local.second);
    } else {
      lv_label_set_text(set_clock_label, "NOT SYNCED");
    }
  }
  if (set_rtc_label != nullptr) {
    uint64_t stored = 0;
    const bool ok = rtc::ready() && rtc::readUtc(stored) && rtc::isValid(stored);
    lv_label_set_text(set_rtc_label, ok ? "RTC STORED" : (rtc::ready() ? "RTC EMPTY" : "NO RTC"));
    lv_obj_set_style_text_color(set_rtc_label,
                                c(ok ? kit::kGreen : (rtc::ready() ? kit::kAmber : kit::kRed)),
                                LV_PART_MAIN);
  }
  if (set_about_label != nullptr) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    lv_label_set_text_fmt(
        set_about_label,
        "CHIP %s  CORES %d  REV %d\n"
        "FLASH %lu MB   HEAP %lu KB\n"
        "SLOT %s   UPTIME %lu s\n"
        "BUILD " __DATE__ " " __TIME__,
        ESP.getChipModel(), ESP.getChipCores(), ESP.getChipRevision(),
        static_cast<unsigned long>(ESP.getFlashChipSize() / (1024u * 1024u)),
        static_cast<unsigned long>(ESP.getFreeHeap() / 1024u),
        running != nullptr ? running->label : "?", 
        static_cast<unsigned long>(sys::uptimeSeconds()));
  }
}

void settingsBuild(lv_obj_t* root) {
  lv_obj_t* bright = panel(root, 0, 0, kWidth, 96);
  kit::sectionTitle(bright, 14, 10, "DISPLAY BRIGHTNESS");
  set_brightness_label = label(bright, "--%", &lv_font_montserrat_18, kit::kWhite);
  lv_obj_align(set_brightness_label, LV_ALIGN_TOP_RIGHT, -14, 9);
  lv_obj_t* slider = lv_slider_create(bright);
  lv_obj_set_pos(slider, 14, 52);
  lv_obj_set_size(slider, 452, 14);
  lv_slider_set_range(slider, 20, 255);
  lv_slider_set_value(slider, sys::brightness(), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, c(kit::kTrack), LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, c(kit::kGreen), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, c(kit::kGreen), LV_PART_KNOB);
  lv_obj_add_event_cb(
      slider,
      [](lv_event_t* event) {
        lv_obj_t* obj = lv_event_get_target(event);
        sys::setBrightness(static_cast<uint8_t>(lv_slider_get_value(obj)));
        settingsRefresh();
      },
      LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t* timeout = panel(root, 0, 104, 232, 96);
  kit::sectionTitle(timeout, 14, 10, "SCREEN TIMEOUT");
  set_timeout_label = label(timeout, "--", &lv_font_montserrat_18, kit::kWhite);
  lv_obj_set_pos(set_timeout_label, 14, 32);
  kit::button(timeout, 110, 30, 108, 40, "NEXT", kit::kPanelAlt, kit::kGreen,
              [](lv_event_t*) {
                const uint32_t count = sys::screenTimeoutOptions();
                set_timeout_index = (set_timeout_index + 1) % static_cast<int>(count);
                sys::setScreenTimeoutMs(sys::screenTimeoutAt(set_timeout_index));
                settingsRefresh();
              },
              nullptr);

  lv_obj_t* time_card = panel(root, 248, 104, 232, 96);
  kit::sectionTitle(time_card, 14, 10, "CLOCK");
  set_clock_label = label(time_card, "--", &lv_font_montserrat_14, kit::kWhite);
  lv_obj_set_pos(set_clock_label, 14, 28);
  set_rtc_label = label(time_card, "--", &lv_font_montserrat_12, kit::kMuted);
  lv_obj_set_pos(set_rtc_label, 14, 50);
  kit::button(time_card, 110, 68, 108, 24, "SYNC", kit::kPanelAlt, kit::kBlue,
              [](lv_event_t*) {
                if (sys::syncRtcFromTime()) {
                  sys::toast("RTC UPDATED");
                } else {
                  sys::toast("NO TIME SOURCE");
                }
                settingsRefresh();
              },
              nullptr);

  kit::button(root, 0, 208, 232, 62, "BOOT XIAOZHI  (OTA_1)", kit::kPanelAlt,
              kit::kViolet, [](lv_event_t*) { sys::bootSlot(1); }, nullptr);
  kit::button(root, 248, 208, 232, 62, "RESTART DEVICE", kit::kPanelAlt, kit::kAmber,
              [](lv_event_t*) { sys::restart(); }, nullptr);

  lv_obj_t* about = panel(root, 0, 278, kWidth, 134);
  kit::sectionTitle(about, 14, 10, "ABOUT");
  set_about_label = label(about, "--", &lv_font_montserrat_12, kit::kMuted);
  lv_obj_set_pos(set_about_label, 14, 34);

  settingsRefresh();
}

void settingsTick(lv_obj_t*, uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 1000) {
    return;
  }
  last = now;
  settingsRefresh();
}

void settingsKey(lv_obj_t*, AppKey key, uint32_t) {
  if (key == AppKey::kRight) {
    const uint32_t count = sys::screenTimeoutOptions();
    set_timeout_index = (set_timeout_index + 1) % static_cast<int>(count);
    sys::setScreenTimeoutMs(sys::screenTimeoutAt(set_timeout_index));
    settingsRefresh();
  }
}

const AppEntry kApps[] = {
    {"xiaozhi", "AI Chat", "AI", "xiaozhi 2.2.4", kit::kViolet, nullptr, nullptr, nullptr},
    {"ccusage", "CC Usage", "CC", "token dashboard", kit::kGreen, nullptr, nullptr, nullptr},
    {"clock", "Clock", "CL", "rtc + network", kit::kGreen, clockBuild, clockTick, clockKey},
    {"calendar", "Calendar", "31", "month view", kit::kAmber, calendarBuild, calendarTick,
     calendarKey},
    {"level", "Level", "LV", "bubble level", kit::kBlue, levelBuild, levelTick, levelKey},
    {"motion", "Motion", "IM", "accel + gyro", kit::kBlue, motionBuild, motionTick,
     motionKey},
    {"gallery", "Gallery", "GV", "procedural art", kit::kViolet, galleryBuild, galleryTick,
     galleryKey},
    {"stopwatch", "Timer", "ST", "stopwatch + laps", kit::kAmber, swBuild, swTick, swKey},
    {"selftest", "System Test", "OK", "hardware checks", kit::kRed, testBuild, testTick,
     testKey},
    {"power", "Power", "PW", "axp2101 detail", kit::kAmber, powerBuild, powerTick, powerKey},
    {"network", "Network", "NW", "wifi + companion", kit::kBlue, netBuild, netTick, netKey},
    {"settings", "Settings", "SE", "device setup", kit::kMuted, settingsBuild,
     settingsTick, settingsKey},
};

}  // namespace

const AppEntry* appTable() { return kApps; }
int appCount() { return static_cast<int>(sizeof(kApps) / sizeof(kApps[0])); }