#pragma once

#include <Arduino.h>

#include "app_state.h"

// Shared device services. Implemented in main.cpp so the UI modules stay
// decoupled from the hardware bring-up code.
namespace sys {

// Display / UI
bool screenOn();
void setScreenOn(bool on);
void toggleScreen();
void noteActivity();
void toast(const char* text);

// Data feed coming from the companion over Wi-Fi
const AppState& state();
uint32_t stateAgeSeconds();

// Power management (AXP2101)
bool powerReady();
int batteryPercent();
bool charging();
bool vbusPresent();
uint16_t batteryVoltageMv();
uint16_t vbusVoltageMv();
uint16_t systemVoltageMv();
uint16_t vbusCurrentLimitMa();
uint16_t buttonBatteryVoltageMv();
float pmuTemperatureC();

// Network
bool online();
String localIp();
int wifiRssi();

// IMU / RTC status flags
bool imuReady();
bool rtcReady();

// Time
bool timeValid();
uint64_t nowUnix();
int16_t localOffsetMinutes();
bool syncRtcFromTime();

// OTA slots / restart
int runningSlot();  // 0 = launcher slot, 1 = ai slot, -1 = unknown
const char* runningSlotLabel();
bool bootSlot(int slot);  // 0 = launcher (ota_0), 1 = xiaozhi (ota_1)
void restart();
uint32_t uptimeSeconds();

// Settings
uint8_t brightness();
void setBrightness(uint8_t value);
uint32_t screenTimeoutMs();
void setScreenTimeoutMs(uint32_t value);
uint32_t screenTimeoutOptions();
uint32_t screenTimeoutAt(uint32_t index);

}  // namespace sys