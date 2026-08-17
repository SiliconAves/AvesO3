#pragma once
#include <I18n.h>
 
#include <string>
#include <vector>
 
#include "Ao3NewChaptersStore.h"
#include "Ao3WipsStore.h"
#include "Ao3MarkedForLaterStore.h"

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
 
class RecentBooksActivity final : public Activity {
 private:
  static constexpr int TAB_COUNT            = 4;
  static constexpr int TAB_MARKED_FOR_LATER = 0;
  static constexpr int TAB_NEW_CHAPTERS     = 1;
  static constexpr int TAB_WIPS             = 2;
  static constexpr int TAB_RECENT_BOOKS     = 3;
 
  ButtonNavigator buttonNavigator;
 
  // selectedTabIndex  : which tab is active (0–3)
  // selectedItemIndex : 0 = tab bar focused, >= 1 = list item
  // (mirrors SettingsActivity's selectedCategoryIndex / selectedSettingIndex)
  int selectedTabIndex  = TAB_RECENT_BOOKS;
  int selectedItemIndex = 0;
  unsigned long lastTabCycleMs = 0;
 
  // Set when a long-press has fired; swallows input until Confirm is released.
  bool longPressFired = false;
 
  // Tab 0 data
  std::vector<Ao3MarkedForLaterEntry> markedForLater;
  
  // Tab 1 data
  std::vector<Ao3NewChaptersEntry> newChapters;

  // Tab 2 data
  std::vector<Ao3WipEntry> wipsEntries;
 
  // Tab 3 data
  std::vector<RecentBook> recentBooks;
 
  int  getCurrentListSize() const;
  void promptRemoveBook(const std::string& path, const std::string& title);
  void promptRemoveNewChaptersEntry(const std::string& path, const std::string& title);
  void promptRemoveWipsEntry(const std::string& path, const std::string& title);
  void promptRemoveMarkedEntry(const std::string& path, const std::string& title);

  void patchMissingTitles(std::vector<Ao3MarkedForLaterEntry>& entries);
 
 public:
  explicit RecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RecentBooks", renderer, mappedInput) {}
 
  void onEnter() override;
  void onExit()  override;
  void loop()    override;
  void render(RenderLock&&) override;
};