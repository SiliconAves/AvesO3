#include "Ao3CoverAssignmentActivity.h"
#include <HalStorage.h>
#include <algorithm>
#include <cstring>
#include <I18n.h>
#include "../../Ao3CompactIndexRecord.h"
#include "../../Ao3FandomCoverStore.h"
#include "../../components/UITheme.h"
#include "../../fontIds.h"

namespace {
bool ciLess(const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; }
}

void Ao3CoverAssignmentActivity::onEnter() {
  Activity::onEnter();
  screen = Screen::FANDOMS;
  selector = 0;
  loadFandoms();
  requestUpdate();
}

void Ao3CoverAssignmentActivity::onExit() {
  Activity::onExit();
  fandoms.clear(); fandoms.shrink_to_fit();
  files.clear();   files.shrink_to_fit();
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
    while (e = dir.openNextFile()) {
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

void Ao3CoverAssignmentActivity::loop() {
  auto& store = Ao3FandomCoverStore::getInstance();

  if (screen == Screen::FILES) {
    const int total = static_cast<int>(files.size());
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      screen = Screen::FANDOMS; requestUpdate(true); return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      store.assign(fandoms[selector].c_str(), fileSelector == 0 ? std::string() : files[fileSelector]);
      screen = Screen::FANDOMS; requestUpdate(true); return;
    }
    buttonNavigator.onNextRelease([this, total] {
      fileSelector = ButtonNavigator::nextIndex(fileSelector, total); requestUpdate(); });
    buttonNavigator.onPreviousRelease([this, total] {
      fileSelector = ButtonNavigator::previousIndex(fileSelector, total); requestUpdate(); });
    buttonNavigator.onNextContinuous([this, total] {
      fileSelector = (fileSelector + 2) % total; requestUpdate(); });
    buttonNavigator.onPreviousContinuous([this, total] {
      fileSelector = (fileSelector + total - 2) % total; requestUpdate(); });
    return;
  }

  const int total = static_cast<int>(fandoms.size());
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) { finish(); return; }

  if (total > 0 && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    loadFiles();
    fileSelector = 0;                                   // preselect the current assignment
    if (const auto* cur = store.find(fandoms[selector].c_str())) {
      for (size_t i = 1; i < files.size(); i++)
        if (files[i] == *cur) { fileSelector = static_cast<int>(i); break; }
    }
    screen = Screen::FILES; requestUpdate(true); return;
  }

  buttonNavigator.onNextRelease([this, total] {
    selector = ButtonNavigator::nextIndex(selector, total); requestUpdate(); });
  buttonNavigator.onPreviousRelease([this, total] {
    selector = ButtonNavigator::previousIndex(selector, total); requestUpdate(); });
  buttonNavigator.onNextContinuous([this, total] {
    selector = (selector + 2) % total; requestUpdate(); });
  buttonNavigator.onPreviousContinuous([this, total] {
    selector = (selector + total - 2) % total; requestUpdate(); });
}

void Ao3CoverAssignmentActivity::render(RenderLock&&) {
  if (screen == Screen::FILES) renderFiles(); else renderFandoms();
}

void Ao3CoverAssignmentActivity::renderFandoms() {
  renderer.clearScreen();
  const auto w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  const auto& m = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, "Customize Fandom Covers");

  const int top = m.topPadding + m.headerHeight + m.verticalSpacing;
  const int ht  = h - top - m.buttonHintsHeight - m.verticalSpacing;

  if (fandoms.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2, "No fandoms indexed yet.");
  } else {
    auto& store = Ao3FandomCoverStore::getInstance();
    GUI.drawList(renderer, Rect{0, top, w, ht}, static_cast<int>(fandoms.size()), selector,
                 [this](int i) { return fandoms[i]; }, nullptr, nullptr,
                 [this, &store](int i) -> std::string {
                   const auto* f = store.find(fandoms[i].c_str());
                   return f ? *f : std::string();
                 }, true);
  }
  const auto l = mappedInput.mapLabels(tr(STR_BACK), "Assign", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, l.btn1, l.btn2, l.btn3, l.btn4);
  renderer.displayBuffer();
}

void Ao3CoverAssignmentActivity::renderFiles() {
  renderer.clearScreen();
  const auto w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  const auto& m = UITheme::getInstance().getMetrics();

  std::string title = "Cover: " + fandoms[selector];
  if (title.length() > 30) title = title.substr(0, 28) + "..";
  GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, title.c_str());

  const int top = m.topPadding + m.headerHeight + m.verticalSpacing;
  const int ht  = h - top - m.buttonHintsHeight - m.verticalSpacing;

  auto& store = Ao3FandomCoverStore::getInstance();
  const std::string* cur = store.find(fandoms[selector].c_str());
  GUI.drawList(renderer, Rect{0, top, w, ht}, static_cast<int>(files.size()), fileSelector,
             [this](int i) { return files[i]; }, nullptr, nullptr, nullptr, false);

  if (files.size() == 1) {
    const int startY = top + m.listRowHeight + 24;
    renderer.drawCenteredText(UI_10_FONT_ID, startY, "No .bmp files found in /.fandom_covers", true, EpdFontFamily::BOLD);

    const int subY = startY + renderer.getLineHeight(SMALL_FONT_ID) + 10;
    renderer.drawCenteredText(SMALL_FONT_ID, subY, "To add covers, create images with:");

    const char* specs[] = {
      "- Size: 80 x 80 px (or smaller)",
      "- Format: 1-bit monochrome .bmp",
      "- Colors: Black logo on white background",
      "- Folder: place it in /.fandom_covers"
    };

    int maxTextWidth = 0;
    for (const char* line : specs) {
      maxTextWidth = std::max(maxTextWidth, renderer.getTextWidth(SMALL_FONT_ID, line));
    }
    const int leftX = std::max(m.contentSidePadding, (w - maxTextWidth) / 2);
    const int itemLh = renderer.getLineHeight(SMALL_FONT_ID) + 6;
    int curY = subY + renderer.getLineHeight(SMALL_FONT_ID) + 10;
    for (const char* line : specs) {
      renderer.drawText(SMALL_FONT_ID, leftX, curY, line);
      curY += itemLh;
    }
  }
  const auto l = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, l.btn1, l.btn2, l.btn3, l.btn4);
  renderer.displayBuffer();
}