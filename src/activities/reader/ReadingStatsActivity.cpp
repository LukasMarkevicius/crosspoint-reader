#include "ReadingStatsActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <I18n.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>

#include "CrossPointState.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int CARD_GAP = 14;
constexpr int CARD_INSET = 14;
constexpr int HEADER_TO_GRID_GAP = 14;
constexpr int ROW_GAP = 58;
constexpr int VALUE_FONT = UI_12_FONT_ID;
constexpr int LABEL_FONT = SMALL_FONT_ID;

void drawMetric(const GfxRenderer& renderer, const int centerX, const int valueY, const char* value, const char* label) {
  const auto valueBounds = renderer.getTextBounds(VALUE_FONT, value, EpdFontFamily::BOLD);
  renderer.drawText(VALUE_FONT, centerX - valueBounds.width / 2, valueY, value, true, EpdFontFamily::BOLD);

  const auto labelBounds = renderer.getTextBounds(LABEL_FONT, label);
  renderer.drawText(LABEL_FONT, centerX - labelBounds.width / 2, valueY + renderer.getLineHeight(VALUE_FONT) + 6, label, true);
}

const RecentBook* findRecentBook(const std::string& path) {
  const auto& recentBooks = RECENT_BOOKS.getBooks();
  const auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                               [&path](const RecentBook& book) { return book.path == path; });
  return it == recentBooks.end() ? nullptr : &(*it);
}
}  // namespace

std::string ReadingStatsActivity::deriveCachePath(const std::string& bookPath) {
  if (bookPath.empty()) return {};
  if (FsHelpers::hasXtcExtension(bookPath)) {
    Xtc xtc(bookPath, "/.crosspoint");
    return xtc.getCachePath();
  }
  if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    Txt txt(bookPath, "/.crosspoint");
    return txt.getCachePath();
  }
  Epub epub(bookPath, "/.crosspoint");
  return epub.getCachePath();
}

void ReadingStatsActivity::loadStats() {
  globalStats = GlobalReadingStats::load();
  currentBookPath.clear();
  currentBookTitle.clear();
  currentBookAuthor.clear();
  currentBookStats = {};
  hasCurrentBook = false;

  if (!APP_STATE.openEpubPath.empty() && Storage.exists(APP_STATE.openEpubPath.c_str())) {
    currentBookPath = APP_STATE.openEpubPath;
  } else {
    const auto& recentBooks = RECENT_BOOKS.getBooks();
    if (!recentBooks.empty()) currentBookPath = recentBooks[0].path;
  }
  if (currentBookPath.empty()) return;

  if (const RecentBook* book = findRecentBook(currentBookPath)) {
    currentBookTitle = book->title;
    currentBookAuthor = book->author;
  } else {
    const RecentBook fallbackBook = RECENT_BOOKS.getDataFromBook(currentBookPath);
    currentBookTitle = fallbackBook.title;
    currentBookAuthor = fallbackBook.author;
  }
  if (currentBookTitle.empty()) currentBookTitle = currentBookPath;

  const std::string cachePath = deriveCachePath(currentBookPath);
  if (cachePath.empty()) return;
  currentBookStats = BookReadingStats::load(cachePath);
  hasCurrentBook = currentBookStats.sessionCount > 0 || currentBookStats.totalReadingSeconds > 0 ||
                   currentBookStats.totalPagesTurned > 0;
}

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  if (!useProvidedStats) {
    loadStats();
  }
  requestUpdate();
}

void ReadingStatsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (!useProvidedStats && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    loadStats();
    requestUpdate();
    return;
  }
}

void ReadingStatsActivity::drawStatsCard(const int x, const int y, const int w, const int h, const char* title,
                                         const char* subtitle, const uint32_t sessions, const uint32_t readingSeconds,
                                         const uint32_t pagesTurned) const {
  renderer.drawRect(x, y, w, h);

  const int innerW = w - CARD_INSET * 2;
  int textY = y + 12;
  const std::string titleLine = renderer.truncatedText(UI_12_FONT_ID, title, innerW, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_12_FONT_ID, textY, titleLine.c_str(), true, EpdFontFamily::BOLD);
  textY += renderer.getLineHeight(UI_12_FONT_ID);
  if (subtitle && subtitle[0] != '\0') {
    const std::string subtitleLine = renderer.truncatedText(UI_10_FONT_ID, subtitle, innerW);
    renderer.drawCenteredText(UI_10_FONT_ID, textY, subtitleLine.c_str(), true);
    textY += renderer.getLineHeight(UI_10_FONT_ID);
  }

  const int gridTop = textY + HEADER_TO_GRID_GAP;
  const int column1 = x + w / 6;
  const int column2 = x + w / 2;
  const int column3 = x + (w * 5) / 6;
  const int row1Y = gridTop;
  const int row2Y = gridTop + ROW_GAP;

  char timeBuf[24];
  char avgBuf[24];
  char pagesBuf[16];
  char ppmBuf[16];
  snprintf(pagesBuf, sizeof(pagesBuf), "%lu", static_cast<unsigned long>(pagesTurned));

  BookReadingStats::formatDuration(readingSeconds, timeBuf, sizeof(timeBuf));
  const uint32_t avgSessionSeconds = sessions > 0 ? readingSeconds / sessions : 0;
  BookReadingStats::formatDuration(avgSessionSeconds, avgBuf, sizeof(avgBuf));
  if (readingSeconds >= 60 && pagesTurned > 0) {
    const uint32_t pagesPerMinuteTimes10 = (pagesTurned * 600u + (readingSeconds / 2u)) / readingSeconds;
    snprintf(ppmBuf, sizeof(ppmBuf), "%lu.%lu", static_cast<unsigned long>(pagesPerMinuteTimes10 / 10u),
             static_cast<unsigned long>(pagesPerMinuteTimes10 % 10u));
  } else {
    snprintf(ppmBuf, sizeof(ppmBuf), "0.0");
  }

  char sessionBuf[16];
  snprintf(sessionBuf, sizeof(sessionBuf), "%lu", static_cast<unsigned long>(sessions));
  drawMetric(renderer, column1, row1Y, sessionBuf, tr(STR_STATS_SESSIONS_LBL));
  drawMetric(renderer, column2, row1Y, timeBuf, tr(STR_STATS_TIME_LBL));
  drawMetric(renderer, column3, row1Y, pagesBuf, tr(STR_STATS_PAGES_LBL));
  drawMetric(renderer, column1, row2Y, avgBuf, tr(STR_STATS_AVG_SESSION_LBL));
  drawMetric(renderer, column3, row2Y, ppmBuf, tr(STR_STATS_PAGES_PER_MIN));
}

void ReadingStatsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentX = metrics.contentSidePadding;
  const int contentW = pageWidth - metrics.contentSidePadding * 2;
  const int cardH = (pageHeight - metrics.topPadding - metrics.headerHeight - metrics.buttonHintsHeight -
                     metrics.verticalSpacing * 2 - CARD_GAP) /
                    2;
  const int topY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_READING_STATS));
  drawStatsCard(contentX, topY, contentW, cardH, hasCurrentBook ? currentBookTitle.c_str() : tr(STR_STATS_THIS_BOOK),
                hasCurrentBook ? currentBookAuthor.c_str() : "", currentBookStats.sessionCount,
                currentBookStats.totalReadingSeconds, currentBookStats.totalPagesTurned);
  drawStatsCard(contentX, topY + cardH + CARD_GAP, contentW, cardH, tr(STR_STATS_ALL_TIME), "",
                globalStats.totalSessions, globalStats.totalReadingSeconds, globalStats.totalPagesTurned);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
