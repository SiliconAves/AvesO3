#include "Ao3LibrarySettingsActivity.h"
#include "Ao3TagMergeActivity.h"
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
  Activity::onEnter();
  loadSettings();
  selectorIndex = 0;
  requestUpdate();
}

void Ao3LibrarySettingsActivity::loop() {
  if (showingCleanupConfirm) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      showingCleanupConfirm = false;
      requestUpdate(true);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      showingCleanupConfirm = false;
      cleaningUpIndex = true;
      requestUpdate(true);
      render(RenderLock());
      cleanupRemovedCount = Ao3Librarian::sanitizeIndex();
      cleaningUpIndex = false;
      showingCleanupResult = true;
      requestUpdate(true);
    }
    return;
  }

  if (showingCleanupResult) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      showingCleanupResult = false;
      requestUpdate(true);
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    saveSettings();
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectorIndex == 0) {
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
    } else if (selectorIndex == 1) {
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
      const int sizes[] = {10, 25, 50};
      int current = 0;
      for (int i = 0; i < 3; i++) {
        if (sizes[i] == batchSize) { current = i; break; }
      }
      batchSize = sizes[(current + 1) % 3];
      saveSettings();
      requestUpdate();
    } else if (selectorIndex == 3) {
      autoIndexOnOpen = !autoIndexOnOpen;
      saveSettings();
      requestUpdate();
    } else if (selectorIndex == 4) {
      hideFinished = !hideFinished;
      saveSettings();
      requestUpdate();
    } else if (selectorIndex == 5) {
      filterMode = (filterMode == FilterMode::AUTOMATIC)
                     ? FilterMode::FOLDER_TREE
                     : FilterMode::AUTOMATIC;
      saveSettings();
      requestUpdate();
    } else if (selectorIndex == 6) {
      // Merge Similar Tags — only active in Automatic mode
      if (filterMode == FilterMode::AUTOMATIC) {
        auto handler = [this](const ActivityResult&) { requestUpdate(true); };
        startActivityForResult(std::make_unique<Ao3TagMergeActivity>(renderer, mappedInput), handler);
      }
      return;
    } else if (selectorIndex == 7) {
      swapNavButtons = !swapNavButtons;
      saveSettings();
      requestUpdate();
    } else if (selectorIndex == 8) {
      showingCleanupConfirm = true;
      requestUpdate(true);
      return;
    }
    return;
}

  buttonNavigator.onNextRelease([this] {
    selectorIndex = (selectorIndex + 1) % 9;
    // Skip "Merge Similar Tags" if disabled (moving forward)
    if (selectorIndex == 6 && filterMode != FilterMode::AUTOMATIC) {
      selectorIndex = 7;
    }
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectorIndex = (selectorIndex + 8) % 9;
    // Skip "Merge Similar Tags" if disabled (moving backward)
    if (selectorIndex == 6 && filterMode != FilterMode::AUTOMATIC) {
      selectorIndex = 5;
    }
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this] {
    selectorIndex = (selectorIndex + 2) % 9;
    // Skip "Merge Similar Tags" if disabled (fast scrolling forward)
    if (selectorIndex == 6 && filterMode != FilterMode::AUTOMATIC) {
      selectorIndex = 7;
    }
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this] {
    selectorIndex = (selectorIndex + 7) % 9;
    // Skip "Merge Similar Tags" if disabled (fast scrolling backward)
    if (selectorIndex == 6 && filterMode != FilterMode::AUTOMATIC) {
      selectorIndex = 5;
    }
    requestUpdate();
  });
}

void Ao3LibrarySettingsActivity::render(RenderLock&&) {
  if (cleaningUpIndex) {
    renderer.clearScreen();
    const auto pageHeight = renderer.getScreenHeight();
    const auto& metrics = UITheme::getInstance().getMetrics();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, "Library Cleanup");
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Please Wait. Cleaning up library...");
    renderer.displayBuffer();
    return;
  }

  if (showingCleanupConfirm) {
    renderer.clearScreen();
    const auto pageHeight = renderer.getScreenHeight();
    const auto& metrics = UITheme::getInstance().getMetrics();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, "Library Cleanup");
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10,
      "The cleanup process will remove");
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 14,
      "ghost books from your AO3 Library.");
    const auto labels = mappedInput.mapLabels("Cancel", "Start", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (showingCleanupResult) {
    renderer.clearScreen();
    const auto pageHeight = renderer.getScreenHeight();
    const auto& metrics = UITheme::getInstance().getMetrics();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, "Library Cleanup");
    if (cleanupRemovedCount < 0) {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Index not found.");
    } else if (cleanupRemovedCount == 0) {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Nothing to clean up.");
    } else {
      char buf[64];
      sprintf(buf, "%d ghost entr%s removed.", cleanupRemovedCount, cleanupRemovedCount == 1 ? "y" : "ies");
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, buf);
    }
    const auto labels = mappedInput.mapLabels("", "Done", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "AO3 Library Settings");

  // Two rows: Your AO3 Folder and Non-AO3 Folders
  std::vector<std::string> rows = {
    "Your AO3 Folder",
    "Never Index",
    "Index Batch Size",
    "Auto-Index on Library Open",
    "Hide Finished Fics",
    "Filter Mode",
    "Merge Similar Tags",
    "Side Button Layout",
    "Library Cleanup"
  };

  auto rowTitle = [&rows](int index) {
    return rows[index];
  };

  auto rowValue = [this](int index) -> std::string {
    if (index == 0) return formatFolderPill();
    if (index == 1) return formatExclusionsPill();
    if (index == 2) return std::to_string(batchSize);
    if (index == 3) return autoIndexOnOpen ? "ON" : "OFF";
    if (index == 4) return hideFinished ? "ON" : "OFF";
    if (index == 5) return (filterMode == FilterMode::FOLDER_TREE) ? "Folder Tree" : "Automatic";
    if (index == 6) return "";
    if (index == 7) return swapNavButtons ? "Scroll List" : "Open Panels";
    return "";
};

  auto rowDimmed = [this](int index) -> bool {
    return index == 6 && filterMode != FilterMode::AUTOMATIC;
};

  int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, 9, selectorIndex,
               rowTitle, nullptr, nullptr, rowValue, true, rowDimmed);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Select", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
