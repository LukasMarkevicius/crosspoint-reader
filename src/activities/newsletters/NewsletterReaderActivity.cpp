#include "NewsletterReaderActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "newsletters/NewsletterStore.h"

namespace {

std::vector<std::string> wrappedReaderTitle(const GfxRenderer& renderer, const std::string& title, const Rect& safe,
                                            const ThemeMetrics& metrics) {
  const int maxWidth = safe.width - metrics.contentSidePadding * 2;
  auto lines = renderer.wrappedText(UI_12_FONT_ID, title.c_str(), maxWidth, 2, EpdFontFamily::BOLD);
  if (lines.empty()) lines.push_back(renderer.truncatedText(UI_12_FONT_ID, title.c_str(), maxWidth, EpdFontFamily::BOLD));
  return lines;
}

std::string readerMetaLine(const GfxRenderer& renderer, const std::string& text, const Rect& safe,
                           const ThemeMetrics& metrics) {
  const int maxWidth = safe.width - metrics.contentSidePadding * 2;
  return renderer.truncatedText(UI_10_FONT_ID, text.c_str(), maxWidth);
}

}  // namespace

NewsletterReaderActivity::NewsletterReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   std::string guid)
    : Activity("NewsletterReader", renderer, mappedInput), guid_(std::move(guid)) {}

std::string NewsletterReaderActivity::normalizeBody(const std::string& body) {
  std::string normalized;
  normalized.reserve(body.size() + 32);

  const auto appendBulletBreakIfNeeded = [&normalized]() {
    while (!normalized.empty() && normalized.back() == ' ') normalized.pop_back();
    if (!normalized.empty() && normalized.back() != '\n') normalized.push_back('\n');
  };

  for (size_t i = 0; i < body.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(body[i]);

    if (c == '\r') continue;
    if (c == '\n') {
      if (!normalized.empty() && normalized.back() != '\n') normalized.push_back('\n');
      continue;
    }

    if (c == 0xE2 && i + 2 < body.size() && static_cast<unsigned char>(body[i + 1]) == 0x80 &&
        static_cast<unsigned char>(body[i + 2]) == 0xA2) {
      appendBulletBreakIfNeeded();
      normalized.append("\xE2\x80\xA2 ");
      i += 2;
      continue;
    }

    if ((c == '.' || c == '!' || c == '?') && i + 3 < body.size() &&
        static_cast<unsigned char>(body[i + 1]) == 0xE2 && static_cast<unsigned char>(body[i + 2]) == 0x80 &&
        static_cast<unsigned char>(body[i + 3]) == 0xA2) {
      normalized.push_back(static_cast<char>(c));
      normalized.push_back('\n');
      normalized.append("\xE2\x80\xA2 ");
      i += 3;
      continue;
    }

    if ((c == '.' || c == ',' || c == ':' || c == ';' || c == '!' || c == '?') && i + 1 < body.size()) {
      normalized.push_back(static_cast<char>(c));
      const unsigned char next = static_cast<unsigned char>(body[i + 1]);
      if (next != ' ' && next != '\n' && next != '\r' && next != 0xE2) {
        normalized.push_back(' ');
      }
      continue;
    }

    if (std::isspace(c)) {
      if (!normalized.empty() && normalized.back() != ' ' && normalized.back() != '\n') {
        normalized.push_back(' ');
      }
      continue;
    }

    normalized.push_back(static_cast<char>(c));
  }

  while (!normalized.empty() && (normalized.back() == ' ' || normalized.back() == '\n')) {
    normalized.pop_back();
  }

  return normalized;
}

void NewsletterReaderActivity::onEnter() {
  Activity::onEnter();
  loadContent();
  rebuildPages();
  requestUpdate();
}

void NewsletterReaderActivity::loadContent() {
  const auto* entry = NEWSLETTER_STORE.getByGuid(guid_);
  if (!entry) return;
  title_ = entry->title.empty() ? tr(STR_UNNAMED) : entry->title;
  source_ = entry->author.empty() ? tr(STR_NEWSLETTER_SOURCE_UNKNOWN) : entry->author;
  date_ = entry->pubDate;
  body_ = NEWSLETTER_STORE.readBody(guid_);
  body_ = normalizeBody(body_);
  if (body_.empty()) body_ = tr(STR_NEWSLETTER_BODY_EMPTY);
}

void NewsletterReaderActivity::rebuildPages() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, false, true);
  const auto titleLines = wrappedReaderTitle(renderer, title_, safe, metrics);
  const auto sourceLine = readerMetaLine(renderer, source_, safe, metrics);
  const auto dateLine = readerMetaLine(renderer, date_, safe, metrics);
  const int titleHeight = static_cast<int>(titleLines.size()) * renderer.getLineHeight(UI_12_FONT_ID);
  int metaLineCount = 0;
  if (!sourceLine.empty()) metaLineCount++;
  if (!dateLine.empty()) metaLineCount++;
  const int metaHeight = metaLineCount * renderer.getLineHeight(UI_10_FONT_ID);
  const int bodyTop = metrics.topPadding + metrics.headerHeight + titleHeight + metaHeight + metrics.verticalSpacing * 4;
  const int bodyBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int maxLines = std::max(1, (bodyBottom - bodyTop) / std::max(1, lineHeight));
  std::vector<std::string> lines;
  lines.reserve(64);
  const int maxWidth = safe.width - metrics.contentSidePadding * 2;
  size_t start = 0;
  while (start <= body_.size()) {
    const size_t end = body_.find('\n', start);
    const std::string paragraph = body_.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (paragraph.empty()) {
      if (!lines.empty() && !lines.back().empty()) lines.push_back("");
    } else {
      const auto wrapped = renderer.wrappedText(UI_12_FONT_ID, paragraph.c_str(), maxWidth, 256, EpdFontFamily::REGULAR);
      if (wrapped.empty()) {
        lines.push_back(paragraph);
      } else {
        lines.insert(lines.end(), wrapped.begin(), wrapped.end());
      }
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }

  pages_.clear();
  pages_.reserve((lines.size() / std::max(1, maxLines)) + 1);
  std::vector<std::string> page;
  page.reserve(maxLines);
  int lineCount = 0;
  for (const auto& line : lines) {
    page.push_back(line);
    lineCount++;
    if (lineCount >= maxLines) {
      pages_.push_back(page);
      page.clear();
      lineCount = 0;
    }
  }
  if (!page.empty() || pages_.empty()) pages_.push_back(page);
  if (currentPage_ >= static_cast<int>(pages_.size())) currentPage_ = static_cast<int>(pages_.size()) - 1;
  if (currentPage_ < 0) currentPage_ = 0;
}

bool NewsletterReaderActivity::openNeighborNewsletter(const bool forward) {
  const std::string nextGuid =
      forward ? NEWSLETTER_STORE.nextVisibleGuidAfter(guid_) : NEWSLETTER_STORE.previousVisibleGuidBefore(guid_);
  if (nextGuid.empty()) return false;
  NEWSLETTER_STORE.markRead(guid_);
  guid_ = nextGuid;
  currentPage_ = 0;
  loadContent();
  rebuildPages();
  requestUpdate();
  return true;
}

void NewsletterReaderActivity::goNext() {
  if (currentPage_ + 1 < static_cast<int>(pages_.size())) {
    currentPage_++;
    requestUpdate();
    return;
  }
  if (!openNeighborNewsletter(true)) {
    NEWSLETTER_STORE.markRead(guid_);
    finish();
  }
}

void NewsletterReaderActivity::goPrevious() {
  if (currentPage_ > 0) {
    currentPage_--;
    requestUpdate();
    return;
  }
  if (!openNeighborNewsletter(false)) finish();
}

void NewsletterReaderActivity::loop() {
  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    const int width = renderer.getScreenWidth();
    if (tx < width / 3) {
      goPrevious();
      return;
    }
    if (tx > (width * 2) / 3) {
      goNext();
      return;
    }
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    goNext();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    goPrevious();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    goNext();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    goPrevious();
    return;
  }
}

bool NewsletterReaderActivity::handleHomeGesture() {
  NEWSLETTER_STORE.markRead(guid_);
  finish();
  return true;
}

void NewsletterReaderActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, false, true);
  const auto titleLines = wrappedReaderTitle(renderer, title_, safe, metrics);
  const auto sourceLine = readerMetaLine(renderer, source_, safe, metrics);
  const auto dateLine = readerMetaLine(renderer, date_, safe, metrics);
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_NEWSLETTERS));

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  for (const auto& line : titleLines) {
    renderer.drawCenteredText(UI_12_FONT_ID, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID);
  }
  y += metrics.verticalSpacing;
  if (!sourceLine.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, sourceLine.c_str(), true);
    y += renderer.getLineHeight(UI_10_FONT_ID);
  }
  if (!dateLine.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, dateLine.c_str(), true);
    y += renderer.getLineHeight(UI_10_FONT_ID);
  }
  y += metrics.verticalSpacing * 2;

  const int left = safe.x + metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  if (!pages_.empty()) {
    const auto& pageLines = pages_[currentPage_];
    for (size_t i = 0; i < pageLines.size(); i++) {
      if (pageLines[i].empty()) continue;
      renderer.drawText(UI_12_FONT_ID, left, y + static_cast<int>(i) * lineHeight, pageLines[i].c_str());
    }
  }

  char pageBuf[32];
  snprintf(pageBuf, sizeof(pageBuf), "%d/%d", currentPage_ + 1, static_cast<int>(pages_.size()));
  renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight() - metrics.buttonHintsHeight - 6, pageBuf, true);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_NEWSLETTER_OPEN_NEXT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
