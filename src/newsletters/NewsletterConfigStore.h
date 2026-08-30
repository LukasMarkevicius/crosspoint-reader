#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <cstring>
#include <string>

class NewsletterConfigStore : public PersistableStore<NewsletterConfigStore> {
 private:
  NewsletterConfigStore() = default;
  friend class PersistableStore<NewsletterConfigStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/newsletters_config.json"; }

  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool isConfigured() const { return feedUrl[0] != '\0'; }
  int syncHour() const { return syncHourLocal <= 23 ? syncHourLocal : 0; }
  void setSyncHour(const uint8_t hour) { syncHourLocal = hour <= 23 ? hour : 0; }
  std::string getFeedUrl() const { return feedUrl; }
  void setFeedUrl(const std::string& url);

  uint8_t autoSyncEnabled = 1;
  uint8_t syncHourLocal = 7;
  char feedUrl[256] = "";
};

#define NEWSLETTER_CONFIG NewsletterConfigStore::getInstance()
