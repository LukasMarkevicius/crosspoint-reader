#include "NewsletterListActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <WiFi.h>

#include <memory>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "NewsletterDeletedActivity.h"
#include "NewsletterReaderActivity.h"
#include "NewsletterSettingsActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "newsletters/NewsletterConfigStore.h"
#include "newsletters/NewsletterSyncClient.h"
#include "util/ClockDateTimeCompat.h"

namespace fui = freeink::ui;

NewsletterListActivity::NewsletterListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Newsletters", renderer, mappedInput, /*wantsTouchLongPress=*/true), syncStatus_(tr(STR_NEWSLETTER_STATUS_READY)) {}

void NewsletterListActivity::onEnter() {
  UiListActivity::onEnter();
  rebuildRows();
  autoSyncCheckPending_ = shouldAutoSyncOnOpen();
  const std::string newestUnread = NEWSLETTER_STORE.newestUnreadGuid();
  if (!newestUnread.empty()) {
    for (size_t i = 0; i < visibleItems_.size(); i++) {
      if (visibleItems_[i]->guid == newestUnread) {
        nav.selected = static_cast<int>(StaticRow::FirstNewsletter) + static_cast<int>(i);
        nav.follow(listCount());
        break;
      }
    }
  }
}

void NewsletterListActivity::onExit() {
  Activity::onExit();
  syncPending_ = false;
  autoSyncCheckPending_ = false;
  rowItems_.clear();
  subtitles_.clear();
  badges_.clear();
  visibleItems_.clear();
  staticValues_.clear();
}

void NewsletterListActivity::loop() {
  if (autoSyncCheckPending_) {
    autoSyncCheckPending_ = false;
    ensureWifiThenSync();
    return;
  }
  if (syncPending_) {
    syncPending_ = false;
    syncNow();
    return;
  }
  UiListActivity::loop();
}

void NewsletterListActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  for (int pass = 0; activeNav().consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }
  if (optionPopup_.processRender(renderer, mappedInput)) return;
  drawFooter();
  renderer.displayBuffer();
}

int NewsletterListActivity::listCount() const { return static_cast<int>(rowItems_.size()); }

const char* NewsletterListActivity::headerTitle() const { return tr(STR_NEWSLETTER_LIST_TITLE); }

bool NewsletterListActivity::handleCustomInput() {
  return optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool NewsletterListActivity::handleButtons() {
  if (optionPopup_.isActive()) return true;
  return UiListActivity::handleButtons();
}

void NewsletterListActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

bool NewsletterListActivity::isNewsletterRow(const int index) const {
  return index >= static_cast<int>(StaticRow::FirstNewsletter);
}

const NewsletterEntry* NewsletterListActivity::entryForIndex(const int index) const {
  if (!isNewsletterRow(index)) return nullptr;
  const int newsletterIndex = index - static_cast<int>(StaticRow::FirstNewsletter);
  if (newsletterIndex < 0 || newsletterIndex >= static_cast<int>(visibleItems_.size())) return nullptr;
  return visibleItems_[newsletterIndex];
}

std::string NewsletterListActivity::subtitleFor(const NewsletterEntry& entry) {
  const char* date = entry.pubDate.empty() ? tr(STR_NEWSLETTER_DATE_UNKNOWN) : entry.pubDate.c_str();
  const char* source = entry.author.empty() ? tr(STR_NEWSLETTER_SOURCE_UNKNOWN) : entry.author.c_str();
  return std::string(source) + "  •  " + date;
}

void NewsletterListActivity::rebuildRows() {
  visibleItems_ = NEWSLETTER_STORE.visibleItems();
  staticValues_.clear();
  subtitles_.clear();
  badges_.clear();
  rowItems_.clear();
  staticValues_.reserve(4);
  subtitles_.reserve(visibleItems_.size());
  badges_.reserve(visibleItems_.size());
  rowItems_.reserve(visibleItems_.size() + 4);

  auto addStatic = [this](const char* label, std::string value, const int16_t actionValue, const UIIcon icon) {
    staticValues_.push_back(std::move(value));
    fui::ListItem item;
    item.label = label;
    item.value = staticValues_.back().empty() ? nullptr : staticValues_.back().c_str();
    item.icon = listIconFor(icon, 24);
    item.actionValue = actionValue;
    rowItems_.push_back(item);
  };

  addStatic(tr(STR_NEWSLETTER_SYNC_NOW), syncStatus_, static_cast<int16_t>(StaticRow::SyncNow), UIIcon::Library);
  addStatic(tr(STR_NEWSLETTER_MARK_ALL_UNREAD), "", static_cast<int16_t>(StaticRow::MarkAllUnread), UIIcon::Bookmark);
  addStatic(tr(STR_NEWSLETTER_SETTINGS), "", static_cast<int16_t>(StaticRow::Settings), Settings);
  addStatic(tr(STR_NEWSLETTER_DELETED), NEWSLETTER_STORE.deletedCount() > 0 ? std::to_string(NEWSLETTER_STORE.deletedCount()) : "",
            static_cast<int16_t>(StaticRow::Deleted), UIIcon::Bookmark);

  for (size_t i = 0; i < visibleItems_.size(); i++) {
    const auto* entry = visibleItems_[i];
    subtitles_.push_back(subtitleFor(*entry));
    badges_.push_back(entry->read ? "" : tr(STR_NEWSLETTER_UNREAD_BADGE));
    fui::ListItem item;
    item.label = entry->title.empty() ? tr(STR_UNNAMED) : entry->title.c_str();
    item.subtitle = subtitles_.back().c_str();
    item.value = badges_.back().empty() ? nullptr : badges_.back().c_str();
    item.icon = listIconFor(UIIcon::Library, 32);
    item.actionValue = static_cast<int16_t>(static_cast<int>(StaticRow::FirstNewsletter) + i);
    rowItems_.push_back(item);
  }
}

void NewsletterListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void NewsletterListActivity::openEntry(const std::string& guid) {
  app.clearTapFlash();
  startActivityForResult(std::make_unique<NewsletterReaderActivity>(renderer, mappedInput, guid),
                         [this](const ActivityResult&) {
                           closeRouting();
                           rebuildRows();
                           requestUpdate();
                         });
}

bool NewsletterListActivity::shouldAutoSyncOnOpen() const {
  if (!NEWSLETTER_CONFIG.autoSyncEnabled || !NEWSLETTER_CONFIG.isConfigured()) return false;
  ClockDateTimeCompat now;
  if (!readClockDateTimeCompat(now, SETTINGS.clockUtcOffsetQ)) return false;
  const int32_t todayKey = now.year * 10000 + static_cast<int32_t>(now.month) * 100 + now.day;
  if (NEWSLETTER_STORE.getLastSuccessfulSyncDateKey() >= todayKey) return false;
  return now.hour >= NEWSLETTER_CONFIG.syncHour();
}

void NewsletterListActivity::syncNow() {
  syncStatus_ = tr(STR_NEWSLETTER_STATUS_SYNCING);
  requestUpdateAndWait();
  const auto result = NewsletterSyncClient::sync();
  if (result.error == NewsletterSyncClient::Error::Ok) {
    if (result.imported == 0 && result.updated == 0 && result.skipped == 0) {
      syncStatus_ = tr(STR_NEWSLETTER_SYNC_RESULT_EMPTY);
    } else if (result.skipped > 0) {
      char resultBuf[64];
      snprintf(resultBuf, sizeof(resultBuf), tr(STR_NEWSLETTER_SYNC_RESULT_WITH_SKIPPED), result.imported,
               result.updated, result.skipped);
      syncStatus_ = resultBuf;
    } else {
      char resultBuf[48];
      snprintf(resultBuf, sizeof(resultBuf), tr(STR_NEWSLETTER_SYNC_RESULT), result.imported, result.updated);
      syncStatus_ = resultBuf;
    }
  } else {
    syncStatus_ = NewsletterSyncClient::errorString(result.error);
  }
  closeRouting();
  rebuildRows();
  requestUpdate();
}

void NewsletterListActivity::ensureWifiThenSync() {
  const bool wifiReady = WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
  if (wifiReady) {
    syncPending_ = true;
    requestUpdate();
    return;
  }

  syncStatus_ = tr(STR_NEWSLETTER_WIFI_REQUIRED);
  rebuildRows();
  requestUpdate();
  app.clearTapFlash();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           closeRouting();
                           if (result.isCancelled) {
                             syncStatus_ = tr(STR_NEWSLETTER_STATUS_CANCELLED);
                             rebuildRows();
                             requestUpdate();
                             return;
                           }
                           syncStatus_ = tr(STR_NEWSLETTER_STATUS_CONNECTED);
                           syncPending_ = true;
                           rebuildRows();
                           requestUpdate();
                         });
}

void NewsletterListActivity::activateIndex(const int index) {
  if (index == static_cast<int>(StaticRow::SyncNow)) {
    ensureWifiThenSync();
    return;
  }
  if (index == static_cast<int>(StaticRow::MarkAllUnread)) {
    const int changed = NEWSLETTER_STORE.markAllUnread();
    if (changed < 0) {
      syncStatus_ = tr(STR_NEWSLETTER_SAVE_ERROR);
    } else {
      char statusBuf[48];
      snprintf(statusBuf, sizeof(statusBuf), tr(STR_NEWSLETTER_MARK_ALL_UNREAD_RESULT), changed);
      syncStatus_ = statusBuf;
    }
    closeRouting();
    rebuildRows();
    requestUpdate();
    return;
  }
  if (index == static_cast<int>(StaticRow::Settings)) {
    app.clearTapFlash();
    startActivityForResult(std::make_unique<NewsletterSettingsActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) {
                             closeRouting();
                             rebuildRows();
                             requestUpdate();
                           });
    return;
  }
  if (index == static_cast<int>(StaticRow::Deleted)) {
    app.clearTapFlash();
    startActivityForResult(std::make_unique<NewsletterDeletedActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) {
                             closeRouting();
                             rebuildRows();
                             requestUpdate();
                           });
    return;
  }

  const auto* entry = entryForIndex(index);
  if (!entry) return;
  openEntry(entry->guid);
}

void NewsletterListActivity::onRowLongPress(const int index) {
  const auto* entry = entryForIndex(index);
  if (!entry) return;
  app.clearTapFlash();
  std::vector<std::string> options;
  options.reserve(2);
  options.push_back(entry->read ? tr(STR_NEWSLETTER_MARK_UNREAD) : tr(STR_NEWSLETTER_MARK_READ));
  options.push_back(tr(STR_NEWSLETTER_DELETE_LOCAL));
  optionPopup_.show(StrId::STR_NEWSLETTERS, options, 0, [this, guid = entry->guid, wasRead = entry->read](const int idx) {
    if (idx == 0) {
      if (wasRead) {
        NEWSLETTER_STORE.markUnread(guid);
      } else {
        NEWSLETTER_STORE.markRead(guid);
      }
    } else if (idx == 1) {
      NEWSLETTER_STORE.deleteLocal(guid);
    }
    closeRouting();
    rebuildRows();
    requestUpdate();
  });
  requestUpdate();
}
