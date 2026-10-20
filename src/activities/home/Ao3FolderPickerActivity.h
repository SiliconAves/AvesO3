#pragma once
#include <string>
#include <vector>
#include "activities/UiListActivity.h"

enum class PickerMode {
  SINGLE,
  MULTI
};

class Ao3FolderPickerActivity final : public UiListActivity {
  std::string title;
  PickerMode mode;
  std::string currentPath;
  std::vector<std::string> directories;
  std::vector<std::string> selectedPaths;

  void loadDirectories();
  bool isSelected(const std::string& path) const;
  void toggleSelection(const std::string& path);
  std::string getFullPath(int index) const;

 public:
  Ao3FolderPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                          std::string title, PickerMode mode,
                          std::vector<std::string> initialSelected = {},
                          std::string startPath = "/")
      // Pass true for wantsTouchLongPress to enable folder entry via touch
      : UiListActivity("Ao3FolderPicker", renderer, mappedInput, true),
        title(std::move(title)),
        mode(mode),
        currentPath(std::move(startPath)),
        selectedPaths(std::move(initialSelected)) {}

  void onEnter() override;
  void onExit() override;

 protected:
  // --- UiListActivity Overrides ---
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleButtons() override;
  
  // --- Chrome & Footer Overrides ---
  const char* headerTitle() const override { return title.c_str(); }
  void drawChrome() override;
  void drawFooter() override;
};