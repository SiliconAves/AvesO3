#include "Ao3FolderPickerActivity.h"
#include "../ActivityResult.h"
#include <HalStorage.h>
#include <Logging.h>
#include <I18n.h>
#include <algorithm>
#include "../../components/UITheme.h"
#include "../../fontIds.h"


void Ao3FolderPickerActivity::loadDirectories() {
  directories.clear();
  auto root = Storage.open(currentPath.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  root.rewindDirectory();
  char name[256];
  FsFile file;
  while (file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if (name[0] != '.' && file.isDirectory() && strcmp(name, "System Volume Information") != 0 && strcmp(name, ".crosspoint") != 0) {
      directories.push_back(name);
    }
    file.close();
  }
  root.close();

  std::sort(directories.begin(), directories.end(), [](const std::string& a, const std::string& b) {
    return strcasecmp(a.c_str(), b.c_str()) < 0;
  });

  if (currentPath == "/" && mode == PickerMode::SINGLE) {
    directories.insert(directories.begin(), "/");
  }
}

bool Ao3FolderPickerActivity::isSelected(const std::string& path) const {
  return std::find(selectedPaths.begin(), selectedPaths.end(), path) != selectedPaths.end();
}

void Ao3FolderPickerActivity::toggleSelection(const std::string& path) {
  auto it = std::find(selectedPaths.begin(), selectedPaths.end(), path);
  if (it != selectedPaths.end()) {
    selectedPaths.erase(it);
  } else {
    selectedPaths.push_back(path);
  }
}

void Ao3FolderPickerActivity::onEnter() {
  Activity::onEnter();
  loadDirectories();
  selectorIndex = 0;
  requestUpdate();
}

void Ao3FolderPickerActivity::onExit() {
  Activity::onExit();
  directories.clear();
}

void Ao3FolderPickerActivity::loop() {
  int listSize = static_cast<int>(directories.size());

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (listSize > 0 && selectorIndex < directories.size()) {
      std::string nextDir = directories[selectorIndex];
      const bool isRootSentinel = (currentPath == "/" && selectorIndex == 0 && mode == PickerMode::SINGLE);

      std::string fullPath = currentPath;
      if (!isRootSentinel) {
        if (fullPath.back() != '/') fullPath += "/";
        fullPath += nextDir;
      }

      if (mappedInput.getHeldTime() >= 1000) {
        // Hold Confirm -> Navigate into
        currentPath = fullPath;
        loadDirectories();
        selectorIndex = 0;
        requestUpdate();
      } else {
        // Short Confirm
        if (mode == PickerMode::SINGLE) {
          // Select and exit
          FolderPickerResult res;
          res.singlePath = fullPath;
          res.isMulti = false;
          setResult(ActivityResult(std::move(res)));
          finish();
        } else {
          // Toggle selection in MULTI mode
          toggleSelection(fullPath);
          requestUpdate();
        }
      }
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (mode == PickerMode::SINGLE) {
      if (currentPath != "/") {
        // Go up one directory level
        size_t lastSlash = currentPath.find_last_of('/');
        if (lastSlash == 0) {
          currentPath = "/";
        } else {
          currentPath = currentPath.substr(0, lastSlash);
        }
        loadDirectories();
        selectorIndex = 0;
        requestUpdate();
      } else {
        // Already at root `/`, exit without selecting (cancelled)
        setResult(ActivityResult::cancel());
        finish();
      }
    } else {
      // MULTI mode Back -> confirms selections and exits
      FolderPickerResult res;
      res.multiPaths = selectedPaths;
      res.isMulti = true;
      setResult(ActivityResult(std::move(res)));
      finish();
    }
    return;
  }

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });
}

void Ao3FolderPickerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());

  // Draw current path below header
  std::string displayPath = "root" + currentPath;
  if (displayPath.back() != '/') displayPath += "/";
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding,
                metrics.topPadding + metrics.headerHeight + 5,
                displayPath.c_str(), true, EpdFontFamily::BOLD);

  const int contentTop = metrics.topPadding + metrics.headerHeight + 38;
  const int helperTextHeight = 37;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - helperTextHeight;

  if (directories.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, "No directories found.");
  } else {
    auto rowTitle = [this](int index) -> std::string {
      if (currentPath == "/" && index == 0 && mode == PickerMode::SINGLE) return "root/";
      return directories[index];
    };
    auto rowValue = [this](int index) {
      if (mode == PickerMode::MULTI) {
        std::string path = (currentPath == "/") ? "/" + directories[index] : currentPath + "/" + directories[index];
        return isSelected(path) ? "•" : "";
      }
      return "";
    };
    auto rowIcon = [this](int index) {
      return UITheme::getFileIcon(directories[index] + "/");
    };

    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, directories.size(), selectorIndex,
                 rowTitle, nullptr, rowIcon, rowValue, false);
  }

  // Draw Helper Text
  const std::string helperText = "Hold: Enter Folder | Short Press: Select Folder";
  int textWidth = renderer.getTextWidth(SMALL_FONT_ID, helperText.c_str());
  int textX = (pageWidth - textWidth) / 2;
  renderer.drawText(SMALL_FONT_ID, textX, pageHeight - metrics.buttonHintsHeight - 35, helperText.c_str());

  // Button hints
  const char* btn1Label = tr(STR_BACK);
  const char* btn2Label = (mode == PickerMode::SINGLE) ? "Select" : "Toggle";
  const char* btn3Label = tr(STR_DIR_UP);
  const char* btn4Label = tr(STR_DIR_DOWN);

  const auto labels = mappedInput.mapLabels(btn1Label, btn2Label, btn3Label, btn4Label);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
