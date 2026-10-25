#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <lvgl.h>

#include "board_config.h"

#include <Arduino_GFX_Library.h>
#include <XPowersLib.h>

#include "app_state.h"
#include "hw_imu.h"
#include "hw_rtc.h"
#include "launcher.h"
#include "net_client.h"
#include "sys.h"
#include "timeutil.h"
#include "ui.h"

namespace {

constexpr uint32_t kUiRefreshMs = 1000;
constexpr uint32_t kDataStaleMs = 60 * 1000;
constexpr uint32_t kButtonDebounceMs = 35;
constexpr uint32_t kLongPressMs = 1000;
constexpr uint32_t kShortPressMaxMs = 800;
constexpr uint32_t kKeyLongPressMs = 900;
constexpr size_t kDrawBufferRows = 30;

const uint32_t kTimeoutOptions[] = {15000, 30000, 60000, 120000, 300000, 0};

Arduino_DataBus* display_bus = new Arduino_ESP32QSPI(
    board::kLcdChipSelect, board::kLcdClock, board::kLcdSdio0,
    board::kLcdSdio1, board::kLcdSdio2, board::kLcdSdio3);

Arduino_CO5300* display = new Arduino_CO5300(
    display_bus, board::kLcdReset, 0, board::kScreenWidth,
    board::kScreenHeight, 0, 0, 0, 0);

XPowersPMU power;
Preferences preferences;

lv_disp_draw_buf_t draw_buffer;
lv_color_t* draw_buffer_a = nullptr;
lv_color_t* draw_buffer_b = nullptr;

int16_t touch_x = 0;
int16_t touch_y = 0;
volatile bool touch_pending = false;

AppState app_state;
uint32_t state_received_at_ms = 0;
uint32_t last_ui_refresh_ms = 0;
uint32_t last_activity_ms = 0;
bool screen_on = true;
bool power_ready = false;
bool display_ready = false;
bool touch_ready = false;
bool imu_ready = false;
bool rtc_ready = false;

uint8_t g_brightness = board::kDisplayBrightness;
uint8_t g_brightness_saved = board::kDisplayBrightness;
uint32_t g_screen_timeout_ms = 60000;

uint64_t g_time_base_unix = 0;
uint32_t g_time_base_ms = 0;
bool g_time_valid = false;
int16_t g_utc_offset_minutes = 480;

struct ButtonState {
  int pin;
  bool active_high = false;
  bool stable_pressed = false;
  bool raw_pressed = false;
  bool last_level = false;
  bool long_press_sent = false;
  uint32_t raw_changed_at = 0;
  uint32_t pressed_at = 0;
};

ButtonState boot_button{board::kBootButton};
ButtonState key_button{board::kUserKey};
ButtonState power_button{board::kPowerKey, true};

void applyBrightness() {
  if (!display_ready) {
    return;
  }
  display->setBrightness(screen_on ? g_brightness : 0);
}

void setScreenOn(bool on, const char* who) {
  if (screen_on == on) {
    return;
  }
  screen_on = on;
  Serial.printf("[screen] %s via %s (timeout=%lums age=%lums)\n", on ? "ON" : "OFF",
                who, static_cast<unsigned long>(g_screen_timeout_ms),
                static_cast<unsigned long>(millis() - last_activity_ms));
  if (!display_ready) {
    return;
  }
  if (on) {
    display->displayOn();
    display_bus->writeC8D8(0x36, 0xA0);
    display->setBrightness(g_brightness);
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(nullptr);
  } else {
    display->setBrightness(0);
    display->displayOff();
  }
}

// Stamped with the caller's time so activity can never appear to come from the
// future relative to a timestamp sampled earlier in the same loop iteration.
void noteActivity() { last_activity_ms = millis(); }

void IRAM_ATTR onTouchInterrupt() { touch_pending = true; }

bool consumeTouchInterrupt() {
  noInterrupts();
  const bool pending = touch_pending;
  touch_pending = false;
  interrupts();
  return pending;
}

bool readTouchPoint(int16_t& x, int16_t& y) {
  uint8_t data[10] = {};
  Wire.beginTransmission(board::kTouchAddress);
  Wire.write(0xD0);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(board::kTouchAddress, 10) != 10) {
    return false;
  }
  for (uint8_t& byte : data) {
    byte = Wire.read();
  }

  if (data[6] != 0xAB || (data[5] & 0x7F) == 0 || (data[0] & 0x0F) != 0x06) {
    return false;
  }

  int32_t raw_x = board::kScreenWidth -
                  static_cast<int32_t>((static_cast<uint16_t>(data[2]) << 4) |
                                       (data[3] & 0x0F));
  int32_t raw_y = static_cast<int32_t>((static_cast<uint16_t>(data[1]) << 4) |
                                       (data[3] >> 4));
  raw_x = raw_x < 0 ? 0 : raw_x;
  raw_y = raw_y < 0 ? 0 : raw_y;
  raw_x = raw_x >= board::kScreenWidth ? board::kScreenWidth - 1 : raw_x;
  raw_y = raw_y >= board::kScreenHeight ? board::kScreenHeight - 1 : raw_y;
  x = static_cast<int16_t>(raw_x);
  y = static_cast<int16_t>(raw_y);
  return true;
}

void updateTouchWake() {
  if (screen_on || !consumeTouchInterrupt()) {
    return;
  }

  int16_t x = 0;
  int16_t y = 0;
  if (readTouchPoint(x, y)) {
    Serial.println("[display] wake by touch");
    setScreenOn(true, "touch-wake");
    noteActivity();
  }
}

void displayFlush(lv_disp_drv_t* driver, const lv_area_t* area,
                  lv_color_t* pixels) {
  if (!screen_on || !display_ready) {
    lv_disp_flush_ready(driver);
    return;
  }

  const uint32_t width = area->x2 - area->x1 + 1;
  const uint32_t height = area->y2 - area->y1 + 1;

#if LV_COLOR_16_SWAP
  display->draw16bitBeRGBBitmap(area->x1, area->y1,
                                reinterpret_cast<uint16_t*>(&pixels->full),
                                width, height);
#else
  display->draw16bitRGBBitmap(area->x1, area->y1,
                              reinterpret_cast<uint16_t*>(&pixels->full),
                              width, height);
#endif

  lv_disp_flush_ready(driver);
}

void roundDisplayArea(lv_disp_drv_t*, lv_area_t* area) {
  if (area->x1 % 2 != 0) {
    --area->x1;
  }
  if (area->y1 % 2 != 0) {
    --area->y1;
  }
  if (area->x2 % 2 == 0) {
    ++area->x2;
  }
  if (area->y2 % 2 == 0) {
    ++area->y2;
  }
}

void readTouch(lv_indev_drv_t*, lv_indev_data_t* data) {
  consumeTouchInterrupt();
  if (!readTouchPoint(touch_x, touch_y)) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  noteActivity();
  data->state = LV_INDEV_STATE_PR;
  data->point.x = touch_x;
  data->point.y = touch_y;
}

bool initPower() {
  if (!power.begin(Wire, AXP2101_SLAVE_ADDRESS, board::kI2cSda, board::kI2cScl)) {
    Serial.println("[pmu] AXP2101 not found");
    return false;
  }

  power.setALDO3Voltage(3300);
  power.enableALDO3();
  delay(100);
  power.disableALDO3();
  delay(100);
  power.enableALDO3();
  delay(150);

  power.setPowerKeyPressOnTime(XPOWERS_POWERON_128MS);
  power.setPowerKeyPressOffTime(XPOWERS_POWEROFF_6S);
  power.enableLongPressShutdown();
  power.setLongPressPowerOFF();
  power.enableBattDetection();
  power.enableBattVoltageMeasure();
  power.enableTemperatureMeasure();
  Serial.println("[pmu] AXP2101 ready, display rail enabled");
  return true;
}

bool initDisplay() {
  if (!display->begin()) {
    Serial.println("[display] initialization failed");
    return false;
  }
  display_bus->writeC8D8(0x36, 0xA0);
  display->fillScreen(RGB565_BLACK);
  display->setBrightness(g_brightness);
  display_ready = true;
  Serial.println("[display] CO5300 ready");
  return true;
}

bool initTouch() {
  pinMode(board::kTouchReset, OUTPUT);
  digitalWrite(board::kTouchReset, HIGH);
  delay(200);
  digitalWrite(board::kTouchReset, LOW);
  delay(200);
  digitalWrite(board::kTouchReset, HIGH);
  delay(200);

  pinMode(board::kTouchInterrupt, INPUT_PULLUP);
  Wire.beginTransmission(board::kTouchAddress);
  if (Wire.endTransmission() != 0) {
    Serial.println("[touch] CST9220 did not acknowledge");
    return false;
  }
  attachInterrupt(digitalPinToInterrupt(board::kTouchInterrupt), onTouchInterrupt,
                  FALLING);
  touch_ready = true;
  Serial.println("[touch] CST9220 ready");
  return true;
}

bool initLvgl() {
  lv_init();

  constexpr size_t pixel_count = board::kScreenWidth * kDrawBufferRows;
  draw_buffer_a = static_cast<lv_color_t*>(heap_caps_malloc(
      pixel_count * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  draw_buffer_b = static_cast<lv_color_t*>(heap_caps_malloc(
      pixel_count * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (draw_buffer_a == nullptr) {
    Serial.println("[lvgl] failed to allocate draw buffer");
    return false;
  }
  if (draw_buffer_b == nullptr) {
    Serial.println("[lvgl] second draw buffer unavailable");
  }

  lv_disp_draw_buf_init(&draw_buffer, draw_buffer_a, draw_buffer_b, pixel_count);

  static lv_disp_drv_t display_driver;
  lv_disp_drv_init(&display_driver);
  display_driver.hor_res = board::kScreenWidth;
  display_driver.ver_res = board::kScreenHeight;
  display_driver.flush_cb = displayFlush;
  display_driver.rounder_cb = roundDisplayArea;
  display_driver.draw_buf = &draw_buffer;
  lv_disp_drv_register(&display_driver);

  static lv_indev_drv_t touch_driver;
  lv_indev_drv_init(&touch_driver);
  touch_driver.type = LV_INDEV_TYPE_POINTER;
  touch_driver.read_cb = readTouch;
  lv_indev_drv_register(&touch_driver);

  return true;
}

template <typename ShortPress, typename LongPress>
void updateButton(ButtonState& button, uint32_t long_press_ms, ShortPress short_press,
                  LongPress long_press) {
  const uint32_t now = millis();
  const bool pressed = digitalRead(button.pin) == (button.active_high ? HIGH : LOW);

  if (pressed != button.last_level) {
    button.last_level = pressed;
    Serial.printf("[btn] pin=%d %s (raw=%d)\n", button.pin, pressed ? "DOWN" : "UP",
                  digitalRead(button.pin));
  }

  if (pressed != button.raw_pressed) {
    button.raw_pressed = pressed;
    button.raw_changed_at = now;
  }

  if (now - button.raw_changed_at < kButtonDebounceMs ||
      button.stable_pressed == button.raw_pressed) {
    if (button.stable_pressed && !button.long_press_sent &&
        now - button.pressed_at >= long_press_ms) {
      button.long_press_sent = true;
      long_press();
    }
    return;
  }

  button.stable_pressed = button.raw_pressed;
  if (button.stable_pressed) {
    button.pressed_at = now;
    button.long_press_sent = false;
    return;
  }

  if (!button.long_press_sent) {
    short_press(now - button.pressed_at);
  }
}

void updateButtons() {
  // BOOT (left): short = screen on/off, long = back to the home screen.
  updateButton(
      boot_button, kLongPressMs,
      [](uint32_t held_ms) {
        if (held_ms <= kShortPressMaxMs) {
          setScreenOn(!screen_on, "boot-button");
          if (screen_on) {
            noteActivity();
          }
        }
      },
      []() {
        setScreenOn(true, "boot-long");
        noteActivity();
        launcherShowHome();
      });

  // PWR (middle): mirrors BOOT for the screen. A 6 s hold powers the board off
  // through the AXP2101, so nothing is bound to a long press here.
  updateButton(
      power_button, kLongPressMs,
      [](uint32_t held_ms) {
        if (held_ms <= kShortPressMaxMs) {
          setScreenOn(!screen_on, "pwr-button");
          if (screen_on) {
            noteActivity();
          }
        }
      },
      []() {});

  // KEY (right): short = context action, long = open / leave an app.
  updateButton(
      key_button, kKeyLongPressMs,
      [](uint32_t held_ms) {
        setScreenOn(true, "key-button");
        noteActivity();
        launcherKey(AppKey::kRight, held_ms);
      },
      []() {
        setScreenOn(true, "key-long");
        noteActivity();
        launcherKey(AppKey::kRight, kKeyLongPressMs);
      });
}

void refreshDashboard() {
  if (launcherAtHome() || launcherCurrentApp() != kAppDashboard) {
    return;
  }
  const uint32_t now = millis();
  if (now - last_ui_refresh_ms < kUiRefreshMs) {
    return;
  }
  last_ui_refresh_ms = now;
  const uint32_t state_age_seconds = (now - state_received_at_ms) / 1000UL;
  uiUpdate(app_state, state_age_seconds);
  if (power_ready) {
    uiUpdateBattery(power.getBatteryPercent(), power.isCharging());
  }
}

void updateTimeBase() {
  if (!app_state.time_valid) {
    return;
  }
  const uint64_t reported = app_state.unix_time;
  if (g_time_valid) {
    const int64_t drift = static_cast<int64_t>(reported) -
                          static_cast<int64_t>(sys::nowUnix());
    if (drift > -5 && drift < 5) {
      return;
    }
  }
  g_time_base_unix = reported;
  g_time_base_ms = millis();
  g_time_valid = true;
  if (state_received_at_ms == 0) {
    state_received_at_ms = millis();
  }
  if (app_state.utc_offset_minutes != 0) {
    g_utc_offset_minutes = app_state.utc_offset_minutes;
  }
  if (rtc_ready) {
    rtc::writeUtc(reported);
  }
  Serial.printf("[time] synced to %llu\n",
                static_cast<unsigned long long>(reported));
}

void updateNetwork() {
  AppState received_state;
  if (netTakeState(received_state)) {
    app_state = received_state;
    state_received_at_ms = millis();
    updateTimeBase();
  }

  NetActionResult result;
  if (netTakeActionResult(result)) {
    uiShowToast(result.message);
  }

  if (!netOnline()) {
    app_state.connection = ConnectionState::kDisconnected;
  } else if (!app_state.data_valid) {
    app_state.connection = ConnectionState::kSyncing;
  } else if (millis() - state_received_at_ms >= kDataStaleMs) {
    app_state.connection = ConnectionState::kStale;
  } else {
    app_state.connection = ConnectionState::kLinked;
  }
}

void printMemory() {
  lv_mem_monitor_t monitor{};
  lv_mem_monitor(&monitor);
  Serial.printf("[mem] heap=%lu lvgl_free=%lu/%lu frag=%u%%\n",
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(monitor.free_size),
                static_cast<unsigned long>(monitor.total_size),
                static_cast<unsigned>(monitor.frag_pct));
}

void handleSerialCommands() {
  static char buffer[24] = {};
  static size_t length = 0;
  while (Serial.available() > 0) {
    const int value = Serial.read();
    if (value < 0) {
      break;
    }
    const char ch = static_cast<char>(value);
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      buffer[length] = '\0';
      if (strcmp(buffer, "ai") == 0) {
        Serial.println("[cmd] booting ota_1 (xiaozhi)");
        sys::bootSlot(1);
      } else if (strcmp(buffer, "home") == 0) {
        launcherShowHome();
        uiShowToast("HOME");
      } else if (strcmp(buffer, "reboot") == 0) {
        sys::restart();
      } else if (strcmp(buffer, "mem") == 0) {
        printMemory();
      } else if (strcmp(buffer, "slot") == 0) {
        Serial.printf("[slot] running=%s launcher=ota_0 ai=ota_1\n",
                      sys::runningSlotLabel());
      } else if (strcmp(buffer, "off") == 0) {
        setScreenOn(false, "serial");
      } else if (strcmp(buffer, "on") == 0) {
        setScreenOn(true, "serial");
      } else if (strcmp(buffer, "cfg") == 0) {
        Serial.printf("[cfg] screen=%d bright=%u timeout=%lu age=%lums boot=%d pwr=%d key=%d\n",
                      screen_on ? 1 : 0, static_cast<unsigned>(g_brightness),
                      static_cast<unsigned long>(g_screen_timeout_ms),
                      static_cast<unsigned long>(millis() - last_activity_ms),
                      digitalRead(board::kBootButton), digitalRead(board::kPowerKey),
                      digitalRead(board::kUserKey));
      } else if (length > 0) {
        Serial.println("[cmd] ai | home | reboot | mem | cfg | off | on | slot");
      }
      length = 0;
      continue;
    }
    if (length + 1 < sizeof(buffer)) {
      buffer[length++] = ch;
    }
  }
}

void loadSettings() {
  preferences.begin("launcher", false);
  g_brightness = static_cast<uint8_t>(preferences.getUInt("bright", board::kDisplayBrightness));
  g_screen_timeout_ms = preferences.getUInt("timeout", 60000);
  preferences.end();
  if (g_brightness < 20) {
    g_brightness = 20;
  }
  g_brightness_saved = g_brightness;
}

void saveSettings() {
  preferences.begin("launcher", false);
  preferences.putUInt("bright", g_brightness);
  preferences.putUInt("timeout", g_screen_timeout_ms);
  preferences.end();
}

}  // namespace

// ---------------------------------------------------------------------------
// sys:: implementation
// ---------------------------------------------------------------------------

namespace sys {

bool screenOn() { return screen_on; }
void setScreenOn(bool on) { ::setScreenOn(on, "sys"); }
void toggleScreen() { ::setScreenOn(!screen_on, "toggle"); }
void noteActivity() { ::noteActivity(); }
void toast(const char* text) { uiShowToast(text); }

const AppState& state() { return app_state; }
uint32_t stateAgeSeconds() {
  return state_received_at_ms == 0 ? 0 : (millis() - state_received_at_ms) / 1000UL;
}

bool powerReady() { return power_ready; }
int batteryPercent() { return power_ready ? power.getBatteryPercent() : -1; }
bool charging() { return power_ready && power.isCharging(); }
bool vbusPresent() { return power_ready && power.isVbusIn(); }
uint16_t batteryVoltageMv() { return power_ready ? power.getBattVoltage() : 0; }
uint16_t vbusVoltageMv() { return power_ready ? power.getVbusVoltage() : 0; }
uint16_t systemVoltageMv() { return power_ready ? power.getSystemVoltage() : 0; }
uint16_t vbusCurrentLimitMa() { return power_ready ? power.getVbusCurrentLimit() : 0; }
uint16_t buttonBatteryVoltageMv() { return power_ready ? power.getButtonBatteryVoltage() : 0; }
float pmuTemperatureC() { return power_ready ? power.getTemperature() : 0.0f; }

bool online() { return netOnline(); }
String localIp() { return WiFi.localIP().toString(); }
int wifiRssi() { return WiFi.RSSI(); }

bool imuReady() { return imu_ready; }
bool rtcReady() { return rtc_ready; }

bool timeValid() { return g_time_valid; }
uint64_t nowUnix() {
  if (!g_time_valid) {
    return 0;
  }
  return g_time_base_unix + (millis() - g_time_base_ms) / 1000UL;
}
int16_t localOffsetMinutes() { return g_utc_offset_minutes; }
bool syncRtcFromTime() {
  if (!g_time_valid || !rtc_ready) {
    return false;
  }
  return rtc::writeUtc(nowUnix());
}

// Partition names follow the Waveshare layout for this board: the launcher
// lives in ota_0 and xiaozhi in ota_1, so xiaozhi's built-in "long press BOOT
// to switch to the factory partition" lands back in ota_0 and returns here.
constexpr const char* kLauncherSlotLabel = "ota_0";
constexpr const char* kAiSlotLabel = "ota_1";

int runningSlot() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == nullptr) {
    return -1;
  }
  if (strcmp(running->label, kLauncherSlotLabel) == 0) {
    return 0;
  }
  if (strcmp(running->label, kAiSlotLabel) == 0) {
    return 1;
  }
  return -1;
}

const char* runningSlotLabel() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  return running != nullptr ? running->label : "?";
}

bool bootSlot(int slot) {
  const char* label = kLauncherSlotLabel;
  if (slot == 1) {
    label = kAiSlotLabel;
  } else if (slot != 0) {
    uiShowToast("BAD SLOT");
    return false;
  }

  const esp_partition_t* target = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label);
  if (target == nullptr) {
    uiShowToast("SLOT MISSING");
    return false;
  }
  if (esp_ota_set_boot_partition(target) != ESP_OK) {
    uiShowToast("SET BOOT FAILED");
    return false;
  }
  Serial.printf("[ota] boot set to %s, rebooting\n", target->label);
  uiShowToast("REBOOTING...");
  delay(400);
  esp_restart();
  return true;
}

void restart() {
  uiShowToast("REBOOTING...");
  delay(300);
  esp_restart();
}

uint32_t uptimeSeconds() { return millis() / 1000UL; }

uint8_t brightness() { return g_brightness; }
void setBrightness(uint8_t value) {
  if (value < 20) {
    value = 20;
  }
  g_brightness = value;
  applyBrightness();
  // The slider fires on every pixel of movement; only touch NVS in coarse steps.
  const int delta = static_cast<int>(g_brightness) - static_cast<int>(g_brightness_saved);
  if (delta >= 8 || delta <= -8) {
    g_brightness_saved = g_brightness;
    saveSettings();
  }
}
uint32_t screenTimeoutMs() { return g_screen_timeout_ms; }
void setScreenTimeoutMs(uint32_t value) {
  g_screen_timeout_ms = value;
  saveSettings();
}
uint32_t screenTimeoutOptions() {
  return static_cast<uint32_t>(sizeof(kTimeoutOptions) / sizeof(kTimeoutOptions[0]));
}
uint32_t screenTimeoutAt(uint32_t index) {
  const uint32_t count = screenTimeoutOptions();
  return kTimeoutOptions[index % count];
}

}  // namespace sys

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nESP32-C6 launcher starting");

  Serial.printf("[btn] idle levels boot=%d key=%d pwr=%d\n",
                digitalRead(board::kBootButton), digitalRead(board::kUserKey),
                digitalRead(board::kPowerKey));

  pinMode(board::kBootButton, INPUT_PULLUP);
  pinMode(board::kUserKey, INPUT_PULLUP);
  pinMode(board::kPowerKey, INPUT);
  Wire.begin(board::kI2cSda, board::kI2cScl);
  Wire.setClock(400000);

  loadSettings();

  power_ready = initPower();
  const bool display_ok = initDisplay();
  touch_ready = initTouch();

  rtc_ready = rtc::begin();
  if (rtc_ready) {
    uint64_t stored = 0;
    if (rtc::readUtc(stored) && rtc::isValid(stored)) {
      g_time_base_unix = stored;
      g_time_base_ms = millis();
      g_time_valid = true;
      Serial.printf("[rtc] boot time %llu\n",
                    static_cast<unsigned long long>(stored));
    }
  }

  imu_ready = imu::begin();
  Serial.printf("[imu] %s\n", imu_ready ? "QMI8658 ready" : "not detected");

  if (!display_ok || !initLvgl()) {
    Serial.println("[fatal] display stack initialization failed");
    while (true) {
      delay(1000);
    }
  }

  // The launcher may be started as a freshly written OTA image; confirm it
  // right away so the bootloader does not roll back to the other slot.
  esp_ota_mark_app_valid_cancel_rollback();

  launcherCreate();

  printMemory();

  if (!netBegin()) {
    uiShowToast("NETWORK TASK FAILED");
  }

  if (!power_ready) {
    uiShowToast("PMU OFFLINE");
  } else if (!touch_ready) {
    uiShowToast("TOUCH OFFLINE");
  }

  Serial.println("ESP32-C6 launcher ready");
  noteActivity();
}

void loop() {
  const uint32_t now = millis();
  updateTouchWake();
  updateButtons();
  lv_timer_handler();
  launcherTick(now);
  updateNetwork();
  handleSerialCommands();
  refreshDashboard();

  // Read the clock again here: activity recorded earlier in this iteration
  // (touch handling, key presses, a wake) is newer than `now`, and subtracting
  // it from a stale timestamp wraps around and would sleep the screen instantly.
  const int32_t idle_ms = static_cast<int32_t>(millis() - last_activity_ms);
  if (screen_on && g_screen_timeout_ms > 0 &&
      idle_ms >= static_cast<int32_t>(g_screen_timeout_ms)) {
    setScreenOn(false, "timeout");
  }
  delay(5);
}