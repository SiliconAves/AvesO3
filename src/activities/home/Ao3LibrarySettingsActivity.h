#pragma once
#include <string>
#include <vector>
#include "../Activity.h"
#include "../../util/ButtonNavigator.h"
#include "../../Ao3Librarian.h"

enum class FilterMode : uint8_t {
    AUTOMATIC   = 0,
    FOLDER_TREE = 1
};
class Ao3LibrarySettingsActivity final : public Activity {
  int selectorIndex = 0;
  ButtonNavigator buttonNavigator;

  std::string ao3Folder;
  std::vector<std::string> excludedFolders;
  int batchSize = 10;
  bool autoIndexOnOpen = false;
  bool swapNavButtons = false;
  FilterMode filterMode = FilterMode::AUTOMATIC;
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
      : Activity("Ao3LibrarySettings", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
