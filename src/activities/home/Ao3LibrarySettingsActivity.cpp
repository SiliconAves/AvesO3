#include "Ao3LibrarySettingsActivity.h"
#include "Ao3TagMergeActivity.h"
#include "../ActivityResult.h"
#include <HalStorage.h>
#include <ArduinoJson.h>
#include <Logging.h>
#include <I18n.h>
#include <algorithm>
#include <initializer_list>
#include "Ao3FolderPickerActivity.h"
#include "Ao3CoverAssignmentActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
// Row labels never change, so they live in flash instead of being rebuilt into
// a std::vector<std::string> on every buildScreen() pass.
constexpr const char* kRowLabels[] = {
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
}  // namespace

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
  // Map FONT_BODY to 10pt for the list items
  uiTarget.setFont(freeink::ui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  
  // Register custom FUI actions for the popup states after base class initialization
  app.on(ACTION_CLEANUP_CANCEL, onActionTrampoline, this);
  app.on(ACTION_CLEANUP_START, onActionTrampoline, this);
  app.on(ACTION_CLEANUP_DONE, onActionTrampoline, this);

  loadSettings();
  requestUpdate();
}

void Ao3LibrarySettingsActivity::onActionTrampoline(const freeink::ui::ActionEvent& event, void* user) {
  static_cast<Ao3LibrarySettingsActivity*>(user)->onAction(event);
}

void Ao3LibrarySettingsActivity::onAction(const freeink::ui::ActionEvent& event) {
  if (event.action == ACTION_CLEANUP_CANCEL) doCleanupCancel();
  else if (event.action == ACTION_CLEANUP_START) doCleanupStart();
  else if (event.action == ACTION_CLEANUP_DONE) doCleanupDone();
}

void Ao3LibrarySettingsActivity::doCleanupCancel() {
  showingCleanupConfirm = false;
  requestUpdate(true);
}

void Ao3LibrarySettingsActivity::doCleanupStart() {
  showingCleanupConfirm = false;
  cleaningUpIndex = true;
  requestUpdate(true);
  
  // Force an immediate render so the "Please Wait" screen appears during the blocking call
  render(RenderLock(*this));
  
  cleanupRemovedCount = Ao3Librarian::sanitizeIndex();
  cleaningUpIndex = false;
  showingCleanupResult = true;
  requestUpdate(true);
}

void Ao3LibrarySettingsActivity::doCleanupDone() {
  showingCleanupResult = false;
  requestUpdate(true);
}

int Ao3LibrarySettingsActivity::listCount() const {
  return 10;
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

bool Ao3LibrarySettingsActivity::handleCustomInput() {
  // Route input to the popup if it's active
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    return true;
  }
  
  if (showingCleanupConfirm) {
    // Route touch to allow the FUI footer to be tapped
    auto route = UiAppHost::routeTouch(mappedInput, false);
    
    // Also handle physical buttons for legacy devices
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      doCleanupCancel();
      return true;
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      doCleanupStart();
      return true;
    }
    return true; // Input absorbed by popup state
  }

  if (showingCleanupResult) {
    // Route touch so tapping anywhere on the screen (or the footer) dismisses the result popup
    auto route = UiAppHost::routeTouch(mappedInput, false);
    
    // Handle physical buttons for legacy devices
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        route.routed) {
      doCleanupDone();
    }
    return true; // Input absorbed by popup state
  }
  
  return false;
}

void Ao3LibrarySettingsActivity::activateIndex(int index) {
  if (optionPopup.isActive()) return;

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
    app.clearTapFlash();
    
    static const char* options[] = {"10", "25", "50"};
    int current = (batchSize == 50) ? 2 : ((batchSize == 25) ? 1 : 0);
    
    optionPopup.show("Index Batch Size", options, 3, current, [this](int selected) {
      batchSize = (selected == 2) ? 50 : ((selected == 1) ? 25 : 10);
      saveSettings();
      requestUpdate();
    });
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
  // Suppress legacy button hints entirely on touch devices
  if (mappedInput.hasTouch()) {
    return;
  }

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
  const auto& metrics = UITheme::getInstance().getMetrics();
  uiScreen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing),
      0,
      static_cast<int16_t>(metrics.buttonHintsHeight),
      0});

  fui::TextStyle centerStyle = uiScreen.theme().bodyText;
  centerStyle.align = fui::TextAlign::Center;

  // std::initializer_list avoids the heap allocation the old std::vector<const char*>
  // parameter made on every call.
  auto drawCenteredLines = [&](std::initializer_list<const char*> lines) {
    const int lh = uiTarget.lineHeight(centerStyle.font);
    const int totalH = lines.size() * lh + (lines.size() - 1) * 5;
    if (uiScreen.body().height > totalH) {
      uiScreen.spacer((uiScreen.body().height - totalH) / 2);
    }
    for (const char* line : lines) {
      uiScreen.target().text(uiScreen.takeTop(lh), line, centerStyle);
      if (line != *(lines.end() - 1)) uiScreen.spacer(5);
    }
  };

  if (cleaningUpIndex) {
    drawCenteredLines({"Please Wait. Cleaning up library..."});
    return;
  }

  if (showingCleanupConfirm) {
    drawCenteredLines({
      "The cleanup process will remove",
      "ghost books from your AO3 Library.",
      "",
      "This process can take up to a few minutes",
      "to complete."
    });
    
    // Touch footer for confirmation state
    if (mappedInput.hasTouch()) {
      freeink::ui::FooterAction actions[] = {
        {"Cancel", ACTION_CLEANUP_CANCEL},
        {"Start",  ACTION_CLEANUP_START}
      };
      uiScreen.footer(actions, 2);
    }
    return;
  }

  if (showingCleanupResult) {
    if (cleanupRemovedCount < 0) {
      drawCenteredLines({"Index not found."});
    } else if (cleanupRemovedCount == 0) {
      drawCenteredLines({"Nothing to clean up."});
    } else {
      char buf[64];
      sprintf(buf, "%d ghost entr%s removed.", cleanupRemovedCount, cleanupRemovedCount == 1 ? "y" : "ies");
      drawCenteredLines({buf});
    }
    
    // Touch footer for result state
    if (mappedInput.hasTouch()) {
      freeink::ui::FooterAction actions[] = { {"Done", ACTION_CLEANUP_DONE} };
      uiScreen.footer(actions, 1);
    }
    return;
  }

  static_assert(sizeof(kRowLabels) / sizeof(kRowLabels[0]) == ROW_COUNT,
                "kRowLabels must have exactly ROW_COUNT entries");

  for (int i = 0; i < ROW_COUNT; i++) {
    rowItems[i] = fui::ListItem{};  // reset state left over from the previous pass
    rowItems[i].label = kRowLabels[i];
    rowItems[i].actionValue = i;
    rowValues[i].clear();           // clear() keeps capacity, so no reallocation
    if (i == 0) rowValues[i] = formatFolderPill();
    if (i == 1) rowValues[i] = formatExclusionsPill();
    if (i == 2) rowValues[i] = std::to_string(batchSize);
    if (i == 3) rowValues[i] = autoIndexOnOpen ? "ON" : "OFF";
    if (i == 4) rowValues[i] = hideFinished ? "ON" : "OFF";
    if (i == 5) rowValues[i] = (filterMode == FilterMode::FOLDER_TREE) ? "Folder Tree" : "Automatic";
    if (i == 7) rowValues[i] = swapNavButtons ? "Scroll List" : "Open Panels";
    if (!rowValues[i].empty()) rowItems[i].value = rowValues[i].c_str();
    if (i == 6 && filterMode != FilterMode::AUTOMATIC) rowItems[i].enabled = false;
  }

  // Capture body BEFORE list() consumes it
  fui::Rect body = uiScreen.body();

  const int rowH = mappedInput.hasTouch()
      ? static_cast<int>(uiScreen.theme().rowHeight)
      : UITheme::getInstance().getMetrics().listRowHeight;
  const int rowGap = UITheme::getInstance().getMetrics().listRowGap;
  const int stride = rowH + rowGap;

  fui::ListProps props;
  syncListViewport(uiScreen, props, false);
  props.labelText = uiScreen.theme().bodyText;
  props.valueText = uiScreen.theme().smallText;
  props.items = rowItems;
  props.count = ROW_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  uiScreen.list(props);

  // Dither mask over disabled "Merge Similar Tags" row (index 6)
  if (filterMode != FilterMode::AUTOMATIC && 6 >= nav.top) {
    const int visualRow = 6 - nav.top;
    const int pyStart   = body.y + visualRow * stride;
    if (pyStart < body.y + body.height) {
      const int pyEnd = std::min(pyStart + rowH, body.y + body.height);
      for (int py = pyStart; py < pyEnd; py++)
        for (int px = body.x; px < body.x + body.width; px++)
          if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
    }
  }
}

void Ao3LibrarySettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}