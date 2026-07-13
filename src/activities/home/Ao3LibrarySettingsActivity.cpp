#include "Ao3LibrarySettingsActivity.h"
#include "../ActivityResult.h"
#include <HalStorage.h>
#include <ArduinoJson.h>
#include <Logging.h>
#include <I18n.h>
#include "Ao3FolderPickerActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

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
  filterMode = static_cast<FilterMode>(doc["filterMode"] | 0);
  if (filterMode > FilterMode::FOLDER_TREE) filterMode = FilterMode::AUTOMATIC;
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
  doc["filterMode"] = static_cast<uint8_t>(filterMode);
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
  Activity::onEnter();
  loadSettings();
  selectorIndex = 0;
  requestUpdate();
}

void Ao3LibrarySettingsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    saveSettings();
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectorIndex == 0) {
      // Pick AO3 Folder
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
    } else if (selectorIndex == 1){
      // Pick Non-AO3 Folders (Exclusions)
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
    } else if (selectorIndex == 2) {
      const int sizes[] = {10, 15, 20};
      int current = 0;
      for (int i = 0; i < 3; i++) {
        if (sizes[i] == batchSize) { current = i; break; }
      }
      batchSize = sizes[(current + 1) % 3];
      saveSettings();
      requestUpdate();
    } else if (selectorIndex == 3) {
      filterMode = (filterMode == FilterMode::AUTOMATIC)
                     ? FilterMode::FOLDER_TREE
                     : FilterMode::AUTOMATIC;
      saveSettings();
      requestUpdate();
    }
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectorIndex = (selectorIndex + 1) % 4;
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectorIndex = (selectorIndex + 3) % 4;
    requestUpdate();
  });
}

void Ao3LibrarySettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "AO3 Library Settings");

  // Two rows: Your AO3 Folder and Non-AO3 Folders
  std::vector<std::string> rows = {
    "Your AO3 Folder",
    "Ignored Folders",
    "Index Batch Size",
    "Filter Mode"
  };

  auto rowTitle = [&rows](int index) {
    return rows[index];
  };

  auto rowValue = [this](int index) -> std::string {
    if (index == 0) return formatFolderPill();
    if (index == 1) return formatExclusionsPill();
    if (index == 2) return std::to_string(batchSize);
    if (index == 3) return (filterMode == FilterMode::FOLDER_TREE) ? "Folder Tree" : "Automatic";
    return "";
  };

  int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, 4, selectorIndex,
               rowTitle, nullptr, nullptr, rowValue, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Select", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
