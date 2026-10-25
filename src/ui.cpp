#include <Arduino.h>

#include "ui.h"

#include <lvgl.h>

#include <algorithm>

namespace {

lv_color_t color(uint32_t hex) {
  return lv_color_hex(hex);
}

constexpr uint32_t kGreen = 0xA8FF4F;
constexpr uint32_t kWhite = 0xF4F7ED;
constexpr uint32_t kMuted = 0x718079;
constexpr uint32_t kPanel = 0x0B1210;
constexpr uint32_t kTrack = 0x1A231E;
constexpr uint32_t kDivider = 0x1E2A25;
constexpr uint32_t kAmber = 0xFFB02E;
constexpr uint32_t kRed = 0xFF5F57;
constexpr uint32_t kBlue = 0x55E6D2;
constexpr uint32_t kViolet = 0xA5B4FC;

UiActionHandler action_handler = nullptr;
lv_obj_t* ui_root = nullptr;

lv_obj_t* tileview = nullptr;
lv_obj_t* pages[4] = {};
lv_obj_t* page_dots[4] = {};
lv_obj_t* time_label = nullptr;
lv_obj_t* connection_label = nullptr;
lv_obj_t* battery_label = nullptr;

lv_obj_t* success_arc = nullptr;
lv_obj_t* success_value_label = nullptr;
lv_obj_t* calls_value_label = nullptr;
lv_obj_t* errors_value_label = nullptr;
lv_obj_t* quota_label = nullptr;

lv_obj_t* request_tokens_label = nullptr;
lv_obj_t* request_ttft_label = nullptr;
lv_obj_t* request_latest_model_label = nullptr;
lv_obj_t* request_status_label = nullptr;
lv_obj_t* request_latency_label = nullptr;

lv_obj_t* session_count_label = nullptr;
lv_obj_t* session_tokens_label = nullptr;
lv_obj_t* session_cost_label = nullptr;
lv_obj_t* top_model_label = nullptr;
lv_obj_t* top_model_calls_label = nullptr;

lv_obj_t* cache_arc = nullptr;
lv_obj_t* cache_value_label = nullptr;
lv_obj_t* context_input_label = nullptr;
lv_obj_t* context_output_label = nullptr;
lv_obj_t* context_cache_label = nullptr;
lv_obj_t* context_reasoning_label = nullptr;
lv_obj_t* mix_segments[4] = {};

lv_obj_t* quick_actions_overlay = nullptr;
lv_obj_t* toast = nullptr;
lv_timer_t* toast_timer = nullptr;

void styleLabel(lv_obj_t* label, const lv_font_t* font, uint32_t text_color) {
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, color(text_color), LV_PART_MAIN);
  lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);
}

lv_obj_t* createLabel(lv_obj_t* parent, const char* text,
                      const lv_font_t* font, uint32_t text_color) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text);
  styleLabel(label, font, text_color);
  return label;
}

void createPageTitle(lv_obj_t* page, const char* title, const char* range) {
  lv_obj_t* label =
      createLabel(page, title, &lv_font_montserrat_18, kGreen);
  lv_obj_set_pos(label, 24, 8);
  lv_obj_t* range_label =
      createLabel(page, range, &lv_font_montserrat_14, kMuted);
  lv_obj_align(range_label, LV_ALIGN_TOP_RIGHT, -24, 11);
}

lv_obj_t* createCard(lv_obj_t* parent, int x, int y, int width, int height,
                     const char* title, lv_obj_t** value_label,
                     const char* note = nullptr) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, width, height);
  lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(card, color(kPanel), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title_label =
      createLabel(card, title, &lv_font_montserrat_14, kMuted);
  lv_obj_set_pos(title_label, 12, 9);
  *value_label = createLabel(card, "--", &lv_font_montserrat_32, kWhite);
  lv_obj_set_pos(*value_label, 12, 25);
  if (note != nullptr) {
    lv_obj_t* note_label =
        createLabel(card, note, &lv_font_montserrat_14, kMuted);
    lv_obj_align(note_label, LV_ALIGN_BOTTOM_LEFT, 12, -7);
  }
  return card;
}

void createRing(lv_obj_t* parent, lv_obj_t** arc, lv_obj_t** value,
                const char* caption, int x, int y, int size) {
  *arc = lv_arc_create(parent);
  lv_obj_set_pos(*arc, x, y);
  lv_obj_set_size(*arc, size, size);
  lv_arc_set_rotation(*arc, 270);
  lv_arc_set_bg_angles(*arc, 0, 360);
  lv_arc_set_range(*arc, 0, 1000);
  lv_arc_set_value(*arc, 0);
  lv_obj_remove_style(*arc, nullptr, LV_PART_KNOB);
  lv_obj_set_style_arc_width(*arc, 12, LV_PART_MAIN);
  lv_obj_set_style_arc_color(*arc, color(kTrack), LV_PART_MAIN);
  lv_obj_set_style_arc_width(*arc, 12, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(*arc, color(kGreen), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(*arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_clear_flag(*arc, LV_OBJ_FLAG_CLICKABLE);

  *value = createLabel(parent, "--.-", &lv_font_montserrat_48, kWhite);
  lv_obj_align_to(*value, *arc, LV_ALIGN_CENTER, 0, -16);
  lv_obj_t* unit_label =
      createLabel(parent, "%", &lv_font_montserrat_18, kGreen);
  lv_obj_align_to(unit_label, *arc, LV_ALIGN_CENTER, 0, 25);
  lv_obj_t* caption_label =
      createLabel(parent, caption, &lv_font_montserrat_14, kMuted);
  lv_obj_align_to(caption_label, *arc, LV_ALIGN_CENTER, 0, 49);
}

void refreshPageDots() {
  lv_obj_t* active = lv_tileview_get_tile_act(tileview);
  for (size_t i = 0; i < 4; ++i) {
    const bool selected = pages[i] == active;
    lv_obj_set_style_bg_color(page_dots[i],
                              color(selected ? kGreen : kMuted), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page_dots[i],
                            selected ? LV_OPA_COVER : LV_OPA_50, LV_PART_MAIN);
  }
}

void updatePageDots(lv_event_t*) {
  refreshPageDots();
}

void dispatchAction(lv_event_t* event) {
  if (action_handler == nullptr) {
    return;
  }
  const auto raw = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
  action_handler(static_cast<UiAction>(raw));
}

void closeOverlay(lv_event_t*) {
  lv_obj_add_flag(quick_actions_overlay, LV_OBJ_FLAG_HIDDEN);
}

void dispatchOverlayAction(lv_event_t* event) {
  lv_obj_add_flag(quick_actions_overlay, LV_OBJ_FLAG_HIDDEN);
  dispatchAction(event);
}

void createActionButton(lv_obj_t* parent, const char* text, int y,
                        UiAction action) {
  lv_obj_t* button = lv_btn_create(parent);
  lv_obj_set_pos(button, 50, y);
  lv_obj_set_size(button, 350, 58);
  lv_obj_set_style_radius(button, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, color(kPanel), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(button, color(kGreen), LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(button, dispatchOverlayAction, LV_EVENT_CLICKED,
                      reinterpret_cast<void*>(static_cast<uintptr_t>(action)));

  lv_obj_t* label = createLabel(button, text, &lv_font_montserrat_18, kWhite);
  lv_obj_center(label);
}

void hideToast(lv_timer_t*) {
  if (toast == nullptr) {
    toast_timer = nullptr;
    return;
  }
  lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
  toast_timer = nullptr;
}

String formatLatency(uint32_t milliseconds) {
  if (milliseconds == 0) {
    return "-";
  }
  if (milliseconds < 1000) {
    return String(milliseconds) + "ms";
  }
  return String(static_cast<double>(milliseconds) / 1000.0, 1) + "s";
}

String formatCostRmb(uint64_t micros) {
  return String(static_cast<double>(micros) / 1000000.0, 2);
}

void createOverviewPage(lv_obj_t* page) {
  createPageTitle(page, "OVERVIEW", "TODAY");

  createRing(page, &success_arc, &success_value_label, "SUCCESS", 24, 52, 220);

  lv_obj_t* metrics = lv_obj_create(page);
  lv_obj_remove_style_all(metrics);
  lv_obj_set_pos(metrics, 268, 58);
  lv_obj_set_size(metrics, 188, 220);
  lv_obj_clear_flag(metrics, LV_OBJ_FLAG_SCROLLABLE);
  createCard(metrics, 0, 0, 188, 64, "CALLS", &calls_value_label);
  createCard(metrics, 0, 76, 188, 64, "ERRORS", &errors_value_label);
  createCard(metrics, 0, 152, 188, 64, "TODAY COST (RMB)", &quota_label);
}

void createRequestsPage(lv_obj_t* page) {
  createPageTitle(page, "REQUESTS", "TODAY");

  lv_obj_t* model_title =
      createLabel(page, "LATEST MODEL", &lv_font_montserrat_14, kMuted);
  lv_obj_set_pos(model_title, 24, 54);
  request_latest_model_label =
      createLabel(page, "-", &lv_font_montserrat_32, kWhite);
  lv_obj_set_pos(request_latest_model_label, 24, 79);
  lv_obj_set_width(request_latest_model_label, 432);
  lv_label_set_long_mode(request_latest_model_label, LV_LABEL_LONG_DOT);

  createCard(page, 24, 145, 205, 82, "STATUS", &request_status_label);
  createCard(page, 251, 145, 205, 82, "LATENCY", &request_latency_label);
  createCard(page, 24, 242, 205, 82, "AVG TTFT", &request_ttft_label);
  createCard(page, 251, 242, 205, 82, "TOKENS TODAY",
             &request_tokens_label);
}

void createSessionsPage(lv_obj_t* page) {
  createPageTitle(page, "TOTALS", "30 DAYS");

  lv_obj_t* count_title =
      createLabel(page, "REQUESTS", &lv_font_montserrat_14, kMuted);
  lv_obj_set_pos(count_title, 24, 43);
  session_count_label =
      createLabel(page, "--", &lv_font_montserrat_48, kWhite);
  lv_obj_set_pos(session_count_label, 24, 60);

  createCard(page, 24, 135, 205, 82, "TOTAL TOKENS",
             &session_tokens_label);
  createCard(page, 251, 135, 205, 82, "EST. COST (RMB)",
             &session_cost_label);

  lv_obj_t* model_title =
      createLabel(page, "TOP MODEL", &lv_font_montserrat_14, kMuted);
  lv_obj_set_pos(model_title, 24, 258);
  top_model_label =
      createLabel(page, "-", &lv_font_montserrat_32, kWhite);
  lv_obj_set_pos(top_model_label, 24, 284);
  lv_obj_set_width(top_model_label, 300);
  lv_label_set_long_mode(top_model_label, LV_LABEL_LONG_DOT);
  top_model_calls_label =
      createLabel(page, "-", &lv_font_montserrat_18, kGreen);
  lv_obj_align(top_model_calls_label, LV_ALIGN_TOP_RIGHT, -24, 290);
}

void createContextPage(lv_obj_t* page) {
  createPageTitle(page, "CONTEXT", "GATEWAY OBSERVED");
  createRing(page, &cache_arc, &cache_value_label, "CACHE HIT", 140, 42, 200);

  lv_obj_t* mix_title =
      createLabel(page, "TOKEN MIX", &lv_font_montserrat_14, kMuted);
  lv_obj_set_pos(mix_title, 24, 258);
  constexpr uint32_t colors[] = {kBlue, kGreen, kViolet, kAmber};
  for (size_t i = 0; i < 4; ++i) {
    mix_segments[i] = lv_obj_create(page);
    lv_obj_remove_style_all(mix_segments[i]);
    lv_obj_set_pos(mix_segments[i], 24, 282);
    lv_obj_set_size(mix_segments[i], 0, 8);
    lv_obj_set_style_bg_color(mix_segments[i], color(colors[i]), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mix_segments[i], LV_OPA_COVER, LV_PART_MAIN);
  }

  context_input_label =
      createLabel(page, "INPUT --", &lv_font_montserrat_14, kBlue);
  lv_obj_set_pos(context_input_label, 24, 306);
  context_output_label =
      createLabel(page, "OUTPUT --", &lv_font_montserrat_14, kGreen);
  lv_obj_set_pos(context_output_label, 251, 306);
  context_cache_label =
      createLabel(page, "CACHE --", &lv_font_montserrat_14, kViolet);
  lv_obj_set_pos(context_cache_label, 24, 338);
  context_reasoning_label =
  createLabel(page, "CACHE W --", &lv_font_montserrat_14, kAmber);
  lv_obj_set_pos(context_reasoning_label, 251, 338);
}

void ensureToast() {
  if (toast != nullptr) {
    return;
  }
  toast = lv_label_create(lv_layer_top());
  lv_obj_set_style_text_font(toast, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(toast, color(kWhite), LV_PART_MAIN);
  lv_obj_set_style_bg_color(toast, color(0x182024), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(toast, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(toast, 18, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(toast, 12, LV_PART_MAIN);
  lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -42);
  lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace

void uiCreate(lv_obj_t* parent, UiActionHandler handler) {
  action_handler = handler;
  ui_root = parent;

  lv_obj_t* screen = parent;
  lv_obj_set_style_bg_color(screen, color(0x030706), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* logo = lv_obj_create(screen);
  lv_obj_remove_style_all(logo);
  lv_obj_set_pos(logo, 20, 9);
  lv_obj_set_size(logo, 38, 38);
  lv_obj_set_style_radius(logo, 9, LV_PART_MAIN);
  lv_obj_set_style_bg_color(logo, color(kGreen), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(logo, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_t* logo_label =
      createLabel(logo, "CC", &lv_font_montserrat_18, 0x07110D);
  lv_obj_center(logo_label);
  lv_obj_t* brand_label =
      createLabel(screen, "CC SWITCH", &lv_font_montserrat_18, kGreen);
  lv_obj_set_pos(brand_label, 68, 11);
  lv_obj_t* gateway_label =
      createLabel(screen, "USAGE DECK", &lv_font_montserrat_14, kMuted);
  lv_obj_set_pos(gateway_label, 68, 31);

  connection_label = createLabel(screen, LV_SYMBOL_WIFI " OFFLINE",
                                 &lv_font_montserrat_14, kRed);
  lv_obj_align(connection_label, LV_ALIGN_TOP_RIGHT, -22, 17);
  lv_obj_add_flag(connection_label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(connection_label, 12);
  lv_obj_add_event_cb(
      connection_label, dispatchAction, LV_EVENT_CLICKED,
      reinterpret_cast<void*>(static_cast<uintptr_t>(UiAction::kReconnect)));

  tileview = lv_tileview_create(screen);
  lv_obj_set_pos(tileview, 0, 56);
  lv_obj_set_size(tileview, 480, 382);
  lv_obj_set_style_bg_opa(tileview, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(tileview, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(tileview, 0, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);
  pages[0] = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_RIGHT);
  pages[1] =
      lv_tileview_add_tile(tileview, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  pages[2] =
      lv_tileview_add_tile(tileview, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  pages[3] = lv_tileview_add_tile(tileview, 3, 0, LV_DIR_LEFT);
  for (lv_obj_t* page : pages) {
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  }
  createOverviewPage(pages[0]);
  createRequestsPage(pages[1]);
  createSessionsPage(pages[2]);
  createContextPage(pages[3]);
  lv_obj_add_event_cb(tileview, updatePageDots, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t* home_button = lv_btn_create(screen);
  lv_obj_set_pos(home_button, 16, 444);
  lv_obj_set_size(home_button, 58, 28);
  lv_obj_set_style_radius(home_button, 8, LV_PART_MAIN);
  lv_obj_set_style_bg_color(home_button, color(kGreen), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(home_button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(home_button, 0, LV_PART_MAIN);
  lv_obj_t* home_label =
      createLabel(home_button, LV_SYMBOL_HOME " HOME", &lv_font_montserrat_12,
                  0x07110D);
  lv_obj_center(home_label);
  lv_obj_add_event_cb(
      home_button, dispatchAction, LV_EVENT_CLICKED,
      reinterpret_cast<void*>(static_cast<uintptr_t>(UiAction::kGoHome)));

  time_label =
      createLabel(screen, "--/-- --:--", &lv_font_montserrat_14, kMuted);
  lv_obj_set_pos(time_label, 86, 452);
  for (size_t i = 0; i < 4; ++i) {
    page_dots[i] = lv_obj_create(screen);
    lv_obj_remove_style_all(page_dots[i]);
    lv_obj_set_pos(page_dots[i], 213 + static_cast<int>(i) * 18, 459);
    lv_obj_set_size(page_dots[i], 7, 7);
    lv_obj_set_style_radius(page_dots[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(page_dots[i], color(i == 0 ? kGreen : kMuted),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page_dots[i],
                            i == 0 ? LV_OPA_COVER : LV_OPA_50, LV_PART_MAIN);
  }
  battery_label =
      createLabel(screen, "BAT --%", &lv_font_montserrat_14, kMuted);
  lv_obj_align(battery_label, LV_ALIGN_BOTTOM_RIGHT, -20, -13);
  lv_obj_add_flag(battery_label, LV_OBJ_FLAG_HIDDEN);

  quick_actions_overlay = lv_obj_create(screen);
  lv_obj_set_size(quick_actions_overlay, 480, 480);
  lv_obj_set_pos(quick_actions_overlay, 0, 0);
  lv_obj_set_style_bg_color(quick_actions_overlay, color(0x000000),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(quick_actions_overlay, LV_OPA_90, LV_PART_MAIN);
  lv_obj_set_style_border_width(quick_actions_overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(quick_actions_overlay, 0, LV_PART_MAIN);
  lv_obj_clear_flag(quick_actions_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(quick_actions_overlay, closeOverlay, LV_EVENT_CLICKED,
                      nullptr);
  lv_obj_t* overlay_title =
      createLabel(quick_actions_overlay, "DASHBOARD ACTIONS",
                  &lv_font_montserrat_18, kMuted);
  lv_obj_align(overlay_title, LV_ALIGN_TOP_MID, 0, 72);
  createActionButton(quick_actions_overlay, "OPEN CC SWITCH", 130,
                     UiAction::kOpenDashboard);
  createActionButton(quick_actions_overlay, "REFRESH NOW", 203,
                     UiAction::kRefresh);
  lv_obj_t* cancel = lv_btn_create(quick_actions_overlay);
  lv_obj_set_pos(cancel, 50, 276);
  lv_obj_set_size(cancel, 350, 58);
  lv_obj_set_style_radius(cancel, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(cancel, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_color(cancel, color(kDivider), LV_PART_MAIN);
  lv_obj_set_style_border_width(cancel, 1, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(cancel, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(cancel, closeOverlay, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* cancel_label =
      createLabel(cancel, "CANCEL", &lv_font_montserrat_18, kMuted);
  lv_obj_center(cancel_label);
  lv_obj_add_flag(quick_actions_overlay, LV_OBJ_FLAG_HIDDEN);

  ensureToast();
}

void uiUpdate(const AppState& state, uint32_t elapsed_seconds) {
  if (ui_root == nullptr) {
    return;
  }
  lv_label_set_text(time_label, formatDateTime(state, elapsed_seconds).c_str());

  const bool linked = state.connection == ConnectionState::kLinked;
  const bool stale = state.connection == ConnectionState::kStale;
  const char* status =
      linked ? "ONLINE" : (stale ? "STALE" : connectionLabel(state.connection));
  lv_label_set_text_fmt(connection_label, LV_SYMBOL_WIFI " %s", status);
  lv_obj_set_style_text_color(
      connection_label, color(linked ? kGreen : (stale ? kAmber : kRed)),
      LV_PART_MAIN);

  if (!state.data_valid) {
    lv_arc_set_value(success_arc, 0);
    lv_arc_set_value(cache_arc, 0);
    lv_label_set_text(success_value_label, "--.-");
    lv_label_set_text(cache_value_label, "--.-");
    lv_label_set_text(request_latest_model_label, "WAITING FOR DATA");
    return;
  }

  lv_arc_set_value(success_arc, std::min<uint16_t>(state.success_permille, 1000));
  lv_label_set_text_fmt(success_value_label, "%u.%u",
                        state.success_permille / 10,
                        state.success_permille % 10);
  lv_obj_align_to(success_value_label, success_arc, LV_ALIGN_CENTER, 0, -16);
  lv_label_set_text(calls_value_label,
                    formatCompactTokens(state.calls_today).c_str());
  lv_label_set_text(errors_value_label,
                    formatCompactTokens(state.errors_today).c_str());
  lv_obj_set_style_text_color(
      errors_value_label, color(state.errors_today > 0 ? kAmber : kWhite),
      LV_PART_MAIN);
  lv_label_set_text(quota_label,
                    formatCostRmb(state.cost_today_cny_micros).c_str());

  const bool latest_ok = !state.latest_failed && state.latest_status >= 200 &&
                         state.latest_status < 400;
  const String latest = String(state.latest_status) + "  " +
                        formatLatency(state.latest_latency_ms);
  lv_label_set_text(request_tokens_label,
                    formatCompactTokens(state.tokens_today).c_str());
  lv_label_set_text(request_ttft_label,
                    formatLatency(state.average_ttft_ms).c_str());
  lv_label_set_text(request_latest_model_label, state.latest_model);
  lv_label_set_text_fmt(request_status_label, "%u", state.latest_status);
  lv_label_set_text(request_latency_label,
                    formatLatency(state.latest_latency_ms).c_str());
  lv_obj_set_style_text_color(request_status_label,
                              color(latest_ok ? kGreen : kAmber), LV_PART_MAIN);

  const uint64_t total_tokens = state.total_input + state.total_output +
                                state.total_cache_read + state.total_reasoning;
  lv_label_set_text(session_count_label,
                    formatCompactTokens(state.total_calls).c_str());
  lv_label_set_text(session_tokens_label,
                    formatCompactTokens(total_tokens).c_str());
  lv_label_set_text(session_cost_label,
                    formatCostRmb(state.total_cost_cny_micros).c_str());
  lv_label_set_text(top_model_label, state.top_model);
  lv_label_set_text_fmt(top_model_calls_label, "%lu CALLS",
                        static_cast<unsigned long>(state.top_model_calls));

  lv_arc_set_value(cache_arc,
                   std::min<uint16_t>(state.cache_hit_permille, 1000));
  lv_label_set_text_fmt(cache_value_label, "%u.%u",
                        state.cache_hit_permille / 10,
                        state.cache_hit_permille % 10);
  lv_obj_align_to(cache_value_label, cache_arc, LV_ALIGN_CENTER, 0, -16);
  lv_label_set_text_fmt(context_input_label, "INPUT %s",
                        formatCompactTokens(state.total_input).c_str());
  lv_label_set_text_fmt(context_output_label, "OUTPUT %s",
                        formatCompactTokens(state.total_output).c_str());
  lv_label_set_text_fmt(context_cache_label, "CACHE R %s",
                        formatCompactTokens(state.total_cache_read).c_str());
  lv_label_set_text_fmt(context_reasoning_label, "CACHE W %s",
                        formatCompactTokens(state.total_reasoning).c_str());

  const uint64_t parts[] = {state.total_input, state.total_output,
                            state.total_cache_read, state.total_reasoning};
  int x = 24;
  int remaining_width = 432;
  for (size_t i = 0; i < 4; ++i) {
    const int width =
        i == 3 ? remaining_width
               : (total_tokens > 0
                      ? static_cast<int>(432ULL * parts[i] / total_tokens)
                      : 0);
    lv_obj_set_pos(mix_segments[i], x, 282);
    lv_obj_set_width(mix_segments[i], width);
    x += width;
    remaining_width -= width;
  }
}

void uiUpdateBattery(int percent, bool charging) {
  if (percent < 0 || percent > 100) {
    lv_obj_add_flag(battery_label, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_label_set_text_fmt(battery_label, charging ? "CHG %d%%" : "BAT %d%%",
                        percent);
  lv_obj_set_style_text_color(
      battery_label,
      color(charging ? kBlue : (percent < 20 ? kAmber : kMuted)), LV_PART_MAIN);
  lv_obj_clear_flag(battery_label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(battery_label, LV_ALIGN_BOTTOM_RIGHT, -20, -13);
}

void uiShowQuickActions() {
  lv_obj_clear_flag(quick_actions_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(quick_actions_overlay);
}

void uiHideQuickActions() {
  if (quick_actions_overlay == nullptr) {
    return;
  }
  lv_obj_add_flag(quick_actions_overlay, LV_OBJ_FLAG_HIDDEN);
}

void uiCyclePage() {
  if (tileview == nullptr) {
    return;
  }

  lv_obj_t* active = lv_tileview_get_tile_act(tileview);
  size_t current = 0;
  for (size_t i = 0; i < 4; ++i) {
    if (pages[i] == active) {
      current = i;
      break;
    }
  }

  const size_t next = (current + 1) % 4;
  lv_obj_set_tile(tileview, pages[next], LV_ANIM_ON);
  refreshPageDots();
}

bool uiModalVisible() {
  return quick_actions_overlay != nullptr &&
         !lv_obj_has_flag(quick_actions_overlay, LV_OBJ_FLAG_HIDDEN);
}

void uiShowToast(const char* text) {
  ensureToast();
  if (toast == nullptr) {
    Serial.println("[ui] toast allocation failed");
    return;
  }
  lv_label_set_text(toast, text);
  lv_obj_clear_flag(toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -42);
  lv_obj_move_foreground(toast);
  if (toast_timer != nullptr) {
    lv_timer_del(toast_timer);
  }
  toast_timer = lv_timer_create(hideToast, 2000, nullptr);
  lv_timer_set_repeat_count(toast_timer, 1);
} 
void uiSetVisible(bool visible) {
  if (ui_root == nullptr) {
    return;
  }
  if (visible) {
    lv_obj_clear_flag(ui_root, LV_OBJ_FLAG_HIDDEN);
  } else {
    uiHideQuickActions();
    lv_obj_add_flag(ui_root, LV_OBJ_FLAG_HIDDEN);
  }
}

lv_obj_t* uiRoot() { return ui_root; }
