#include <Arduino.h>

#include "ui_kit.h"

namespace kit {

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font,
                uint32_t color) {
  lv_obj_t* obj = lv_label_create(parent);
  lv_label_set_text(obj, text);
  lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(obj, c(color), LV_PART_MAIN);
  return obj;
}

lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h, uint32_t bg,
                int radius) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
  lv_obj_set_style_bg_color(obj, c(bg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}

lv_obj_t* card(lv_obj_t* parent, int x, int y, int w, int h, const char* caption,
               uint32_t caption_color, lv_obj_t** value_out,
               const lv_font_t* value_font, const char* note) {
  lv_obj_t* box = panel(parent, x, y, w, h);
  lv_obj_t* cap = label(box, caption, &lv_font_montserrat_12, caption_color);
  lv_obj_set_pos(cap, 12, 8);
  lv_obj_t* value = label(box, "--", value_font != nullptr ? value_font
                                                          : &lv_font_montserrat_26,
                          kWhite);
  lv_obj_set_pos(value, 12, 26);
  if (note != nullptr) {
    lv_obj_t* note_label = label(box, note, &lv_font_montserrat_12, kMuted);
    lv_obj_align(note_label, LV_ALIGN_BOTTOM_LEFT, 12, -6);
  }
  if (value_out != nullptr) {
    *value_out = value;
  }
  return box;
}

lv_obj_t* button(lv_obj_t* parent, int x, int y, int w, int h, const char* text,
                 uint32_t bg, uint32_t fg, lv_event_cb_t cb, void* user_data) {
  lv_obj_t* obj = lv_btn_create(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_style_radius(obj, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(obj, c(bg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
  lv_obj_t* text_label = label(obj, text, &lv_font_montserrat_14, fg);
  lv_obj_center(text_label);
  if (cb != nullptr) {
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, user_data);
  }
  return obj;
}

lv_obj_t* textButton(lv_obj_t* parent, int x, int y, int w, int h,
                     const char* text, uint32_t fg, lv_event_cb_t cb,
                     void* user_data) {
  lv_obj_t* obj = lv_btn_create(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_style_radius(obj, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_color(obj, c(kDivider), LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
  lv_obj_t* text_label = label(obj, text, &lv_font_montserrat_14, fg);
  lv_obj_center(text_label);
  if (cb != nullptr) {
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, user_data);
  }
  return obj;
}

void sectionTitle(lv_obj_t* parent, int x, int y, const char* text) {
  lv_obj_t* obj = label(parent, text, &lv_font_montserrat_12, kMuted);
  lv_obj_set_pos(obj, x, y);
}

}  // namespace kit