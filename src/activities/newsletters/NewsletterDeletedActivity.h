#pragma once

#include <vector>

#include "activities/UiListActivity.h"
#include "newsletters/NewsletterStore.h"

class NewsletterDeletedActivity final : public UiListActivity {
 public:
  explicit NewsletterDeletedActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 private:
  std::vector<const NewsletterEntry*> deletedItems_;
  std::vector<std::string> subtitles_;
  std::vector<freeink::ui::ListItem> rowItems_;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void rebuildRows();
};
