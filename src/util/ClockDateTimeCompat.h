#pragma once

#include <HalClock.h>

#include <cstdint>

struct ClockDateTimeCompat {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t weekday = 0;  // 0 = Sunday
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
};

namespace clockDateTimeCompat {
inline bool isLeapYear(const uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

inline uint8_t daysInMonth(const uint16_t year, const uint8_t month) {
  static constexpr uint8_t MONTH_DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) return 29;
  if (month < 1 || month > 12) return 31;
  return MONTH_DAYS[month - 1];
}

inline int dayDeltaForMonth(const uint16_t year, const uint8_t month, const uint8_t day, const int dayDelta) {
  int adjustedDay = static_cast<int>(day) + dayDelta;
  if (dayDelta > 0) {
    return adjustedDay > daysInMonth(year, month) ? 1 : 0;
  }
  if (dayDelta < 0) {
    return adjustedDay < 1 ? -1 : 0;
  }
  return 0;
}

inline void shiftDateByOneDay(ClockDateTimeCompat& out, const int direction) {
  if (direction > 0) {
    const uint8_t monthDays = daysInMonth(out.year, out.month);
    if (out.day < monthDays) {
      out.day++;
    } else {
      out.day = 1;
      if (out.month < 12) {
        out.month++;
      } else {
        out.month = 1;
        out.year++;
      }
    }
    out.weekday = static_cast<uint8_t>((out.weekday + 1) % 7);
    return;
  }

  if (out.day > 1) {
    out.day--;
  } else if (out.month > 1) {
    out.month--;
    out.day = daysInMonth(out.year, out.month);
  } else {
    out.month = 12;
    out.year--;
    out.day = 31;
  }
  out.weekday = static_cast<uint8_t>((out.weekday + 6) % 7);
}

inline uint8_t weekdayFromDate(const uint16_t year, const uint8_t month, const uint8_t day) {
  static constexpr int MONTH_OFFSETS[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int adjustedYear = year;
  if (month < 3) adjustedYear--;
  return static_cast<uint8_t>((adjustedYear + adjustedYear / 4 - adjustedYear / 100 + adjustedYear / 400 +
                               MONTH_OFFSETS[month - 1] + day) %
                              7);
}
}  // namespace clockDateTimeCompat

inline bool readClockDateTimeCompat(ClockDateTimeCompat& out, const uint8_t utcOffsetQuarterHoursBiased = 48) {
#ifdef SIMULATOR
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  if (!halClock.getDateTime(year, month, day, hour, minute)) return false;

  out.year = year;
  out.month = month;
  out.day = day;
  out.hour = hour;
  out.minute = minute;
  out.second = 0;
  out.weekday = clockDateTimeCompat::weekdayFromDate(year, month, day);

  const int offsetQuarterHours = static_cast<int>(utcOffsetQuarterHoursBiased) - 48;
  const int totalMinutes = static_cast<int>(hour) * 60 + static_cast<int>(minute) + offsetQuarterHours * 15;
  const int normalizedMinutes = ((totalMinutes % 1440) + 1440) % 1440;
  const int dayDelta = totalMinutes < 0 ? -1 : (totalMinutes >= 1440 ? 1 : 0);

  out.hour = static_cast<uint8_t>(normalizedMinutes / 60);
  out.minute = static_cast<uint8_t>(normalizedMinutes % 60);
  if (dayDelta != 0) {
    clockDateTimeCompat::shiftDateByOneDay(out, dayDelta);
  }
  return true;
#else
  HalClock::DateTime now;
  if (!halClock.getDateTime(now, utcOffsetQuarterHoursBiased)) return false;
  out.year = now.year;
  out.month = now.month;
  out.day = now.day;
  out.weekday = now.weekday;
  out.hour = now.hour;
  out.minute = now.minute;
  out.second = now.second;
  return true;
#endif
}
