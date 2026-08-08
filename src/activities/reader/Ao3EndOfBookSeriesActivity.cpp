#include "Ao3EndOfBookSeriesActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <cstring>
#include <algorithm>
#include <functional>
#include <new>

#include "../../Ao3Librarian.h"
#include "../../fontIds.h"
#include "../../MappedInputManager.h"
#include "../../components/UITheme.h"
#include "ReaderUtils.h"

// ---------------------------------------------------------------------------
//  onEnter
// ---------------------------------------------------------------------------

void Ao3EndOfBookSeriesActivity::onEnter() {
  Activity::onEnter();
  buttonNavigator.setMappedInputManager(mappedInput);
  loadViewEntries();
  requestUpdate();
}

// ---------------------------------------------------------------------------
//  loadViewEntries
// ---------------------------------------------------------------------------

void Ao3EndOfBookSeriesActivity::loadViewEntries() {
  viewEntries.clear();
  const char* indexPath = "/.crosspoint/ao3_library_index.bin";
  if (!Storage.exists(indexPath)) return;

  HalFile f;
  if (!Storage.openFileForRead("AO3S", indexPath, f)) return;

  char     magic[4];
  uint8_t  version;
  uint16_t recordCount;
  uint32_t nextSequence;
  uint8_t  reserved;

  const bool readOk =
      f.read(magic, 4) == 4 &&
      f.read(&version, 1) == 1 &&
      f.read((uint8_t*)&recordCount, 2) == 2 &&
      f.read((uint8_t*)&nextSequence, 4) == 4 &&
      f.read(&reserved, 1) == 1;

  if (!readOk || memcmp(magic, "AO3X", 4) != 0 || version != 1 || recordCount > MAX_LIBRARY_BOOKS) {
    f.close();
    return;
  }

  CompactIndexRecord rec;
  for (uint16_t i = 0; i < recordCount; i++) {
    if (f.read((uint8_t*)&rec, sizeof(rec)) != sizeof(rec)) break;
    if (rec.flags & 0x01) continue;                     // tombstone
    if (fnv1a(rec.seriesName) != seriesHash_) continue;  // different series
    viewEntries.push_back(buildViewEntry(rec));
    yield();
  }
  f.close();

  // Always sort by series part ascending
  std::sort(viewEntries.begin(), viewEntries.end(), [](const ViewEntry& a, const ViewEntry& b) {
    return a.seriesPart < b.seriesPart;
  });

  // Position selector on the just-closed book
  for (size_t i = 0; i < viewEntries.size(); i++) {
    if (viewEntries[i].cacheHash == originCacheHash_) {
      selectorIndex = i;
      break;
    }
  }
}

// ---------------------------------------------------------------------------
//  getBookStatus
// ---------------------------------------------------------------------------

BookStatus Ao3EndOfBookSeriesActivity::getBookStatus(uint32_t cacheHash) {
  std::string cachePath = "/.crosspoint/epub_" + std::to_string(cacheHash) + "/progress.bin";
  HalFile f;
  if (Storage.openFileForRead("AO3S", cachePath, f)) {
    uint8_t data[7];
    if (f.read(data, 7) >= 7) {
      f.close();
      return static_cast<BookStatus>(data[6]);
    }
    f.close();
  }
  return BookStatus::START;
}

// ---------------------------------------------------------------------------
//  loadPageCache
// ---------------------------------------------------------------------------

void Ao3EndOfBookSeriesActivity::loadPageCache(int page) {
  const int startIdx = page * 3;
  const int endIdx   = std::min(startIdx + 3, static_cast<int>(viewEntries.size()));

  for (int i = 0; i < 3; i++) {
    new (&pageCache[i]) Ao3LibraryMetadata();
    pageCacheStatus[i] = BookStatus::START;
  }

  for (int i = startIdx; i < endIdx; i++) {
    const int slot = i - startIdx;
    std::string infoPath =
        "/.crosspoint/epub_" + std::to_string(viewEntries[i].cacheHash) + "/ao3_library_info";
    HalFile f;
    if (Storage.openFileForRead("AO3S", infoPath, f)) {
      f.read((uint8_t*)&pageCache[slot], sizeof(Ao3LibraryMetadata));
      f.close();
    }
    pageCacheStatus[slot] = getBookStatus(viewEntries[i].cacheHash);
  }

  cachedPage = page;

  const int textWidth = renderer.getScreenWidth() - 40;
  for (int i = 0; i < 3; i++) {
    wrappedSummary[i].clear();
    if (pageCache[i].summary[0] != 0) {
      for (int j = 0; pageCache[i].summary[j] != '\0'; j++) {
        if (pageCache[i].summary[j] == '\n' || pageCache[i].summary[j] == '\r') {
          pageCache[i].summary[j] = ' ';
        }
      }
      wrappedSummary[i] = renderer.wrappedText(SMALL_FONT_ID, pageCache[i].summary, textWidth, 3);
    }
  }
}

// ---------------------------------------------------------------------------
//  loop
// ---------------------------------------------------------------------------

void Ao3EndOfBookSeriesActivity::loop() {
  // Long press Back → go home
  if (mappedInput.isPressed(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
    onGoHome();
    return;
  }

  // Short press Back → reopen origin book (lands back on its EOB screen)
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    activityManager.goToReader(originEpubPath_);
    return;
  }

  // Confirm → open the selected book
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!viewEntries.empty()) {
      const int selPage = static_cast<int>(selectorIndex) / 3;
      if (selPage != cachedPage) loadPageCache(selPage);
      const int slot = static_cast<int>(selectorIndex) % 3;
      const std::string epubPath(pageCache[slot].filepath);
      if (!epubPath.empty()) {
        activityManager.goToReader(epubPath);
        return;
      }
    }
  }

  const int total = static_cast<int>(viewEntries.size());
  if (total > 0) {
    // All four nav buttons behave the same — no panels to open in this view.
    // Right and Down = next entry; Left and Up = prev entry.
    buttonNavigator.onPress({MappedInputManager::Button::Right}, [this, total] {
      selectorIndex = (selectorIndex + 1) % total;
      requestUpdate();
    });
    buttonNavigator.onPress({MappedInputManager::Button::Down}, [this, total] {
      selectorIndex = (selectorIndex + 1) % total;
      requestUpdate();
    });
    buttonNavigator.onPress({MappedInputManager::Button::Left}, [this, total] {
      selectorIndex = (selectorIndex + total - 1) % total;
      requestUpdate();
    });
    buttonNavigator.onPress({MappedInputManager::Button::Up}, [this, total] {
      selectorIndex = (selectorIndex + total - 1) % total;
      requestUpdate();
    });
    // Long press → skip a full page of 3 entries
    buttonNavigator.onContinuous({MappedInputManager::Button::Right}, [this, total] {
      selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, total, 3);
      requestUpdate();
    });
    buttonNavigator.onContinuous({MappedInputManager::Button::Down}, [this, total] {
      selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, total, 3);
      requestUpdate();
    });
    buttonNavigator.onContinuous({MappedInputManager::Button::Left}, [this, total] {
      selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, total, 3);
      requestUpdate();
    });
    buttonNavigator.onContinuous({MappedInputManager::Button::Up}, [this, total] {
      selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, total, 3);
      requestUpdate();
    });
  }
}

// ---------------------------------------------------------------------------
//  render
// ---------------------------------------------------------------------------

void Ao3EndOfBookSeriesActivity::render(RenderLock&& lock) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();

  // Header: series name, truncated if needed
  const std::string headerText = renderer.truncatedText(
      UI_12_FONT_ID, seriesName_.c_str(), renderer.getScreenWidth() - 30, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, 15, 12, headerText.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawLine(0, 48, renderer.getScreenWidth(), 48);

  if (viewEntries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2,
                              "No books found in this series.");
  } else {
    const int startIdx = (selectorIndex / 3) * 3;
    const int endIdx   = std::min(startIdx + 3, static_cast<int>(viewEntries.size()));

    const int topPad       = 18;
    const int contentEnd   = renderer.getScreenHeight() - metrics.buttonHintsHeight;
    const int entrySlot    = (contentEnd - (42 + topPad)) / 3;
    const int contentStart = 48 + topPad;

    const int currentPage = static_cast<int>(selectorIndex) / 3;
    if (currentPage != cachedPage) {
      loadPageCache(currentPage);
    }

    int y = contentStart;
    for (int i = startIdx; i < endIdx; i++) {
      const bool selected = (i == static_cast<int>(selectorIndex));
      renderEntry(lock, y, viewEntries[i], i - startIdx, selected);
      y += entrySlot;
      if (i < endIdx - 1) {
        renderer.drawLine(15, y - topPad, renderer.getScreenWidth() - 15, y - topPad);
      }
    }

    // Page counter
    if (static_cast<int>(viewEntries.size()) > 3) {
      char pageBuf[32];
      sprintf(pageBuf, "%d / %d",
              (startIdx / 3) + 1,
              (static_cast<int>(viewEntries.size()) + 2) / 3);
      // Anchor symmetrically to the right with a 15px margin
      const int counterX =
          renderer.getScreenWidth() - 15 - renderer.getTextWidth(SMALL_FONT_ID, pageBuf);
      renderer.drawText(SMALL_FONT_ID, counterX, 15, pageBuf);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

// ---------------------------------------------------------------------------
//  renderEntry — mirrors Ao3LibraryActivity exactly
// ---------------------------------------------------------------------------

void Ao3EndOfBookSeriesActivity::renderEntry(RenderLock& lock, int y, const ViewEntry& ve,
                                             int cacheSlot, bool selected) {
  const int margin          = 20;
  const int selectionHeight = 56;
  const int squareSize      = selectionHeight;
  const int textX           = margin + squareSize + 15;

  if (selected) {
    renderer.fillRoundedRect(textX - 8, y - 3,
                             renderer.getScreenWidth() - textX - 15,
                             selectionHeight + 6, 8, LightGray);
  }

  const Ao3LibraryMetadata& meta       = pageCache[cacheSlot];
  const bool                metaLoaded = meta.isValid();

  const char rating    = metaLoaded ? meta.rating         : '-';
  const char warning   = metaLoaded ? meta.warning        : 0;
  const bool completed = metaLoaded ? (bool)meta.isCompleted : false;

  drawAo3Square(lock, margin, y, squareSize, rating, warning, completed, pageCacheStatus[cacheSlot]);

  std::string title      = metaLoaded && meta.title[0]  ? std::string(meta.title)  : std::string(ve.title);
  std::string authorText = metaLoaded && meta.author[0] ? std::string(meta.author) : std::string(ve.authorKey);

  if (metaLoaded && meta.seriesName[0] != 0) {
    if (authorText.length() > 11) {
      authorText = authorText.substr(0, 11) + ".";
    }
    char seriesBuf[256];
    if (meta.seriesPart > 0) {
      sprintf(seriesBuf, " \xE2\x80\xA2 %d of %s", meta.seriesPart, meta.seriesName);
    } else {
      sprintf(seriesBuf, " \xE2\x80\xA2 %s", meta.seriesName);
    }
    authorText += seriesBuf;
  }

  const int maxTextWidth = renderer.getScreenWidth() - textX - 25;

  auto truncateToFit = [&](std::string& text, int fontId, EpdFontFamily::Style style) {
    if (renderer.getTextWidth(fontId, text.c_str(), style) > maxTextWidth) {
      while (!text.empty() &&
             renderer.getTextWidth(fontId, (text + "..").c_str(), style) > maxTextWidth) {
        while (!text.empty()) {
          const char c = text.back();
          text.pop_back();
          if ((c & 0xC0) != 0x80) break;
        }
      }
      text += "..";
    }
  };

  truncateToFit(title,      UI_12_FONT_ID, EpdFontFamily::BOLD);
  truncateToFit(authorText, UI_10_FONT_ID, EpdFontFamily::REGULAR);

  renderer.drawText(UI_12_FONT_ID, textX, y + 6,  title.c_str(),      true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, textX, y + 32, authorText.c_str());

  if (metaLoaded) {
    int blockY = y + selectionHeight + 12;

    int tagX = margin;
    for (int j = 0; j < 4; j++) {
      if (meta.tags[j][0] == 0) break;
      const int tagW = renderer.getTextWidth(SMALL_FONT_ID, meta.tags[j]) + 16;
      if (tagX + tagW > renderer.getScreenWidth() - margin) break;
      renderer.drawRoundedRect(tagX, blockY, tagW, 20, 1, 6, true);
      renderer.drawText(SMALL_FONT_ID, tagX + 8, blockY - 2, meta.tags[j]);
      tagX += tagW + 8;
    }
    blockY += 28;

    for (const auto& line : wrappedSummary[cacheSlot]) {
      renderer.drawText(SMALL_FONT_ID, margin, blockY, line.c_str());
      blockY += 20;
    }
    blockY += 10;

    char metaBuf[128];
    if (meta.updatedDate[0] != '\0') {
      sprintf(metaBuf, "Chapters: %d   Words: %lu   Updated: %s",
              meta.chapterCount, (unsigned long)meta.wordCount, meta.updatedDate);
    } else {
      sprintf(metaBuf, "Chapters: %d   Words: %lu",
              meta.chapterCount, (unsigned long)meta.wordCount);
    }
    renderer.drawText(SMALL_FONT_ID, margin, blockY, metaBuf);
  }
}

void Ao3EndOfBookSeriesActivity::drawAo3Square(RenderLock& lock, int x, int y, int s,
                                               char rating, char warning, bool completed,
                                               BookStatus status) {
  const int h = s / 2;
  renderSymbol(x + 1, y + 1, h - 1, rating, true, false, false, false, -1);
  renderStatusSymbol(x + h + 1, y + 1, h - 1, status, false, true, false, false, -1);
  renderWarningSymbol(x + 1, y + h + 1, h - 1, warning, false, false, true, false, -2);
  renderCompletionSymbol(x + h + 1, y + h + 1, h - 1, completed, false, false, false, true, -2);
  renderer.drawRoundedRect(x, y, s, s, 1, 6, true);
  renderer.drawLine(x + 1, y + h, x + s - 1, y + h);
  renderer.drawLine(x + h, y + 1, x + h, y + s - 1);
}

void Ao3EndOfBookSeriesActivity::renderSymbol(int x, int y, int s, char c,
                                              bool tl, bool tr, bool bl, bool br, int yOffset) {
  Color bg = White;
  if (c == 'T') bg = LightGray;
  if (c == 'M') bg = DarkGray;
  if (c == 'E') bg = Black;
  if (bg != White) renderer.fillRoundedRect(x, y, s, s, 6, tl, tr, bl, br, bg);
  char buf[2] = {c, 0};
  if (c == '-' || c == 0) buf[0] = '-';
  const int tw = renderer.getTextWidth(UI_10_FONT_ID, buf);
  const int th = renderer.getTextHeight(UI_10_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, x + (s - tw) / 2, y + (s - th) / 2 + yOffset,
                    buf, (bg == DarkGray || bg == Black) ? false : true);
}

void Ao3EndOfBookSeriesActivity::renderStatusSymbol(int x, int y, int s, BookStatus status,
                                                    bool tl, bool tr, bool bl, bool br, int yOffset) {
  if (status == BookStatus::WAITING_FOR_CHAPTER || status == BookStatus::NEW_CHAPTER_AVAILABLE) {
    int triW = s / 2;
    int triH = s / 2;
    int triX = x + (s - triW) / 2;
    int triY = y + (s - triH) / 2 + yOffset;
    int xPts[] = { triX + triW / 2, triX, triX + triW };
    int yPts[] = { triY, triY + triH, triY + triH };
    renderer.fillPolygon(xPts, yPts, 3, Black);
    if (status == BookStatus::NEW_CHAPTER_AVAILABLE) {
      renderer.fillRoundedRect(x + s - 6, y - 3, 11, 10, 4, true, true, true, true, Black);
    }
    return;
  }
  const char* txt = "-";
  Color bg = White;
  if (status == BookStatus::READING)  { bg = LightGray; txt = "R"; }
  if (status == BookStatus::FINISHED) { bg = Black;     txt = "F"; }
  if (bg != White) renderer.fillRoundedRect(x, y, s, s, 6, tl, tr, bl, br, bg);
  const int tw = renderer.getTextWidth(UI_10_FONT_ID, txt);
  const int th = renderer.getTextHeight(UI_10_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, x + (s - tw) / 2, y + (s - th) / 2 + yOffset,
                    txt, (bg == Black) ? false : true);
}

void Ao3EndOfBookSeriesActivity::renderWarningSymbol(int x, int y, int s, char warning,
                                                     bool tl, bool tr, bool bl, bool br, int yOffset) {
  Color bg = White;
  const char* txt = "-";
  if (warning == 'B') { bg = DarkGray; txt = "!?"; }
  if (warning == '!') { bg = Black;    txt = "!"; }
  if (bg != White) renderer.fillRoundedRect(x, y, s, s, 6, tl, tr, bl, br, bg);
  const int tw = renderer.getTextWidth(UI_10_FONT_ID, txt);
  const int th = renderer.getTextHeight(UI_10_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, x + (s - tw) / 2, y + (s - th) / 2 + yOffset,
                    txt, (bg == DarkGray || bg == Black) ? false : true);
}

void Ao3EndOfBookSeriesActivity::renderCompletionSymbol(int x, int y, int s, bool completed,
                                                        bool tl, bool tr, bool bl, bool br, int yOffset) {
  const Color bg = completed ? LightGray : Black;
  renderer.fillRoundedRect(x, y, s, s, 6, tl, tr, bl, br, bg);
  if (completed) {
    for (int dy = 0; dy < 4; dy++) {
      renderer.drawLine(x + 8,  y + 14 + yOffset + dy, x + 12, y + 18 + yOffset + dy);
      renderer.drawLine(x + 12, y + 18 + yOffset + dy, x + 19, y + 11 + yOffset + dy);
    }
  } else {
    renderer.drawLine(x + 7, y + 8  + yOffset, x + 18, y + 18 + yOffset, 4, false);
    renderer.drawLine(x + 7, y + 18 + yOffset, x + 18, y + 8  + yOffset, 4, false);
  }
}