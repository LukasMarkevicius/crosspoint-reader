#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>
#include <vector>

struct NewsletterFeedItem {
  std::string guid;
  std::string title;
  std::string author;
  std::string pubDate;
  std::string description;
  int64_t sortKey = 0;
  uint32_t contentHash = 0;
};

struct NewsletterEntry {
  std::string guid;
  std::string title;
  std::string author;
  std::string pubDate;
  int64_t sortKey = 0;
  uint32_t contentHash = 0;
  bool read = true;
  bool deleted = false;
};

class NewsletterStore : public PersistableStore<NewsletterStore> {
 private:
  NewsletterStore();
  friend class PersistableStore<NewsletterStore>;

  std::vector<NewsletterEntry> items;
  bool initialImportDone = false;
  int32_t lastSuccessfulSyncDateKey = 0;
  int32_t lastSeenFeedDateKey = 0;

  static bool newerFirst(const NewsletterEntry& a, const NewsletterEntry& b) { return a.sortKey > b.sortKey; }
  std::vector<NewsletterEntry>::iterator findByGuid(const std::string& guid);
  std::vector<NewsletterEntry>::const_iterator findByGuid(const std::string& guid) const;
  static std::string itemPathForGuid(const std::string& guid);

 public:
  static constexpr size_t MAX_RETAINED_NEWSLETTERS = 120;
  static const char* getFilePath() { return "/.crosspoint/newsletters_index.json"; }
  static constexpr const char* ITEMS_DIR = "/.crosspoint/newsletters/items";
  static constexpr const char* FEED_ARCHIVE_PATH = "/.crosspoint/newsletters/feed.xml";
  static uint32_t fnv1a(const std::string& text);

  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool ensureDirectories() const;
  bool writeBody(const std::string& guid, const std::string& text) const;
  std::string readBody(const std::string& guid) const;
  void pruneRetainedItems();
  void reserveForSync(size_t expectedCount);

  const std::vector<NewsletterEntry>& allItems() const { return items; }
  int unreadCount() const;
  int deletedCount() const;
  std::vector<const NewsletterEntry*> visibleItems() const;
  std::vector<const NewsletterEntry*> deletedItems() const;
  const NewsletterEntry* getByGuid(const std::string& guid) const;
  std::string newestUnreadGuid() const;
  std::string nextVisibleGuidAfter(const std::string& guid) const;
  std::string previousVisibleGuidBefore(const std::string& guid) const;
  std::string nextUnreadGuidAfter(const std::string& guid) const;

  bool markRead(const std::string& guid);
  bool markUnread(const std::string& guid);
  int markAllUnread();
  bool deleteLocal(const std::string& guid);
  bool restoreDeleted(const std::string& guid);

  bool applyFeedItem(const NewsletterFeedItem& item);
  bool finishSync(int32_t localDateKey);
  bool needsFirstImport() const { return !initialImportDone; }
  void markInitialImportDone() { initialImportDone = true; }
  int32_t getLastSuccessfulSyncDateKey() const { return lastSuccessfulSyncDateKey; }
  int32_t getLastSeenFeedDateKey() const { return lastSeenFeedDateKey; }
};

#define NEWSLETTER_STORE NewsletterStore::getInstance()
