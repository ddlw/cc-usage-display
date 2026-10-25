#pragma once
#include <stdint.h>

// Minimal PCF85063A RTC driver on the shared I2C bus.
namespace rtc {

bool begin();
bool ready();
bool readUtc(uint64_t& unix_utc);
bool writeUtc(uint64_t unix_utc);
// True when the stored time looks plausible (year >= 2024).
bool isValid(uint64_t unix_utc);

}  // namespace rtc