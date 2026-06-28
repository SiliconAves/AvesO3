#include "Ao3LibraryActivity.h"
#include "../../Ao3Librarian.h"
#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <HalDisplay.h>
#include <Serialization.h>
#include <I18n.h>
#include <cstring>
#include <algorithm>
#include <new>
#include <ArduinoJson.h>
#include "BookActionActivity.h"
#include "../../fontIds.h"
#include "../../MappedInputManager.h"
#include "../../components/UITheme.h"
#include "../../RecentBooksStore.h"

// ---------------------------------------------------------------------------
//  onEnter
// ---------------------------------------------------------------------------

void Ao3LibraryActivity::onEnter() {
  buttonNavigator.setMappedInputManager(mappedInput);
  indexState = IndexState::UNKNOWN;
  screenState = ScreenState::LIBRARY;
  loadSortFilterState();
  requestUpdate();
}

// ---------------------------------------------------------------------------
//  loadViewEntries — delegates to rebuildViewEntries
// ---------------------------------------------------------------------------

void Ao3LibraryActivity::loadViewEntries() {
  if (indexState != IndexState::UNKNOWN) return;
  rebuildViewEntries();
}

// ---------------------------------------------------------------------------
//  getBookStatus — reads the 7-byte progress.bin for the given cache hash
// ---------------------------------------------------------------------------

BookStatus Ao3LibraryActivity::getBookStatus(uint32_t cacheHash) {
  std::string cachePath = "/.crosspoint/epub_" + std::to_string(cacheHash) + "/progress.bin";
  FsFile f;
  if (Storage.openFileForRead("AO3L", cachePath, f)) {
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
//  loadPageCache — pulls full Ao3LibraryMetadata for the visible 3 entries
// ---------------------------------------------------------------------------

void Ao3LibraryActivity::loadPageCache(int page) {
  const int startIdx = page * 3;
  const int endIdx   = std::min(startIdx + 3, static_cast<int>(viewEntries.size()));

  // Zero all slots first
  for (int i = 0; i < 3; i++) {
    new (&pageCache[i]) Ao3LibraryMetadata();
    pageCacheStatus[i] = BookStatus::START;
  }

  for (int i = startIdx; i < endIdx; i++) {
    const int slot = i - startIdx;
    std::string infoPath =
        "/.crosspoint/epub_" + std::to_string(viewEntries[i].cacheHash) + "/ao3_library_info";
    FsFile f;
    if (Storage.openFileForRead("AO3L", infoPath, f)) {
      f.read((uint8_t*)&pageCache[slot], sizeof(Ao3LibraryMetadata));
      f.close();
    }
    pageCacheStatus[slot] = getBookStatus(viewEntries[i].cacheHash);
  }

  cachedPage = page;

  // Pre-compute wrapped summary lines — zero CPU work in render()
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

void Ao3LibraryActivity::loop() {
  // Still loading — loadViewEntries() will flip indexState on first call
  if (indexState == IndexState::UNKNOWN) {
    loadViewEntries();
    requestUpdate();
    return;
  }

  // --- STATE: LIBRARY ---
  if (screenState == ScreenState::LIBRARY) {

    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      screenState = ScreenState::FILTER_PANEL;
      pendingState = activeState;
      overlayRowIndex = 0;
      requestUpdate(true);
      return;
    }
    const int total = static_cast<int>(viewEntries.size());
    if (total > 0) {
      // Tap: step one entry using Right and Left buttons only
      buttonNavigator.onPress({MappedInputManager::Button::Right}, [this, total] {
        selectorIndex = (selectorIndex + 1) % total;
        requestUpdate();
      });
      buttonNavigator.onPress({MappedInputManager::Button::Left}, [this, total] {
        selectorIndex = (selectorIndex + total - 1) % total;
        requestUpdate();
      });
      // Hold: jump a full page using Right and Left buttons only
      buttonNavigator.onContinuous({MappedInputManager::Button::Right}, [this, total] {
        selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, total, 3);
        requestUpdate();
      });
      buttonNavigator.onContinuous({MappedInputManager::Button::Left}, [this, total] {
        selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, total, 3);
        requestUpdate();
      });
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      activityManager.popActivity();
      return;
    }

    if (!viewEntries.empty() && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      // Ensure the page cache is fresh for the current selector position
      const int selPage = static_cast<int>(selectorIndex) / 3;
      if (selPage != cachedPage) loadPageCache(selPage);

      const int slot = static_cast<int>(selectorIndex) % 3;
      // Full epub path and title live in the ao3_library_info sidecar (page cache)
      const std::string epubPath(pageCache[slot].filepath);
      const std::string epubTitle(
          pageCache[slot].title[0] ? pageCache[slot].title : viewEntries[selectorIndex].title);
      const uint32_t hash = viewEntries[selectorIndex].cacheHash;

      if (mappedInput.getHeldTime() >= 1000 && !epubPath.empty()) {
        // Long-press → BookAction menu
        auto handler = [this, epubPath, hash](const ActivityResult& res) {
          if (const auto* actionRes = std::get_if<BookActionResult>(&res.data)) {
            if (actionRes->modified) {
              if (actionRes->deleted) {
                // Tombstone in the index, remove file + cache from disk
                Ao3Librarian::tombstoneRecord(epubPath);
                if (Storage.remove(epubPath.c_str())) {
                  Epub(epubPath, "/.crosspoint").clearCache();
                }
                // Remove from in-RAM viewEntries (no full reload needed)
                auto it = std::find_if(viewEntries.begin(), viewEntries.end(),
                    [hash](const ViewEntry& v) { return v.cacheHash == hash; });
                if (it != viewEntries.end()) viewEntries.erase(it);

                // Clamp selectorIndex so we don't go out of bounds on next render
                if (!viewEntries.empty()) {
                  if (selectorIndex >= viewEntries.size()) {
                    selectorIndex = viewEntries.size() - 1;
                  }
                } else {
                  selectorIndex = 0;
                }
                cachedPage = -1; // invalidate so next render reloads page cache
              } else {
                // Status change only — update in-place without a full reload
                if (static_cast<int>(selectorIndex) / 3 == cachedPage) {
                  pageCacheStatus[selectorIndex % 3] = actionRes->newStatus;
                }
              }
              requestUpdate(true);
            }
          }
        };
        startActivityForResult(
            std::make_unique<BookActionActivity>(renderer, mappedInput, epubPath, epubTitle),
            handler);
      } else if (!epubPath.empty()) {
        activityManager.goToReader(epubPath);
      }
      return;
    }
  }

  // --- STATE: FILTER_PANEL ---
  else if (screenState == ScreenState::FILTER_PANEL) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      screenState = ScreenState::LIBRARY;
      requestUpdate(true);
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      if (mappedInput.getHeldTime() >= 500) {
        // Skip to confirm
        overlayRowIndex = 4;
      } else {
        // Move Next Row (Wrapping around total of 5 rows: 0 to 4)
        overlayRowIndex = (overlayRowIndex + 1) % 5;
        
        // If Relationship row is disabled, skip it moving downwards
        if (overlayRowIndex == 1 && pendingState.fandom[0] == '\0') {
          overlayRowIndex = 2; 
        }
      }
      requestUpdate(true);
      return;
    }

if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
        mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      if (mappedInput.getHeldTime() >= 500) {
        overlayRowIndex = 4;
      } else {
        // Move Prev Row (Wrapping around backward: 0 becomes 4)
        overlayRowIndex = (overlayRowIndex + 5 - 1) % 5;
        
        // If Relationship row is disabled, skip it moving upwards
        if (overlayRowIndex == 1 && pendingState.fandom[0] == '\0') {
          overlayRowIndex = 0; 
        }
      }
      requestUpdate(true);
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (overlayRowIndex == 0) {
        // Fandom row: cycle if <= 3, open picker if >= 4
        std::vector<std::string> fandoms;
        buildFandomList(fandoms);
        if (fandoms.size() <= 4) {
          pickerItems.clear();
          pickerItems.push_back("Any");
          for (const auto& f : fandoms) pickerItems.push_back(f);

          size_t currentIdx = 0;
          for (size_t i = 0; i < pickerItems.size(); i++) {
            if (strcmp(pendingState.fandom, pickerItems[i].c_str()) == 0) {
              currentIdx = i;
              break;
            }
          }
          currentIdx = (currentIdx + 1) % pickerItems.size();
          if (currentIdx == 0) {
            pendingState.fandom[0] = '\0';
            pendingState.relationship[0] = '\0';
            pendingState.relationshipNoneOnly = false;
          } else {
            strncpy(pendingState.fandom, pickerItems[currentIdx].c_str(), 31);
            pendingState.fandom[31] = '\0';
            pendingState.relationship[0] = '\0';
            pendingState.relationshipNoneOnly = false;
          }
        } else {
          screenState = ScreenState::FANDOM_PICKER;
          pickerItems.clear();
          pickerItems.push_back("Any");
          for (const auto& f : fandoms) pickerItems.push_back(f);

          pickerSelectedIndex = 0;
          for (size_t i = 0; i < pickerItems.size(); i++) {
            if (strcmp(pendingState.fandom, pickerItems[i].c_str()) == 0) {
              pickerSelectedIndex = i;
              break;
            }
          }
        }
      } else if (overlayRowIndex == 1) {
        // Relationship row: always opens a list picker if fandom is active
        if (pendingState.fandom[0] != '\0') {
          screenState = ScreenState::RELATIONSHIP_PICKER;
          std::vector<std::string> rels;
          buildRelationshipList(pendingState.fandom, rels, pickerHasNone);

          pickerItems.clear();
          pickerItems.push_back("Any");
          if (pickerHasNone) pickerItems.push_back("None");
          for (const auto& r : rels) pickerItems.push_back(r);

          pickerSelectedIndex = 0;
          if (pendingState.relationshipNoneOnly) {
            if (pickerHasNone) pickerSelectedIndex = 1;
          } else if (pendingState.relationship[0] != '\0') {
            for (size_t i = 0; i < pickerItems.size(); i++) {
              if (strcmp(pendingState.relationship, pickerItems[i].c_str()) == 0) {
                pickerSelectedIndex = i;
                break;
              }
            }
          }
        }
      } else if (overlayRowIndex == 2) {
        // Sort by cycle: Title -> Author -> Word Count -> Date Added -> Series -> Title
        switch (pendingState.sortMode) {
          case SortMode::ALPHABETIC: pendingState.sortMode = SortMode::AUTHOR; break;
          case SortMode::AUTHOR: pendingState.sortMode = SortMode::WORD_COUNT; break;
          case SortMode::WORD_COUNT: pendingState.sortMode = SortMode::DATE_ADDED; break;
          case SortMode::DATE_ADDED: pendingState.sortMode = SortMode::SERIES; break;
          case SortMode::SERIES: pendingState.sortMode = SortMode::ALPHABETIC; break;
        }
      } else if (overlayRowIndex == 3) {
        // Order cycle
        pendingState.ascending = !pendingState.ascending;
      } else if (overlayRowIndex == 4) {
        // Confirm Button pressed
        SortFilterState previous = activeState;
        activeState = pendingState;
        screenState = ScreenState::LIBRARY;
        saveSortFilterState();
        applyStateChange(previous, activeState);
      }
      requestUpdate(true);
      return;
    }
  }

  // --- STATE: PICKERS ---
  else if (screenState == ScreenState::FANDOM_PICKER || screenState == ScreenState::RELATIONSHIP_PICKER) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      screenState = ScreenState::FILTER_PANEL;
      requestUpdate(true);
      return;
    }

  const int total = static_cast<int>(pickerItems.size());
    if (total > 0) {
      // Tap: step one item
      buttonNavigator.onPress({MappedInputManager::Button::Down, MappedInputManager::Button::Right}, [this, total] {
        pickerSelectedIndex = (pickerSelectedIndex + 1) % total;
        requestUpdate(true);
      });
      buttonNavigator.onPress({MappedInputManager::Button::Up, MappedInputManager::Button::Left}, [this, total] {
        pickerSelectedIndex = (pickerSelectedIndex + total - 1) % total;
        requestUpdate(true);
      });
      // Hold: skip 2 items continuously without lifting finger
      buttonNavigator.onContinuous({MappedInputManager::Button::Down, MappedInputManager::Button::Right}, [this, total] {
        pickerSelectedIndex = (pickerSelectedIndex + 2) % total;
        requestUpdate(true);
      });
      buttonNavigator.onContinuous({MappedInputManager::Button::Up, MappedInputManager::Button::Left}, [this, total] {
        pickerSelectedIndex = (pickerSelectedIndex + total - 2) % total;
        requestUpdate(true);
      });
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (screenState == ScreenState::FANDOM_PICKER) {
        if (pickerSelectedIndex == 0) {
          pendingState.fandom[0] = '\0';
          pendingState.relationship[0] = '\0';
          pendingState.relationshipNoneOnly = false;
        } else {
          strncpy(pendingState.fandom, pickerItems[pickerSelectedIndex].c_str(), 31);
          pendingState.fandom[31] = '\0';
          pendingState.relationship[0] = '\0';
          pendingState.relationshipNoneOnly = false;
        }
      } else {
        if (pickerSelectedIndex == 0) {
          pendingState.relationship[0] = '\0';
          pendingState.relationshipNoneOnly = false;
        } else if (pickerSelectedIndex == 1 && pickerHasNone) {
          pendingState.relationship[0] = '\0';
          pendingState.relationshipNoneOnly = true;
        } else {
          strncpy(pendingState.relationship, pickerItems[pickerSelectedIndex].c_str(), 31);
          pendingState.relationship[31] = '\0';
          pendingState.relationshipNoneOnly = false;
        }
      }
      screenState = ScreenState::FILTER_PANEL;
      requestUpdate(true);
      return;
    }
  }
}

// ---------------------------------------------------------------------------
//  render
// ---------------------------------------------------------------------------

void Ao3LibraryActivity::render(RenderLock&& lock) {
  if (screenState == ScreenState::FANDOM_PICKER || screenState == ScreenState::RELATIONSHIP_PICKER) {
    renderPicker();
    return;
  }

  // Draw library view (serves as the background for the overlay as well)
  renderLibrary(lock);

  if (screenState == ScreenState::FILTER_PANEL) {
    renderFilterOverlay();
  }

  renderer.displayBuffer();
}

// ---------------------------------------------------------------------------
//  renderLibrary — renders standard book list (or empty messages)
// ---------------------------------------------------------------------------

void Ao3LibraryActivity::renderLibrary(RenderLock& lock) {
  renderer.clearScreen();

  // Draw Header Title (Truncate to 25 chars if fandom filter is active)
  char headerTitle[32] = "AO3 Library";
  if (activeState.fandom[0] != '\0') {
    std::string cleanFandom(activeState.fandom);
    if (cleanFandom.length() > 29) {
      cleanFandom = cleanFandom.substr(0, 27) + "..";
    }
    strcpy(headerTitle, cleanFandom.c_str());
  }
  renderer.drawText(UI_12_FONT_ID, 15, 12, headerTitle, true, EpdFontFamily::BOLD);
  renderer.drawLine(0, 48, renderer.getScreenWidth(), 48);

  // States handling
  if (indexState == IndexState::UNKNOWN) {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, "Loading...");
    return;
  }
      // Downward triangle indicator
    {
      const int tx = renderer.getScreenWidth() - 26;
      const int ty = 26;
      if (screenState == ScreenState::FILTER_PANEL) {
        // triangle with black border pointing up
        const int xPts[] = { tx, tx + 12, tx + 6 };
        const int yPts[] = { ty + 5, ty + 5, ty - 5 };
        renderer.fillPolygon(xPts, yPts, 3, Black);
      } else {
        // Solid black triangle pointing down
        const int xPts[] = { tx, tx + 12, tx + 6 };
        const int yPts[] = { ty - 5, ty - 5, ty + 5 };
        renderer.fillPolygon(xPts, yPts, 3, Black);
      }
    }  

  if (indexState == IndexState::MISSING || (indexState == IndexState::OK && viewEntries.empty())) {
    if (activeState.fandom[0] != '\0') {
      renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2 - 12, "No matches for current filter.");
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2 - 12, "No books indexed yet.");
      renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight() / 2 + 12, "Open a book to add it.");
    }
  } else if (indexState == IndexState::CORRUPT) {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2 - 12, "Library index missing or corrupt.");
    renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight() / 2 + 12, "Open a book to recreate it.");
  } else {
    const int startIdx = (selectorIndex / 3) * 3;
    const int endIdx   = std::min(startIdx + 3, static_cast<int>(viewEntries.size()));

    const auto& metrics = UITheme::getInstance().getMetrics();
    const int   topPad  = 18;

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
      const int counterX =
          renderer.getScreenWidth() - 36 - renderer.getTextWidth(SMALL_FONT_ID, pageBuf);
      renderer.drawText(SMALL_FONT_ID, counterX, 15, pageBuf);
    }
  }

  // Draw Button hints
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---------------------------------------------------------------------------
//  renderFilterOverlay — draws the drop-down menu overlay
// ---------------------------------------------------------------------------

void Ao3LibraryActivity::renderFilterOverlay() {
  const int screenWidth = renderer.getScreenWidth();
  const int overlayH = 320;
  const int startY = 48;

  // Background white filled rectangle
  renderer.fillRoundedRect(0, startY, screenWidth, overlayH, 0, White);
  // Top black edge
  renderer.fillRect(0, startY, screenWidth, 1, Black);
  // Bottom edge has a 6-pixel-thick black line
  for (int i = 0; i < 6; i++) {
    renderer.drawLine(0, startY + overlayH - 6 + i, screenWidth, startY + overlayH - 6 + i);
  }

  // Margins
  const int margin = 20;

  // Ubuntu 12pt Bold Header "Sort & Filter"
  renderer.drawText(UI_12_FONT_ID, margin + 12, startY + 20, "Sort & Filter", true, EpdFontFamily::BOLD);

  // helper to draw pill or label
  auto drawOverlayRow = [&](int rowIndex, const char* label, const char* val, bool disabled = false) {
    const int rowY = startY + 55 + rowIndex * 40 + ((rowIndex < 4) ? 6 : 0) + ((rowIndex == 2 || rowIndex == 3) ? 20 : 0);
    const bool isSelected = (overlayRowIndex == rowIndex);

    if (isSelected && !disabled) {
      renderer.fillRoundedRect(margin, rowY - 6, screenWidth - (2 * margin), 34, 6, true, true, true, true, LightGray);
    }

    // Label text
    renderer.drawText(UI_10_FONT_ID, margin + 12, rowY, label, !disabled);

    // Value text
    if (isSelected && !disabled) {
      // Draw black pill
      int valW = renderer.getTextWidth(UI_10_FONT_ID, val);
      int pillW = valW + 24;
      int pillX = screenWidth - margin - pillW;
      renderer.fillRoundedRect(pillX, rowY - 6, pillW, 34, 6, Black);
      renderer.drawText(UI_10_FONT_ID, pillX + 12, rowY - 1, val, false);
    } else {
      // Plain right-aligned value text
      int valW = renderer.getTextWidth(UI_10_FONT_ID, val);
      renderer.drawText(UI_10_FONT_ID, screenWidth - margin - valW - 12, rowY, val, !disabled);
    }
  };

  // 16-character truncation helper
  auto getTruncatedValue = [](const char* src) -> std::string {
    if (!src || src[0] == '\0') return "Any";
    std::string raw(src);
    if (raw.length() > 27) {
      return raw.substr(0, 25) + "..";
    }
    return raw;
  };

  // Row 0: Fandom
  std::string fandomVal = getTruncatedValue(pendingState.fandom);
  drawOverlayRow(0, "Fandom", fandomVal.c_str());

  // Row 1: Relationship (disabled if fandom is "Any")
  bool relDisabled = (pendingState.fandom[0] == '\0');
  std::string relVal = "Any";
  if (pendingState.relationshipNoneOnly) {
    relVal = "None";
  } else {
    relVal = getTruncatedValue(pendingState.relationship);
  }
  drawOverlayRow(1, "Relationship", relVal.c_str(), relDisabled);

  // Row 2: Sort By
  const char* sortLabel = "Title";
  switch (pendingState.sortMode) {
    case SortMode::ALPHABETIC: sortLabel = "Title"; break;
    case SortMode::AUTHOR: sortLabel = "Author"; break;
    case SortMode::WORD_COUNT: sortLabel = "Word Count"; break;
    case SortMode::DATE_ADDED: sortLabel = "Date Added"; break;
    case SortMode::SERIES: sortLabel = "Series"; break;
  }
  drawOverlayRow(2, "Sort by", sortLabel);

  // Row 3: Order
  drawOverlayRow(3, "Order", pendingState.ascending ? "Ascending" : "Descending");

  // Row 4: Confirm Button
  const int btnY = startY + 250;
  const int btnW = screenWidth - margin * 2;
  const int btnH = 36;
  const bool confirmSelected = (overlayRowIndex == 4);

  if (confirmSelected) {
    renderer.fillRoundedRect(margin, btnY, btnW, btnH, 8, Black);
    renderer.drawRoundedRect(margin, btnY, btnW, btnH, 1, 8, true);
    renderer.drawCenteredText(UI_12_FONT_ID, btnY + 2, "Confirm", false, EpdFontFamily::BOLD);
  } else {
    renderer.drawRoundedRect(margin, btnY, btnW, btnH, 1, 8, true);
    renderer.drawCenteredText(UI_12_FONT_ID, btnY + 2, "Confirm", true, EpdFontFamily::BOLD);
  }
}

// ---------------------------------------------------------------------------
//  renderPicker — draws full screen pickers
// ---------------------------------------------------------------------------

void Ao3LibraryActivity::renderPicker() {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const char* title = (screenState == ScreenState::FANDOM_PICKER) ? "Select Fandom" : "Select Relationship";

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, title);

  auto rowTitle = [this](int index) {
    return pickerItems[index];
  };

  Rect listRect{0, metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing,
               renderer.getScreenWidth(),
               renderer.getScreenHeight() - metrics.headerHeight - metrics.buttonHintsHeight - metrics.verticalSpacing * 2};

  GUI.drawList(renderer, listRect, pickerItems.size(), pickerSelectedIndex, rowTitle);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

// ---------------------------------------------------------------------------
//  renderEntry
// ---------------------------------------------------------------------------

void Ao3LibraryActivity::renderEntry(RenderLock& lock, int y, const ViewEntry& ve, int cacheSlot, bool selected) {
  const int margin         = 20;
  const int selectionHeight = 56;
  const int squareSize     = selectionHeight;
  const int textX          = margin + squareSize + 15;

  if (selected) {
    renderer.fillRoundedRect(textX - 8, y - 3,
                             renderer.getScreenWidth() - textX - 15,
                             selectionHeight + 6, 8, LightGray);
  }

  const Ao3LibraryMetadata& meta       = pageCache[cacheSlot];
  const bool                metaLoaded = meta.isValid();

  const char rating    = metaLoaded ? meta.rating    : '-';
  const char warning   = metaLoaded ? meta.warning   : 0;
  const bool completed = metaLoaded ? (bool)meta.isCompleted : false;

  drawAo3Square(lock, margin, y, squareSize, rating, warning, completed, pageCacheStatus[cacheSlot]);

  std::string title = metaLoaded && meta.title[0] ? std::string(meta.title) : std::string(ve.title);
  std::string authorText = metaLoaded && meta.author[0]
                               ? std::string(meta.author)
                               : std::string(ve.authorKey);

  if (metaLoaded && meta.seriesName[0] != 0) {
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

    // Tags
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

// ---------------------------------------------------------------------------
//  drawAo3Square
// ---------------------------------------------------------------------------

void Ao3LibraryActivity::drawAo3Square(RenderLock& lock, int x, int y, int s,
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

// ---------------------------------------------------------------------------
//  Symbol Renderers
// ---------------------------------------------------------------------------

void Ao3LibraryActivity::renderSymbol(int x, int y, int s, char c, bool tl, bool tr, bool bl, bool br, int yOffset) {
  Color bg = White;
  if (c == 'T') bg = LightGray;
  if (c == 'M') bg = DarkGray;
  if (c == 'E') bg = Black;
  if (bg != White) renderer.fillRoundedRect(x, y, s, s, 6, tl, tr, bl, br, bg);
  char buf[2] = {c, 0};
  if (c == '-' || c == 0) buf[0] = '-';
  const int tw = renderer.getTextWidth(UI_10_FONT_ID, buf);
  const int th = renderer.getTextHeight(UI_10_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, x + (s - tw)/2, y + (s - th)/2 + yOffset,
                    buf, (bg == DarkGray || bg == Black) ? false : true);
}

void Ao3LibraryActivity::renderStatusSymbol(int x, int y, int s, BookStatus status, bool tl, bool tr, bool bl, bool br, int yOffset) {
  // Handle geometric custom renders for chapter status updates
  if (status == BookStatus::WAITING_FOR_CHAPTER || status == BookStatus::NEW_CHAPTER_AVAILABLE) {
    // 1. Calculate an upward-pointing triangle centered inside the quadrant
    int triW = s / 2; 
    int triH = s / 2;
    int triX = x + (s - triW) / 2;
    int triY = y + (s - triH) / 2 + yOffset;

    int xPts[] = { triX + triW / 2, triX, triX + triW };
    int yPts[] = { triY, triY + triH, triY + triH };
    
    // Draw the black filled triangle (Both statuses get this on a white background)
    renderer.fillPolygon(xPts, yPts, 3, Black);

    // 2. If a new chapter is available, add the 9x9 circle notification badge
    if (status == BookStatus::NEW_CHAPTER_AVAILABLE) {
      int dotW = 11;
      int dotH = 10;
      // Positioned with a 1-pixel margin inside the top-right corner radius padding
      int dotX = x + s - 6;
      int dotY = y - 3;
      
      // A corner radius of 4 on a 9x9 square forces a perfect circle primitive
      renderer.fillRoundedRect(dotX, dotY, dotW, dotH, 4, true, true, true, true, Black);
    }
    return; // Early exit so we don't fall back to font text rendering
  }

  // --- Fallback text rendering for standard statuses (READING, FINISHED, etc.) ---
  const char* txt = "-";
  Color bg = White;
  if (status == BookStatus::READING)               { bg = LightGray; txt = "R"; }
  if (status == BookStatus::FINISHED)              { bg = Black;     txt = "F"; }
  
  if (bg != White) renderer.fillRoundedRect(x, y, s, s, 6, tl, tr, bl, br, bg);
  const int tw = renderer.getTextWidth(UI_10_FONT_ID, txt);
  const int th = renderer.getTextHeight(UI_10_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, x + (s - tw)/2, y + (s - th)/2 + yOffset,
                    txt, (bg == Black) ? false : true);
}

void Ao3LibraryActivity::renderWarningSymbol(int x, int y, int s, char warning, bool tl, bool tr, bool bl, bool br, int yOffset) {
  Color bg = White;
  const char* txt = "-";
  if (warning == 'B') { bg = DarkGray; txt = "!?"; }
  if (warning == '!') { bg = Black;    txt = "!"; }
  if (bg != White) renderer.fillRoundedRect(x, y, s, s, 6, tl, tr, bl, br, bg);
  const int tw = renderer.getTextWidth(UI_10_FONT_ID, txt);
  const int th = renderer.getTextHeight(UI_10_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, x + (s - tw)/2, y + (s - th)/2 + yOffset,
                    txt, (bg == DarkGray || bg == Black) ? false : true);
}

void Ao3LibraryActivity::renderCompletionSymbol(int x, int y, int s, bool completed, bool tl, bool tr, bool bl, bool br, int yOffset) {
  const Color bg  = completed ? LightGray : Black;
  renderer.fillRoundedRect(x, y, s, s, 6, tl, tr, bl, br, bg);

  if (completed) {
    // Draw a sharp 3px thick black checkmark using line primitives
    for (int dy = 0; dy < 4; dy++) {
      // Left short downward stroke
      renderer.drawLine(x + 8, y + 14 + yOffset + dy, x + 12, y + 18 + yOffset + dy);
      // Right long upward stroke
      renderer.drawLine(x + 12, y + 18 + yOffset + dy, x + 19, y + 11 + yOffset + dy);
    }
  } else {
    // Draw a 4px thick cross (X) using white line primitives
    // The 5th parameter (4) sets the thickness, and the 6th parameter (false) forces it to draw White
    
    // Line 1: Top-Left to Bottom-Right
    renderer.drawLine(x + 7, y + 8 + yOffset, x + 18, y + 18 + yOffset, 4, false);
    
    // Line 2: Bottom-Left to Top-Right
    renderer.drawLine(x + 7, y + 18 + yOffset, x + 18, y + 8 + yOffset, 4, false);
  }
}

// ---------------------------------------------------------------------------
//  Sort & Filter States Load/Save
// ---------------------------------------------------------------------------

void Ao3LibraryActivity::loadSortFilterState() {
  memset(activeState.fandom,       0, 32);
  memset(activeState.relationship, 0, 32);
  activeState.relationshipNoneOnly = false;
  activeState.sortMode  = SortMode::ALPHABETIC;
  activeState.ascending = true;

  const char* path = "/.crosspoint/ao3SortFilterState.json";
  if (!Storage.exists(path)) return;

  String json = Storage.readFile(path);
  if (json.isEmpty()) return;

  JsonDocument doc;
  if (deserializeJson(doc, json)) return;

  strncpy(activeState.fandom,       doc["fandom"]       | "", 31);
  strncpy(activeState.relationship, doc["relationship"] | "", 31);
  activeState.relationshipNoneOnly = doc["relationshipNoneOnly"] | false;
  activeState.sortMode  = static_cast<SortMode>(doc["sortMode"] | 0);
  activeState.ascending = doc["ascending"] | true;

  if (activeState.sortMode > SortMode::AUTHOR)
    activeState.sortMode = SortMode::ALPHABETIC;

  if (activeState.fandom[0] == '\0') {
    memset(activeState.relationship, 0, 32);
    activeState.relationshipNoneOnly = false;
  }
}

void Ao3LibraryActivity::saveSortFilterState() const {
  JsonDocument doc;
  doc["fandom"]               = activeState.fandom;
  doc["relationship"]         = activeState.relationship;
  doc["relationshipNoneOnly"] = activeState.relationshipNoneOnly;
  doc["sortMode"]             = static_cast<uint8_t>(activeState.sortMode);
  doc["ascending"]            = activeState.ascending;

  String json;
  serializeJson(doc, json);
  Storage.writeFile("/.crosspoint/ao3SortFilterState.json", json);
}

// ---------------------------------------------------------------------------
//  Sorting Logic
// ---------------------------------------------------------------------------

void Ao3LibraryActivity::resortViewEntries() {
  switch (activeState.sortMode) {

    case SortMode::ALPHABETIC:
      std::sort(viewEntries.begin(), viewEntries.end(), [&](const ViewEntry& a, const ViewEntry& b) {
        int cmp = strncasecmp(a.title, b.title, 32);
      return activeState.ascending ? cmp < 0 : cmp > 0;
      });
      break;

    case SortMode::WORD_COUNT:
      std::sort(viewEntries.begin(), viewEntries.end(), [&](const ViewEntry& a, const ViewEntry& b) {
        if (a.wordCount != b.wordCount)
          return activeState.ascending ? a.wordCount < b.wordCount : a.wordCount > b.wordCount;
        return strncmp(a.title, b.title, 32) < 0;
      });
      break;

    case SortMode::DATE_ADDED:
      std::sort(viewEntries.begin(), viewEntries.end(), [&](const ViewEntry& a, const ViewEntry& b) {
        if (a.addedSequence != b.addedSequence)
          return activeState.ascending ? a.addedSequence < b.addedSequence : a.addedSequence > b.addedSequence;
        return strncmp(a.title, b.title, 32) < 0;
      });
      break;

    case SortMode::SERIES:
      std::sort(viewEntries.begin(), viewEntries.end(), [&](const ViewEntry& a, const ViewEntry& b) {
        bool aHas = a.seriesHash != 0;
        bool bHas = b.seriesHash != 0;
        if (aHas != bHas) return aHas > bHas; // no-series goes last

        if (a.seriesHash != b.seriesHash)
          return activeState.ascending ? a.seriesHash < b.seriesHash : a.seriesHash > b.seriesHash;

        if (a.seriesPart != b.seriesPart)
          return a.seriesPart < b.seriesPart; // always ascending seriesPart

        return strncmp(a.title, b.title, 32) < 0;
      });
      break;

    case SortMode::AUTHOR:
      std::sort(viewEntries.begin(), viewEntries.end(), [&](const ViewEntry& a, const ViewEntry& b) {
        int cmp = strncmp(a.authorKey, b.authorKey, 16);
        if (cmp != 0)
          return activeState.ascending ? cmp < 0 : cmp > 0;
        return strncmp(a.title, b.title, 32) < 0;
      });
      break;
  }
}

// ---------------------------------------------------------------------------
//  Filtering Logic
// ---------------------------------------------------------------------------

bool Ao3LibraryActivity::passesFilter(const ViewEntry& v, const FilterHashes& h) const {
  if (h.fandomActive) {
    if (v.fandomHash != h.fandomHash) return false;
    if (h.relationshipNoneOnly) {
      if (v.rel1Hash != 0 || v.rel2Hash != 0) return false;
    } else if (h.relationshipActive) {
      if (v.rel1Hash != h.relationshipHash && v.rel2Hash != h.relationshipHash) return false;
    }
  }
  return true;
}

void Ao3LibraryActivity::rebuildViewEntries() {
  viewEntries.clear();
  const char* indexPath = "/.crosspoint/ao3_library_index.bin";

  if (!Storage.exists(indexPath)) {
    indexState = IndexState::MISSING;
    return;
  }

  FsFile f;
  if (!Storage.openFileForRead("AO3L", indexPath, f)) {
    indexState = IndexState::CORRUPT;
    return;
  }

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

  if (!readOk || memcmp(magic, "AO3X", 4) != 0 || version != 1) {
    f.close();
    indexState = IndexState::CORRUPT;
    return;
  }

  if (recordCount > MAX_LIBRARY_BOOKS) {
    f.close();
    indexState = IndexState::CORRUPT;
    return;
  }

  const FilterHashes filterHashes = computeFilterHashes(activeState);
  viewEntries.reserve(recordCount);

  CompactIndexRecord rec;
  for (uint16_t i = 0; i < recordCount; i++) {
    const uint32_t offset = offsetOf(i);
    if (f.read((uint8_t*)&rec, sizeof(rec)) != sizeof(rec)) break;
    if (rec.flags & 0x01) continue; // Skip tombstone
    ViewEntry v = buildViewEntry(rec, offset);
    if (passesFilter(v, filterHashes)) {
      viewEntries.push_back(v);
    }
    yield();
  }
  f.close();

  resortViewEntries();
  indexState = IndexState::OK;
  cachedPage = -1;
  buttonsSetup = false;
}

void Ao3LibraryActivity::applyStateChange(const SortFilterState& prev, const SortFilterState& next) {
  bool filterChanged =
      strcmp(prev.fandom,       next.fandom)       != 0 ||
      strcmp(prev.relationship, next.relationship)  != 0 ||
      prev.relationshipNoneOnly != next.relationshipNoneOnly;

  bool sortChanged =
      prev.sortMode  != next.sortMode ||
      prev.ascending != next.ascending;

  if (filterChanged) {
    rebuildViewEntries();
  } else if (sortChanged) {
    resortViewEntries();
  }

  selectorIndex = 0;
  cachedPage    = -1;
  requestUpdate();
}

// ---------------------------------------------------------------------------
//  Picker List Builders
// ---------------------------------------------------------------------------

void Ao3LibraryActivity::buildFandomList(std::vector<std::string>& out) const {
  const char* indexPath = "/.crosspoint/ao3_library_index.bin";
  FsFile f;
  if (!Storage.openFileForRead("AO3L", indexPath, f)) return;

  char     magic[4];
  uint8_t  version;
  uint16_t recordCount;
  f.read(magic, 4);
  f.read(&version, 1);
  f.read((uint8_t*)&recordCount, 2);

  if (memcmp(magic, "AO3X", 4) != 0 || recordCount > MAX_LIBRARY_BOOKS) {
    f.close();
    return;
  }

  // Skip remaining header bytes (nextSequence [4] + reserved [1] = 5 bytes)
  f.seek(12);

  CompactIndexRecord rec;
  for (uint16_t i = 0; i < recordCount; i++) {
    if (f.read((uint8_t*)&rec, sizeof(rec)) != sizeof(rec)) break;
    if (rec.flags & 0x01 || rec.fandom[0] == '\0') continue;

    bool found = false;
    for (const auto& s : out) {
      if (s == rec.fandom) {
        found = true;
        break;
      }
    }
    if (!found) {
      out.push_back(rec.fandom);
    }
  }
  f.close();
  std::sort(out.begin(), out.end());
}

void Ao3LibraryActivity::buildRelationshipList(const char* fandom, std::vector<std::string>& out, bool& hasNoneEntries) const {
  hasNoneEntries = false;
  const char* indexPath = "/.crosspoint/ao3_library_index.bin";
  FsFile f;
  if (!Storage.openFileForRead("AO3L", indexPath, f)) return;

  char     magic[4];
  uint8_t  version;
  uint16_t recordCount;
  f.read(magic, 4);
  f.read(&version, 1);
  f.read((uint8_t*)&recordCount, 2);

  if (memcmp(magic, "AO3X", 4) != 0 || recordCount > MAX_LIBRARY_BOOKS) {
    f.close();
    return;
  }

  f.seek(12);

  CompactIndexRecord rec;
  for (uint16_t i = 0; i < recordCount; i++) {
    if (f.read((uint8_t*)&rec, sizeof(rec)) != sizeof(rec)) break;
    if (rec.flags & 0x01) continue;
    if (strcmp(rec.fandom, fandom) != 0) continue;

    if (rec.relationship1[0] == '\0' && rec.relationship2[0] == '\0') {
      hasNoneEntries = true;
      continue;
    }

    for (const char* rel : { rec.relationship1, rec.relationship2 }) {
      if (rel[0] == '\0') continue;
      bool found = false;
      for (const auto& s : out) {
        if (s == rel) {
          found = true;
          break;
        }
      }
      if (!found) {
        out.push_back(rel);
      }
    }
  }
  f.close();
  std::sort(out.begin(), out.end());
}
