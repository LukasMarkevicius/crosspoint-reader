#include "NewsletterConfigStore.h"

#include <cstring>

void NewsletterConfigStore::toJson(JsonDocument& doc) const {
  doc["autoSyncEnabled"] = autoSyncEnabled;
  doc["syncHourLocal"] = syncHour();
  doc["feedUrl"] = feedUrl;
}

bool NewsletterConfigStore::fromJson(JsonVariantConst doc) {
  autoSyncEnabled = (doc["autoSyncEnabled"] | 1) ? 1 : 0;
  syncHourLocal = doc["syncHourLocal"] | 7;
  if (syncHourLocal > 23) syncHourLocal = 7;

  const char* url = doc["feedUrl"] | "";
  strncpy(feedUrl, url, sizeof(feedUrl) - 1);
  feedUrl[sizeof(feedUrl) - 1] = '\0';
  return true;
}

void NewsletterConfigStore::setFeedUrl(const std::string& url) {
  strncpy(feedUrl, url.c_str(), sizeof(feedUrl) - 1);
  feedUrl[sizeof(feedUrl) - 1] = '\0';
}
