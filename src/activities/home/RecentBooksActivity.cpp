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

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h" // Needed for the specific 8pt and 10pt font mapping

namespace fui = freeink::ui;

namespace {
constexpr const char* TAB_NAMES[4] = {
    "For Later",
    "New Chapters",
    "WIPs",
    "Recents",
};

constexpr const char* EMPTY_MESSAGES[4] = {
    "No books marked for later",
    "No fics with new chapters",
    "No WIPs waiting for a chapter",
    nullptr, 
};
}  // namespace

RecentBooksActivity::RecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         int initialTab, int initialIndex, std::string initialPath)
    : UiTabListActivity("RecentBooks", renderer, mappedInput),
      initialTab_(initialTab),
      initialIndex_(initialIndex),
      initialPath_(std::move(initialPath)) {
  // Map fonts precisely for 10pt Titles and 8pt Authors
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
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
    if (dataSize == 7)  status = static_cast<BookStatus>(data[6]);
    if (dataSize == 11) status = static_cast<BookStatus>(data[10]);
  }
  return status;
}

fui::BitmapRef RecentBooksActivity::getStatusIcon(BookStatus status, const std::string& path) {
  // START keeps the normal per-file-type icon (book/text/image/...).
  if (status != BookStatus::START) return bookStatusIconFor(status, 32);
  return listIconFor(UITheme::getFileIcon(path), 32);
}

void RecentBooksActivity::rebuildRowItems() {
  rowItems.clear();
  
  if (lastRenderedTab != selectedTabIndex) {
    visibleStatusCache.clear();
    lastRenderedTab = selectedTabIndex;
  }

  auto addRow = [&](const std::string& title, const std::string& author, const std::string& path, int index, BookStatus status) {
    fui::ListItem item;
    item.label = title.c_str(); 
    if (!author.empty()) item.subtitle = author.c_str();
    item.icon = getStatusIcon(status, path); 
    item.actionValue = static_cast<int16_t>(index);
    rowItems.push_back(item);
  };

  if (selectedTabIndex == TAB_MARKED_FOR_LATER) {
    for (size_t i = 0; i < markedForLater.size(); i++) {
      if (!visibleStatusCache.count(i)) {
        BookStatus base = getBookStatus(markedForLater[i].path);
        if (base == BookStatus::MARKED_FOR_LATER) {
          base = static_cast<BookStatus>(static_cast<uint8_t>(BookStatus::MARKED_FOR_LATER) + i);
        }
        visibleStatusCache[i] = base;
      }
      addRow(markedForLater[i].title, markedForLater[i].author, markedForLater[i].path, i, visibleStatusCache[i]);
    }
  } else if (selectedTabIndex == TAB_NEW_CHAPTERS) {
    for (size_t i = 0; i < newChapters.size(); i++) {
      if (!visibleStatusCache.count(i)) visibleStatusCache[i] = getBookStatus(newChapters[i].path);
      addRow(newChapters[i].title, newChapters[i].author, newChapters[i].path, i, visibleStatusCache[i]);
    }
  } else if (selectedTabIndex == TAB_WIPS) {
    for (size_t i = 0; i < wipsEntries.size(); i++) {
      if (!visibleStatusCache.count(i)) visibleStatusCache[i] = getBookStatus(wipsEntries[i].path);
      addRow(wipsEntries[i].title, wipsEntries[i].author, wipsEntries[i].path, i, visibleStatusCache[i]);
    }
  } else if (selectedTabIndex == TAB_RECENT_BOOKS) {
    for (size_t i = 0; i < recentBooks.size(); i++) {
      if (!visibleStatusCache.count(i)) {
        BookStatus base = getBookStatus(recentBooks[i].path);
        if (base == BookStatus::MARKED_FOR_LATER) {
          for (int j = 0; j < (int)markedForLater.size(); j++) {
            if (markedForLater[j].path == recentBooks[i].path) {
              base = static_cast<BookStatus>(static_cast<uint8_t>(BookStatus::MARKED_FOR_LATER) + j);
              break;
            }
          }
        }
        visibleStatusCache[i] = base;
      }
      addRow(recentBooks[i].title, recentBooks[i].author, recentBooks[i].path, i, visibleStatusCache[i]);
    }
  }
}

void RecentBooksActivity::onEnter() {
  UiTabListActivity::onEnter();

  MARKED_FOR_LATER_STORE.loadFromFile();
  if (MARKED_FOR_LATER_STORE.pruneMissing()) MARKED_FOR_LATER_STORE.saveToFile();
  markedForLater = MARKED_FOR_LATER_STORE.getEntries();
  
  NEW_CHAPTERS_STORE.loadFromFile();
  if (NEW_CHAPTERS_STORE.pruneMissing()) NEW_CHAPTERS_STORE.saveToFile();
  newChapters = NEW_CHAPTERS_STORE.getEntries();

  AO3_WIPS_STORE.loadFromFile();
  if (AO3_WIPS_STORE.pruneMissing()) AO3_WIPS_STORE.saveToFile();
  wipsEntries = AO3_WIPS_STORE.getEntries();

  if (RECENT_BOOKS.pruneMissing()) RECENT_BOOKS.saveToFile();
  recentBooks = RECENT_BOOKS.getBooks();

  if (initialTab_ >= 0 && initialTab_ < TAB_COUNT) selectedTabIndex = initialTab_;
  else if (!markedForLater.empty()) selectedTabIndex = TAB_MARKED_FOR_LATER;
  else if (!newChapters.empty()) selectedTabIndex = TAB_NEW_CHAPTERS;
  else if (!wipsEntries.empty()) selectedTabIndex = TAB_WIPS;
  else selectedTabIndex = TAB_RECENT_BOOKS;

  rowItems.clear();
  isLoading = true;
  requestUpdate(true);
}

void RecentBooksActivity::onExit() {
  UiTabListActivity::onExit();
  rowItems.clear();
  markedForLater.clear();
  newChapters.clear();
  wipsEntries.clear();
  recentBooks.clear();
  MARKED_FOR_LATER_STORE.clearEntries();
  NEW_CHAPTERS_STORE.clearEntries();     
  AO3_WIPS_STORE.clearEntries(); 
}

void RecentBooksActivity::loop() {
  if (isLoading) {
    if (selectedTabIndex == TAB_MARKED_FOR_LATER) {
      bool storeNeedsSave = false;
      for (auto& entry : markedForLater) {
        if (!entry.title.empty()) continue;
        std::string infoPath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(entry.path)) + "/ao3_library_info";
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
      if (storeNeedsSave) MARKED_FOR_LATER_STORE.saveToFile();
    }

    isLoading = false;
    rebuildRowItems();

    if (!rowItems.empty() && (initialIndex_ >= 0 || !initialPath_.empty())) {
      int targetIndex = initialIndex_;
      if (!initialPath_.empty()) {
        if (selectedTabIndex == TAB_MARKED_FOR_LATER) {
          for (size_t i = 0; i < markedForLater.size(); ++i) {
            if (markedForLater[i].path == initialPath_) { targetIndex = static_cast<int>(i); break; }
          }
        } else if (selectedTabIndex == TAB_NEW_CHAPTERS) {
          for (size_t i = 0; i < newChapters.size(); ++i) {
            if (newChapters[i].path == initialPath_) { targetIndex = static_cast<int>(i); break; }
          }
        } else if (selectedTabIndex == TAB_WIPS) {
          for (size_t i = 0; i < wipsEntries.size(); ++i) {
            if (wipsEntries[i].path == initialPath_) { targetIndex = static_cast<int>(i); break; }
          }
        } else if (selectedTabIndex == TAB_RECENT_BOOKS) {
          for (size_t i = 0; i < recentBooks.size(); ++i) {
            if (recentBooks[i].path == initialPath_) { targetIndex = static_cast<int>(i); break; }
          }
        }
      }
      if (targetIndex >= 0) {
        targetIndex = std::min(targetIndex, static_cast<int>(rowItems.size()) - 1);
        activeNav().selected = targetIndex + 1;
        activeNav().followOnBuild = true;
      }
      initialIndex_ = -1;
      initialPath_.clear();
    }

    requestUpdate(true);
    return;
  }
  
  UiListActivity::loop(); 
}

const char* RecentBooksActivity::tabLabel(int index) const {
    if (index >= 0 && index < TAB_COUNT) return TAB_NAMES[index];
    return "";
}

// ---------------------------------------------------------------------------
//  Framework Input Routing
// ---------------------------------------------------------------------------

void RecentBooksActivity::onTabAction(int index) {
  selectedTabIndex = index;
  moveRingTo(0);
  rebuildRowItems();
  requestUpdate(true);
}

void RecentBooksActivity::stepTab(int direction) {
  selectedTabIndex = direction > 0 ? ButtonNavigator::nextIndex(selectedTabIndex, TAB_COUNT)
                                   : ButtonNavigator::previousIndex(selectedTabIndex, TAB_COUNT);
  moveRingTo(0);
  rebuildRowItems();
  requestUpdate(true);
}

void RecentBooksActivity::activateIndex(int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  
  std::string path;
  if (selectedTabIndex == TAB_MARKED_FOR_LATER) path = markedForLater[index].path;
  else if (selectedTabIndex == TAB_NEW_CHAPTERS) path = newChapters[index].path;
  else if (selectedTabIndex == TAB_WIPS) path = wipsEntries[index].path;
  else if (selectedTabIndex == TAB_RECENT_BOOKS) path = recentBooks[index].path;
  
  if (!path.empty()) {
    APP_STATE.ao3LibraryReturnIndex = -1;
    APP_STATE.dashboardReturnTab = selectedTabIndex;
    APP_STATE.dashboardReturnIndex = index;
    onSelectBook(path);
  }
}

void RecentBooksActivity::onRowLongPress(int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();

  std::string path, title;
  if (selectedTabIndex == TAB_MARKED_FOR_LATER) { path = markedForLater[index].path; title = markedForLater[index].title; }
  else if (selectedTabIndex == TAB_NEW_CHAPTERS) { path = newChapters[index].path; title = newChapters[index].title; }
  else if (selectedTabIndex == TAB_WIPS) { path = wipsEntries[index].path; title = wipsEntries[index].title; }
  else if (selectedTabIndex == TAB_RECENT_BOOKS) { path = recentBooks[index].path; title = recentBooks[index].title; }
  
  launchDashboardMenu(path, title);
}

bool RecentBooksActivity::handleButtons() {
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 1000)) {
    if (ringPos() > 0) {
      onRowLongPress(ringPos() - 1); 
    }
    return true; 
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0) {
      stepTab(1); 
    } else {
      activateIndex(ringPos() - 1); 
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (ringPos() > 0) {
      moveRingTo(0); 
      requestUpdate(true);
    } else {
      onGoHome(); 
    }
    return true;
  }
  return false;
}

void RecentBooksActivity::navigateButtons() {
    const int listSize = listCount();
    
    buttonNavigator.onNextRelease([this, listSize] {
        moveRingTo(ButtonNavigator::nextIndex(ringPos(), listSize + 1));
        requestUpdate(true);
    });
    buttonNavigator.onPreviousRelease([this, listSize] {
        moveRingTo(ButtonNavigator::previousIndex(ringPos(), listSize + 1));
        requestUpdate(true);
    });
    
    buttonNavigator.onSideNextContinuous([this] { stepTab(1); });
    buttonNavigator.onSidePreviousContinuous([this] { stepTab(-1); });
    
    buttonNavigator.onFrontNextContinuous([this, listSize] {
        int current = ringPos();
        if (current == 0) moveRingTo(std::min(2, listSize));
        else moveRingTo(std::min(current + 2, listSize));
        requestUpdate(true);
    });
    buttonNavigator.onFrontPreviousContinuous([this, listSize] {
        int current = ringPos();
        if (current == 0) moveRingTo(listSize);
        else moveRingTo(std::max(current - 2, 1));
        requestUpdate(true);
    });
}

bool RecentBooksActivity::handleCustomInput() {
  // Intercept the FUI touch router to manually inject the long-press flag
  auto route = UiAppHost::routeTouch(mappedInput, /*withLongPress=*/true);
  
  // If the touch resulted in a visual state change (like a row highlight), request a screen update
  if (route.routed && app.invalidated()) requestUpdate(true);
  
  // Return the event state. If true (a tap/hold occurred), the base loop consumes it. 
  // If false (a swipe or empty touch), the base loop continues to handle gestures.
  return static_cast<bool>(route); 
}

void RecentBooksActivity::onRowAction(const fui::ActionEvent& event) {
  // UiTabListActivity requires the selected index to be offset by 1 (0 is the tab bar)
  activeNav().selected = event.value + 1; 
  
  // Catch the FUI touch long-press event before it defaults to a standard tap
  if (event.longPress) {
    onRowLongPress(event.value);
    return;
  }
  
  // Standard tap
  activateIndex(event.value);
}

// ---------------------------------------------------------------------------
//  Engine Layout & Render
// ---------------------------------------------------------------------------

void RecentBooksActivity::buildScreen(UiScreen& screen) {
  // 1. Force the FUI Small Font slot to 10pt so the Tab Bar renders at the correct size
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_10_FONT_ID);

  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(metrics.topPadding + metrics.headerHeight),
      0,
      static_cast<int16_t>(metrics.buttonHintsHeight),
      0});
  buildTabBar(screen);

  int textOffset = screen.theme().listSidePadding + 12 + 10;

  if (isLoading) {
    fui::Rect loadingRect = screen.takeTop(20);
    fui::TextStyle loadingStyle = screen.theme().bodyText;
    loadingStyle.align = fui::TextAlign::Left;
    loadingRect.x += textOffset;
    screen.target().text(loadingRect, "Loading...", loadingStyle);
    return;
  }

  if (rowItems.empty()) {
    screen.spacer(20);
    const char* msg = (selectedTabIndex == TAB_RECENT_BOOKS) ? tr(STR_NO_RECENT_BOOKS) : EMPTY_MESSAGES[selectedTabIndex];
    fui::Rect emptyRect = screen.takeTop(40);
    fui::TextStyle emptyStyle = screen.theme().bodyText;
    emptyStyle.align = fui::TextAlign::Left;
    emptyRect.x += textOffset;
    screen.target().text(emptyRect, msg, emptyStyle);
    return;
  }

  screen.insetContent(fui::Insets{-6, 0, 0, 0});

  fui::ListProps props;
  syncTabListViewport(screen, props, /*hasSubtitle=*/true);

  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;

  props.labelText = screen.theme().bodyText;
  props.subtitleText = screen.theme().smallText;
  
  // 2. Switch the FUI Small Font slot to 8pt right before drawing the list for the Authors
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);

  screen.list(props);
}

void RecentBooksActivity::drawFooter() {
  const char* confirmLabel = (ringPos() == 0) ? TAB_NAMES[(selectedTabIndex + 1) % TAB_COUNT] : tr(STR_OPEN);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---------------------------------------------------------------------------
//  Secondary Logic
// ---------------------------------------------------------------------------

void RecentBooksActivity::revertMarkedForLaterStatus(const std::string& path) {
  const std::string cachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(path));
  const std::string progressPath = cachePath + "/progress.bin";

  uint8_t data[11] = {0};
  HalFile f;
  if (Storage.openFileForRead("RBA", progressPath, f)) {
    f.read(data, 10);
    f.close();
  }
  if (Storage.openFileForWrite("RBA", progressPath, f)) {
    f.write(data, 11);
    f.close();
  }
}

void RecentBooksActivity::launchDashboardMenu(const std::string& path, const std::string& title) {
  const int savedTab = selectedTabIndex;

  auto handler = [this, path, savedTab](const ActivityResult& res) {
    if (const auto* r = std::get_if<BookActionResult>(&res.data)) {
      RenderLock lock(*this); 
      if (r->removedFromList) {
        switch (savedTab) {
          case TAB_MARKED_FOR_LATER:
            MARKED_FOR_LATER_STORE.removeByPath(path);
            MARKED_FOR_LATER_STORE.saveToFile(); 
            revertMarkedForLaterStatus(path);
            markedForLater = MARKED_FOR_LATER_STORE.getEntries();
            break;
          case TAB_NEW_CHAPTERS:
            NEW_CHAPTERS_STORE.removeByPath(path);
            NEW_CHAPTERS_STORE.saveToFile(); 
            newChapters = NEW_CHAPTERS_STORE.getEntries();
            break;
          case TAB_WIPS:
            AO3_WIPS_STORE.removeBook(path);
            AO3_WIPS_STORE.saveToFile(); 
            wipsEntries = AO3_WIPS_STORE.getEntries();
            break;
          case TAB_RECENT_BOOKS:
            RECENT_BOOKS.removeByPath(path);
            recentBooks = RECENT_BOOKS.getBooks();
            break;
        }
        visibleStatusCache.clear();
      } else if (r->modified) {
        markedForLater = MARKED_FOR_LATER_STORE.getEntries();
        newChapters    = NEW_CHAPTERS_STORE.getEntries();
        wipsEntries    = AO3_WIPS_STORE.getEntries();
        recentBooks    = RECENT_BOOKS.getBooks();
        visibleStatusCache.clear();
      }
      
      rebuildRowItems();
      int listSize = listCount();
      if (listSize == 0) {
        moveRingTo(0);
      } else if (ringPos() - 1 >= listSize) {
        moveRingTo(listSize);
      }
      lock.unlock();
      requestUpdate(true);
    }
  };

  startActivityForResult(
      std::make_unique<BookActionActivity>(renderer, mappedInput, path, title, BookActionMode::DASHBOARD),
      std::move(handler));
}