#include "NewsletterStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

namespace {
String toArduinoString(const std::string& text) { return String(text.c_str()); }

std::string extractBodyFromFeedArchive(const std::string& guid) {
  if (guid.empty()) return "";

  HalFile feedFile;
  if (!Storage.openFileForRead("NEWS", NewsletterStore::FEED_ARCHIVE_PATH, feedFile) || !feedFile.isOpen()) {
    return "";
  }

  char chunk[1024];
  std::string tail;
  tail.reserve(4096);

  auto extractTag = [](const std::string& itemXml, const char* tag, std::string& out) {
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
  };

  auto stripCdata = [](std::string text) {
    const char* cdataStart = "<![CDATA[";
    const char* cdataEnd = "]]>";
    if (text.rfind(cdataStart, 0) == 0) {
      text.erase(0, strlen(cdataStart));
    }
    if (text.size() >= strlen(cdataEnd) &&
        text.compare(text.size() - strlen(cdataEnd), strlen(cdataEnd), cdataEnd) == 0) {
      text.erase(text.size() - strlen(cdataEnd));
    }
    return text;
  };

  auto xmlDecode = [](std::string text) {
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
  };

  while (feedFile.available() > 0) {
    const int bytesRead = feedFile.read(chunk, sizeof(chunk));
    if (bytesRead <= 0) break;

    tail.append(chunk, static_cast<size_t>(bytesRead));
    size_t itemStart = 0;
    while ((itemStart = tail.find("<item>")) != std::string::npos) {
      const size_t itemEnd = tail.find("</item>", itemStart);
      if (itemEnd == std::string::npos) break;
      const size_t itemCloseEnd = itemEnd + strlen("</item>");
      const std::string itemXml = tail.substr(itemStart, itemCloseEnd - itemStart);
      tail.erase(0, itemCloseEnd);

      std::string itemGuid;
      if (!extractTag(itemXml, "guid", itemGuid)) continue;
      itemGuid = xmlDecode(stripCdata(itemGuid));
      if (itemGuid != guid) continue;

      std::string body;
      if (!extractTag(itemXml, "description", body)) return "";
      return xmlDecode(stripCdata(body));
    }

    if (tail.size() > 8192) {
      const size_t lastItemOpen = tail.rfind("<item>");
      if (lastItemOpen == std::string::npos) {
        tail.clear();
      } else if (lastItemOpen > 0) {
        tail.erase(0, lastItemOpen);
      }
    }
  }

  return "";
}
}

NewsletterStore::NewsletterStore() { items.reserve(MAX_RETAINED_NEWSLETTERS); }

void NewsletterStore::toJson(JsonDocument& doc) const {
  doc["initialImportDone"] = initialImportDone;
  doc["lastSuccessfulSyncDateKey"] = lastSuccessfulSyncDateKey;
  doc["lastSeenFeedDateKey"] = lastSeenFeedDateKey;
  JsonArray arr = doc["items"].to<JsonArray>();
  for (const auto& item : items) {
    JsonObject obj = arr.add<JsonObject>();
    obj["guid"] = item.guid;
    obj["title"] = item.title;
    obj["author"] = item.author;
    obj["pubDate"] = item.pubDate;
    obj["sortKey"] = item.sortKey;
    obj["contentHash"] = item.contentHash;
    obj["read"] = item.read;
    obj["deleted"] = item.deleted;
  }
}

bool NewsletterStore::fromJson(JsonVariantConst doc) {
  initialImportDone = doc["initialImportDone"] | false;
  lastSuccessfulSyncDateKey = doc["lastSuccessfulSyncDateKey"] | 0;
  lastSeenFeedDateKey = doc["lastSeenFeedDateKey"] | 0;
  items.clear();

  JsonArrayConst arr = doc["items"].as<JsonArrayConst>();
  items.reserve(arr.size());
  for (JsonObjectConst obj : arr) {
    NewsletterEntry item;
    item.guid = obj["guid"] | "";
    item.title = obj["title"] | "";
    item.author = obj["author"] | "";
    item.pubDate = obj["pubDate"] | "";
    item.sortKey = obj["sortKey"] | static_cast<int64_t>(0);
    item.contentHash = obj["contentHash"] | 0;
    item.read = obj["read"] | true;
    item.deleted = obj["deleted"] | false;
    if (!item.guid.empty()) {
      items.push_back(std::move(item));
    }
  }
  std::sort(items.begin(), items.end(), newerFirst);
  pruneRetainedItems();
  return true;
}

std::vector<NewsletterEntry>::iterator NewsletterStore::findByGuid(const std::string& guid) {
  return std::find_if(items.begin(), items.end(), [&](const NewsletterEntry& item) { return item.guid == guid; });
}

std::vector<NewsletterEntry>::const_iterator NewsletterStore::findByGuid(const std::string& guid) const {
  return std::find_if(items.begin(), items.end(), [&](const NewsletterEntry& item) { return item.guid == guid; });
}

std::string NewsletterStore::itemPathForGuid(const std::string& guid) { return std::string(ITEMS_DIR) + "/" + guid + ".txt"; }

uint32_t NewsletterStore::fnv1a(const std::string& text) {
  uint32_t hash = 2166136261u;
  for (const unsigned char c : text) {
    hash ^= c;
    hash *= 16777619u;
  }
  return hash;
}

bool NewsletterStore::ensureDirectories() const {
  return Storage.ensureDirectoryExists("/.crosspoint") && Storage.ensureDirectoryExists("/.crosspoint/newsletters") &&
         Storage.ensureDirectoryExists(ITEMS_DIR);
}

bool NewsletterStore::writeBody(const std::string& guid, const std::string& text) const {
  if (!ensureDirectories()) return false;
  return Storage.writeFile(itemPathForGuid(guid).c_str(), toArduinoString(text));
}

std::string NewsletterStore::readBody(const std::string& guid) const {
  const String text = Storage.readFile(itemPathForGuid(guid).c_str());
  if (!text.isEmpty()) return text.c_str();
  return extractBodyFromFeedArchive(guid);
}

int NewsletterStore::unreadCount() const {
  int count = 0;
  for (const auto& item : items) {
    if (!item.deleted && !item.read) count++;
  }
  return count;
}

int NewsletterStore::deletedCount() const {
  int count = 0;
  for (const auto& item : items) {
    if (item.deleted) count++;
  }
  return count;
}

std::vector<const NewsletterEntry*> NewsletterStore::visibleItems() const {
  std::vector<const NewsletterEntry*> out;
  out.reserve(items.size());
  for (const auto& item : items) {
    if (!item.deleted) out.push_back(&item);
  }
  return out;
}

std::vector<const NewsletterEntry*> NewsletterStore::deletedItems() const {
  std::vector<const NewsletterEntry*> out;
  out.reserve(items.size());
  for (const auto& item : items) {
    if (item.deleted) out.push_back(&item);
  }
  return out;
}

const NewsletterEntry* NewsletterStore::getByGuid(const std::string& guid) const {
  const auto it = findByGuid(guid);
  return it == items.end() ? nullptr : &(*it);
}

std::string NewsletterStore::newestUnreadGuid() const {
  for (const auto& item : items) {
    if (!item.deleted && !item.read) return item.guid;
  }
  return "";
}

std::string NewsletterStore::nextVisibleGuidAfter(const std::string& guid) const {
  bool found = guid.empty();
  for (const auto& item : items) {
    if (item.deleted) continue;
    if (found) return item.guid;
    if (item.guid == guid) found = true;
  }
  return "";
}

std::string NewsletterStore::previousVisibleGuidBefore(const std::string& guid) const {
  std::string previousGuid;
  for (const auto& item : items) {
    if (item.deleted) continue;
    if (item.guid == guid) return previousGuid;
    previousGuid = item.guid;
  }
  return "";
}

std::string NewsletterStore::nextUnreadGuidAfter(const std::string& guid) const {
  bool found = guid.empty();
  for (const auto& item : items) {
    if (item.deleted) continue;
    if (found && !item.read) return item.guid;
    if (item.guid == guid) found = true;
  }
  return "";
}

bool NewsletterStore::markRead(const std::string& guid) {
  const auto it = findByGuid(guid);
  if (it == items.end()) return false;
  if (!it->read) {
    it->read = true;
    return saveToFile();
  }
  return true;
}

bool NewsletterStore::markUnread(const std::string& guid) {
  const auto it = findByGuid(guid);
  if (it == items.end()) return false;
  if (it->read) {
    it->read = false;
    return saveToFile();
  }
  return true;
}

int NewsletterStore::markAllUnread() {
  int changed = 0;
  for (auto& item : items) {
    if (item.deleted || !item.read) continue;
    item.read = false;
    changed++;
  }
  if (changed > 0 && !saveToFile()) {
    LOG_ERR("NEWS", "Failed to save mark-all-unread state");
    return -1;
  }
  return changed;
}

bool NewsletterStore::deleteLocal(const std::string& guid) {
  const auto it = findByGuid(guid);
  if (it == items.end()) return false;
  if (!it->deleted) {
    it->deleted = true;
    return saveToFile();
  }
  return true;
}

bool NewsletterStore::restoreDeleted(const std::string& guid) {
  const auto it = findByGuid(guid);
  if (it == items.end()) return false;
  if (it->deleted) {
    it->deleted = false;
    return saveToFile();
  }
  return true;
}

bool NewsletterStore::applyFeedItem(const NewsletterFeedItem& item) {
  if (item.guid.empty()) return false;
  const auto it = findByGuid(item.guid);
  if (it == items.end()) {
    NewsletterEntry entry;
    entry.guid = item.guid;
    entry.title = item.title;
    entry.author = item.author;
    entry.pubDate = item.pubDate;
    entry.sortKey = item.sortKey;
    entry.contentHash = item.contentHash;
    entry.read = false;
    entry.deleted = false;
    const bool shouldPersistBody = initialImportDone;
    if (shouldPersistBody && !writeBody(item.guid, item.description)) {
      LOG_ERR("NEWS", "Failed to write newsletter body for %s", item.guid.c_str());
      return false;
    }
    items.push_back(std::move(entry));
    return true;
  }

  if (it->deleted) {
    return false;
  }

  it->title = item.title;
  it->author = item.author;
  it->pubDate = item.pubDate;
  it->sortKey = item.sortKey;
  if (!it->read && it->contentHash != item.contentHash) {
    if (!writeBody(item.guid, item.description)) {
      LOG_ERR("NEWS", "Failed to update newsletter body for %s", item.guid.c_str());
      return false;
    }
    it->contentHash = item.contentHash;
  }
  if (it->read) {
    it->contentHash = item.contentHash;
  }
  return true;
}

bool NewsletterStore::finishSync(const int32_t localDateKey) {
  lastSuccessfulSyncDateKey = localDateKey;
  if (!initialImportDone) {
    initialImportDone = true;
  }
  std::sort(items.begin(), items.end(), newerFirst);
  pruneRetainedItems();
  return saveToFile();
}

void NewsletterStore::pruneRetainedItems() {
  if (items.size() <= MAX_RETAINED_NEWSLETTERS) return;
  std::sort(items.begin(), items.end(), newerFirst);
  items.erase(items.begin() + static_cast<std::ptrdiff_t>(MAX_RETAINED_NEWSLETTERS), items.end());
}

void NewsletterStore::reserveForSync(const size_t expectedCount) {
  const size_t desired = std::min(expectedCount, MAX_RETAINED_NEWSLETTERS);
  if (desired > items.capacity()) items.reserve(desired);
}
