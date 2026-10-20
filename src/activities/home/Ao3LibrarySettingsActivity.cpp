#include "Ao3LibrarySettingsActivity.h"
#include "Ao3TagMergeActivity.h"
#include "../ActivityResult.h"
#include <HalStorage.h>
#include <ArduinoJson.h>
#include <Logging.h>
#include <I18n.h>
#include "Ao3FolderPickerActivity.h"
#include "Ao3CoverAssignmentActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

void Ao3LibrarySettingsActivity::loadSettings() {
  ao3Folder = "";
  excludedFolders.clear();

  const char* path = "/.crosspoint/ao3_settings.json";
  if (!Storage.exists(path)) return;

  String json = Storage.readFile(path);
  if (json.isEmpty()) return;

  JsonDocument doc;
  if (deserializeJson(doc, json)) return;

  ao3Folder = doc["ao3Folder"] | "";
  batchSize = doc["batchSize"] | 10;
  autoIndexOnOpen = doc["autoIndexOnOpen"] | false;
  hideFinished = doc["hideFinished"] | false;
  filterMode = static_cast<FilterMode>(doc["filterMode"] | 0);
  if (filterMode > FilterMode::FOLDER_TREE) filterMode = FilterMode::AUTOMATIC;
  swapNavButtons = doc["swapNavButtons"] | false;
  JsonArray arr = doc["excludedFolders"];
  if (!arr.isNull()) {
    for (JsonVariant val : arr) {
      excludedFolders.push_back(val.as<std::string>());
    }
  }
}

void Ao3LibrarySettingsActivity::saveSettings() {
  JsonDocument doc;
  doc["ao3Folder"] = ao3Folder;
  doc["batchSize"] = batchSize;
  doc["autoIndexOnOpen"] = autoIndexOnOpen;
  doc["hideFinished"] = hideFinished;
  doc["filterMode"] = static_cast<uint8_t>(filterMode);
  doc["swapNavButtons"] = swapNavButtons;
  JsonArray arr = doc["excludedFolders"].to<JsonArray>();
  for (const auto& folder : excludedFolders) {
    arr.add(folder);
  }

  String json;
  serializeJson(doc, json);
  Storage.writeFile("/.crosspoint/ao3_settings.json", json);
}

std::string Ao3LibrarySettingsActivity::getFolderLastComponent(const std::string& path) const {
  if (path == "/") return "root/";
  if (path.empty()) return "";
  size_t lastSlash = path.find_last_of('/');
  if (lastSlash == std::string::npos) return path;
  return path.substr(lastSlash + 1);
}

std::string Ao3LibrarySettingsActivity::formatFolderPill() const {
  if (ao3Folder.empty()) return "Not Set";
  std::string last = getFolderLastComponent(ao3Folder);
  if (last.length() > 24) {
    return last.substr(0, 22) + "..";
  }
  return last;
}

std::string Ao3LibrarySettingsActivity::formatExclusionsPill() const {
  if (excludedFolders.empty()) return "Not Set";
  std::string result = "";
  for (size_t i = 0; i < excludedFolders.size(); i++) {
    if (i > 0) result += ",";
    result += getFolderLastComponent(excludedFolders[i]);
  }
  if (result.length() > 24) {
    return result.substr(0, 22) + "..";
  }
  return result;
}

void Ao3LibrarySettingsActivity::onEnter() {
  UiListActivity::onEnter();
  loadSettings();
  requestUpdate();
}

int Ao3LibrarySettingsActivity::listCount() const {
  return 10;
}

bool Ao3LibrarySettingsActivity::handleCustomInput() {
  if (showingCleanupConfirm) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      showingCleanupConfirm = false;
      requestUpdate(true);
      return true;
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      showingCleanupConfirm = false;
      cleaningUpIndex = true;
      requestUpdate(true);
      
      // Force an immediate render so the "Please Wait" screen appears during the blocking call
      render(RenderLock(*this));
      
      cleanupRemovedCount = Ao3Librarian::sanitizeIndex();
      cleaningUpIndex = false;
      showingCleanupResult = true;
      requestUpdate(true);
      return true;
    }
    return true; // Input absorbed by popup state
  }

  if (showingCleanupResult) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      showingCleanupResult = false;
      requestUpdate(true);
    }
    return true; // Input absorbed by popup state
  }
  return false;
}

bool Ao3LibrarySettingsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    saveSettings();
    finish();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }

  return false;
}

void Ao3LibrarySettingsActivity::navigateButtons() {
  // Override navigation to natively skip the greyed-out Tag Merge row
  auto nextVisual = [this](int current) -> int {
    int next = (current + 1) % 10;
    if (next == 6 && filterMode != FilterMode::AUTOMATIC) next = 7;
    return next;
  };

  auto prevVisual = [this](int current) -> int {
    int prev = (current + 9) % 10;
    if (prev == 6 && filterMode != FilterMode::AUTOMATIC) prev = 5;
    return prev;
  };

  buttonNavigator.onNextRelease([this, nextVisual] {
    nav.selected = nextVisual(nav.selected);
    nav.follow(10);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, prevVisual] {
    nav.selected = prevVisual(nav.selected);
    nav.follow(10);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this] {
    int next = (nav.selected + 2) % 10;
    if (next == 6 && filterMode != FilterMode::AUTOMATIC) next = 7;
    nav.selected = next;
    nav.follow(10);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this] {
    int prev = (nav.selected + 8) % 10;
    if (prev == 6 && filterMode != FilterMode::AUTOMATIC) prev = 5;
    nav.selected = prev;
    nav.follow(10);
    requestUpdate();
  });
}

void Ao3LibrarySettingsActivity::activateIndex(int index) {
  if (index == 0) {
    auto handler = [this](const ActivityResult& res) {
      if (!res.isCancelled) {
        if (const auto* pickerRes = std::get_if<FolderPickerResult>(&res.data)) {
          if (!pickerRes->isMulti) {
            ao3Folder = pickerRes->singlePath;
            excludedFolders.clear();
            saveSettings();
          }
        }
      }
      requestUpdate(true);
    };
    startActivityForResult(std::make_unique<Ao3FolderPickerActivity>(renderer, mappedInput, "Select AO3 Folder", PickerMode::SINGLE), handler);
  } else if (index == 1) {
    auto handler = [this](const ActivityResult& res) {
      if (!res.isCancelled) {
        if (const auto* pickerRes = std::get_if<FolderPickerResult>(&res.data)) {
          if (pickerRes->isMulti) {
            excludedFolders = pickerRes->multiPaths;
            saveSettings();
          }
        }
      }
      requestUpdate(true);
    };
    std::string startPath = ao3Folder.empty() ? "/" : ao3Folder;
    startActivityForResult(std::make_unique<Ao3FolderPickerActivity>(renderer, mappedInput, "Select Folders to Exclude", PickerMode::MULTI, excludedFolders, startPath), handler);
  } else if (index == 2) {
    const int sizes[] = {10, 25, 50};
    int current = 0;
    for (int i = 0; i < 3; i++) {
      if (sizes[i] == batchSize) { current = i; break; }
    }
    batchSize = sizes[(current + 1) % 3];
    saveSettings();
    requestUpdate();
  } else if (index == 3) {
    autoIndexOnOpen = !autoIndexOnOpen;
    saveSettings();
    requestUpdate();
  } else if (index == 4) {
    hideFinished = !hideFinished;
    saveSettings();
    requestUpdate();
  } else if (index == 5) {
    filterMode = (filterMode == FilterMode::AUTOMATIC)
                   ? FilterMode::FOLDER_TREE
                   : FilterMode::AUTOMATIC;
    saveSettings();
    requestUpdate();
  } else if (index == 6) {
    // Merge Similar Tags
    if (filterMode == FilterMode::AUTOMATIC) {
      auto handler = [this](const ActivityResult&) { requestUpdate(true); };
      startActivityForResult(std::make_unique<Ao3TagMergeActivity>(renderer, mappedInput), handler);
    }
  } else if (index == 7) {
    swapNavButtons = !swapNavButtons;
    saveSettings();
    requestUpdate();
  } else if (index == 8) {                       
    auto handler = [this](const ActivityResult&) { requestUpdate(true); };
    startActivityForResult(std::make_unique<Ao3CoverAssignmentActivity>(renderer, mappedInput), handler);
  } else if (index == 9) {                      
    showingCleanupConfirm = true;
    requestUpdate(true);
  }
}

void Ao3LibrarySettingsActivity::drawFooter() {
  if (cleaningUpIndex) return;

  if (showingCleanupConfirm) {
    const auto labels = mappedInput.mapLabels("Cancel", "Start", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }

  if (showingCleanupResult) {
    const auto labels = mappedInput.mapLabels("", "Done", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Select", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void Ao3LibrarySettingsActivity::buildScreen(UiScreen& uiScreen) {
  if (cleaningUpIndex) {
    uiScreen.centeredText("Please Wait. Cleaning up library...", uiScreen.theme().bodyText);
    return;
  }

  if (showingCleanupConfirm) {
    uiScreen.spacer(60);
    fui::TextStyle style = uiScreen.theme().bodyText;
    uiScreen.centeredText("The cleanup process will remove", style);
    uiScreen.spacer(5);
    uiScreen.centeredText("ghost books from your AO3 Library.", style);
    uiScreen.spacer(20);
    uiScreen.centeredText("This process can take up to a few minutes ", style);
    uiScreen.spacer(5);
    uiScreen.centeredText("to complete.", style);
    return;
  }

  if (showingCleanupResult) {
    if (cleanupRemovedCount < 0) {
      uiScreen.centeredText("Index not found.", uiScreen.theme().bodyText);
    } else if (cleanupRemovedCount == 0) {
      uiScreen.centeredText("Nothing to clean up.", uiScreen.theme().bodyText);
    } else {
      char buf[64];
      sprintf(buf, "%d ghost entr%s removed.", cleanupRemovedCount, cleanupRemovedCount == 1 ? "y" : "ies");
      uiScreen.centeredText(buf, uiScreen.theme().bodyText);
    }
    return;
  }

  fui::ListProps props;
  syncListViewport(uiScreen, props, false);

  std::vector<std::string> rows = {
    "Your AO3 Folder",
    "Never Index",
    "Index Batch Size",
    "Auto-Index on Library Open",
    "Hide Finished Fics",
    "Filter Mode",
    "Merge Similar Tags",
    "Side Button Layout",
    "Customize Fandom Covers",
    "Library Cleanup"
  };

  std::vector<fui::ListItem> items(10);
  std::vector<std::string> values(10);

  for (int i = 0; i < 10; i++) {
    items[i].label = rows[i].c_str();

    if (i == 0) values[i] = formatFolderPill();
    if (i == 1) values[i] = formatExclusionsPill();
    if (i == 2) values[i] = std::to_string(batchSize);
    if (i == 3) values[i] = autoIndexOnOpen ? "ON" : "OFF";
    if (i == 4) values[i] = hideFinished ? "ON" : "OFF";
    if (i == 5) values[i] = (filterMode == FilterMode::FOLDER_TREE) ? "Folder Tree" : "Automatic";
    if (i == 7) values[i] = swapNavButtons ? "Scroll List" : "Open Panels";

    // Standard list component formatting handles the black pill inherently
    if (!values[i].empty()) items[i].value = values[i].c_str();

    // The 1.6 framework automatically dims the text when set to false
    if (i == 6 && filterMode != FilterMode::AUTOMATIC) {
      items[i].enabled = false;
    }
  }

  props.valueText = uiScreen.theme().smallText;
  props.items = items.data();
  props.count = 10;
  uiScreen.list(props);
}