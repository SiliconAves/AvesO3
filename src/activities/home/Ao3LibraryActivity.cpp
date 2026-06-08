#include "Ao3LibraryActivity.h"
#include "../../Ao3Librarian.h"
#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <HalDisplay.h>
#include <Serialization.h>
#include <I18n.h>
#include "BookActionActivity.h"
#include "../../fontIds.h"
#include "../../MappedInputManager.h"
#include "../../components/UITheme.h"
#include "../../RecentBooksStore.h"

void Ao3LibraryActivity::onEnter() {
  buttonNavigator.setMappedInputManager(mappedInput);
  requestUpdate();
}

void Ao3LibraryActivity::loadEntries() {
  if (isLoaded) return;
  
  entries.clear();
  const char* cacheRoot = "/.crosspoint";
  FsFile root = Storage.open(cacheRoot);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    isLoaded = true;
    return;
  }

  root.rewindDirectory();
  char name[64];
  for (auto folder = root.openNextFile(); folder; folder = root.openNextFile()) {
    if (folder.isDirectory()) {
      folder.getName(name, sizeof(name));
      if (strncmp(name, "epub_", 5) == 0) {
        std::string infoPath = std::string(cacheRoot) + "/" + name + "/ao3_library_info";
        if (Storage.exists(infoPath.c_str())) {
          FsFile f;
          if (Storage.openFileForRead("AO3L", infoPath, f)) {
            Ao3LibraryMetadata m;
            if (f.read((uint8_t*)&m, sizeof(m)) == sizeof(m)) {
              if (m.isValid() && m.version == 8) {
                Entry e;
                e.path = m.filepath;
                e.title = m.title;
                e.author = m.author;
                e.rating = m.rating;
                e.warning = m.warning;
                e.completed = m.isCompleted;
                e.cachePath = infoPath;
                entries.push_back(e);
              }
            }
            f.close();
          }
        }
      }
    }
    folder.close();
    yield();
  }
  root.close();
  
  if (!entries.empty()) {
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
      return a.title < b.title;
    });
  }
  
  isLoaded = true;
  cachedPage = -1;    // Invalidate page cache on fresh scan
  buttonsSetup = false; // Reset so handlers are re-registered with the new entry count
}

BookStatus Ao3LibraryActivity::getBookStatus(const std::string& path) {
  std::string cachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(path));
  FsFile f;
  if (Storage.openFileForRead("AO3L", cachePath + "/progress.bin", f)) {
    uint8_t data[7];
    if (f.read(data, 7) >= 7) {
      f.close();
      return static_cast<BookStatus>(data[6]);
    }
    f.close();
  }
  return BookStatus::START;
}

void Ao3LibraryActivity::loadPageCache(int page) {
  const int startIdx = page * 3;
  const int endIdx = std::min(startIdx + 3, static_cast<int>(entries.size()));

  // Zero the cache slots first
  for (int i = 0; i < 3; i++) {
    new (&pageCache[i]) Ao3LibraryMetadata(); // Re-construct to zero all fields
    pageCacheStatus[i] = BookStatus::START;
  }

  for (int i = startIdx; i < endIdx; i++) {
    int slot = i - startIdx;
    FsFile f;
    if (Storage.openFileForRead("AO3L", entries[i].cachePath, f)) {
      f.read((uint8_t*)&pageCache[slot], sizeof(Ao3LibraryMetadata));
      f.close();
    }
    pageCacheStatus[slot] = getBookStatus(entries[i].path);
  }

  cachedPage = page;

  // Pre-compute wrapped summary lines for each slot so render() does zero CPU work
  const int textWidth = renderer.getScreenWidth() - 40; // margin * 2
  for (int i = 0; i < 3; i++) {
    wrappedSummary[i].clear();
    if (pageCache[i].summary[0] != 0) {
      // Replace newlines and carriage returns with spaces to avoid missing character placeholders
      for (int j = 0; pageCache[i].summary[j] != '\0'; j++) {
        if (pageCache[i].summary[j] == '\n' || pageCache[i].summary[j] == '\r') {
          pageCache[i].summary[j] = ' ';
        }
      }
      wrappedSummary[i] = renderer.wrappedText(SMALL_FONT_ID, pageCache[i].summary, textWidth, 3);
    }
  }
}

void Ao3LibraryActivity::loop() {
  if (!isLoaded) {
    loadEntries();
    requestUpdate();
    return;
  }

  const int total = static_cast<int>(entries.size());
  if (total > 0) {
    // Tap: step one entry
    buttonNavigator.onNextPress([this, total] {
      selectorIndex = (selectorIndex + 1) % total;
      requestUpdate();
    });
    buttonNavigator.onPreviousPress([this, total] {
      selectorIndex = (selectorIndex + total - 1) % total;
      requestUpdate();
    });
    // Hold: jump a full page
    buttonNavigator.onNextContinuous([this, total] {
      selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, total, 3);
      requestUpdate();
    });
    buttonNavigator.onPreviousContinuous([this, total] {
      selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, total, 3);
      requestUpdate();
    });
    // Side buttons: also page jump (kept for compatibility)
    buttonNavigator.onSideNextContinuous([this, total] {
      selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, total, 3);
      requestUpdate();
    });
    buttonNavigator.onSidePreviousContinuous([this, total] {
      selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, total, 3);
      requestUpdate();
    });
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.popActivity();
    return;
  }

  if (!entries.empty() && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const auto& entry = entries[selectorIndex];
    if (mappedInput.getHeldTime() >= 1000) {
      auto handler = [this, entry](const ActivityResult& res) {
        if (const auto* actionRes = std::get_if<BookActionResult>(&res.data)) {
          if (actionRes->modified) {
            if (actionRes->deleted) {
              if (Storage.remove(entry.path.c_str())) {
                Epub(entry.path, "/.crosspoint").clearCache();
                isLoaded = false; // Force a reload
              }
            } else {
              // Update status without full reload if it was just a status change
              for (size_t i = 0; i < entries.size(); i++) {
                if (entries[i].path == entry.path) {
                  // Only update the cache if it's currently on-screen
                  int page = i / 3;
                  if (page == cachedPage) {
                    pageCacheStatus[i % 3] = actionRes->newStatus;
                  }
                  break;
                }
              }
            }
            requestUpdate(true);
          }
        }
      };
      startActivityForResult(std::make_unique<BookActionActivity>(renderer, mappedInput, entry.path, entry.title), handler);
    } else {
      activityManager.goToReader(entry.path);
    }
    return;
  }
}

void Ao3LibraryActivity::render(RenderLock&& lock) {
  renderer.clearScreen();
  
  // Header with BOLD title
  renderer.drawText(UI_12_FONT_ID, 15, 12, "AO3 Library", true, EpdFontFamily::BOLD);
  renderer.drawLine(0, 48, renderer.getScreenWidth(), 48); // Moved down 6px

  if (!isLoaded) {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight()/2, "Scanning AO3 Global Library...");
    renderer.displayBuffer();
    return;
  }

  if (entries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight()/2 - 20, "No AO3 books identified.");
    renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight()/2 + 10, "Open fics once to see them here.");
  } else {
    const int startIdx = (selectorIndex / 3) * 3;
    const int endIdx = std::min(startIdx + 3, static_cast<int>(entries.size()));

    // Evenly distribute entries in the space between header and button hints
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int topPad = 18;
    
    // Calculate slot size using the original 42px header line to maintain the exact 
    // same internal entry size and spacing that the user called "perfect"
    const int contentEnd = renderer.getScreenHeight() - metrics.buttonHintsHeight;
    const int entrySlot = (contentEnd - (42 + topPad)) / 3;

    // Start the content block 6px lower to preserve the 18px distance from the new 48px header line
    const int contentStart = 48 + topPad;

    // Reload cache if the visible page has changed (e.g. on page turn)
    const int currentPage = static_cast<int>(selectorIndex) / 3;
    if (currentPage != cachedPage) {
      loadPageCache(currentPage);
    }

    int y = contentStart;
    for (int i = startIdx; i < endIdx; i++) {
      bool selected = (i == static_cast<int>(selectorIndex));
      renderEntry(lock, y, entries[i], i - startIdx, selected);
      y += entrySlot;
      if (i < endIdx - 1) {
        // Separator sits in the middle of the gap between entries
        renderer.drawLine(15, y - topPad, renderer.getScreenWidth() - 15, y - topPad);
      }
    }

    if (entries.size() > 3) {
      char pageBuf[32];
      sprintf(pageBuf, "%d / %d", (startIdx / 3) + 1, (static_cast<int>(entries.size()) + 2) / 3);
      renderer.drawText(SMALL_FONT_ID, renderer.getScreenWidth() - 60, 15, pageBuf);
    }
  }

  // Standard Button Hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void Ao3LibraryActivity::renderEntry(RenderLock& lock, int y, const Entry& entry, int cacheSlot, bool selected) {
  const int margin = 20;
  const int selectionHeight = 56; // Slimmer: still centered around title+author rows
  const int squareSize = selectionHeight;
  const int textX = margin + squareSize + 15;

  if (selected) {
    // Selection only covers the text block, not the square
    renderer.fillRoundedRect(textX - 8, y - 3, renderer.getScreenWidth() - textX - 15, selectionHeight + 6, 8, LightGray);
  }

  // Block 1: Square + Title/Author (side by side)
  drawAo3Square(lock, margin, y, entry, squareSize, pageCacheStatus[cacheSlot]);

  // Read metadata from page cache early to use it for series info
  const Ao3LibraryMetadata& meta = pageCache[cacheSlot];
  const bool metaLoaded = meta.isValid();

  std::string title = entry.title;
  std::string authorText = entry.author; 
  
  if (metaLoaded && meta.seriesName[0] != 0) {
    char seriesBuf[256];
    // ASCII bullet dot · (183) or bullet •
    // Let's use the UTF-8 bullet point "•" (U+2022) which is usually supported.
    if (meta.seriesPart > 0) {
      sprintf(seriesBuf, " • %d of %s", meta.seriesPart, meta.seriesName);
    } else {
      sprintf(seriesBuf, " • %s", meta.seriesName);
    }
    authorText += seriesBuf;
  }

  const int maxTextWidth = renderer.getScreenWidth() - textX - 25; // Keep inside selection rect with a 10px right margin

  
  // Reusable UTF-8 safe truncation logic
  auto truncateToFit = [&](std::string& text, int fontId, EpdFontFamily::Style style) {
    if (renderer.getTextWidth(fontId, text.c_str(), style) > maxTextWidth) {
      while (!text.empty() && renderer.getTextWidth(fontId, (text + "..").c_str(), style) > maxTextWidth) {
        while (!text.empty()) {
          char c = text.back();
          text.pop_back();
          if ((c & 0xC0) != 0x80) break; // Stop popping when we hit the start byte of a UTF-8 char
        }
      }
      text += "..";
    }
  };

  truncateToFit(title, UI_12_FONT_ID, EpdFontFamily::BOLD);
  truncateToFit(authorText, UI_10_FONT_ID, EpdFontFamily::REGULAR);

  renderer.drawText(UI_12_FONT_ID, textX, y + 6, title.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, textX, y + 32, authorText.c_str());

  // Block 2: Tags, Summary, Meta — read directly from page cache, zero SD ops

  if (metaLoaded) {
    int blockY = y + selectionHeight + 12;

    // Tags row
    int tagX = margin;
    for (int j = 0; j < 4; j++) {
      if (meta.tags[j][0] == 0) break;
      int tagW = renderer.getTextWidth(SMALL_FONT_ID, meta.tags[j]) + 16;
      if (tagX + tagW > renderer.getScreenWidth() - margin) break;
      renderer.drawRoundedRect(tagX, blockY, tagW, 20, 1, 6, true);
      renderer.drawText(SMALL_FONT_ID, tagX + 8, blockY - 2, meta.tags[j]);
      tagX += tagW + 8;
    }
    blockY += 28; // Uniform gap after tags

    // Summary (max 3 lines) — pre-computed in loadPageCache, zero CPU overhead here
    for (const auto& line : wrappedSummary[cacheSlot]) {
      renderer.drawText(SMALL_FONT_ID, margin, blockY, line.c_str());
      blockY += 20;
    }
    blockY += 10; // Slightly larger gap before meta row

    // Chapters + Words flows right after summary, not hardcoded
    char metaBuf[128];
    if (meta.updatedDate[0] != '\0') {
      sprintf(metaBuf, "Chapters: %d   Words: %lu   Updated: %s", meta.chapterCount, (unsigned long)meta.wordCount, meta.updatedDate);
    } else {
      sprintf(metaBuf, "Chapters: %d   Words: %lu", meta.chapterCount, (unsigned long)meta.wordCount);
    }
    renderer.drawText(SMALL_FONT_ID, margin, blockY, metaBuf);
  }
}

void Ao3LibraryActivity::drawAo3Square(RenderLock& lock, int x, int y, const Entry& entry, int s, BookStatus status) {
  const int h = s / 2;

  // Status is pre-fetched from page cache — no SD access here
  // Rounded fills for each quadrant; top symbols -1px, bottom symbols -2px for visual centering
  renderSymbol(x + 1, y + 1, h - 1, entry.rating, true, false, false, false, -1);            // Top-Left
  renderStatusSymbol(x + h + 1, y + 1, h - 1, status, false, true, false, false, -1);        // Top-Right
  renderWarningSymbol(x + 1, y + h + 1, h - 1, entry.warning, false, false, true, false, -2); // Bottom-Left
  renderCompletionSymbol(x + h + 1, y + h + 1, h - 1, entry.completed, false, false, false, true, -2); // Bottom-Right

  // Draw border on top for clean edges
  renderer.drawRoundedRect(x, y, s, s, 1, 6, true);
  // Lines stop 1px inside the outer border to avoid overshooting
  renderer.drawLine(x + 1, y + h, x + s - 1, y + h); // Horizontal divider
  renderer.drawLine(x + h, y + 1, x + h, y + s - 1); // Vertical divider
}

void Ao3LibraryActivity::renderSymbol(int x, int y, int s, char c, bool tl, bool tr, bool bl, bool br, int yOffset) {
  Color bg = White;
  if (c == 'T') bg = LightGray;
  if (c == 'M') bg = DarkGray;
  if (c == 'E') bg = Black;
  if (bg != White) renderer.fillRoundedRect(x, y, s, s, 6, tl, tr, bl, br, bg);
  char buf[2] = {c, 0};
  if (c == '-' || c == 0) buf[0] = '-';
  int tw = renderer.getTextWidth(UI_10_FONT_ID, buf);
  int th = renderer.getTextHeight(UI_10_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, x + (s - tw)/2, y + (s - th)/2 + yOffset, buf, (bg == DarkGray || bg == Black) ? false : true);
}

void Ao3LibraryActivity::renderStatusSymbol(int x, int y, int s, BookStatus status, bool tl, bool tr, bool bl, bool br, int yOffset) {
  const char* txt = "-";
  Color bg = White;
  if (status == BookStatus::READING) { bg = LightGray; txt = "R"; }
  if (status == BookStatus::FINISHED) { bg = Black; txt = "F"; }
  if (status == BookStatus::WAITING_FOR_CHAPTER) { bg = White; txt = "•"; }
  if (status == BookStatus::NEW_CHAPTER_AVAILABLE) { bg = Black; txt = "•"; }
  if (bg != White) renderer.fillRoundedRect(x, y, s, s, 6, tl, tr, bl, br, bg);
  int tw = renderer.getTextWidth(UI_10_FONT_ID, txt);
  int th = renderer.getTextHeight(UI_10_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, x + (s - tw)/2, y + (s - th)/2 + yOffset, txt, (bg == Black) ? false : true);
}

void Ao3LibraryActivity::renderWarningSymbol(int x, int y, int s, char warning, bool tl, bool tr, bool bl, bool br, int yOffset) {
  Color bg = White;
  const char* txt = "-";
  if (warning == 'B') { bg = DarkGray; txt = "!?"; }
  if (warning == '!') { bg = Black; txt = "!"; }
  if (bg != White) renderer.fillRoundedRect(x, y, s, s, 6, tl, tr, bl, br, bg);
  int tw = renderer.getTextWidth(UI_10_FONT_ID, txt);
  int th = renderer.getTextHeight(UI_10_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, x + (s - tw)/2, y + (s - th)/2 + yOffset, txt, (bg == DarkGray || bg == Black) ? false : true);
}

void Ao3LibraryActivity::renderCompletionSymbol(int x, int y, int s, bool completed, bool tl, bool tr, bool bl, bool br, int yOffset) {
  Color bg = completed ? LightGray : Black;
  const char* txt = completed ? "v" : "x"; 
  renderer.fillRoundedRect(x, y, s, s, 6, tl, tr, bl, br, bg);
  int tw = renderer.getTextWidth(UI_10_FONT_ID, txt);
  int th = renderer.getTextHeight(UI_10_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, x + (s - tw)/2, y + (s - th)/2 + yOffset, txt, (bg == Black) ? false : true);
}
