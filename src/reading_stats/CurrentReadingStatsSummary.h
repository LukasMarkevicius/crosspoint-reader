#pragma once

#include <string>

#include "reading_stats/BookReadingStats.h"
#include "reading_stats/GlobalReadingStats.h"

struct CurrentReadingStatsSummary {
  std::string currentBookPath;
  std::string currentBookTitle;
  std::string currentBookAuthor;
  BookReadingStats currentBookStats;
  GlobalReadingStats globalStats;
  bool hasBookContext = false;
};

std::string deriveReadingStatsCachePath(const std::string& bookPath);
CurrentReadingStatsSummary loadCurrentReadingStatsSummary();
