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
#include "activities/UiTabListActivity.h"

class RecentBooksActivity final : public UiTabListActivity {
 private:
  static constexpr int TAB_COUNT            = 4;
  static constexpr int TAB_MARKED_FOR_LATER = 0;
  static constexpr int TAB_NEW_CHAPTERS     = 1;
  static constexpr int TAB_WIPS             = 2;
  static constexpr int TAB_RECENT_BOOKS     = 3;

  int selectedTabIndex = TAB_RECENT_BOOKS;
  bool isLoading = false;

  std::vector<Ao3MarkedForLaterEntry> markedForLater;
  std::vector<Ao3NewChaptersEntry> newChapters;
  std::vector<Ao3WipEntry> wipsEntries;
  std::vector<RecentBook> recentBooks;

  std::vector<freeink::ui::ListItem> rowItems;
  std::map<int, BookStatus> visibleStatusCache;
  int lastRenderedTab = -1;

  int tabCount() const override { return TAB_COUNT; }
  int activeTab() const override { return selectedTabIndex; }
  const char* tabLabel(int index) const override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;
  
  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  const char* headerTitle() const override { return tr(STR_MENU_RECENT_BOOKS); }
  void drawFooter() override;
  
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleButtons() override;
  bool handleCustomInput() override; // NEW: Touch long-press intercept
  void onRowAction(const freeink::ui::ActionEvent& event) override;
  void navigateButtons() override;

  void rebuildRowItems();
  void launchDashboardMenu(const std::string& path, const std::string& title);
  void revertMarkedForLaterStatus(const std::string& path);
  BookStatus getBookStatus(const std::string& path);
  freeink::ui::BitmapRef getStatusIcon(BookStatus status, const std::string& path);

 public:
  explicit RecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override; 
};