#pragma once

#include <functional>
#include <string>

#include "NewsletterStore.h"

class NewsletterSyncClient {
 public:
  enum class Error {
    Ok,
    NotConfigured,
    NoWifi,
    BadUrl,
    Network,
    Parse,
    Save,
    LowMemory,
  };

  using ProgressFn = std::function<void(int imported, int updated)>;

  struct Result {
    Error error = Error::Ok;
    int imported = 0;
    int updated = 0;
    int skipped = 0;
  };

  static Result sync(const ProgressFn& progress = nullptr);
  static const char* errorString(Error error);
  static int64_t parseRfc822Date(const std::string& text);
  static int32_t currentLocalDateKey();
  static std::string xmlDecode(std::string text);
  static std::string stripCdata(std::string text);
  static bool extractTag(const std::string& itemXml, const char* tag, std::string& out);
};
