#include <Arduino.h>

#include "hw_rtc.h"

#include <Arduino.h>
#include <Wire.h>

#include "timeutil.h"

namespace rtc {
namespace {

constexpr uint8_t kAddress = 0x51;
constexpr uint8_t kRegSeconds = 0x04;   // .. 0x0A (seconds..years), BCD

bool g_ready = false;

uint8_t bcdEncode(uint8_t value) {
  return static_cast<uint8_t>(((value / 10u) << 4) | (value % 10u));
}

uint8_t bcdDecode(uint8_t value) {
  return static_cast<uint8_t>((value >> 4) * 10u + (value & 0x0Fu));
}

bool readBytes(uint8_t reg, uint8_t* out, size_t length) {
  Wire.beginTransmission(kAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(static_cast<int>(kAddress), static_cast<int>(length)) !=
      static_cast<int>(length)) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    out[i] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

bool writeBytes(uint8_t reg, const uint8_t* data, size_t length) {
  Wire.beginTransmission(kAddress);
  Wire.write(reg);
  for (size_t i = 0; i < length; ++i) {
    Wire.write(data[i]);
  }
  return Wire.endTransmission() == 0;
}

}  // namespace

bool begin() {
  g_ready = false;
  Wire.beginTransmission(kAddress);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  uint8_t ctrl1 = 0;
  if (!readBytes(0x00, &ctrl1, 1)) {
    return false;
  }
  // Clear STOP so the oscillator runs, keep 12.5 pF load caps.
  ctrl1 = static_cast<uint8_t>((ctrl1 & ~0x20u) | 0x01u);
  g_ready = writeBytes(0x00, &ctrl1, 1);
  return g_ready;
}

bool ready() { return g_ready; }

bool readUtc(uint64_t& unix_utc) {
  if (!g_ready) {
    return false;
  }
  uint8_t b[7] = {};
  if (!readBytes(kRegSeconds, b, sizeof(b))) {
    return false;
  }
  const uint8_t second_raw = b[0] & 0x7Fu;
  const uint8_t minute_raw = b[1] & 0x7Fu;
  const uint8_t hour_raw = b[2] & 0x3Fu;
  const uint8_t day_raw = b[3] & 0x3Fu;
  const uint8_t month_raw = b[5] & 0x1Fu;
  const uint8_t year_raw = b[6];

  timeutil::DateTime value;
  value.second = bcdDecode(second_raw);
  value.minute = bcdDecode(minute_raw);
  value.hour = bcdDecode(hour_raw);
  value.day = bcdDecode(day_raw);
  value.month = bcdDecode(month_raw);
  value.year = 1970 + bcdDecode(year_raw);

  if (value.second > 59 || value.minute > 59 || value.hour > 23 ||
      value.day < 1 || value.day > 31 || value.month < 1 || value.month > 12) {
    return false;
  }
  unix_utc = timeutil::fromUtc(value);
  return true;
}

bool writeUtc(uint64_t unix_utc) {
  if (!g_ready) {
    return false;
  }
  timeutil::DateTime value;
  timeutil::toUtc(unix_utc, value);
  if (value.year < 1970) {
    return false;
  }

  uint8_t b[7] = {};
  b[0] = bcdEncode(static_cast<uint8_t>(value.second));
  b[1] = bcdEncode(static_cast<uint8_t>(value.minute));
  b[2] = bcdEncode(static_cast<uint8_t>(value.hour));
  b[3] = bcdEncode(static_cast<uint8_t>(value.day));
  b[4] = static_cast<uint8_t>(value.weekday + 1u);  // 1..7
  b[5] = bcdEncode(static_cast<uint8_t>(value.month));
  const int year_offset = value.year - 1970;
  b[6] = bcdEncode(static_cast<uint8_t>(year_offset > 99 ? 99 : year_offset));
  return writeBytes(kRegSeconds, b, sizeof(b));
}

bool isValid(uint64_t unix_utc) { return unix_utc >= 1704067200ULL; }

}  // namespace rtc