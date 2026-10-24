#pragma once
#include <string>
#include <vector>
#include "activities/UiListActivity.h"
#include "Ao3Librarian.h"
#include "components/OptionPopup.h"

enum class FilterMode : uint8_t {
    AUTOMATIC   = 0,
    FOLDER_TREE = 1
};

class Ao3LibrarySettingsActivity final : public UiListActivity {
  std::string ao3Folder;
  std::vector<std::string> excludedFolders;
  int batchSize = 10;
  bool autoIndexOnOpen = false;
  bool hideFinished = false;
  bool swapNavButtons = false;
  FilterMode filterMode = FilterMode::AUTOMATIC;
  OptionPopup optionPopup;
  
  bool showingCleanupResult = false;
  bool cleaningUpIndex = false;
  bool showingCleanupConfirm = false;
  int cleanupRemovedCount = 0;

  void loadSettings();
  void saveSettings();
  std::string getFolderLastComponent(const std::string& path) const;
  std::string formatFolderPill() const;
  std::string formatExclusionsPill() const;

 public:
  explicit Ao3LibrarySettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("Ao3LibrarySettings", renderer, mappedInput) {}

  void onEnter() override;

 protected:
  // --- UiListActivity Overrides ---
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void render(RenderLock&&) override;

  bool handleCustomInput() override;
  bool handleButtons() override;
  void navigateButtons() override;

  // --- Chrome & Footer Overrides ---
  const char* headerTitle() const override { return "AO3 Library Settings"; }
  void drawFooter() override;
};