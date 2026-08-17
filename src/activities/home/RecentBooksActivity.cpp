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

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Hold threshold for the long-press "remove from list" action.
constexpr unsigned long LONG_PRESS_MS = 1000;

// Tab display names. Not routed through I18N for now — AO3-specific and
// unlikely to need translation in the near term.
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

// Patch missing title/author for books not yet opened
void RecentBooksActivity::patchMissingTitles(std::vector<Ao3MarkedForLaterEntry>& entries) {
  for (auto& entry : entries) {
    if (!entry.title.empty()) continue;
    std::string infoPath = "/.crosspoint/epub_" +
        std::to_string(std::hash<std::string>{}(entry.path)) + "/ao3_library_info";
    HalFile f;
    if (Storage.openFileForRead("MFL", infoPath, f)) {
      Ao3LibraryMetadata meta;
      if (f.read((uint8_t*)&meta, sizeof(meta)) == sizeof(meta) && meta.isValid()) {
        entry.title  = meta.title;
        entry.author = meta.author;
      }
      f.close();
    }
  }
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

  // Patch missing title/author for books not yet opened
  markedForLater = MARKED_FOR_LATER_STORE.getEntries();
  patchMissingTitles(markedForLater);
  
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
  const int listSize = getCurrentListSize();

  // Swallow all input until Confirm is physically released after a long-press.
  if (longPressFired) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      longPressFired = false;
    }
    return;
  }

  // Long-press Confirm (Tab 0, list focused): prompt to remove from Marked for Later.
  if (selectedTabIndex == TAB_MARKED_FOR_LATER && selectedItemIndex > 0 &&
      !markedForLater.empty() &&
      (selectedItemIndex - 1) < static_cast<int>(markedForLater.size()) &&
      mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= LONG_PRESS_MS) {
    longPressFired = true;
    const int idx = selectedItemIndex - 1;
    promptRemoveMarkedEntry(markedForLater[idx].path, markedForLater[idx].title);
    return;
  }

  // Long-press Confirm (Tab 1, list focused): prompt to remove from New Chapters.
  if (selectedTabIndex == TAB_NEW_CHAPTERS && selectedItemIndex > 0 &&
      !newChapters.empty() &&
      (selectedItemIndex - 1) < static_cast<int>(newChapters.size()) &&
      mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= LONG_PRESS_MS) {
    longPressFired = true;
    const int idx = selectedItemIndex - 1;
    promptRemoveNewChaptersEntry(newChapters[idx].path, newChapters[idx].title);
    return;
  }

  // Long-press Confirm (Tab 2, list focused): prompt to remove from WIPs.
  if (selectedTabIndex == TAB_WIPS && selectedItemIndex > 0 &&
      !wipsEntries.empty() &&
      (selectedItemIndex - 1) < static_cast<int>(wipsEntries.size()) &&
      mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= LONG_PRESS_MS) {
    longPressFired = true;
    const int idx = selectedItemIndex - 1;
    promptRemoveWipsEntry(wipsEntries[idx].path, wipsEntries[idx].title);
    return;
  }

  // Long-press Confirm (Tab 3, list focused): prompt to remove from recents.
  if (selectedTabIndex == TAB_RECENT_BOOKS && selectedItemIndex > 0 &&
      !recentBooks.empty() &&
      (selectedItemIndex - 1) < static_cast<int>(recentBooks.size()) &&
      mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= LONG_PRESS_MS) {
    longPressFired = true;
    const int idx = selectedItemIndex - 1;
    promptRemoveBook(recentBooks[idx].path, recentBooks[idx].title);
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
    return;
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

void RecentBooksActivity::promptRemoveMarkedEntry(const std::string& path,
                                                   const std::string& title) {
  auto handler = [this, path](const ActivityResult& res) {
    if (res.isCancelled) return;
    if (MARKED_FOR_LATER_STORE.removeByPath(path)) {
      // Reset book status back to Unread (START = 0), preserving the
      // position bytes (0–5) in progress.bin exactly as BookActionActivity does.
      {
        const std::string cachePath =
            "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(path));
        const std::string progressPath = cachePath + "/progress.bin";
        uint8_t data[7] = {0, 0, 0, 0, 0, 0, 0};  // byte 6 = BookStatus::START
        HalFile f;
        if (Storage.openFileForRead("RBA", progressPath, f)) {
          f.read(data, 6);  // preserve position bytes
          f.close();
        }
        // data[6] stays 0 (BookStatus::START / Unread)
        if (Storage.openFileForWrite("RBA", progressPath, f)) {
          f.write(data, 7);
          f.close();
        }
      }

      markedForLater = MARKED_FOR_LATER_STORE.getEntries();
      patchMissingTitles(markedForLater);
      const int listSize = static_cast<int>(markedForLater.size());
      if (listSize == 0) {
        selectedItemIndex = 0;
      } else if (selectedItemIndex > listSize) {
        selectedItemIndex = listSize;
      }
      requestUpdate(true);
    }
  };
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                                            "Remove from Marked for Later", title),
      std::move(handler));
}

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
      requestUpdate(true);
    }
  };
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                                            tr(STR_REMOVE_FROM_RECENTS), title),
      std::move(handler));
}

void RecentBooksActivity::promptRemoveNewChaptersEntry(const std::string& path,
                                                       const std::string& title) {
  auto handler = [this, path](const ActivityResult& res) {
    if (res.isCancelled) return;
    if (NEW_CHAPTERS_STORE.removeByPath(path)) {
      newChapters = NEW_CHAPTERS_STORE.getEntries();
      const int listSize = static_cast<int>(newChapters.size());
      if (listSize == 0) {
        selectedItemIndex = 0;
      } else if (selectedItemIndex > listSize) {
        selectedItemIndex = listSize;
      }
      requestUpdate(true);
    }
  };
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                                            tr(STR_REMOVE_FROM_NEW_CHAPTERS), title),
      std::move(handler));
}

void RecentBooksActivity::promptRemoveWipsEntry(const std::string& path,
                                               const std::string& title) {
  auto handler = [this, path](const ActivityResult& res) {
    if (res.isCancelled) return;
    if (AO3_WIPS_STORE.removeBook(path)) {
      wipsEntries = AO3_WIPS_STORE.getEntries();
      const int listSize = static_cast<int>(wipsEntries.size());
      if (listSize == 0) {
        selectedItemIndex = 0;
      } else if (selectedItemIndex > listSize) {
        selectedItemIndex = listSize;
      }
      requestUpdate(true);
    }
  };
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                                            tr(STR_REMOVE_FROM_RECENTS), title),
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

  const int contentTop =
      topPadding + metrics.headerHeight + metrics.tabBarHeight + 10;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight;
  const int listSize = getCurrentListSize();

  if (listSize == 0) {
    const char* msg = (selectedTabIndex == TAB_RECENT_BOOKS)
                          ? tr(STR_NO_RECENT_BOOKS)
                          : EMPTY_MESSAGES[selectedTabIndex];
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, msg);
  
  } else if (selectedTabIndex == TAB_MARKED_FOR_LATER) {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight},
        markedForLater.size(), selectedItemIndex - 1,
        [this](int index) { return markedForLater[index].title; },
        [this](int index) { return markedForLater[index].author; },
        [](int) { return UIIcon::Book; });
  } else if (selectedTabIndex == TAB_NEW_CHAPTERS) {
    // Same list style as Tab 3 (title line 1, author line 2, file icon on left).
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight},
        newChapters.size(), selectedItemIndex - 1,
        [this](int index) { return newChapters[index].title; },
        [this](int index) { return newChapters[index].author; },
        [this](int index) { return UITheme::getFileIcon(newChapters[index].path); });
  } else if (selectedTabIndex == TAB_WIPS) {
    // Same list style as Tab 3 (title line 1, author line 2, file icon on left).
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight},
        wipsEntries.size(), selectedItemIndex - 1,
        [this](int index) { return wipsEntries[index].title; },
        [this](int index) { return wipsEntries[index].author; },
        [this](int index) { return UITheme::getFileIcon(wipsEntries[index].path); });
  } else if (selectedTabIndex == TAB_RECENT_BOOKS) {
    // selectedItemIndex - 1 passes -1 to drawList when tab bar is focused,
    // which drawList treats as "no row selected" — same as SettingsActivity.
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight},
        recentBooks.size(), selectedItemIndex - 1,
        [this](int index) { return recentBooks[index].title; },
        [this](int index) { return recentBooks[index].author; },
        [this](int index) { return UITheme::getFileIcon(recentBooks[index].path); });
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