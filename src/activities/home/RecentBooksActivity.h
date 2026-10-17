#pragma once
#include <I18n.h>
#include <string>
#include <vector>
#include <map>

#include "Ao3NewChaptersStore.h"
#include "Ao3WipsStore.h"
#include "Ao3MarkedForLaterStore.h"
#include "RecentBooksStore.h"
#include "BookStatus.h"
#include "activities/UiListActivity.h"

class RecentBooksActivity final : public UiListActivity {
 private:
  static constexpr int TAB_COUNT            = 4;
  static constexpr int TAB_MARKED_FOR_LATER = 0;
  static constexpr int TAB_NEW_CHAPTERS     = 1;
  static constexpr int TAB_WIPS             = 2;
  static constexpr int TAB_RECENT_BOOKS     = 3;

  int selectedTabIndex  = TAB_RECENT_BOOKS;
  bool tabBarFocused    = true; // Replaces selectedItemIndex == 0
  unsigned long lastTabCycleMs = 0;

  // Tab Data Payloads
  std::vector<Ao3MarkedForLaterEntry> markedForLater;
  std::vector<Ao3NewChaptersEntry> newChapters;
  std::vector<Ao3WipEntry> wipsEntries;
  std::vector<RecentBook> recentBooks;

  // 1.6 UI Engine Data
  std::vector<freeink::ui::ListItem> rowItems;
  std::map<int, BookStatus> visibleStatusCache;
  int lastRenderedTab = -1;

  // Engine Overrides
  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void drawChrome() override;
  void drawFooter() override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;

  // Custom AvesO3 Logic
  void rebuildRowItems();
  void launchDashboardMenu(const std::string& path, const std::string& title);
  void revertMarkedForLaterStatus(const std::string& path);
  BookStatus getBookStatus(const std::string& path);

 public:
  explicit RecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
};