#pragma once

#include <string>

#include "activities/Activity.h"
#include "reading_stats/BookReadingStats.h"
#include "reading_stats/GlobalReadingStats.h"

class ReadingStatsActivity final : public Activity {
  std::string currentBookPath;
  std::string currentBookTitle;
  std::string currentBookAuthor;
  BookReadingStats currentBookStats;
  GlobalReadingStats globalStats;
  bool hasCurrentBook = false;
  bool useProvidedStats = false;

  static std::string deriveCachePath(const std::string& bookPath);
  void loadStats();
  void drawStatsCard(int x, int y, int w, int h, const char* title, const char* subtitle, uint32_t sessions,
                     uint32_t readingSeconds, uint32_t pagesTurned) const;

 public:
  explicit ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingStats", renderer, mappedInput) {}
  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string currentBookPath,
                       std::string currentBookTitle, std::string currentBookAuthor, const BookReadingStats& currentBookStats,
                       const GlobalReadingStats& globalStats)
      : Activity("ReadingStats", renderer, mappedInput),
        currentBookPath(std::move(currentBookPath)),
        currentBookTitle(std::move(currentBookTitle)),
        currentBookAuthor(std::move(currentBookAuthor)),
        currentBookStats(currentBookStats),
        globalStats(globalStats),
        hasCurrentBook(true),
        useProvidedStats(true) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
