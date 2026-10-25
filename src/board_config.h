#pragma once

#define XPOWERS_CHIP_AXP2101

namespace board {

constexpr int kScreenWidth = 480;
constexpr int kScreenHeight = 480;

// Waveshare ESP32-C6-Touch-AMOLED-2.16 QSPI panel.
constexpr int kLcdSdio0 = 1;
constexpr int kLcdSdio1 = 2;
constexpr int kLcdSdio2 = 3;
constexpr int kLcdSdio3 = 4;
constexpr int kLcdClock = 0;
constexpr int kLcdChipSelect = 15;
constexpr int kLcdReset = -1;  // The panel rail is reset through AXP2101 ALDO3.

constexpr int kI2cSda = 8;
constexpr int kI2cScl = 7;
constexpr int kTouchAddress = 0x5A;
constexpr int kTouchInterrupt = 5;
constexpr int kTouchReset = 11;
constexpr int kPmuAddress = 0x34;

// BOOT is GPIO9 on the ESP32-C6 module; KEY is a separate user key on GPIO10.
// PWR is wired to the AXP2101 power key and is read through the PMU.
constexpr int kBootButton = 9;
constexpr int kUserKey = 10;
// PWR is wired to the AXP2101 PWRON pin, and is also mirrored onto GPIO18
// through a BSS138 level shifter (reads HIGH while the key is held down).
constexpr int kPowerKey = 18;

constexpr uint8_t kDisplayBrightness = 170;

}  // namespace board
