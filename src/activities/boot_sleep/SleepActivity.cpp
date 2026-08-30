#include "SleepActivity.h"

#include <Epub.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <PNGdec.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "RecentBooksStore.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "newsletters/NewsletterStore.h"
#include "reading_stats/BookReadingStats.h"
#include "reading_stats/GlobalReadingStats.h"
#include "images/Logo120.h"
#include "images/MoonIcon.h"
#include "util/ClockDateTimeCompat.h"

namespace {

// Kept separate from /sleep.bmp and /.sleep so alpha-overlay art does not mix with full-screen wallpapers.
constexpr char TRANSPARENT_SLEEP_ROOT_BMP[] = "/sleep-overlay.bmp";
constexpr char TRANSPARENT_SLEEP_ROOT_PNG[] = "/sleep-overlay.png";
constexpr char TRANSPARENT_SLEEP_DIR[] = "/.sleep-overlay";
constexpr char TRANSPARENT_SLEEP_LEGACY_DIR[] = "/sleep-overlay";
constexpr size_t MAX_SLEEP_FILE_NAME_LEN = 256;
constexpr uint8_t MIN_VISIBLE_ALPHA = 8;
constexpr const char* WEEKDAY_NAMES[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY",
                                         "THURSDAY", "FRIDAY", "SATURDAY"};
constexpr const char* MONTH_NAMES[] = {"JANUARY",   "FEBRUARY", "MARCH",    "APRIL",
                                       "MAY",       "JUNE",     "JULY",     "AUGUST",
                                       "SEPTEMBER", "OCTOBER",  "NOVEMBER", "DECEMBER"};
constexpr const char* CALENDAR_HEADERS[] = {"M", "T", "W", "T", "F", "S", "S"};

struct BitmapPlacement {
  int x = 0;
  int y = 0;
  float cropX = 0.0f;
  float cropY = 0.0f;
};

struct OverlayBmpInfo {
  int width = 0;
  int height = 0;
  bool topDown = false;
  uint32_t dataOffset = 0;
  uint32_t rowBytes = 0;
};

struct SleepReadingStatsSummary {
  std::string currentBookPath;
  std::string currentBookTitle;
  std::string currentBookAuthor;
  BookReadingStats currentBookStats;
  GlobalReadingStats globalStats;
  bool hasBookContext = false;
};

bool isLeapYear(const uint16_t year) {
  if ((year % 4) != 0) return false;
  if ((year % 100) != 0) return true;
  return (year % 400) == 0;
}

uint8_t sleepScreenDaysInMonth(const uint16_t year, const uint8_t month) {
  static constexpr uint8_t DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 31;
  if (month == 2 && isLeapYear(year)) return 29;
  return DAYS[month - 1];
}

const RecentBook* findRecentBookForPath(const std::string& path) {
  const auto& recentBooks = RECENT_BOOKS.getBooks();
  const auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                               [&path](const RecentBook& book) { return book.path == path; });
  return it == recentBooks.end() ? nullptr : &(*it);
}

std::string deriveReadingStatsCachePath(const std::string& bookPath) {
  if (bookPath.empty()) return {};
  if (FsHelpers::hasXtcExtension(bookPath)) {
    Xtc xtc(bookPath, "/.crosspoint");
    return xtc.getCachePath();
  }
  if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    Txt txt(bookPath, "/.crosspoint");
    return txt.getCachePath();
  }
  Epub epub(bookPath, "/.crosspoint");
  return epub.getCachePath();
}

SleepReadingStatsSummary loadSleepReadingStatsSummary() {
  SleepReadingStatsSummary summary;
  summary.globalStats = GlobalReadingStats::load();

  if (!APP_STATE.openEpubPath.empty() && Storage.exists(APP_STATE.openEpubPath.c_str())) {
    summary.currentBookPath = APP_STATE.openEpubPath;
  } else {
    const auto& recentBooks = RECENT_BOOKS.getBooks();
    if (!recentBooks.empty()) summary.currentBookPath = recentBooks[0].path;
  }

  if (summary.currentBookPath.empty()) return summary;

  if (const RecentBook* recentBook = findRecentBookForPath(summary.currentBookPath)) {
    summary.currentBookTitle = recentBook->title;
    summary.currentBookAuthor = recentBook->author;
  } else {
    const RecentBook fallbackBook = RECENT_BOOKS.getDataFromBook(summary.currentBookPath);
    summary.currentBookTitle = fallbackBook.title;
    summary.currentBookAuthor = fallbackBook.author;
  }

  if (summary.currentBookTitle.empty()) summary.currentBookTitle = summary.currentBookPath;
  summary.hasBookContext = !summary.currentBookTitle.empty();

  const std::string cachePath = deriveReadingStatsCachePath(summary.currentBookPath);
  if (!cachePath.empty()) summary.currentBookStats = BookReadingStats::load(cachePath);
  return summary;
}

void drawSleepMetric(const GfxRenderer& renderer, const int centerX, const int valueY, const char* value, const char* label) {
  const auto valueBounds = renderer.getTextBounds(UI_12_FONT_ID, value, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, centerX - valueBounds.width / 2, valueY, value, true, EpdFontFamily::BOLD);

  const auto labelBounds = renderer.getTextBounds(SMALL_FONT_ID, label);
  renderer.drawText(SMALL_FONT_ID, centerX - labelBounds.width / 2, valueY + renderer.getLineHeight(UI_12_FONT_ID) + 4, label, true);
}

void drawCircleOutline(GfxRenderer& renderer, const int centerX, const int centerY, const int radius, const int lineWidth) {
  const int diameter = radius * 2;
  renderer.drawRoundedRect(centerX - radius, centerY - radius, diameter, diameter, lineWidth, radius, true);
}

void drawFilledCircle(GfxRenderer& renderer, const int centerX, const int centerY, const int radius) {
  for (int dy = -radius; dy <= radius; dy++) {
    const int span = static_cast<int>(std::sqrt(radius * radius - dy * dy));
    renderer.drawLine(centerX - span, centerY + dy, centerX + span, centerY + dy, true);
  }
}

uint16_t readLE16(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  return static_cast<uint16_t>(b0) | (static_cast<uint16_t>(b1) << 8);
}

uint32_t readLE32(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const int c2 = file.read();
  const int c3 = file.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  const auto b2 = static_cast<uint8_t>(c2 < 0 ? 0 : c2);
  const auto b3 = static_cast<uint8_t>(c3 < 0 ? 0 : c3);
  return static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8) | (static_cast<uint32_t>(b2) << 16) |
         (static_cast<uint32_t>(b3) << 24);
}

uint32_t readBE32(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const int c2 = file.read();
  const int c3 = file.read();
  if (c0 < 0 || c1 < 0 || c2 < 0 || c3 < 0) return 0;
  return (static_cast<uint32_t>(c0) << 24) | (static_cast<uint32_t>(c1) << 16) | (static_cast<uint32_t>(c2) << 8) |
         static_cast<uint32_t>(c3);
}

bool isValidPngHeader(HalFile& file) {
  static constexpr uint8_t PNG_SIGNATURE[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  static constexpr uint32_t MAX_SOURCE_PIXELS = 2048u * 1536u;
  uint8_t signature[8];
  if (!file.seek(0) || file.read(signature, sizeof(signature)) != static_cast<int>(sizeof(signature)) ||
      !std::equal(std::begin(signature), std::end(signature), std::begin(PNG_SIGNATURE))) {
    return false;
  }

  const uint32_t ihdrLength = readBE32(file);
  char chunkType[4];
  if (file.read(reinterpret_cast<uint8_t*>(chunkType), sizeof(chunkType)) != static_cast<int>(sizeof(chunkType)) ||
      ihdrLength != 13 || !std::equal(std::begin(chunkType), std::end(chunkType), "IHDR")) {
    return false;
  }

  const uint32_t width = readBE32(file);
  const uint32_t height = readBE32(file);
  const int bitDepth = file.read();
  const int colorType = file.read();
  const int compression = file.read();
  const int filter = file.read();
  const int interlace = file.read();

  const bool supportedBitDepth =
      bitDepth == 8 || ((colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_INDEXED) &&
                        (bitDepth == 1 || bitDepth == 2 || bitDepth == 4));
  const bool supportedColorType = colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_TRUECOLOR ||
                                  colorType == PNG_PIXEL_INDEXED || colorType == PNG_PIXEL_GRAY_ALPHA ||
                                  colorType == PNG_PIXEL_TRUECOLOR_ALPHA;
  return width > 0 && height > 0 && width <= 2048 && height <= 3072 && width * height <= MAX_SOURCE_PIXELS &&
         supportedBitDepth && supportedColorType && compression == 0 && filter == 0 && interlace == 0;
}

BitmapPlacement calculateBitmapPlacement(const int bitmapWidth, const int bitmapHeight, const GfxRenderer& renderer) {
  BitmapPlacement placement;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (bitmapWidth > pageWidth || bitmapHeight > pageHeight) {
    float ratio = static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    if (ratio > screenRatio) {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropX = 1.0f - (screenRatio / ratio);
        ratio = (1.0f - placement.cropX) * static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
      }
      placement.x = 0;
      placement.y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropY = 1.0f - (ratio / screenRatio);
        ratio = static_cast<float>(bitmapWidth) / ((1.0f - placement.cropY) * static_cast<float>(bitmapHeight));
      }
      placement.x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      placement.y = 0;
    }
  } else {
    placement.x = (pageWidth - bitmapWidth) / 2;
    placement.y = (pageHeight - bitmapHeight) / 2;
  }

  return placement;
}

bool parseOverlayBmpHeader(HalFile& file, OverlayBmpInfo& info, const bool logErrors) {
  if (!file) return false;
  if (!file.seek(0)) return false;

  if (readLE16(file) != 0x4D42) {
    if (logErrors) LOG_ERR("SLP", "Transparent overlay is not a BMP");
    return false;
  }

  file.seekCur(8);
  info.dataOffset = readLE32(file);

  const uint32_t dibSize = readLE32(file);
  if (dibSize < 40) {
    if (logErrors) LOG_ERR("SLP", "Unsupported BMP DIB header: %u", static_cast<unsigned>(dibSize));
    return false;
  }

  info.width = static_cast<int32_t>(readLE32(file));
  const auto rawHeight = static_cast<int32_t>(readLE32(file));
  if (rawHeight == std::numeric_limits<int32_t>::min()) {
    if (logErrors) LOG_ERR("SLP", "Bad transparent overlay dimensions: %dx%d", info.width, rawHeight);
    return false;
  }
  info.topDown = rawHeight < 0;
  info.height = info.topDown ? -rawHeight : rawHeight;

  const uint16_t planes = readLE16(file);
  const uint16_t bpp = readLE16(file);
  const uint32_t compression = readLE32(file);

  // Match Bitmap::parseHeaders(): accept BI_RGB (0) and 32bpp BI_BITFIELDS (3), but keep the same
  // byte-layout assumption as custom sleep BMPs. The renderer below treats pixels as BGRA and does not parse masks.
  if (planes != 1 || bpp != 32 || !(compression == 0 || compression == 3)) {
    if (logErrors) {
      LOG_ERR("SLP", "Transparent overlay must be 32-bit BGRA BMP (planes=%u bpp=%u comp=%u)", planes, bpp,
              static_cast<unsigned>(compression));
    }
    return false;
  }

  constexpr int MAX_IMAGE_WIDTH = 2048;
  constexpr int MAX_IMAGE_HEIGHT = 3072;
  if (info.width <= 0 || info.height <= 0 || info.width > MAX_IMAGE_WIDTH || info.height > MAX_IMAGE_HEIGHT) {
    if (logErrors) LOG_ERR("SLP", "Bad transparent overlay dimensions: %dx%d", info.width, info.height);
    return false;
  }

  info.rowBytes = static_cast<uint32_t>(info.width) * 4u;
  if (!file.seek(info.dataOffset)) {
    if (logErrors) LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return false;
  }

  return true;
}

uint8_t bayerThreshold4x4(const int x, const int y) {
  static constexpr uint8_t BAYER_4X4[16] = {0, 128, 32, 160, 192, 64, 224, 96, 48, 176, 16, 144, 240, 112, 208, 80};
  return BAYER_4X4[((y & 0x03) << 2) | (x & 0x03)];
}

enum class TransparentOverlayPass : uint8_t { BW, GrayscaleLsb, GrayscaleMsb };

uint8_t quantizeOverlayLum(const uint8_t lum) {
  // Match Bitmap's native-palette path: 0, 85, 170, 255 map directly to levels 0..3.
  return lum >> 6;
}

bool renderTransparentOverlayPass(HalFile& file, const OverlayBmpInfo& info, const BitmapPlacement& placement,
                                  const GfxRenderer& renderer, uint8_t* row, const TransparentOverlayPass pass) {
  if (!file.seek(info.dataOffset)) {
    LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return false;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int cropPixX = std::floor(info.width * placement.cropX / 2.0f);
  const int cropPixY = std::floor(info.height * placement.cropY / 2.0f);
  const float croppedWidth = (1.0f - placement.cropX) * static_cast<float>(info.width);
  const float croppedHeight = (1.0f - placement.cropY) * static_cast<float>(info.height);

  float scale = 1.0f;
  if (croppedWidth > 0.0f && croppedHeight > 0.0f) {
    const float widthScale = static_cast<float>(pageWidth) / croppedWidth;
    const float heightScale = static_cast<float>(pageHeight) / croppedHeight;
    scale = std::min(widthScale, heightScale);
    if (scale > 1.0f) scale = 1.0f;
  }
  const bool isScaled = scale < 1.0f;

  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) {
      LOG_ERR("SLP", "Short read in transparent overlay row %d", bmpY);
      return false;
    }

    int screenY = -cropPixY + (info.topDown ? bmpY : info.height - 1 - bmpY);
    if (isScaled) screenY = std::floor(screenY * scale);
    screenY += placement.y;

    if (screenY >= pageHeight) {
      if (info.topDown) break;
      continue;
    }
    if (screenY < 0) {
      if (!info.topDown) break;
      continue;
    }

    for (int bmpX = cropPixX; bmpX < info.width - cropPixX; bmpX++) {
      int screenX = bmpX - cropPixX;
      if (isScaled) screenX = std::floor(screenX * scale);
      screenX += placement.x;

      if (screenX >= renderer.getScreenWidth()) break;
      if (screenX < 0) continue;

      const uint8_t* pixel = row + (static_cast<size_t>(bmpX) * 4u);
      const uint8_t alpha = pixel[3];
      if (alpha < MIN_VISIBLE_ALPHA || alpha <= bayerThreshold4x4(screenX, screenY)) continue;

      const uint8_t lum = (77u * pixel[2] + 150u * pixel[1] + 29u * pixel[0]) >> 8;
      const uint8_t level = quantizeOverlayLum(lum);

      switch (pass) {
        case TransparentOverlayPass::BW:
          // Same first pass as custom bitmap sleep: all non-white levels are painted black.
          // Transparent overlay's only difference is that opaque white explicitly erases underlying text.
          renderer.drawPixel(screenX, screenY, level < 3);
          break;
        case TransparentOverlayPass::GrayscaleLsb:
          if (level == 1) renderer.drawPixel(screenX, screenY, false);
          break;
        case TransparentOverlayPass::GrayscaleMsb:
          if (level == 1 || level == 2) renderer.drawPixel(screenX, screenY, false);
          break;
      }
    }
  }

  return true;
}

enum class AlphaOverlayResult : uint8_t { Rendered, NotAlphaOverlay, Error };
enum class AlphaScanResult : uint8_t { Useful, NotUseful, Error };

AlphaScanResult scanForUsefulAlpha(HalFile& file, const OverlayBmpInfo& info, uint8_t* row) {
  if (!file.seek(info.dataOffset)) {
    LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return AlphaScanResult::Error;
  }

  bool hasVisiblePixel = false;
  bool hasNonOpaquePixel = false;
  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) {
      LOG_ERR("SLP", "Short read while checking transparent overlay row %d", bmpY);
      return AlphaScanResult::Error;
    }

    for (int bmpX = 0; bmpX < info.width; bmpX++) {
      const uint8_t alpha = row[static_cast<size_t>(bmpX) * 4u + 3u];
      hasVisiblePixel |= alpha >= MIN_VISIBLE_ALPHA;
      hasNonOpaquePixel |= alpha < 255;
      if (hasVisiblePixel && hasNonOpaquePixel) return AlphaScanResult::Useful;
    }
  }

  return AlphaScanResult::NotUseful;
}

AlphaOverlayResult tryRenderTransparentOverlayBmp(HalFile& file, GfxRenderer& renderer, const char* pathForLog) {
  OverlayBmpInfo info;
  if (!parseOverlayBmpHeader(file, info, false)) return AlphaOverlayResult::NotAlphaOverlay;

  const auto placement = calculateBitmapPlacement(info.width, info.height, renderer);
  auto row = makeUniqueNoThrow<uint8_t[]>(info.rowBytes);
  if (!row) {
    LOG_ERR("SLP", "OOM: transparent overlay row (%u bytes)", static_cast<unsigned>(info.rowBytes));
    return AlphaOverlayResult::Error;
  }

  const auto alphaScanResult = scanForUsefulAlpha(file, info, row.get());
  if (alphaScanResult == AlphaScanResult::Error) return AlphaOverlayResult::Error;
  if (alphaScanResult == AlphaScanResult::NotUseful) return AlphaOverlayResult::NotAlphaOverlay;

  LOG_DBG("SLP", "Rendering transparent overlay: %s (%dx%d)", pathForLog, info.width, info.height);

  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::BW))
    return AlphaOverlayResult::Error;
  renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::GrayscaleLsb)) {
    renderer.setRenderMode(GfxRenderer::BW);
    // The BW composite is already on the panel. Keep it instead of falling
    // through to another overlay with this grayscale work buffer cleared.
    return AlphaOverlayResult::Rendered;
  }
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::GrayscaleMsb)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return AlphaOverlayResult::Rendered;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return AlphaOverlayResult::Rendered;
}

enum class SleepRecentKind : uint8_t { Standard, Overlay };

bool isRecentSleepIndex(const SleepRecentKind recentKind, const uint16_t idx, const uint8_t window) {
  return recentKind == SleepRecentKind::Overlay ? APP_STATE.isRecentOverlaySleep(idx, window)
                                                : APP_STATE.isRecentSleep(idx, window);
}

void pushRecentSleepIndex(const SleepRecentKind recentKind, const uint16_t idx) {
  if (recentKind == SleepRecentKind::Overlay) {
    APP_STATE.pushRecentOverlaySleep(idx);
  } else {
    APP_STATE.pushRecentSleep(idx);
  }
}

bool findNextValidSleepImage(HalFile& dir, const SleepRecentKind recentKind, char* name) {
  for (auto dirFile = dir.openNextFile(); dirFile; dirFile = dir.openNextFile()) {
    if (dirFile.isDirectory()) continue;

    dirFile.getName(name, MAX_SLEEP_FILE_NAME_LEN);
    if (name[0] == '\0' || name[0] == '.') continue;

    const bool isBmp = FsHelpers::hasBmpExtension(name);
    const bool isPng = recentKind == SleepRecentKind::Overlay && FsHelpers::hasPngExtension(std::string_view{name});
    if (!isBmp && !isPng) {
      LOG_DBG("SLP", "Skipping unsupported sleep image: %s", name);
      continue;
    }

    const bool isValid = isBmp ? [&dirFile]() {
      Bitmap bitmap(dirFile);
      return bitmap.parseHeaders() == BmpReaderError::Ok;
    }()
                               : isValidPngHeader(dirFile);
    if (!isValid) {
      LOG_DBG("SLP", "Skipping invalid sleep image: %s", name);
      continue;
    }
    return true;
  }
  return false;
}

bool selectRandomSleepFile(const char* dirPath, const SleepRecentKind recentKind, std::string& selectedPath) {
  auto dir = Storage.open(dirPath);
  if (!dir || !dir.isDirectory()) return false;

  auto name = makeUniqueNoThrow<char[]>(MAX_SLEEP_FILE_NAME_LEN);
  if (!name) {
    LOG_ERR("SLP", "OOM: sleep filename buffer");
    return false;
  }

  uint16_t fileCount = 0;
  while (fileCount < UINT16_MAX && findNextValidSleepImage(dir, recentKind, name.get())) ++fileCount;
  if (fileCount == 0) return false;

  // Pick a random wallpaper, excluding recently shown ones.
  // Window: up to SLEEP_RECENT_COUNT entries, capped at fileCount-1.
  const uint8_t recentFill =
      recentKind == SleepRecentKind::Overlay ? APP_STATE.recentOverlaySleepFill : APP_STATE.recentSleepFill;
  const uint8_t window = static_cast<uint8_t>(std::min<uint16_t>(recentFill, fileCount - 1));
  auto randomFileIndex = static_cast<uint16_t>(random(fileCount));
  for (uint8_t attempt = 0; attempt < 20 && isRecentSleepIndex(recentKind, randomFileIndex, window); attempt++) {
    randomFileIndex = static_cast<uint16_t>(random(fileCount));
  }

  dir.rewindDirectory();
  for (uint16_t index = 0; index <= randomFileIndex; ++index) {
    if (!findNextValidSleepImage(dir, recentKind, name.get())) return false;
  }

  selectedPath.reserve(strlen(dirPath) + 1 + strlen(name.get()));
  selectedPath = dirPath;
  selectedPath += "/";
  selectedPath += name.get();
  pushRecentSleepIndex(recentKind, randomFileIndex);
  APP_STATE.saveToFile();
  return true;
}

bool drawSleepPopupPreservingFrame(GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int frameThickness = metrics.popupFrameThickness;
  const int popupY = static_cast<int>(renderer.getScreenHeight() * metrics.popupTopOffsetRatio);
  const int popupHeight = renderer.getLineHeight(UI_12_FONT_ID) + metrics.popupMarginY * 2;
  const int bandTop = std::max(0, popupY - frameThickness);
  const int bandBottom = std::min(renderer.getScreenHeight(), popupY + popupHeight + frameThickness);
  const int bandHeight = bandBottom - bandTop;
  const size_t bandBytes = renderer.getRegionByteSize(0, bandTop, renderer.getScreenWidth(), bandHeight);

  auto savedBand = makeUniqueNoThrow<uint8_t[]>(bandBytes);
  if (!savedBand) {
    LOG_ERR("SLP", "OOM: sleep popup background (%u bytes)", static_cast<unsigned>(bandBytes));
    return false;
  }
  if (!renderer.copyRegionToBuffer(0, bandTop, renderer.getScreenWidth(), bandHeight, savedBand.get(), bandBytes)) {
    LOG_ERR("SLP", "Failed to save sleep popup background");
    return false;
  }

  GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  if (!renderer.copyBufferToRegion(0, bandTop, renderer.getScreenWidth(), bandHeight, savedBand.get(), bandBytes)) {
    LOG_ERR("SLP", "Failed to restore sleep popup background");
    return false;
  }
  return true;
}

void releaseSdFontCachesForDecode(const GfxRenderer& renderer) {
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG("SLP", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    fcm->releaseSdFontCaches();
    LOG_DBG("SLP", "Free heap before sleep image decode: %d bytes", ESP.getFreeHeap());
  }
}

}  // namespace

void SleepActivity::onEnter() {
  Activity::onEnter();

  const bool frameWasInverted = display.isInverted();

  // Sleep screens always use normal polarity. This activity draws directly
  // from onEnter (outside ActivityManager's per-render polarity resolution),
  // so clear any inversion left over from a night-mode reader render.
  display.setInverted(false);

  const bool renderQuickResume =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);

  if (renderQuickResume) {
    return renderLastScreenSleepScreen();
  }

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM) {
    // Transparent mode retains the current framebuffer. Materialize any
    // output-level inversion first so the retained content keeps its visible
    // polarity after the display driver returns to normal.
    if (frameWasInverted) renderer.invertScreen();
    if (APP_STATE.lastSleepFromReader) {
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    }
    drawSleepPopupPreservingFrame(renderer);
    if (APP_STATE.lastSleepFromReader) {
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    }
    releaseSdFontCachesForDecode(renderer);
    return renderTransparentCustomSleepScreen();
  }

  // Show popup with reader orientation only when going to sleep from reader
  if (APP_STATE.lastSleepFromReader) {
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  } else {
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  }

  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      return renderCustomSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
      return renderCoverSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      if (APP_STATE.lastSleepFromReader) {
        return renderCoverSleepScreen();
      } else {
        return renderCustomSleepScreen();
      }
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CLOCK):
      return renderClockSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_STATS):
      return renderCalendarStatsSleepScreen();
    default:
      return renderDefaultSleepScreen();
  }
}

void SleepActivity::renderCustomSleepScreen() const {
  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  // This takes priority over the /sleep folder.
  HalFile file;
  if (Storage.openFileForRead("SLP", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Loading: /sleep.bmp");
      renderBitmapSleepScreen(bitmap);
      file.close();
      return;
    }
    file.close();
  }

  std::string selectedPath;
  if (!selectRandomSleepFile("/.sleep", SleepRecentKind::Standard, selectedPath)) {
    selectRandomSleepFile("/sleep", SleepRecentKind::Standard, selectedPath);
  }

  if (!selectedPath.empty()) {
    HalFile randFile;
    if (Storage.openFileForRead("SLP", selectedPath, randFile)) {
      LOG_DBG("SLP", "Randomly loading: %s", selectedPath.c_str());
      delay(100);
      Bitmap bitmap(randFile, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderBitmapSleepScreen(bitmap);
        randFile.close();
        return;
      }
      randFile.close();
    }
  }

  renderDefaultSleepScreen();
}

// Sleep screens paint with a single HALF refresh (stock parity): the OEM X4
// firmware's only clean refresh in normal operation is the single-pass 0xD7
// sequence, used once for the sleep image. It never runs the multi-flash GC
// waveform (0xF7) that FULL_REFRESH selects (#2471's blinking complaint).
void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_SLEEPING));
  renderNewsletterUnreadFooter();

  // Make sleep screen dark unless light is selected in settings
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap, const bool preserveBackground) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto placement = calculateBitmapPlacement(bitmap.getWidth(), bitmap.getHeight(), renderer);
  const int x = placement.x;
  const int y = placement.y;
  const float cropX = placement.cropX;
  const float cropY = placement.cropY;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  if (!preserveBackground) renderer.clearScreen();

  const bool hasGreyscale =
      bitmap.hasGreyscale() && (preserveBackground || SETTINGS.sleepScreenCoverFilter ==
                                                          CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (!preserveBackground &&
      SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  if (hasGreyscale) {
    // OEM grayscale pipeline base. Must stay HALF: the gray nudge LUT is
    // calibrated against the pixel state the single-pass HALF waveform leaves
    // behind. A FULL (GC) base parks pixels in a different charge state and
    // the differential nudge then lands unevenly (blotchy noise in gray areas).
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

  if (hasGreyscale) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

bool SleepActivity::renderSleepOverlayFile(HalFile& file, const char* pathForLog) const {
  const auto alphaResult = tryRenderTransparentOverlayBmp(file, renderer, pathForLog);
  if (alphaResult == AlphaOverlayResult::Rendered) return true;
  if (alphaResult == AlphaOverlayResult::Error) return false;

  Bitmap bitmap(file, true);
  const auto parseResult = bitmap.parseHeaders();
  if (parseResult != BmpReaderError::Ok) {
    LOG_ERR("SLP", "Invalid sleep overlay BMP %s: %s", pathForLog, Bitmap::errorToString(parseResult));
    return false;
  }

  LOG_DBG("SLP", "Rendering regular BMP sleep overlay: %s (%dx%d)", pathForLog, bitmap.getWidth(), bitmap.getHeight());
  // drawBitmap leaves white pixels untouched; skipping the initial clear makes
  // them transparent while retaining the existing grayscale pipeline.
  renderBitmapSleepScreen(bitmap, true);
  return true;
}

bool SleepActivity::renderTransparentOverlayPng(const std::string& path) const {
  ImageDimensions dimensions;
  if (!PngToFramebufferConverter::getDimensionsStatic(path, dimensions)) return false;

  const auto placement = calculateBitmapPlacement(dimensions.width, dimensions.height, renderer);
  RenderConfig config;
  config.x = placement.x;
  config.y = placement.y;
  config.maxWidth = renderer.getScreenWidth();
  config.maxHeight = renderer.getScreenHeight();
  config.useDithering = false;
  config.sourceCropX = placement.cropX;
  config.sourceCropY = placement.cropY;
  config.useExactDimensions = placement.cropX > 0.0f || placement.cropY > 0.0f;
  config.preserveAlpha = true;

  PngToFramebufferConverter converter;
  LOG_DBG("SLP", "Rendering transparent PNG overlay: %s (%dx%d)", path.c_str(), dimensions.width, dimensions.height);

  if (!converter.decodeToFramebuffer(path, renderer, config)) return false;
  renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!converter.decodeToFramebuffer(path, renderer, config)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return true;
  }
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!converter.decodeToFramebuffer(path, renderer, config)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return true;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return true;
}

bool SleepActivity::renderSleepOverlayPath(const std::string& path) const {
  if (FsHelpers::hasPngExtension(path)) {
    return Storage.exists(path.c_str()) && renderTransparentOverlayPng(path);
  }

  HalFile file;
  return Storage.openFileForRead("SLP", path, file) && renderSleepOverlayFile(file, path.c_str());
}

void SleepActivity::renderTransparentCustomSleepScreen() const {
  if (renderSleepOverlayPath(TRANSPARENT_SLEEP_ROOT_BMP)) return;
  if (renderSleepOverlayPath(TRANSPARENT_SLEEP_ROOT_PNG)) return;

  std::string selectedPath;
  if (!selectRandomSleepFile(TRANSPARENT_SLEEP_DIR, SleepRecentKind::Overlay, selectedPath)) {
    selectRandomSleepFile(TRANSPARENT_SLEEP_LEGACY_DIR, SleepRecentKind::Overlay, selectedPath);
  }

  if (!selectedPath.empty() && renderSleepOverlayPath(selectedPath)) return;

  LOG_ERR("SLP", "No valid transparent sleep overlay found");
  renderDefaultSleepScreen();
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  if (APP_STATE.openEpubPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (FsHelpers::hasXtcExtension(APP_STATE.openEpubPath)) {
    // Handle XTC file
    Xtc lastXtc(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastXtc.load()) {
      LOG_ERR("SLP", "Failed to load last XTC");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastXtc.generateCoverBmp()) {
      LOG_ERR("SLP", "Failed to generate XTC cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (FsHelpers::hasTxtExtension(APP_STATE.openEpubPath)) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      LOG_ERR("SLP", "Failed to load last TXT");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastTxt.generateCoverBmp()) {
      LOG_ERR("SLP", "No cover image found for TXT file");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (FsHelpers::hasEpubExtension(APP_STATE.openEpubPath)) {
    // Handle EPUB file
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    // Skip loading css since we only need metadata here
    if (!lastEpub.load(true, true)) {
      LOG_ERR("SLP", "Failed to load last epub");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastEpub.generateCoverBmp(cropped)) {
      LOG_ERR("SLP", "Failed to generate cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
  } else {
    return (this->*renderNoCoverSleepScreen)();
  }

  HalFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderLastScreenSleepScreen() const {
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawImage(MoonIcon, 0, pageHeight - MOONICON_HEIGHT, MOONICON_WIDTH, MOONICON_HEIGHT);
  if (gpio.deviceIsX3()) {
    // The controller still holds the displayed page, so its differential base
    // waveform can add the moon without a full-screen flash.
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderClockUnavailableSleepScreen() const {
  const auto pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 34, tr(STR_CLOCK), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NOT_SET));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 28, tr(STR_CLOCK_SYNC_NOW));
  renderNewsletterUnreadFooter();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderClockSleepScreen() const {
  ClockDateTimeCompat now;
  if (!readClockDateTimeCompat(now, SETTINGS.clockUtcOffsetQ)) {
    LOG_ERR("SLP", "Clock sleep screen unavailable: RTC date/time not available");
    renderClockUnavailableSleepScreen();
    return;
  }

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int outerMargin = 28;
  const int contentLeft = outerMargin + 12;
  const int contentRight = pageWidth - outerMargin - 12;
  const int contentWidth = contentRight - contentLeft;
  const int titleY = 44;
  const int subtitleY = 90;
  const int headersY = 150;
  const int gridTop = 188;
  const int gridBottom = pageHeight - 56;
  const int gridHeight = gridBottom - gridTop;
  const int cellWidth = contentWidth / 7;
  const int cellHeight = gridHeight / 6;

  char titleText[32];
  snprintf(titleText, sizeof(titleText), "%s %04u", MONTH_NAMES[now.month - 1], now.year);

  char subtitleText[32];
  snprintf(subtitleText, sizeof(subtitleText), "%s %u", WEEKDAY_NAMES[now.weekday % 7], static_cast<unsigned>(now.day));

  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, titleY, titleText, true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, subtitleY, subtitleText, true);
  renderer.drawLine(contentLeft + 12, 126, contentRight - 12, 126, true);

  const int monthStartWeekday = (static_cast<int>(now.weekday) - ((static_cast<int>(now.day) - 1) % 7) + 7) % 7;
  const int startColumn = (monthStartWeekday + 6) % 7;  // Monday-first
  const int monthDays = sleepScreenDaysInMonth(now.year, now.month);

  for (int col = 0; col < 7; col++) {
    const int centerX = contentLeft + col * cellWidth + cellWidth / 2;
    const int headerWidth = renderer.getTextWidth(SMALL_FONT_ID, CALENDAR_HEADERS[col], EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, centerX - headerWidth / 2, headersY, CALENDAR_HEADERS[col], true,
                      EpdFontFamily::BOLD);
  }

  for (int day = 1; day <= monthDays; day++) {
    const int offset = startColumn + day - 1;
    const int row = offset / 7;
    const int col = offset % 7;
    if (row >= 6) break;

    const int cellX = contentLeft + col * cellWidth;
    const int cellY = gridTop + row * cellHeight;
    const bool isToday = day == now.day;
    const int centerX = cellX + cellWidth / 2;
    const int centerY = cellY + cellHeight / 2 + 2;

    char dayText[4];
    snprintf(dayText, sizeof(dayText), "%d", day);
    const auto dayStyle = EpdFontFamily::REGULAR;
    const int textWidth = renderer.getTextWidth(NOTOSERIF_14_FONT_ID, dayText, dayStyle);
    const int textX = centerX - textWidth / 2;
    const int textY = centerY - 4;

    if (isToday) {
      const auto bounds = renderer.getTextBounds(NOTOSERIF_14_FONT_ID, dayText, dayStyle);
      const int inkCenterX = textX + bounds.left + bounds.width / 2;
      const int inkCenterY = textY + bounds.top + bounds.height / 2 + 2;
      drawFilledCircle(renderer, inkCenterX, inkCenterY, 24);
      renderer.drawText(NOTOSERIF_14_FONT_ID, textX, textY, dayText, false, dayStyle);
    } else {
      renderer.drawText(NOTOSERIF_14_FONT_ID, textX, textY, dayText, true, dayStyle);
    }
  }

  renderNewsletterUnreadFooter();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderCalendarStatsSleepScreen() const {
  ClockDateTimeCompat now;
  if (!readClockDateTimeCompat(now, SETTINGS.clockUtcOffsetQ)) {
    LOG_ERR("SLP", "Calendar+stats sleep screen unavailable: RTC date/time not available");
    renderClockUnavailableSleepScreen();
    return;
  }

  const SleepReadingStatsSummary stats = loadSleepReadingStatsSummary();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  int viewTop = 0;
  int viewRight = 0;
  int viewBottom = 0;
  int viewLeft = 0;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);

  const int contentLeft = viewLeft + 34;
  const int contentRight = pageWidth - viewRight - 34;
  const int contentWidth = contentRight - contentLeft;
  const int titleY = viewTop + 26;
  const int subtitleY = titleY + 32;
  const int dividerY = subtitleY + 30;
  const int headersY = dividerY + 28;
  const int gridTop = headersY + 30;
  const int gridBottom = 396;
  const int gridHeight = gridBottom - gridTop;
  const int cellWidth = contentWidth / 7;
  const int cellHeight = gridHeight / 6;
  const int statsDividerY = gridBottom + 18;
  const int statsTop = statsDividerY + 26;
  const int allBooksLabelY = statsTop + 166;
  const int allBooksMetricY = allBooksLabelY + 18;
  const int footerY = pageHeight - viewBottom - 22;

  char titleText[32];
  snprintf(titleText, sizeof(titleText), "%s %04u", MONTH_NAMES[now.month - 1], now.year);

  char subtitleText[32];
  snprintf(subtitleText, sizeof(subtitleText), "%s %u", WEEKDAY_NAMES[now.weekday % 7], static_cast<unsigned>(now.day));

  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, titleY, titleText, true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, subtitleY, subtitleText, true);
  renderer.drawLine(contentLeft + 12, dividerY, contentRight - 12, dividerY, true);

  const int monthStartWeekday = (static_cast<int>(now.weekday) - ((static_cast<int>(now.day) - 1) % 7) + 7) % 7;
  const int startColumn = (monthStartWeekday + 6) % 7;
  const int monthDays = sleepScreenDaysInMonth(now.year, now.month);

  for (int col = 0; col < 7; col++) {
    const int centerX = contentLeft + col * cellWidth + cellWidth / 2;
    const int headerWidth = renderer.getTextWidth(SMALL_FONT_ID, CALENDAR_HEADERS[col], EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, centerX - headerWidth / 2, headersY, CALENDAR_HEADERS[col], true,
                      EpdFontFamily::BOLD);
  }

  for (int day = 1; day <= monthDays; day++) {
    const int offset = startColumn + day - 1;
    const int row = offset / 7;
    const int col = offset % 7;
    if (row >= 6) break;

    const int cellX = contentLeft + col * cellWidth;
    const int cellY = gridTop + row * cellHeight;
    const bool isToday = day == now.day;
    const int centerX = cellX + cellWidth / 2;
    const int centerY = cellY + cellHeight / 2 + 2;

    char dayText[4];
    snprintf(dayText, sizeof(dayText), "%d", day);
    const auto dayStyle = EpdFontFamily::REGULAR;
    const int textWidth = renderer.getTextWidth(NOTOSERIF_14_FONT_ID, dayText, dayStyle);
    const int textX = centerX - textWidth / 2;
    const int textY = centerY - 4;

    if (isToday) {
      const auto bounds = renderer.getTextBounds(NOTOSERIF_14_FONT_ID, dayText, dayStyle);
      const int inkCenterX = textX + bounds.left + bounds.width / 2;
      const int inkCenterY = textY + bounds.top + bounds.height / 2 + 2;
      drawFilledCircle(renderer, inkCenterX, inkCenterY, 20);
      renderer.drawText(NOTOSERIF_14_FONT_ID, textX, textY, dayText, false, dayStyle);
    } else {
      renderer.drawText(NOTOSERIF_14_FONT_ID, textX, textY, dayText, true, dayStyle);
    }
  }

  renderer.drawLine(contentLeft + 8, statsDividerY, contentRight - 8, statsDividerY, true);

  if (stats.hasBookContext) {
    renderer.drawCenteredText(SMALL_FONT_ID, statsTop, tr(STR_STATS_THIS_BOOK), true, EpdFontFamily::BOLD);

    const auto titleLines =
        renderer.wrappedText(UI_10_FONT_ID, stats.currentBookTitle.c_str(), contentWidth - 40, 2, EpdFontFamily::BOLD);
    int textY = statsTop + 20;
    for (const auto& line : titleLines) {
      renderer.drawCenteredText(UI_10_FONT_ID, textY, line.c_str(), true, EpdFontFamily::BOLD);
      textY += renderer.getLineHeight(UI_10_FONT_ID);
    }

    if (!stats.currentBookAuthor.empty()) {
      const std::string authorLine = renderer.truncatedText(SMALL_FONT_ID, stats.currentBookAuthor.c_str(), contentWidth - 60);
      renderer.drawCenteredText(SMALL_FONT_ID, textY + 2, authorLine.c_str(), true);
      textY += renderer.getLineHeight(SMALL_FONT_ID) + 6;
    } else {
      textY += 6;
    }

    char currentTimeBuf[24];
    char currentPagesBuf[16];
    char currentSessionsBuf[16];
    BookReadingStats::formatDuration(stats.currentBookStats.totalReadingSeconds, currentTimeBuf, sizeof(currentTimeBuf));
    snprintf(currentPagesBuf, sizeof(currentPagesBuf), "%lu",
             static_cast<unsigned long>(stats.currentBookStats.totalPagesTurned));
    snprintf(currentSessionsBuf, sizeof(currentSessionsBuf), "%lu",
             static_cast<unsigned long>(stats.currentBookStats.sessionCount));

    const int metricRowY = textY;
    drawSleepMetric(renderer, contentLeft + contentWidth / 6, metricRowY, currentSessionsBuf, tr(STR_STATS_SESSIONS_LBL));
    drawSleepMetric(renderer, contentLeft + contentWidth / 2, metricRowY, currentTimeBuf, tr(STR_STATS_TIME_LBL));
    drawSleepMetric(renderer, contentLeft + (contentWidth * 5) / 6, metricRowY, currentPagesBuf, tr(STR_STATS_PAGES_LBL));
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, statsTop + 24, tr(STR_NO_OPEN_BOOK), true, EpdFontFamily::BOLD);
  }

  char totalTimeBuf[24];
  char totalPagesBuf[16];
  char totalSessionsBuf[16];
  BookReadingStats::formatDuration(stats.globalStats.totalReadingSeconds, totalTimeBuf, sizeof(totalTimeBuf));
  snprintf(totalPagesBuf, sizeof(totalPagesBuf), "%lu", static_cast<unsigned long>(stats.globalStats.totalPagesTurned));
  snprintf(totalSessionsBuf, sizeof(totalSessionsBuf), "%lu", static_cast<unsigned long>(stats.globalStats.totalSessions));

  renderer.drawCenteredText(SMALL_FONT_ID, allBooksLabelY, tr(STR_STATS_ALL_TIME), true, EpdFontFamily::BOLD);
  drawSleepMetric(renderer, contentLeft + contentWidth / 6, allBooksMetricY, totalSessionsBuf, tr(STR_STATS_SESSIONS_LBL));
  drawSleepMetric(renderer, contentLeft + contentWidth / 2, allBooksMetricY, totalTimeBuf, tr(STR_STATS_TIME_LBL));
  drawSleepMetric(renderer, contentLeft + (contentWidth * 5) / 6, allBooksMetricY, totalPagesBuf, tr(STR_STATS_PAGES_LBL));

  const int unread = NEWSLETTER_STORE.unreadCount();
  if (unread > 0) {
    char unreadBuf[32];
    snprintf(unreadBuf, sizeof(unreadBuf), tr(STR_NEWSLETTER_UNREAD_COUNT), static_cast<unsigned>(unread));
    renderer.drawCenteredText(SMALL_FONT_ID, footerY, unreadBuf, true);
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderNewsletterUnreadFooter() const {
  const int unread = NEWSLETTER_STORE.unreadCount();
  if (unread <= 0) return;
  char unreadBuf[32];
  snprintf(unreadBuf, sizeof(unreadBuf), tr(STR_NEWSLETTER_UNREAD_COUNT), static_cast<unsigned>(unread));
  renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight() - 24, unreadBuf, true);
}
