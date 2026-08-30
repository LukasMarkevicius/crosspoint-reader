#include "NewsletterSyncClient.h"

#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "CrossPointSettings.h"
#include "NewsletterConfigStore.h"
#include "network/HttpDownloader.h"
#include "util/ClockDateTimeCompat.h"

namespace {
constexpr uint32_t MIN_FREE_FOR_TLS = 35000;
constexpr uint32_t MIN_BLOCK_FOR_TLS = 20000;
constexpr size_t FEED_READ_CHUNK = 1024;
constexpr int MAX_FEED_ITEMS_PER_SYNC = 160;
constexpr const char* FEED_CACHE_PATH = "/.crosspoint/newsletters/feed.xml";

int monthNumber(const std::string& mon) {
  static constexpr const char* MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (int i = 0; i < 12; i++) {
    if (mon == MONTHS[i]) return i + 1;
  }
  return 1;
}

bool insufficientHeap() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
  return freeHeap < MIN_FREE_FOR_TLS || maxAllocHeap < MIN_BLOCK_FOR_TLS;
}

class NewsletterFeedParser final {
 public:
  explicit NewsletterFeedParser(const NewsletterSyncClient::ProgressFn& onChunk, const int maxItemsToProcess)
      : onChunk_(onChunk), maxItemsToProcess_(maxItemsToProcess) {}

  bool feed(const uint8_t* buffer, const size_t size) {
    if (limitReached_ || !buffer || size == 0) return true;
    return appendChunk(buffer, size);
  }

  int imported() const { return imported_; }
  int updated() const { return updated_; }
  int skipped() const { return skipped_; }
  int processedItems() const { return processedItems_; }
  bool limitReached() const { return limitReached_; }

 private:
  bool appendChunk(const uint8_t* data, const size_t len) {
    tail_.append(reinterpret_cast<const char*>(data), len);
    size_t itemStart = 0;
    while ((itemStart = tail_.find("<item>")) != std::string::npos) {
      if (processedItems_ >= maxItemsToProcess_) {
        limitReached_ = true;
        tail_.clear();
        return true;
      }
      const size_t itemEnd = tail_.find("</item>", itemStart);
      if (itemEnd == std::string::npos) break;
      const size_t itemCloseEnd = itemEnd + strlen("</item>");
      const std::string itemXml = tail_.substr(itemStart, itemCloseEnd - itemStart);
      tail_.erase(0, itemCloseEnd);
      processedItems_++;

      NewsletterFeedItem item;
      if (!NewsletterSyncClient::extractTag(itemXml, "guid", item.guid) ||
          !NewsletterSyncClient::extractTag(itemXml, "title", item.title) ||
          !NewsletterSyncClient::extractTag(itemXml, "pubDate", item.pubDate) ||
          !NewsletterSyncClient::extractTag(itemXml, "description", item.description)) {
        continue;
      }
      NewsletterSyncClient::extractTag(itemXml, "author", item.author);
      item.guid = NewsletterSyncClient::xmlDecode(NewsletterSyncClient::stripCdata(item.guid));
      item.title = NewsletterSyncClient::xmlDecode(NewsletterSyncClient::stripCdata(item.title));
      item.author = NewsletterSyncClient::xmlDecode(NewsletterSyncClient::stripCdata(item.author));
      item.pubDate = NewsletterSyncClient::xmlDecode(NewsletterSyncClient::stripCdata(item.pubDate));
      item.description = NewsletterSyncClient::xmlDecode(NewsletterSyncClient::stripCdata(item.description));
      item.sortKey = NewsletterSyncClient::parseRfc822Date(item.pubDate);
      item.contentHash = NewsletterStore::fnv1a(item.title + '\n' + item.author + '\n' + item.description);

      const auto before = NEWSLETTER_STORE.getByGuid(item.guid);
      const bool existed = before != nullptr;
      const uint32_t oldHash = existed ? before->contentHash : 0;
      if (!NEWSLETTER_STORE.applyFeedItem(item)) {
        skipped_++;
        continue;
      }
      const auto after = NEWSLETTER_STORE.getByGuid(item.guid);
      if (!existed && after) {
        imported_++;
      } else if (after && oldHash != after->contentHash) {
        updated_++;
      }
      if ((processedItems_ % 10) == 0) {
        LOG_DBG("NEWS", "Imported scan progress: processed=%d imported=%d updated=%d skipped=%d heap=%u", processedItems_,
                imported_, updated_, skipped_, static_cast<unsigned>(ESP.getFreeHeap()));
      }
      if (onChunk_) onChunk_(imported_, updated_);
    }

    if (tail_.size() > 8192) {
      const size_t lastItemOpen = tail_.rfind("<item>");
      if (lastItemOpen == std::string::npos) {
        tail_.clear();
      } else if (lastItemOpen > 0) {
        tail_.erase(0, lastItemOpen);
      }
    }
    return true;
  }

  const NewsletterSyncClient::ProgressFn& onChunk_;
  std::string tail_;
  int imported_ = 0;
  int updated_ = 0;
  int skipped_ = 0;
  int processedItems_ = 0;
  int maxItemsToProcess_ = MAX_FEED_ITEMS_PER_SYNC;
  bool limitReached_ = false;
};
}  // namespace

NewsletterSyncClient::Result NewsletterSyncClient::sync(const ProgressFn& progress) {
  Result result;
  if (!NEWSLETTER_CONFIG.isConfigured()) {
    result.error = Error::NotConfigured;
    return result;
  }
  if (WiFi.status() != WL_CONNECTED) {
    result.error = Error::NoWifi;
    return result;
  }
  if (insufficientHeap()) {
    result.error = Error::LowMemory;
    return result;
  }

  LOG_DBG("NEWS", "Starting feed sync, free heap=%u max alloc=%u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
  const std::string& feedUrl = NEWSLETTER_CONFIG.getFeedUrl();
  if (!Storage.ensureDirectoryExists("/.crosspoint") || !Storage.ensureDirectoryExists("/.crosspoint/newsletters")) {
    result.error = Error::Save;
    return result;
  }

  const auto downloadResult = HttpDownloader::downloadToFile(feedUrl, FEED_CACHE_PATH, nullptr, nullptr);
  LOG_DBG("NEWS", "Feed download result=%d", static_cast<int>(downloadResult));
  if (downloadResult != HttpDownloader::OK) {
    result.error = downloadResult == HttpDownloader::FILE_ERROR ? Error::Save : Error::Network;
    return result;
  }

  HalFile feedFile;
  if (!Storage.openFileForRead("NEWS", FEED_CACHE_PATH, feedFile) || !feedFile.isOpen()) {
    result.error = Error::Save;
    return result;
  }
  LOG_DBG("NEWS", "Opened feed archive for import");

  auto readBuffer = makeUniqueNoThrow<uint8_t[]>(FEED_READ_CHUNK);
  if (!readBuffer) {
    feedFile.close();
    result.error = Error::LowMemory;
    return result;
  }
  LOG_DBG("NEWS", "Allocated %u-byte parser buffer", static_cast<unsigned>(FEED_READ_CHUNK));

  NEWSLETTER_STORE.reserveForSync(NewsletterStore::MAX_RETAINED_NEWSLETTERS);

  NewsletterFeedParser parser(progress, MAX_FEED_ITEMS_PER_SYNC);
  while (feedFile.available() > 0) {
    const int bytesRead = feedFile.read(readBuffer.get(), FEED_READ_CHUNK);
    if (bytesRead < 0) {
      feedFile.close();
      result.error = Error::Parse;
      return result;
    }
    if (bytesRead == 0) break;
    if (!parser.feed(readBuffer.get(), static_cast<size_t>(bytesRead))) {
      feedFile.close();
      result.error = Error::Parse;
      return result;
    }
    if (parser.limitReached()) {
      LOG_DBG("NEWS", "Stopping import after %d feed items", parser.processedItems());
      break;
    }
  }

  feedFile.close();

  result.imported = parser.imported();
  result.updated = parser.updated();
  result.skipped = parser.skipped();
  LOG_DBG("NEWS", "Feed parse finished: processed=%d imported=%d updated=%d skipped=%d", parser.processedItems(),
          result.imported, result.updated, result.skipped);

  LOG_DBG("NEWS", "Saving newsletter index");
  if (!NEWSLETTER_STORE.finishSync(currentLocalDateKey())) {
    result.error = Error::Save;
    return result;
  }
  LOG_DBG("NEWS", "Newsletter sync saved successfully");

  result.error = Error::Ok;
  return result;
}

const char* NewsletterSyncClient::errorString(const Error error) {
  switch (error) {
    case Error::Ok:
      return "OK";
    case Error::NotConfigured:
      return tr(STR_NEWSLETTER_NOT_CONFIGURED);
    case Error::NoWifi:
      return tr(STR_NEWSLETTER_WIFI_REQUIRED);
    case Error::BadUrl:
      return tr(STR_NEWSLETTER_BAD_URL);
    case Error::Network:
      return tr(STR_NEWSLETTER_NETWORK_ERROR);
    case Error::Parse:
      return tr(STR_NEWSLETTER_PARSE_ERROR);
    case Error::Save:
      return tr(STR_NEWSLETTER_SAVE_ERROR);
    case Error::LowMemory:
      return tr(STR_NEWSLETTER_LOW_MEMORY);
  }
  return "Unknown error";
}

int64_t NewsletterSyncClient::parseRfc822Date(const std::string& text) {
  char weekday[8] = "";
  int day = 1;
  char month[8] = "";
  int year = 1970;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (sscanf(text.c_str(), "%7[^,], %d %7s %d %d:%d:%d", weekday, &day, month, &year, &hour, &minute, &second) != 7) {
    return 0;
  }
  const int mon = monthNumber(month);
  return static_cast<int64_t>(year) * 10000000000LL + static_cast<int64_t>(mon) * 100000000LL +
         static_cast<int64_t>(day) * 1000000LL + static_cast<int64_t>(hour) * 10000LL +
         static_cast<int64_t>(minute) * 100LL + second;
}

int32_t NewsletterSyncClient::currentLocalDateKey() {
  ClockDateTimeCompat now;
  if (!readClockDateTimeCompat(now, SETTINGS.clockUtcOffsetQ)) return 0;
  return now.year * 10000 + static_cast<int32_t>(now.month) * 100 + now.day;
}

std::string NewsletterSyncClient::xmlDecode(std::string text) {
  struct Replacement {
    const char* from;
    const char* to;
  };
  static constexpr Replacement REPLACEMENTS[] = {
      {"&amp;", "&"}, {"&apos;", "'"}, {"&quot;", "\""}, {"&lt;", "<"}, {"&gt;", ">"}};
  for (const auto& replacement : REPLACEMENTS) {
    size_t pos = 0;
    while ((pos = text.find(replacement.from, pos)) != std::string::npos) {
      text.replace(pos, strlen(replacement.from), replacement.to);
      pos += strlen(replacement.to);
    }
  }
  return text;
}

std::string NewsletterSyncClient::stripCdata(std::string text) {
  const char* cdataStart = "<![CDATA[";
  const char* cdataEnd = "]]>";
  if (text.rfind(cdataStart, 0) == 0) {
    text.erase(0, strlen(cdataStart));
  }
  if (text.size() >= strlen(cdataEnd) && text.compare(text.size() - strlen(cdataEnd), strlen(cdataEnd), cdataEnd) == 0) {
    text.erase(text.size() - strlen(cdataEnd));
  }
  return text;
}

bool NewsletterSyncClient::extractTag(const std::string& itemXml, const char* tag, std::string& out) {
  const std::string open = std::string("<") + tag;
  const size_t openStart = itemXml.find(open);
  if (openStart == std::string::npos) return false;
  const size_t openEnd = itemXml.find('>', openStart);
  if (openEnd == std::string::npos) return false;
  const std::string close = std::string("</") + tag + ">";
  const size_t closeStart = itemXml.find(close, openEnd + 1);
  if (closeStart == std::string::npos) return false;
  out = itemXml.substr(openEnd + 1, closeStart - openEnd - 1);
  return true;
}
