#include "CurrentReadingStatsSummary.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>

#include "CrossPointState.h"
#include "RecentBooksStore.h"

namespace {
const RecentBook* findRecentBookForPath(const std::string& path) {
  const auto& recentBooks = RECENT_BOOKS.getBooks();
  const auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                               [&path](const RecentBook& book) { return book.path == path; });
  return it == recentBooks.end() ? nullptr : &(*it);
}
}  // namespace

std::string deriveReadingStatsCachePath(const std::string& bookPath) {
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

CurrentReadingStatsSummary loadCurrentReadingStatsSummary() {
  CurrentReadingStatsSummary summary;
  summary.globalStats = GlobalReadingStats::load();

  if (!APP_STATE.openEpubPath.empty() && Storage.exists(APP_STATE.openEpubPath.c_str())) {
    summary.currentBookPath = APP_STATE.openEpubPath;
  } else {
    const auto& recentBooks = RECENT_BOOKS.getBooks();
    if (!recentBooks.empty()) summary.currentBookPath = recentBooks[0].path;
  }

  if (summary.currentBookPath.empty()) return summary;

  if (const RecentBook* recentBook = findRecentBookForPath(summary.currentBookPath)) {
    summary.currentBookTitle = recentBook->title;
    summary.currentBookAuthor = recentBook->author;
  } else {
    const RecentBook fallbackBook = RECENT_BOOKS.getDataFromBook(summary.currentBookPath);
    summary.currentBookTitle = fallbackBook.title;
    summary.currentBookAuthor = fallbackBook.author;
  }

  if (summary.currentBookTitle.empty()) summary.currentBookTitle = summary.currentBookPath;
  summary.hasBookContext = !summary.currentBookTitle.empty();

  const std::string cachePath = deriveReadingStatsCachePath(summary.currentBookPath);
  if (!cachePath.empty()) summary.currentBookStats = BookReadingStats::load(cachePath);
  return summary;
}
