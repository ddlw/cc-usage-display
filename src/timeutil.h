#pragma once
#include <stdint.h>

// Minimal civil-time helpers (no libc time dependency, UTC based).
namespace timeutil {

inline int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= (m <= 2);
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

inline void civilFromDays(int64_t z, int& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
  const int64_t yy = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
  const unsigned mp = (5u * doy + 2u) / 153u;
  d = doy - (153u * mp + 2u) / 5u + 1u;
  m = mp + (mp < 10u ? 3u : static_cast<unsigned>(-9));
  y = static_cast<int>(yy + (m <= 2));
}

struct DateTime {
  int year = 1970;
  unsigned month = 1;
  unsigned day = 1;
  unsigned hour = 0;
  unsigned minute = 0;
  unsigned second = 0;
  unsigned weekday = 4;  // 0 = Sunday
};

inline void toUtc(uint64_t unix_sec, DateTime& out) {
  const int64_t days = static_cast<int64_t>(unix_sec / 86400ULL);
  uint32_t rem = static_cast<uint32_t>(unix_sec % 86400ULL);
  civilFromDays(days, out.year, out.month, out.day);
  out.hour = rem / 3600u;
  rem %= 3600u;
  out.minute = rem / 60u;
  out.second = rem % 60u;
  int64_t w = (days + 4) % 7;
  if (w < 0) w += 7;
  out.weekday = static_cast<unsigned>(w);
}

inline uint64_t fromUtc(const DateTime& v) {
  const int64_t days = daysFromCivil(v.year, v.month, v.day);
  return static_cast<uint64_t>(days * 86400 +
                               static_cast<int64_t>(v.hour) * 3600 +
                               static_cast<int64_t>(v.minute) * 60 +
                               static_cast<int64_t>(v.second));
}

inline void fromUnixWithOffset(uint64_t unix_sec, int16_t offset_minutes,
                               DateTime& out) {
  int64_t v = static_cast<int64_t>(unix_sec) +
              static_cast<int64_t>(offset_minutes) * 60;
  if (v < 0) {
    v = 0;
  }
  toUtc(static_cast<uint64_t>(v), out);
}

inline const char* weekdayShort(unsigned w) {
  static const char* kNames[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  return kNames[w % 7u];
}

inline const char* monthShort(unsigned m) {
  static const char* kNames[] = {"---", "JAN", "FEB", "MAR", "APR", "MAY",
                                 "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  return kNames[(m >= 1u && m <= 12u) ? m : 0u];
}

inline int daysInMonth(int y, unsigned m) {
  static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1u || m > 12u) {
    return 30;
  }
  if (m == 2u && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) {
    return 29;
  }
  return kDays[m - 1u];
}

}  // namespace timeutil