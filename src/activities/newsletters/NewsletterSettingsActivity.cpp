#include "NewsletterSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <memory>

#include "MappedInputManager.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "newsletters/NewsletterConfigStore.h"

namespace fui = freeink::ui;

namespace {
constexpr int MENU_ITEMS = 3;
const StrId menuNames[MENU_ITEMS] = {StrId::STR_NEWSLETTER_FEED_URL, StrId::STR_NEWSLETTER_AUTO_SYNC,
                                     StrId::STR_NEWSLETTER_SYNC_HOUR};
}

NewsletterSettingsActivity::NewsletterSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("NewsletterSettings", renderer, mappedInput) {
  for (int i = 0; i < MENU_ITEMS; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

int NewsletterSettingsActivity::listCount() const { return MENU_ITEMS; }

const char* NewsletterSettingsActivity::headerTitle() const { return tr(STR_NEWSLETTER_SETTINGS); }

void NewsletterSettingsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (index == 0) {
    const std::string prefill = NEWSLETTER_CONFIG.getFeedUrl().empty() ? "https://" : NEWSLETTER_CONFIG.getFeedUrl();
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_NEWSLETTER_FEED_URL),
                                                                   prefill, 255, InputType::Url),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               const std::string urlToSave =
                                   (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
                               NEWSLETTER_CONFIG.setFeedUrl(urlToSave);
                               NEWSLETTER_CONFIG.saveToFile();
                               requestUpdate();
                             }
                           });
    return;
  }
  if (index == 1) {
    NEWSLETTER_CONFIG.autoSyncEnabled = NEWSLETTER_CONFIG.autoSyncEnabled ? 0 : 1;
    NEWSLETTER_CONFIG.saveToFile();
    requestUpdate();
    return;
  }
  if (index == 2) {
    startActivityForResult(std::make_unique<IntervalSelectionActivity>(
                               renderer, mappedInput, "NewsletterSyncHour", StrId::STR_NEWSLETTER_SYNC_HOUR,
                               NEWSLETTER_CONFIG.syncHour(), 0, 23, 1, 1, StrId::STR_NEWSLETTER_SYNC_HOUR_FORMAT,
                               false, StrId::STR_NONE_OPT),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& interval = std::get<IntervalResult>(result.data);
                               NEWSLETTER_CONFIG.setSyncHour(static_cast<uint8_t>(interval.value));
                               NEWSLETTER_CONFIG.saveToFile();
                               requestUpdate();
                             }
                           });
  }
}

void NewsletterSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  rowValues_[0] = NEWSLETTER_CONFIG.getFeedUrl().empty() ? tr(STR_NOT_SET) : NEWSLETTER_CONFIG.getFeedUrl();
  rowValues_[1] = NEWSLETTER_CONFIG.autoSyncEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  char hourBuf[16];
  snprintf(hourBuf, sizeof(hourBuf), tr(STR_NEWSLETTER_SYNC_HOUR_FORMAT),
           static_cast<unsigned>(NEWSLETTER_CONFIG.syncHour()));
  rowValues_[2] = hourBuf;

  for (int i = 0; i < MENU_ITEMS; i++) {
    rowItems_[i].value = rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEMS);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
