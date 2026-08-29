#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"

class NewsletterReaderActivity final : public Activity {
 public:
  explicit NewsletterReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string guid);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return true; }
  bool handleHomeGesture() override;

 private:
  std::string guid_;
  std::string title_;
  std::string source_;
  std::string date_;
  std::string body_;
  std::vector<std::vector<std::string>> pages_;
  int currentPage_ = 0;

  static std::string normalizeBody(const std::string& body);
  void loadContent();
  void rebuildPages();
  bool openNeighborNewsletter(bool forward);
  void goNext();
  void goPrevious();
};
