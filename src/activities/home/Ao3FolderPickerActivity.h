#pragma once
#include <string>
#include <vector>
#include "../Activity.h"
#include "../../util/ButtonNavigator.h"

enum class PickerMode {
  SINGLE,
  MULTI
};

class Ao3FolderPickerActivity final : public Activity {
  std::string title;
  PickerMode mode;
  std::string currentPath;
  std::vector<std::string> directories;
  std::vector<std::string> selectedPaths;
  
  size_t selectorIndex = 0;
  ButtonNavigator buttonNavigator;

  void loadDirectories();
  bool isSelected(const std::string& path) const;
  void toggleSelection(const std::string& path);

 public:
  Ao3FolderPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                          std::string title, PickerMode mode,
                          std::vector<std::string> initialSelected = {},
                          std::string startPath = "/")
      : Activity("Ao3FolderPicker", renderer, mappedInput),
        title(std::move(title)),
        mode(mode),
        currentPath(std::move(startPath)),
        selectedPaths(std::move(initialSelected)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
