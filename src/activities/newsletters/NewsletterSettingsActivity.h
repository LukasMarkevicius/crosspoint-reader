#pragma once

#include <string>

#include "activities/UiListActivity.h"

class NewsletterSettingsActivity final : public UiListActivity {
 public:
  explicit NewsletterSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  static constexpr int MENU_ITEMS = 3;

  std::string rowValues_[MENU_ITEMS];
  freeink::ui::ListItem rowItems_[MENU_ITEMS]{};

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
};
