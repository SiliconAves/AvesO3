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

  // Row storage reused by every buildScreen() pass. Building the rows into
  // member storage (instead of local std::vectors) avoids allocator churn on
  // every repaint. Labels are constant (see kRowLabels in the .cpp); only the
  // value strings change between passes.
  static constexpr int ROW_COUNT = 10;
  freeink::ui::ListItem rowItems[ROW_COUNT];
  std::string rowValues[ROW_COUNT];  // backing storage for rowItems[i].value

  void loadSettings();
  void saveSettings();
  std::string getFolderLastComponent(const std::string& path) const;
  std::string formatFolderPill() const;
  std::string formatExclusionsPill() const;

  // --- FreeInkUI Custom Actions for Cleanup States ---
  static constexpr freeink::ui::ActionId ACTION_CLEANUP_CANCEL = 10;
  static constexpr freeink::ui::ActionId ACTION_CLEANUP_START  = 11;
  static constexpr freeink::ui::ActionId ACTION_CLEANUP_DONE   = 12;

  static void onActionTrampoline(const freeink::ui::ActionEvent& event, void* user);
  void onAction(const freeink::ui::ActionEvent& event);

  void doCleanupCancel();
  void doCleanupStart();
  void doCleanupDone();

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