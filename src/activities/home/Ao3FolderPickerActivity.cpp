#include "Ao3FolderPickerActivity.h"
#include "../ActivityResult.h"
#include <HalStorage.h>
#include <Logging.h>
#include <I18n.h>
#include <algorithm>
#include "components/UITheme.h"
#include "fontIds.h"

#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

void Ao3FolderPickerActivity::loadDirectories() {
  directories.clear();
  auto root = Storage.open(currentPath.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  root.rewindDirectory();
  char name[256];
  HalFile file;
  while ((file = root.openNextFile())) {
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

std::string Ao3FolderPickerActivity::getFullPath(int index) const {
  if (currentPath == "/" && index == 0 && mode == PickerMode::SINGLE) return "/";
  std::string fullPath = currentPath;
  if (fullPath.back() != '/') fullPath += "/";
  fullPath += directories[index];
  return fullPath;
}

void Ao3FolderPickerActivity::onEnter() {
  UiListActivity::onEnter();
  loadDirectories();
  requestUpdate();
}

void Ao3FolderPickerActivity::onExit() {
  UiListActivity::onExit();
  directories.clear();
}

int Ao3FolderPickerActivity::listCount() const {
  return directories.size();
}

// Replaces the old Short Confirm
void Ao3FolderPickerActivity::activateIndex(int index) {
  std::string fullPath = getFullPath(index);
  
  if (mode == PickerMode::SINGLE) {
    FolderPickerResult res;
    res.singlePath = fullPath;
    res.isMulti = false;
    setResult(ActivityResult(std::move(res)));
    finish();
  } else {
    toggleSelection(fullPath);
    requestUpdate();
  }
}

// Replaces the old Hold Confirm
void Ao3FolderPickerActivity::onRowLongPress(int index) {
  std::string fullPath = getFullPath(index);
  if (fullPath == "/") return; // Sentinel, don't navigate into root sentinel

  currentPath = fullPath;
  loadDirectories();
  nav.selected = 0; // Reset scroll position for the new folder
  requestUpdate();
}

bool Ao3FolderPickerActivity::handleButtons() {
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
        nav.selected = 0;
        requestUpdate();
      } else {
        // Cancelled
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
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    int index = nav.selected;
    if (index >= 0 && index < listCount()) {
      if (mappedInput.getHeldTime() >= 1000) {
        onRowLongPress(index);
      } else {
        activateIndex(index);
      }
    }
    return true;
  }

  return false;
}

void Ao3FolderPickerActivity::drawChrome() {
  UiListActivity::drawChrome(); // Draws standard header
  
  // Draw current path below header
  const auto& metrics = UITheme::getInstance().getMetrics();
  std::string displayPath = "root" + currentPath;
  if (displayPath.back() != '/') displayPath += "/";
  
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding,
                    metrics.topPadding + metrics.headerHeight + 5,
                    displayPath.c_str(), true, EpdFontFamily::BOLD);
}

void Ao3FolderPickerActivity::drawFooter() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

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
}

void Ao3FolderPickerActivity::buildScreen(UiScreen& screen) {
  // Push the list down slightly so it doesn't overlap our manually drawn path in drawChrome
  screen.spacer(25);
  // Reserve space for the helper text
  screen.takeBottom(37);

  if (directories.empty()) {
    // FIX 1: Use screen.theme().bodyText directly (it returns the correct fui::TextStyle)
    screen.target().text(screen.takeTop(30), "No directories found.", screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  syncListViewport(screen, props, false);

  std::vector<fui::ListItem> items(directories.size());
  for (size_t i = 0; i < directories.size(); i++) {
    if (currentPath == "/" && i == 0 && mode == PickerMode::SINGLE) {
      items[i].label = "root/";
    } else {
      items[i].label = directories[i].c_str();
    }
    
    // FIX 2: Wrap the legacy UIIcon in listIconFor() to satisfy the 1.6 BitmapRef requirement
    items[i].icon = listIconFor(UITheme::getFileIcon(directories[i] + "/"));
    
    if (mode == PickerMode::MULTI) {
      if (isSelected(getFullPath(i))) {
        items[i].value = "•";
      }
    }
  }

  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  screen.list(props);
}