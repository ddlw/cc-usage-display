#include <Arduino.h>

#include "launcher.h"

#include <Arduino.h>

#include "app_state.h"
#include "net_client.h"
#include "sys.h"
#include "timeutil.h"
#include "ui.h"
#include "ui_kit.h"

namespace {

using kit::c;
using kit::label;
using kit::panel;

constexpr int kTileCountMax = 16;

lv_obj_t* home_root = nullptr;
lv_obj_t* home_clock = nullptr;
lv_obj_t* home_date = nullptr;
lv_obj_t* home_link = nullptr;
lv_obj_t* home_wifi = nullptr;
lv_obj_t* home_battery = nullptr;
lv_obj_t* tiles[kTileCountMax] = {};
int selection = 0;
int app_count = 0;

lv_obj_t* app_frame = nullptr;
lv_obj_t* app_content = nullptr;
lv_obj_t* dash_root = nullptr;
int current_app = -1;
uint64_t home_last_second = 0;

void updateSelection() {
  for (int i = 0; i < app_count; ++i) {
    if (tiles[i] == nullptr) {
      continue;
    }
    if (i == selection) {
      lv_obj_set_style_border_width(tiles[i], 2, LV_PART_MAIN);
      lv_obj_set_style_border_color(tiles[i], c(kit::kGreen), LV_PART_MAIN);
    } else {
      lv_obj_set_style_border_width(tiles[i], 0, LV_PART_MAIN);
    }
  }
}

void uiActionHandler(UiAction action) {
  switch (action) {
    case UiAction::kRefresh:
      netRequestRefresh();
      uiShowToast("REFRESH QUEUED");
      break;
    case UiAction::kReconnect:
      netReconnect();
      uiShowToast("RECONNECT QUEUED");
      break;
    case UiAction::kOpenDashboard:
      netRequestOpenDashboard();
      break;
    case UiAction::kGoHome:
      launcherShowHome();
      break;
  }
}

void tileClicked(lv_event_t* event) {
  const int index =
      static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  selection = index;
  updateSelection();
  launcherOpenApp(index);
}

void onBackClicked(lv_event_t*) { launcherCloseApp(); }

void xiaozhiConfirm(lv_event_t* event) {
  lv_obj_t* mbox = lv_event_get_current_target(event);
  const uint16_t choice = lv_msgbox_get_active_btn(mbox);
  lv_msgbox_close(mbox);
  if (choice == 1) {
    sys::bootSlot(1);
  }
}

void showXiaozhiConfirm() {
  static const char* kButtons[] = {"CANCEL", "SWITCH", ""};
  lv_obj_t* mbox =
      lv_msgbox_create(nullptr, "AI ASSISTANT",
                       "Boot xiaozhi 2.2.4 from\nthe OTA_1 slot?\n\nThe device will reboot.",
                       kButtons, false);
  lv_obj_add_event_cb(mbox, xiaozhiConfirm, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_center(mbox);
}

void updateHomeStatus() {
  const uint64_t now = sys::nowUnix();
  if (now == home_last_second) {
    return;
  }
  home_last_second = now;

  if (sys::timeValid()) {
    timeutil::DateTime local;
    timeutil::fromUnixWithOffset(
        now, static_cast<int16_t>(sys::localOffsetMinutes()), local);
    lv_label_set_text_fmt(home_clock, "%02u:%02u", local.hour, local.minute);
    lv_label_set_text_fmt(home_date, "%s %02u %s", timeutil::weekdayShort(local.weekday),
                          local.day, timeutil::monthShort(local.month));
  } else {
    lv_label_set_text(home_clock, "--:--");
    lv_label_set_text(home_date, "NO TIME SYNC");
  }

  const AppState& state = sys::state();
  const char* link = "CC SWITCH - WAITING";
  uint32_t link_color = kit::kMuted;
  switch (state.connection) {
    case ConnectionState::kLinked:
      link = "CC SWITCH - LINKED";
      link_color = kit::kGreen;
      break;
    case ConnectionState::kStale:
      link = "CC SWITCH - STALE";
      link_color = kit::kAmber;
      break;
    case ConnectionState::kSyncing:
      link = "CC SWITCH - SYNCING";
      link_color = kit::kBlue;
      break;
    case ConnectionState::kDisconnected:
    default:
      break;
  }
  lv_label_set_text(home_link, link);
  lv_obj_set_style_text_color(home_link, c(link_color), LV_PART_MAIN);

  if (sys::online()) {
    lv_label_set_text(home_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(home_wifi, c(kit::kGreen), LV_PART_MAIN);
  } else {
    lv_label_set_text(home_wifi, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(home_wifi, c(kit::kRed), LV_PART_MAIN);
  }

  if (sys::powerReady()) {
    const int percent = sys::batteryPercent();
    if (percent >= 0) {
      lv_label_set_text_fmt(home_battery, sys::charging() ? LV_SYMBOL_CHARGE " %d%%"
                                                          : "%d%%",
                            percent);
      lv_obj_set_style_text_color(
          home_battery,
          c(percent < 20 ? kit::kAmber : kit::kMuted), LV_PART_MAIN);
    } else {
      lv_label_set_text(home_battery, "BAT --");
    }
  } else {
    lv_label_set_text(home_battery, "NO PMU");
  }
}

void createDashboard() {
  if (dash_root != nullptr) {
    return;
  }
  dash_root = lv_obj_create(lv_scr_act());
  lv_obj_set_pos(dash_root, 0, 0);
  lv_obj_set_size(dash_root, 480, 480);
  lv_obj_set_style_border_width(dash_root, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(dash_root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(dash_root, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(dash_root, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_clear_flag(dash_root, LV_OBJ_FLAG_SCROLLABLE);
  uiCreate(dash_root, uiActionHandler);
  lv_obj_add_flag(dash_root, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace

void launcherCreate() {
  app_count = appCount();
  if (app_count > kTileCountMax) {
    app_count = kTileCountMax;
  }
  const AppEntry* apps = appTable();

  home_root = lv_obj_create(lv_scr_act());
  lv_obj_set_pos(home_root, 0, 0);
  lv_obj_set_size(home_root, 480, 480);
  lv_obj_set_style_bg_color(home_root, c(kit::kBg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(home_root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(home_root, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(home_root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(home_root, 0, LV_PART_MAIN);
  lv_obj_clear_flag(home_root, LV_OBJ_FLAG_SCROLLABLE);

  // --- header -------------------------------------------------------------
  lv_obj_t* logo = panel(home_root, 20, 16, 40, 40, kit::kGreen, 10);
  lv_obj_t* logo_text = label(logo, "C6", &lv_font_montserrat_18, kit::kInk);
  lv_obj_center(logo_text);
  lv_obj_t* brand = label(home_root, "ESP32-C6 AMOLED", &lv_font_montserrat_18,
                          kit::kGreen);
  lv_obj_set_pos(brand, 72, 14);
  home_link = label(home_root, "CC SWITCH - WAITING", &lv_font_montserrat_12, kit::kMuted);
  lv_obj_set_pos(home_link, 72, 40);

  home_clock = label(home_root, "--:--", &lv_font_montserrat_26, kit::kWhite);
  lv_obj_align(home_clock, LV_ALIGN_TOP_RIGHT, -18, 12);
  home_date = label(home_root, "---", &lv_font_montserrat_12, kit::kMuted);
  lv_obj_align(home_date, LV_ALIGN_TOP_RIGHT, -18, 44);

  // --- grid ---------------------------------------------------------------
  for (int i = 0; i < app_count; ++i) {
    const AppEntry& app = apps[i];
    const int col = i % 3;
    const int row = i / 3;
    const int x = 14 + col * 154;
    const int y = 84 + row * 86;
    lv_obj_t* tile = panel(home_root, x, y, 144, 78, kit::kPanel, 12);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile, tileClicked, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    lv_obj_t* badge = panel(tile, 12, 10, 30, 30, app.accent, LV_RADIUS_CIRCLE);
    lv_obj_t* mono = label(badge, app.monogram, &lv_font_montserrat_12, kit::kInk);
    lv_obj_center(mono);
    lv_obj_t* title = label(tile, app.title, &lv_font_montserrat_14, kit::kWhite);
    lv_obj_set_pos(title, 12, 42);
    lv_obj_t* subtitle = label(tile, app.subtitle, &lv_font_montserrat_12, kit::kMuted);
    lv_obj_set_pos(subtitle, 12, 58);
    tiles[i] = tile;
  }
  updateSelection();

  // --- footer -------------------------------------------------------------
  lv_obj_t* hint = label(home_root, "LEFT: SCREEN   HOLD LEFT: HOME   RIGHT: SELECT",
                         &lv_font_montserrat_12, kit::kMuted);
  lv_obj_set_pos(hint, 16, 444);
  home_wifi = label(home_root, LV_SYMBOL_WIFI, &lv_font_montserrat_14, kit::kRed);
  lv_obj_align(home_wifi, LV_ALIGN_BOTTOM_RIGHT, -96, -22);
  home_battery = label(home_root, "BAT --", &lv_font_montserrat_12, kit::kMuted);
  lv_obj_align(home_battery, LV_ALIGN_BOTTOM_RIGHT, -18, -22);

  updateHomeStatus();

  // Build the dashboard up front so the first tap is instant and so its memory
  // footprint is known before any app page is allocated.
  createDashboard();
}

void launcherOpenApp(int index) {
  if (index < 0 || index >= app_count || home_root == nullptr) {
    return;
  }
  const AppEntry* apps = appTable();
  if (index == kAppXiaozhi) {
    showXiaozhiConfirm();
    return;
  }

  current_app = index;

  if (index == kAppDashboard) {
    createDashboard();
    lv_obj_add_flag(home_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(dash_root, LV_OBJ_FLAG_HIDDEN);
    uiSetVisible(true);
    lv_obj_move_foreground(dash_root);
    return;
  }

  const AppEntry& app = apps[index];
  app_frame = lv_obj_create(lv_scr_act());
  lv_obj_set_pos(app_frame, 0, 0);
  lv_obj_set_size(app_frame, 480, 480);
  lv_obj_set_style_bg_color(app_frame, c(kit::kBg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(app_frame, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(app_frame, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(app_frame, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(app_frame, 0, LV_PART_MAIN);
  lv_obj_clear_flag(app_frame, LV_OBJ_FLAG_SCROLLABLE);

  kit::button(app_frame, 12, 12, 92, 40, LV_SYMBOL_LEFT " HOME", kit::kPanelAlt,
              kit::kGreen, onBackClicked, nullptr);
  lv_obj_t* title = label(app_frame, app.title, &lv_font_montserrat_18, kit::kWhite);
  lv_obj_set_pos(title, 118, 22);
  lv_obj_t* subtitle = label(app_frame, app.subtitle, &lv_font_montserrat_12, kit::kMuted);
  lv_obj_align(subtitle, LV_ALIGN_TOP_RIGHT, -16, 25);
  lv_obj_t* underline = panel(app_frame, 0, 62, 480, 3, app.accent, 0);

  app_content = lv_obj_create(app_frame);
  lv_obj_set_pos(app_content, 0, 68);
  lv_obj_set_size(app_content, 480, 412);
  lv_obj_set_style_bg_opa(app_content, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(app_content, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(app_content, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(app_content, 0, LV_PART_MAIN);
  lv_obj_clear_flag(app_content, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_add_flag(home_root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(app_frame);

  if (app.build != nullptr) {
    app.build(app_content);
  }
}

void launcherCloseApp() {
  if (current_app < 0) {
    return;
  }
  if (current_app == kAppDashboard) {
    uiSetVisible(false);
  } else if (app_frame != nullptr) {
    lv_obj_del(app_frame);
    app_frame = nullptr;
    app_content = nullptr;
  }
  current_app = -1;
  if (home_root != nullptr) {
    lv_obj_clear_flag(home_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(home_root);
  }
  home_last_second = 0;
}

void launcherShowHome() { launcherCloseApp(); }

bool launcherAtHome() { return current_app < 0; }

int launcherCurrentApp() { return current_app; }

void launcherTick(uint32_t now_ms) {
  if (current_app < 0) {
    updateHomeStatus();
    return;
  }
  if (current_app == kAppDashboard || app_content == nullptr) {
    return;
  }
  const AppEntry& app = appTable()[current_app];
  if (app.tick != nullptr) {
    app.tick(app_content, now_ms);
  }
}

void launcherKey(AppKey key, uint32_t held_ms) {
  if (key != AppKey::kRight) {
    return;
  }
  if (held_ms >= 900) {
    if (current_app < 0) {
      launcherOpenApp(selection);
    } else {
      launcherCloseApp();
    }
    return;
  }

  if (current_app < 0) {
    selection = (selection + 1) % app_count;
    updateSelection();
    return;
  }
  if (current_app == kAppDashboard) {
    if (uiModalVisible()) {
      uiHideQuickActions();
    } else {
      uiShowQuickActions();
    }
    return;
  }
  if (app_content == nullptr) {
    return;
  }
  const AppEntry& app = appTable()[current_app];
  if (app.key != nullptr) {
    app.key(app_content, key, held_ms);
  }
}