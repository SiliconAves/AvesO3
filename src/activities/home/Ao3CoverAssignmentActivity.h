#pragma once
#include <string>
#include <vector>
#include "activities/UiListActivity.h"


class Ao3CoverAssignmentActivity final : public UiListActivity {
 public:
  explicit Ao3CoverAssignmentActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("Ao3CoverAssign", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 protected:
  // --- UiListActivity Overrides ---
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override; // Touch fallback route
  void onRowAction(const freeink::ui::ActionEvent& event) override; // Native touch handling
  bool handleButtons() override;
  void navigateButtons() override;
  freeink::ui::ListNav& activeNav() override;
  
  // --- Chrome & Footer Overrides ---
  void drawChrome() override;
  void drawFooter() override;

 private:
  enum class Screen { FANDOMS, FILES };
  Screen screen = Screen::FANDOMS;
  
  // Independent scroll state trackers for each screen
  freeink::ui::ListNav fandomsNav;
  freeink::ui::ListNav filesNav;

  std::vector<std::string> fandoms;   // raw names from the index
  std::vector<std::string> files;     // files[0] = "None"
  
  int cachedFandomSelector = 0;

  void loadFandoms();
  void loadFiles();
  void buildFandomsScreen(UiScreen& uiScreen);
  void buildFilesScreen(UiScreen& uiScreen);
};