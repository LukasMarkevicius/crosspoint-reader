#include "NewsletterDeletedActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

NewsletterDeletedActivity::NewsletterDeletedActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("NewsletterDeleted", renderer, mappedInput) {}

void NewsletterDeletedActivity::onEnter() {
  UiListActivity::onEnter();
  rebuildRows();
}

void NewsletterDeletedActivity::onExit() {
  Activity::onExit();
  rowItems_.clear();
  subtitles_.clear();
  deletedItems_.clear();
}

int NewsletterDeletedActivity::listCount() const { return static_cast<int>(rowItems_.size()); }

const char* NewsletterDeletedActivity::headerTitle() const { return tr(STR_NEWSLETTER_DELETED_TITLE); }

void NewsletterDeletedActivity::rebuildRows() {
  deletedItems_ = NEWSLETTER_STORE.deletedItems();
  subtitles_.clear();
  rowItems_.clear();
  subtitles_.reserve(deletedItems_.size());
  rowItems_.reserve(deletedItems_.size());
  for (size_t i = 0; i < deletedItems_.size(); i++) {
    const auto* entry = deletedItems_[i];
    subtitles_.push_back(entry->author.empty() ? tr(STR_NEWSLETTER_SOURCE_UNKNOWN) : entry->author);
    fui::ListItem item;
    item.label = entry->title.empty() ? tr(STR_UNNAMED) : entry->title.c_str();
    item.subtitle = subtitles_.back().c_str();
    item.value = tr(STR_NEWSLETTER_RESTORE);
    item.icon = listIconFor(UIIcon::Bookmark, 32);
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

void NewsletterDeletedActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (rowItems_.empty()) {
    screen.centeredText(tr(STR_NO_ENTRIES), screen.theme().bodyText);
    return;
  }
  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void NewsletterDeletedActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(deletedItems_.size())) return;
  NEWSLETTER_STORE.restoreDeleted(deletedItems_[index]->guid);
  closeRouting();
  rebuildRows();
  requestUpdate();
}
