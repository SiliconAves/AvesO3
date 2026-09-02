#include "RecentBooksActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Epub.h>

#include <algorithm>
#include <memory>

#include "Ao3NewChaptersStore.h"
#include "Ao3WipsStore.h"
#include "Ao3MarkedForLaterStore.h"
#include "../../Ao3LibraryMetadata.h"
#include "BookActionActivity.h"

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Hold threshold for the long-press "remove from list" action.
constexpr unsigned long LONG_PRESS_MS = 1000;

// Tab display names. Not routed through I18N for now
constexpr const char* TAB_NAMES[4] = {
    "For Later",
    "New Chapters",
    "WIPs",
    "Recents",
};

// Empty-state messages per tab.
// TODO: replace literals with tr(STR_NO_MARKED_FOR_LATER) etc. once those
// StrId entries are added to the enum and english.yaml.
constexpr const char* EMPTY_MESSAGES[4] = {
    "No books marked for later",
    "No fics with new chapters",
    "No WIPs waiting for a chapter",
    nullptr,  // Tab 3 uses tr(STR_NO_RECENT_BOOKS) — set in render()
};
}  // namespace

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

int RecentBooksActivity::getCurrentListSize() const {
  switch (selectedTabIndex) {
    case TAB_MARKED_FOR_LATER: return static_cast<int>(markedForLater.size());
    case TAB_NEW_CHAPTERS: return static_cast<int>(newChapters.size());
    case TAB_WIPS:         return static_cast<int>(wipsEntries.size());
    case TAB_RECENT_BOOKS: return static_cast<int>(recentBooks.size());
    default:                   return 0;
  }
}

BookStatus RecentBooksActivity::getBookStatus(const std::string& path) {
  if (path.empty()) return BookStatus::START;

  std::string cachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(path));
  HalFile f;
  BookStatus status = BookStatus::START;
  
  if (Storage.openFileForRead("RBA", cachePath + "/progress.bin", f)) {
    uint8_t data[11];
    int dataSize = f.read(data, sizeof(data));
    f.close();
    if (dataSize == 7)  status = static_cast<BookStatus>(data[6]);   // legacy
    if (dataSize == 11) status = static_cast<BookStatus>(data[10]);  // new
  }
  return status;
}

// ---------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------

void RecentBooksActivity::onEnter() {
  Activity::onEnter();

  // Load Marked for Later; prune stale entries first.
  if (MARKED_FOR_LATER_STORE.pruneMissing()) {
    MARKED_FOR_LATER_STORE.saveToFile();
  }
  MARKED_FOR_LATER_STORE.loadFromFile();
  markedForLater = MARKED_FOR_LATER_STORE.getEntries();
  
  // Load New Chapters; prune stale entries first.
  if (NEW_CHAPTERS_STORE.pruneMissing()) {
    NEW_CHAPTERS_STORE.saveToFile();
  }
  NEW_CHAPTERS_STORE.loadFromFile();
  newChapters = NEW_CHAPTERS_STORE.getEntries();

  // Load WIPs; prune stale entries first. (Matches New Chapters pattern)
  if (AO3_WIPS_STORE.pruneMissing()) {
    AO3_WIPS_STORE.saveToFile();
  }
  AO3_WIPS_STORE.loadFromFile();
  wipsEntries = AO3_WIPS_STORE.getEntries();

  // Load Recent Books; prune stale entries first.
  if (RECENT_BOOKS.pruneMissing()) {
    RECENT_BOOKS.saveToFile();
  }
  recentBooks = RECENT_BOOKS.getBooks();

  // Smart default tab selection logic
  if (!markedForLater.empty()) {
    selectedTabIndex = TAB_MARKED_FOR_LATER;
  } else if (!newChapters.empty()) {
    selectedTabIndex = TAB_NEW_CHAPTERS;
  } else if (!wipsEntries.empty()) {
    selectedTabIndex = TAB_WIPS;
  } else {
    selectedTabIndex = TAB_RECENT_BOOKS;
  }
  selectedItemIndex = 0;
  longPressFired    = false;

  visibleStatusCache.clear();
  lastRenderedTab = -1;

  // Universally trigger the non-blocking loading state if the active tab has items
  isLoading = (getCurrentListSize() > 0);

  requestUpdate();
}

void RecentBooksActivity::onExit() {
  Activity::onExit();
  markedForLater.clear();
  newChapters.clear();
  wipsEntries.clear();
  recentBooks.clear();
}

// ---------------------------------------------------------------------------
//  Loop
// ---------------------------------------------------------------------------

void RecentBooksActivity::loop() {

  if (isLoading) {
    // If we landed on Marked for Later, patch any missing titles progressively
    if (selectedTabIndex == TAB_MARKED_FOR_LATER) {
      bool storeNeedsSave = false;

      for (auto& entry : markedForLater) {
        if (!entry.title.empty()) continue;
        std::string infoPath = "/.crosspoint/epub_" +
            std::to_string(std::hash<std::string>{}(entry.path)) + "/ao3_library_info";
        HalFile f;
        if (Storage.openFileForRead("MFL", infoPath, f)) {
          Ao3LibraryMetadata meta;
          if (f.read((uint8_t*)&meta, sizeof(meta)) == sizeof(meta) && meta.isValid()) {
            entry.title  = meta.title;
            entry.author = meta.author;
            MARKED_FOR_LATER_STORE.updateEntryMetadata(entry.path, meta.title, meta.author);
            storeNeedsSave = true;
          }
          f.close();
        }
      }
      if (storeNeedsSave) {
        MARKED_FOR_LATER_STORE.saveToFile();
      }
    }

    isLoading = false;
    requestUpdate();
    return; // Skip this tick so the UI snaps open instantly
  }
  int listSize = getCurrentListSize();

  // Swallow all input until Confirm is physically released after a long-press.
  if (longPressFired) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      longPressFired = false;
    }
    return;
  }

    // Long-press Confirm (list focused): open context menu
  if (selectedItemIndex > 0 &&
      mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= LONG_PRESS_MS) {

    const int idx = selectedItemIndex - 1;
    longPressFired = true;

    if (selectedTabIndex == TAB_RECENT_BOOKS) {
      // Tab 3: existing plain confirmation, no BookActionActivity
      if (idx < static_cast<int>(recentBooks.size()))
        promptRemoveBook(recentBooks[idx].path, recentBooks[idx].title);
      return;
    }

    // Tabs 0–2: Dashboard context menu
    std::string path, title;
    if (selectedTabIndex == TAB_MARKED_FOR_LATER &&
        idx < static_cast<int>(markedForLater.size())) {
      path  = markedForLater[idx].path;
      title = markedForLater[idx].title;
    } else if (selectedTabIndex == TAB_NEW_CHAPTERS &&
               idx < static_cast<int>(newChapters.size())) {
      path  = newChapters[idx].path;
      title = newChapters[idx].title;
    } else if (selectedTabIndex == TAB_WIPS &&
               idx < static_cast<int>(wipsEntries.size())) {
      path  = wipsEntries[idx].path;
      title = wipsEntries[idx].title;
    } else {
      longPressFired = false;
      return;
    }
    launchDashboardMenu(path, title);
    return;
  }

  // Confirm at tab bar (index 0): cycle to next tab immediately, then repeat on hold.
  if (selectedItemIndex == 0) {
    const bool initialPress = mappedInput.wasPressed(MappedInputManager::Button::Confirm);
    const bool heldRepeat   = mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
                              mappedInput.getHeldTime() >= 500 &&
                              (millis() - lastTabCycleMs) >= 400;
    if (initialPress || heldRepeat) {
      lastTabCycleMs    = millis();
      selectedTabIndex  = (selectedTabIndex + 1) % TAB_COUNT;
      selectedItemIndex = 0;
      requestUpdate();
      return;
    }
  }

  // ---------------------------------------------------------------------------
  //  Touch input (X4 Pro only — gracefully ignored on hardware without touch)
  // ---------------------------------------------------------------------------
  if (mappedInput.hasTouch()) {
    const auto& metrics     = UITheme::getInstance().getMetrics();
    const int topPadding    = metrics.topPadding / 2;
    const int tabBarTop     = topPadding + metrics.headerHeight;
    const int contentTop    = tabBarTop + metrics.tabBarHeight + 10;
    const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight;

    // Tab bar: tap any tab to switch to it directly
    int tappedTab = 0;
    const int tabStep = renderer.getScreenWidth() / TAB_COUNT;
    const auto tabTouch = mappedInput.colTouch(
        tappedTab, 0, tabStep, TAB_COUNT,
        tabBarTop, tabBarTop + metrics.tabBarHeight, tabStep);
    if (tabTouch == MappedInputManager::RowTouch::Tap) {
      selectedTabIndex  = tappedTab;
      selectedItemIndex = 0;
      visibleStatusCache.clear();
      requestUpdate();
      return;
    }

    // List: tap an item to open it (all tabs)
    if (listSize > 0) {
      int tappedItem = 0;
      if (mappedInput.wasListItemTapped(tappedItem, listSize,
                                        std::max(0, selectedItemIndex - 1),
                                        contentTop, contentHeight, true)) {
        selectedItemIndex = tappedItem + 1;
        std::string path;
        if      (selectedTabIndex == TAB_MARKED_FOR_LATER && tappedItem < (int)markedForLater.size())
          path = markedForLater[tappedItem].path;
        else if (selectedTabIndex == TAB_NEW_CHAPTERS     && tappedItem < (int)newChapters.size())
          path = newChapters[tappedItem].path;
        else if (selectedTabIndex == TAB_WIPS             && tappedItem < (int)wipsEntries.size())
          path = wipsEntries[tappedItem].path;
        else if (selectedTabIndex == TAB_RECENT_BOOKS     && tappedItem < (int)recentBooks.size())
          path = recentBooks[tappedItem].path;
        if (!path.empty()) {
          onSelectBook(path);
          return;
        }
      }

      // Swipe up/down to scroll the list
      const auto swipe = mappedInput.wasSwipe();
      if (swipe == MappedInputManager::SwipeDir::Up) {
        selectedItemIndex = std::min(listSize, selectedItemIndex + 1);
        requestUpdate();
        return;
      }
      if (swipe == MappedInputManager::SwipeDir::Down) {
        selectedItemIndex = std::max(1, selectedItemIndex - 1);
        requestUpdate();
        return;
      }
    }
  }

  // Confirm in list (index > 0): open selected book.
  // Uses wasReleased to avoid triggering after a long-press.
  if (selectedItemIndex > 0 &&
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedTabIndex == TAB_MARKED_FOR_LATER) {
      const int idx = selectedItemIndex - 1;
      if (idx >= 0 && idx < static_cast<int>(markedForLater.size())) {
        onSelectBook(markedForLater[idx].path);
        return;
      }
    } else if (selectedTabIndex == TAB_NEW_CHAPTERS) {
      const int idx = selectedItemIndex - 1;
      if (idx >= 0 && idx < static_cast<int>(newChapters.size())) {
        onSelectBook(newChapters[idx].path);
        return;
      }
    } else if (selectedTabIndex == TAB_RECENT_BOOKS) {
      const int idx = selectedItemIndex - 1;
      if (idx >= 0 && idx < static_cast<int>(recentBooks.size())) {
        onSelectBook(recentBooks[idx].path);
        return;
      }
    } else if (selectedTabIndex == TAB_WIPS) {
      const int idx = selectedItemIndex - 1;
      if (idx >= 0 && idx < static_cast<int>(wipsEntries.size())) {
        onSelectBook(wipsEntries[idx].path);
        return;
      }
    }
  }

  // Back: return to tab bar if in list, otherwise go home.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (selectedItemIndex > 0) {
      selectedItemIndex = 0;
      requestUpdate();
    } else {
      onGoHome();
    }
  }

  // Side short press: navigate one row up/down in the current list.
  // Wraps through the tab bar (index 0) at both ends, matching SettingsActivity.
  buttonNavigator.onNextRelease([this, listSize] {
    selectedItemIndex = ButtonNavigator::nextIndex(selectedItemIndex, listSize + 1);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectedItemIndex = ButtonNavigator::previousIndex(selectedItemIndex, listSize + 1);
    requestUpdate();
  });

  // Side held: cycle tabs, reset list position.
  buttonNavigator.onSideNextContinuous([this] {
    selectedTabIndex  = ButtonNavigator::nextIndex(selectedTabIndex, TAB_COUNT);
    selectedItemIndex = 0;
    requestUpdate();
  });

  buttonNavigator.onSidePreviousContinuous([this] {
    selectedTabIndex  = ButtonNavigator::previousIndex(selectedTabIndex, TAB_COUNT);
    selectedItemIndex = 0;
    requestUpdate();
  });

  // Front held: scroll 2 rows forward, entering the list from the tab bar
  // if needed (matches SettingsActivity behaviour).
  buttonNavigator.onFrontNextContinuous([this, listSize] {
    if (selectedItemIndex == 0) {
      selectedItemIndex = std::min(2, listSize);
    } else {
      const int target  = selectedItemIndex + 2;
      selectedItemIndex = (target <= listSize) ? target : listSize;
    }
    requestUpdate();
  });

  buttonNavigator.onFrontPreviousContinuous([this, listSize] {
    if (selectedItemIndex == 0) {
      selectedItemIndex = listSize;
    } else {
      const int target  = selectedItemIndex - 2;
      selectedItemIndex = (target >= 1) ? target : 1;
    }
    requestUpdate();
  });
}

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

void RecentBooksActivity::promptRemoveBook(const std::string& path,
                                           const std::string& title) {
  auto handler = [this, path](const ActivityResult& res) {
    if (res.isCancelled) return;
    if (RECENT_BOOKS.removeByPath(path)) {
      recentBooks = RECENT_BOOKS.getBooks();
      const int listSize = static_cast<int>(recentBooks.size());
      if (listSize == 0) {
        selectedItemIndex = 0;
      } else if (selectedItemIndex > listSize) {
        selectedItemIndex = listSize;
      }
      visibleStatusCache.clear();
      requestUpdate(true);
    }
  };
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                                            tr(STR_REMOVE_FROM_RECENTS), title),
      std::move(handler));
}

void RecentBooksActivity::clampSelectorIndex() {
  const int listSize = getCurrentListSize();
  if (listSize == 0)
    selectedItemIndex = 0;
  else if (selectedItemIndex > listSize)
    selectedItemIndex = listSize;
}

void RecentBooksActivity::revertMarkedForLaterStatus(const std::string& path) {
  const std::string cachePath =
      "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(path));
  const std::string progressPath = cachePath + "/progress.bin";

  // Read up to 10 bytes to preserve position + visibleTextOffset
  uint8_t data[11] = {0};  // byte 10 = BookStatus::START
  HalFile f;
  if (Storage.openFileForRead("RBA", progressPath, f)) {
    f.read(data, 10);  // preserve bytes 0-9 (position + visibleTextOffset)
    f.close();
  }
  // data[10] stays 0 = BookStatus::START
  if (Storage.openFileForWrite("RBA", progressPath, f)) {
    f.write(data, 11);
    f.close();
  }
}

void RecentBooksActivity::launchDashboardMenu(const std::string& path,
                                              const std::string& title) {
  const int savedTab  = selectedTabIndex;
  const int savedItem = selectedItemIndex;

  auto handler = [this, path, savedTab](const ActivityResult& res) {
    if (const auto* r = std::get_if<BookActionResult>(&res.data)) {

if (r->removedFromList) {
        switch (savedTab) {
          case TAB_MARKED_FOR_LATER:
            MARKED_FOR_LATER_STORE.removeByPath(path);
            MARKED_FOR_LATER_STORE.saveToFile(); // Persist removal to disk
            revertMarkedForLaterStatus(path);
            markedForLater = MARKED_FOR_LATER_STORE.getEntries();
            break;
          case TAB_NEW_CHAPTERS:
            NEW_CHAPTERS_STORE.removeByPath(path);
            NEW_CHAPTERS_STORE.saveToFile(); // Persist removal to disk[cite: 4]
            newChapters = NEW_CHAPTERS_STORE.getEntries();
            break;
          case TAB_WIPS:
            AO3_WIPS_STORE.removeBook(path);
            AO3_WIPS_STORE.saveToFile(); // Persist removal to disk[cite: 4]
            wipsEntries = AO3_WIPS_STORE.getEntries();
            break;
        }
        visibleStatusCache.clear();
        clampSelectorIndex();

      } else if (r->modified) {
        // Re-sync all local vectors from stores — saveStatus() already wrote to them
        markedForLater = MARKED_FOR_LATER_STORE.getEntries();
        newChapters    = NEW_CHAPTERS_STORE.getEntries();
        wipsEntries    = AO3_WIPS_STORE.getEntries();
        visibleStatusCache.clear();
        clampSelectorIndex();
      }
    }
    requestUpdate(true);
  };

  startActivityForResult(
      std::make_unique<BookActionActivity>(renderer, mappedInput, path, title,
                                          BookActionMode::DASHBOARD),
      std::move(handler));
}

// ---------------------------------------------------------------------------
//  Render
// ---------------------------------------------------------------------------

void RecentBooksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth  = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics   = UITheme::getInstance().getMetrics();

  // Reduce top padding locally to free up vertical space for 10 entries
  const int topPadding = metrics.topPadding / 2;

  // Header — STR_MENU_RECENT_BOOKS value changed to "Dashboard" in english.yaml.
  GUI.drawHeader(renderer,
                 Rect{0, topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_MENU_RECENT_BOOKS));

  // Tab bar — mirrors SettingsActivity::render exactly.
  std::vector<TabInfo> tabs;
  tabs.reserve(TAB_COUNT);
  for (int i = 0; i < TAB_COUNT; i++) {
    tabs.push_back({TAB_NAMES[i], selectedTabIndex == i});
  }
  GUI.drawTabBar(renderer,
                 Rect{0, topPadding + metrics.headerHeight, pageWidth,
                      metrics.tabBarHeight},
                 tabs, selectedItemIndex == 0);

  // Clear cache if the user switched tabs
  if (lastRenderedTab != selectedTabIndex) {
    visibleStatusCache.clear();
    lastRenderedTab = selectedTabIndex;
  }

  const int contentTop =
      topPadding + metrics.headerHeight + metrics.tabBarHeight + 10;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight;
  int listSize = getCurrentListSize();

  if (isLoading) {
    // Universal loading state for any tab on cold-open
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, "Loading...");

  } else if (listSize == 0) {
    const char* msg = (selectedTabIndex == TAB_RECENT_BOOKS)
                          ? tr(STR_NO_RECENT_BOOKS)
                          : EMPTY_MESSAGES[selectedTabIndex];
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, msg);
  
  } else if (selectedTabIndex == TAB_MARKED_FOR_LATER) {
    // Marked for Later
    const auto rowStatus = [this](int index) -> BookStatus {
      if (index < 0 || index >= static_cast<int>(markedForLater.size())) return BookStatus::START;
      if (visibleStatusCache.count(index)) return visibleStatusCache[index];
      BookStatus base = getBookStatus(markedForLater[index].path);
      if (base == BookStatus::MARKED_FOR_LATER) {
        base = static_cast<BookStatus>(static_cast<uint8_t>(BookStatus::MARKED_FOR_LATER) + index);
      }
    visibleStatusCache[index] = base;
    return base;
    };

    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight},
        markedForLater.size(), selectedItemIndex - 1,
        [this](int index) { return markedForLater[index].title; },
        [this](int index) { return markedForLater[index].author; },
        [](int) { return UIIcon::Book; },
        nullptr, false, nullptr,
        rowStatus);
        
  } else if (selectedTabIndex == TAB_NEW_CHAPTERS) {
    // New Chapters
    const auto rowStatus = [this](int index) {
      if (index < 0 || index >= static_cast<int>(newChapters.size())) return BookStatus::START;
      if (visibleStatusCache.count(index)) return visibleStatusCache[index];
      visibleStatusCache[index] = getBookStatus(newChapters[index].path);
      return visibleStatusCache[index];
    };

    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight},
        newChapters.size(), selectedItemIndex - 1,
        [this](int index) { return newChapters[index].title; },
        [this](int index) { return newChapters[index].author; },
        [this](int index) { return UITheme::getFileIcon(newChapters[index].path); },
        nullptr, false, nullptr,
        rowStatus);
        
  } else if (selectedTabIndex == TAB_WIPS) {
    // WIPs
    const auto rowStatus = [this](int index) {
      if (index < 0 || index >= static_cast<int>(wipsEntries.size())) return BookStatus::START;
      if (visibleStatusCache.count(index)) return visibleStatusCache[index];
      visibleStatusCache[index] = getBookStatus(wipsEntries[index].path);
      return visibleStatusCache[index];
    };

    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight},
        wipsEntries.size(), selectedItemIndex - 1,
        [this](int index) { return wipsEntries[index].title; },
        [this](int index) { return wipsEntries[index].author; },
        [this](int index) { return UITheme::getFileIcon(wipsEntries[index].path); },
        nullptr, false, nullptr,
        rowStatus);
        
  } else if (selectedTabIndex == TAB_RECENT_BOOKS) {
    // Recent Books
    const auto rowStatus = [this](int index) -> BookStatus {
     if (index < 0 || index >= static_cast<int>(recentBooks.size())) return BookStatus::START;
     if (visibleStatusCache.count(index)) return visibleStatusCache[index];
      BookStatus base = getBookStatus(recentBooks[index].path);
     if (base == BookStatus::MARKED_FOR_LATER) {
        const std::string& path = recentBooks[index].path;
        for (int i = 0; i < static_cast<int>(markedForLater.size()); i++) {
          if (markedForLater[i].path == path) {
           base = static_cast<BookStatus>(static_cast<uint8_t>(BookStatus::MARKED_FOR_LATER) + i);
           break;
          }
       }
      }
      visibleStatusCache[index] = base;
      return base;
    };

    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight},
        recentBooks.size(), selectedItemIndex - 1,
        [this](int index) { return recentBooks[index].title; },
        [this](int index) { return recentBooks[index].author; },
        [this](int index) { return UITheme::getFileIcon(recentBooks[index].path); },
        nullptr, false, nullptr,
        rowStatus);
  }

  // Button hints — Confirm label shows next tab name when at the tab bar.
  const char* confirmLabel =
      (selectedItemIndex == 0)
          ? TAB_NAMES[(selectedTabIndex + 1) % TAB_COUNT]
          : tr(STR_OPEN);
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}