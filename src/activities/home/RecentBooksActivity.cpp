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
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long LONG_PRESS_MS = 1000;

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

RecentBooksActivity::RecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("RecentBooks", renderer, mappedInput, /*wantsTouchLongPress=*/true) {}

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
    // TODO: Map visibleStatusCache to static Lucide icon
    item.icon = listIconFor(UITheme::getFileIcon(path), 32); 
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
  UiListActivity::onEnter();

  MARKED_FOR_LATER_STORE.loadFromFile();
  if (MARKED_FOR_LATER_STORE.pruneMissing()) MARKED_FOR_LATER_STORE.saveToFile();
  MARKED_FOR_LATER_STORE.loadFromFile();
  markedForLater = MARKED_FOR_LATER_STORE.getEntries();
  
  NEW_CHAPTERS_STORE.loadFromFile();
  if (NEW_CHAPTERS_STORE.pruneMissing()) NEW_CHAPTERS_STORE.saveToFile();
  NEW_CHAPTERS_STORE.loadFromFile();
  newChapters = NEW_CHAPTERS_STORE.getEntries();

  AO3_WIPS_STORE.loadFromFile();
  if (AO3_WIPS_STORE.pruneMissing()) AO3_WIPS_STORE.saveToFile();
  AO3_WIPS_STORE.loadFromFile();
  wipsEntries = AO3_WIPS_STORE.getEntries();

  if (RECENT_BOOKS.pruneMissing()) RECENT_BOOKS.saveToFile();
  recentBooks = RECENT_BOOKS.getBooks();

  if (!markedForLater.empty()) selectedTabIndex = TAB_MARKED_FOR_LATER;
  else if (!newChapters.empty()) selectedTabIndex = TAB_NEW_CHAPTERS;
  else if (!wipsEntries.empty()) selectedTabIndex = TAB_WIPS;
  else selectedTabIndex = TAB_RECENT_BOOKS;

  tabBarFocused = true;
  nav.selected = 0;
  
  // NOTE: Loading title patches for Marked For Later has been moved to a separate initialization flow or should happen here before rebuild.
  rebuildRowItems();
}

void RecentBooksActivity::onExit() {
  Activity::onExit();
  rowItems.clear();
  markedForLater.clear();
  newChapters.clear();
  wipsEntries.clear();
  recentBooks.clear();
  MARKED_FOR_LATER_STORE.clearEntries();
  NEW_CHAPTERS_STORE.clearEntries();     
  AO3_WIPS_STORE.clearEntries(); 
}

void RecentBooksActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  
  std::string path;
  if (selectedTabIndex == TAB_MARKED_FOR_LATER) path = markedForLater[index].path;
  else if (selectedTabIndex == TAB_NEW_CHAPTERS) path = newChapters[index].path;
  else if (selectedTabIndex == TAB_WIPS) path = wipsEntries[index].path;
  else if (selectedTabIndex == TAB_RECENT_BOOKS) path = recentBooks[index].path;
  
  if (!path.empty()) onSelectBook(path);
}

void RecentBooksActivity::onRowLongPress(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();

  std::string path, title;
  if (selectedTabIndex == TAB_MARKED_FOR_LATER) { path = markedForLater[index].path; title = markedForLater[index].title; }
  else if (selectedTabIndex == TAB_NEW_CHAPTERS) { path = newChapters[index].path; title = newChapters[index].title; }
  else if (selectedTabIndex == TAB_WIPS) { path = wipsEntries[index].path; title = wipsEntries[index].title; }
  else if (selectedTabIndex == TAB_RECENT_BOOKS) { path = recentBooks[index].path; title = recentBooks[index].title; }
  
  launchDashboardMenu(path, title);
}

bool RecentBooksActivity::handleCustomInput() {
  if (mappedInput.hasTouch()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int topPadding = metrics.topPadding / 2;
    const int tabBarTop = topPadding + metrics.headerHeight;

    int tappedTab = 0;
    const int tabStep = renderer.getScreenWidth() / TAB_COUNT;
    const auto tabTouch = mappedInput.colTouch(
        tappedTab, 0, tabStep, TAB_COUNT,
        tabBarTop, tabBarTop + metrics.tabBarHeight, tabStep);
        
    if (tabTouch == MappedInputManager::RowTouch::Tap && tappedTab != selectedTabIndex) {
      RenderLock lock(*this);
      selectedTabIndex = tappedTab;
      tabBarFocused = true;
      nav.selected = 0;
      rebuildRowItems();
      lock.unlock();
      requestUpdate();
      return true;
    }
  }
  return false;
}

bool RecentBooksActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (tabBarFocused) {
      const bool initialPress = mappedInput.wasPressed(MappedInputManager::Button::Confirm);
      if (initialPress || (millis() - lastTabCycleMs) >= 400) {
        lastTabCycleMs = millis();
        RenderLock lock(*this);
        selectedTabIndex = (selectedTabIndex + 1) % TAB_COUNT;
        nav.selected = 0;
        rebuildRowItems();
        lock.unlock();
        requestUpdate();
      }
      return true;
    } else {
      if (nav.selected >= 0 && nav.selected < listCount()) {
        activateIndex(nav.selected);
      }
      return true;
    }
  }

  // Intercept Down/Up to seamlessly transition focus between the Tab Bar and the List
  if (mappedInput.wasReleased(MappedInputManager::Button::Next) && tabBarFocused) {
    if (listCount() > 0) {
      tabBarFocused = false;
      nav.selected = 0;
      requestUpdate();
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Previous) && !tabBarFocused && nav.selected == 0) {
    tabBarFocused = true;
    requestUpdate();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (!tabBarFocused) {
      tabBarFocused = true;
      requestUpdate();
    } else {
      onGoHome();
    }
    return true;
  }
  return false;
}

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
      RenderLock lock(*this); // Lock before touching UI arrays
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
      } else if (r->modified) {
        markedForLater = MARKED_FOR_LATER_STORE.getEntries();
        newChapters    = NEW_CHAPTERS_STORE.getEntries();
        wipsEntries    = AO3_WIPS_STORE.getEntries();
        recentBooks    = RECENT_BOOKS.getBooks();
      }
      
      rebuildRowItems();
      if (rowItems.empty()) {
        tabBarFocused = true;
        nav.selected = 0;
      } else if (nav.selected >= listCount()) {
        nav.selected = listCount() - 1;
      }
      nav.follow(listCount());
      lock.unlock();
      requestUpdate(true);
    }
  };

  startActivityForResult(
      std::make_unique<BookActionActivity>(renderer, mappedInput, path, title, BookActionMode::DASHBOARD),
      std::move(handler));
}

void RecentBooksActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Clear space for both the Header AND the Tab Bar
  const int topPadding = metrics.topPadding / 2;
  const int contentTop = topPadding + metrics.headerHeight + metrics.tabBarHeight + 10;
  
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(contentTop), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});

  if (rowItems.empty()) {
    const char* msg = (selectedTabIndex == TAB_RECENT_BOOKS) ? tr(STR_NO_RECENT_BOOKS) : EMPTY_MESSAGES[selectedTabIndex];
    screen.centeredText(msg, screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  
  // If the Tab Bar is focused, explicitly clear selection visualization from the list
  if (tabBarFocused) props.selectionOverride = -1;

  fui::TextStyle label = screen.theme().smallText;
  label.bold = true;
  props.labelText = label;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void RecentBooksActivity::drawChrome() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int topPadding = metrics.topPadding / 2;

  GUI.drawHeader(renderer, Rect{0, topPadding, pageWidth, metrics.headerHeight}, tr(STR_MENU_RECENT_BOOKS));

  std::vector<TabInfo> tabs;
  tabs.reserve(TAB_COUNT);
  for (int i = 0; i < TAB_COUNT; i++) {
    tabs.push_back({TAB_NAMES[i], selectedTabIndex == i});
  }
  
  GUI.drawTabBar(renderer, Rect{0, topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                 tabs, tabBarFocused);
}

void RecentBooksActivity::drawFooter() {
  const char* confirmLabel = tabBarFocused ? TAB_NAMES[(selectedTabIndex + 1) % TAB_COUNT] : tr(STR_OPEN);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}