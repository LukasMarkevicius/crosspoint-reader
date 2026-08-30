#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "newsletters/NewsletterStore.h"

class NewsletterListActivity final : public UiListActivity {
 public:
  explicit NewsletterListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class StaticRow : int16_t {
    SyncNow = 0,
    MarkAllUnread = 1,
    Settings = 2,
    Deleted = 3,
    FirstNewsletter = 4,
  };

  std::vector<const NewsletterEntry*> visibleItems_;
  std::vector<std::string> staticValues_;
  std::vector<std::string> subtitles_;
  std::vector<std::string> badges_;
  std::vector<freeink::ui::ListItem> rowItems_;
  OptionPopup optionPopup_;
  std::string syncStatus_;
  bool syncPending_ = false;
  bool autoSyncCheckPending_ = false;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  void rebuildRows();
  bool isNewsletterRow(int index) const;
  const NewsletterEntry* entryForIndex(int index) const;
  void openEntry(const std::string& guid);
  void syncNow();
  void ensureWifiThenSync();
  bool shouldAutoSyncOnOpen() const;
  static std::string subtitleFor(const NewsletterEntry& entry);
};
