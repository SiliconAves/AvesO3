#include "Ao3CoverAssignmentActivity.h"
#include <HalStorage.h>
#include <algorithm>
#include <cstring>
#include <I18n.h>
#include "Ao3CompactIndexRecord.h"
#include "Ao3FandomCoverStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"

#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
bool ciLess(const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; }
}

void Ao3CoverAssignmentActivity::onEnter() {
  fandomsNav.reset();
  filesNav.reset();
  UiListActivity::onEnter();
  
  // Set FONT_BODY to 10pt globally for this activity
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  
  screen = Screen::FANDOMS;
  loadFandoms();
  requestUpdate();
}

bool Ao3CoverAssignmentActivity::handleCustomInput() {
  // If the list is empty or the user taps outside a row, this fallback ensures
  // the Back/Confirm software button hints are still tappable.
  auto route = UiAppHost::routeTouch(mappedInput, false);
  return static_cast<bool>(route);
}

void Ao3CoverAssignmentActivity::onRowAction(const fui::ActionEvent& event) {
  // Native FUI touch router calls this when a row hitbox is tapped
  activeNav().selected = event.value;
  activateIndex(event.value);
}

void Ao3CoverAssignmentActivity::onExit() {
  UiListActivity::onExit();
  fandoms.clear(); fandoms.shrink_to_fit();
  files.clear();   files.shrink_to_fit();
}

fui::ListNav& Ao3CoverAssignmentActivity::activeNav() {
  return screen == Screen::FILES ? filesNav : fandomsNav;
}

int Ao3CoverAssignmentActivity::listCount() const {
  return screen == Screen::FILES ? files.size() : fandoms.size();
}

void Ao3CoverAssignmentActivity::loadFandoms() {
  fandoms.clear();
  HalFile f;
  if (!Storage.openFileForRead("COV", "/.crosspoint/ao3_library_index.bin", f)) return;
  char magic[4]; uint8_t ver; uint16_t n;
  if (f.read(magic, 4) != 4 || f.read(&ver, 1) != 1 || f.read((uint8_t*)&n, 2) != 2 ||
      memcmp(magic, "AO3X", 4) != 0) { f.close(); return; }
  f.seek(12);
  CompactIndexRecord rec;
  for (uint16_t i = 0; i < n; i++) {
    if (f.read((uint8_t*)&rec, sizeof(rec)) != sizeof(rec)) break;
    if ((rec.flags & 1) || !rec.fandom[0]) continue;
    if (std::find(fandoms.begin(), fandoms.end(), rec.fandom) == fandoms.end())
      fandoms.push_back(rec.fandom);
  }
  f.close();
  std::sort(fandoms.begin(), fandoms.end(), ciLess);
}

void Ao3CoverAssignmentActivity::loadFiles() {
  files.clear();
  files.push_back("None");
  Storage.mkdir("/.fandom_covers");
  HalFile dir = Storage.open("/.fandom_covers");
  if (dir && dir.isDirectory()) {
    dir.rewindDirectory();
    char name[128];
    HalFile e;
    while ((e = dir.openNextFile())) {
      e.getName(name, sizeof(name));
      const size_t len = strlen(name);
      if (!e.isDirectory() && name[0] != '.' && len > 4 && strcasecmp(name + len - 4, ".bmp") == 0)
        files.push_back(name);
      e.close();
    }
    dir.close();
  }
  std::sort(files.begin() + 1, files.end(), ciLess);
}

bool Ao3CoverAssignmentActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (screen == Screen::FILES) {
      screen = Screen::FANDOMS;
      requestUpdate(true);
      return true;
    }
    finish();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    int index = activeNav().selected;
    if (index >= 0 && index < listCount()) {
      activateIndex(index);
    }
    return true;
  }
  return false;
}

void Ao3CoverAssignmentActivity::navigateButtons() {
  const int total = listCount();
  if (total == 0) return;

  // Standard short press (Release) -> Move 1 line
  buttonNavigator.onNextRelease([this, total] {
    activeNav().selected = ButtonNavigator::nextIndex(activeNav().selected, total);
    activeNav().follow(total);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, total] {
    activeNav().selected = ButtonNavigator::previousIndex(activeNav().selected, total);
    activeNav().follow(total);
    requestUpdate();
  });

  // Long press (Continuous hold) -> Skip 2 lines
  buttonNavigator.onNextContinuous([this, total] {
    activeNav().selected = (activeNav().selected + 2) % total;
    activeNav().follow(total);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, total] {
    activeNav().selected = (activeNav().selected + total - 2) % total;
    activeNav().follow(total);
    requestUpdate();
  });
}

void Ao3CoverAssignmentActivity::activateIndex(int index) {
  auto& store = Ao3FandomCoverStore::getInstance();

  if (screen == Screen::FILES) {
    store.assign(fandoms[cachedFandomSelector].c_str(), index == 0 ? std::string() : files[index]);
    screen = Screen::FANDOMS;
    requestUpdate(true);
  } else {
    cachedFandomSelector = index;
    loadFiles();
    filesNav.reset(); // preselect the current assignment
    if (const auto* cur = store.find(fandoms[cachedFandomSelector].c_str())) {
      for (size_t i = 1; i < files.size(); i++) {
        if (files[i] == *cur) { filesNav.selected = static_cast<int>(i); break; }
      }
    }
    screen = Screen::FILES;
    requestUpdate(true);
  }
}

void Ao3CoverAssignmentActivity::drawChrome() {
  UiListActivity::drawChrome();
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  
  if (screen == Screen::FILES) {
    std::string title = "Cover: " + fandoms[cachedFandomSelector];
    if (title.length() > 30) title = title.substr(0, 28) + "..";
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());
  } else {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Customize Fandom Covers");
  }
}

void Ao3CoverAssignmentActivity::drawFooter() {
  const char* btn2 = (screen == Screen::FILES) ? tr(STR_SELECT) : "Assign";
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), btn2, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void Ao3CoverAssignmentActivity::buildScreen(UiScreen& uiScreen) {
  if (screen == Screen::FILES) {
    buildFilesScreen(uiScreen);
  } else {
    buildFandomsScreen(uiScreen);
  }
}

void Ao3CoverAssignmentActivity::buildFandomsScreen(UiScreen& uiScreen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  uiScreen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing),
      0,
      static_cast<int16_t>(metrics.buttonHintsHeight),
      0});

  if (fandoms.empty()) {
    uiScreen.centeredText("No fandoms indexed yet.", uiScreen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  syncListViewport(uiScreen, props, false);
  
  auto& store = Ao3FandomCoverStore::getInstance();
  std::vector<fui::ListItem> items(fandoms.size());
  
  for (size_t i = 0; i < fandoms.size(); i++) {
    items[i].label = fandoms[i].c_str();
    items[i].actionValue = i; // Assign value for FUI touch routing
    
    const auto* f = store.find(fandoms[i].c_str());
    if (f) {
      items[i].value = f->c_str(); 
    }
  }

  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;

  props.labelText = uiScreen.theme().bodyText;
  props.valueText = uiScreen.theme().smallText;
  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  uiScreen.list(props);
}

void Ao3CoverAssignmentActivity::buildFilesScreen(UiScreen& uiScreen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  uiScreen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing),
      0,
      static_cast<int16_t>(metrics.buttonHintsHeight),
      0});

  fui::ListProps props;
  syncListViewport(uiScreen, props, false);

  std::vector<fui::ListItem> items(files.size());
  for (size_t i = 0; i < files.size(); i++) {
    items[i].label = files[i].c_str();
    items[i].actionValue = i; // Assign value for FUI touch routing
  }

  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  
  props.labelText = uiScreen.theme().bodyText;
  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  uiScreen.list(props);

  if (files.size() == 1) {
    // 1. Advance the cursor dynamically based on the font and row gap to safely skip the "None" row
    uiScreen.spacer(uiScreen.target().lineHeight(uiScreen.theme().bodyText.font) * 2 + metrics.listRowGap + 30);
    
    // Calculate the exact side margin to perfectly match the list rows
    const int16_t textMargin = metrics.listInset + metrics.listSidePadding;
    
    // 2. Format the warning text and ensure it is horizontally centered
    fui::TextStyle boldText = uiScreen.theme().smallText;
    boldText.font = uiScaleSpec().bodyFontId; 
    boldText.align = fui::TextAlign::Center; 
    
    // Use the dynamic margin for the header
    fui::Rect noFilesBlock = uiScreen.takeTop(uiScreen.target().lineHeight(boldText.font) + 6);
    uiScreen.target().text(noFilesBlock.inset(fui::Insets{0, textMargin, 0, textMargin}), "No .bmp files found in /.fandom_covers.", boldText);
    
    uiScreen.spacer(20);
    
    // 3. Draw the specs 
    const char* specs[] = {
      "To add covers, create images with:",
      "- Size: 80 x 80 px (or smaller)",
      "- Format: 1-bit monochrome .bmp",
      "- Colors: Black logo on white background",
      "- Folder: place it in /.fandom_covers"
    };

    auto leftAlignStyle = uiScreen.theme().smallText;
    leftAlignStyle.align = fui::TextAlign::Left;
    
    // Use the dynamic margin for the list of specs
    for (const char* line : specs) {
      fui::Rect block = uiScreen.takeTop(uiScreen.target().lineHeight(leftAlignStyle.font) + 6);
      uiScreen.target().text(block.inset(fui::Insets{0, textMargin, 0, textMargin}), line, leftAlignStyle);
    }
  }
}