#include "Ao3TagMergeActivity.h"
#include <HalStorage.h>
#include <ArduinoJson.h>
#include <Logging.h>
#include <I18n.h>
#include <algorithm>
#include <cstring>
#include "../../Ao3CompactIndexRecord.h"
#include "../../Ao3TagMergeStore.h"
#include "../../components/UITheme.h"
#include "../../fontIds.h"

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

std::vector<Ao3TagMergeActivity::MergeGroup>& Ao3TagMergeActivity::activeGroups() {
    return (selectedTabIndex == 0) ? fandomGroups : relGroups;
}

void Ao3TagMergeActivity::loadMergeData() {
    fandomGroups.clear();
    relGroups.clear();
    if (!Storage.exists(Ao3TagMergeStore::kPath)) return;
    String json = Storage.readFile(Ao3TagMergeStore::kPath);
    if (json.isEmpty()) return;
    JsonDocument doc;
    if (deserializeJson(doc, json)) return;

    auto parseGroups = [](JsonArray arr, std::vector<MergeGroup>& out) {
        for (JsonObject g : arr) {
            const char* master = g["master"] | "";
            if (!master[0]) continue;
            MergeGroup mg;
            mg.master = master;
            for (const char* sub : g["subs"].as<JsonArray>()) {
                if (sub && sub[0]) mg.subs.push_back(sub);
            }
            out.push_back(std::move(mg));
        }
    };
    parseGroups(doc["fandoms"].as<JsonArray>(), fandomGroups);
    parseGroups(doc["relationships"].as<JsonArray>(), relGroups);
}

void Ao3TagMergeActivity::saveMergeData() {
    JsonDocument doc;
    auto writeGroups = [](JsonArray arr, const std::vector<MergeGroup>& groups) {
        for (const auto& g : groups) {
            if (g.subs.empty()) continue; // dissolved groups are not written
            JsonObject obj = arr.add<JsonObject>();
            obj["master"] = g.master;
            JsonArray subs = obj["subs"].to<JsonArray>();
            for (const auto& s : g.subs) subs.add(s);
        }
    };
    writeGroups(doc["fandoms"].to<JsonArray>(), fandomGroups);
    writeGroups(doc["relationships"].to<JsonArray>(), relGroups);

    String json;
    serializeJson(doc, json);
    Storage.writeFile(Ao3TagMergeStore::kPath, json);
    Ao3TagMergeStore::load(); // refresh store immediately so library picks up changes
    dirty = false;
}

void Ao3TagMergeActivity::loadAllTagsForActiveTab(std::vector<std::string>& out) const {
    const char* indexPath = "/.crosspoint/ao3_library_index.bin";
    HalFile f;
    if (!Storage.openFileForRead("TMG", indexPath, f)) return;

    char magic[4]; uint8_t version; uint16_t recordCount;
    if (f.read(magic, 4) != 4 || f.read(&version, 1) != 1 ||
        f.read((uint8_t*)&recordCount, 2) != 2 ||
        memcmp(magic, "AO3X", 4) != 0) {
        f.close(); return;
    }
    f.seek(12); // skip remaining header

    CompactIndexRecord rec;
    for (uint16_t i = 0; i < recordCount; i++) {
        if (f.read((uint8_t*)&rec, sizeof(rec)) != sizeof(rec)) break;
        if (rec.flags & 0x01) continue;

        const char* tags[2] = { nullptr, nullptr };
        int tagCount = 0;
        if (selectedTabIndex == 0) {
            if (rec.fandom[0]) tags[tagCount++] = rec.fandom;
        } else {
            if (rec.relationship1[0]) tags[tagCount++] = rec.relationship1;
            if (rec.relationship2[0]) tags[tagCount++] = rec.relationship2;
        }

        for (int t = 0; t < tagCount; t++) {
            bool found = false;
            for (const auto& s : out) if (s == tags[t]) { found = true; break; }
            if (!found) out.push_back(tags[t]);
        }
    }
    f.close();
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return strcasecmp(a.c_str(), b.c_str()) < 0;
    });
}

void Ao3TagMergeActivity::loadFandomsForRelTab(std::vector<std::string>& out) const {
    const char* indexPath = "/.crosspoint/ao3_library_index.bin";
    HalFile f;
    if (!Storage.openFileForRead("TMG", indexPath, f)) return;

    char magic[4]; uint8_t version; uint16_t recordCount;
    if (f.read(magic, 4) != 4 || f.read(&version, 1) != 1 ||
        f.read((uint8_t*)&recordCount, 2) != 2 ||
        memcmp(magic, "AO3X", 4) != 0) {
        f.close(); return;
    }
    f.seek(12);

    // Resolve a raw fandom name to its master using fandomGroups
    auto resolveName = [this](const char* name) -> std::string {
        for (const auto& g : fandomGroups)
            for (const auto& s : g.subs)
                if (s == name) return g.master;
        return name;
    };

    CompactIndexRecord rec;
    for (uint16_t i = 0; i < recordCount; i++) {
        if (f.read((uint8_t*)&rec, sizeof(rec)) != sizeof(rec)) break;
        if (rec.flags & 0x01) continue;
        if (rec.fandom[0] == '\0') continue;
        // Only include fandoms that have at least one relationship tagged
        if (rec.relationship1[0] == '\0' && rec.relationship2[0] == '\0') continue;
        std::string resolved = resolveName(rec.fandom);
        bool found = false;
        for (const auto& s : out) if (s == resolved) { found = true; break; }
        if (!found) out.push_back(resolved);
    }
    f.close();
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return strcasecmp(a.c_str(), b.c_str()) < 0;
    });
}

void Ao3TagMergeActivity::loadRelationshipsForFandom(const std::string& fandom,
                                                      std::vector<std::string>& out) const {
    // Build the set of raw fandom names to match: master + all its subs
    std::vector<std::string> matchNames = {fandom};
    for (const auto& g : fandomGroups) {
        if (g.master == fandom) {
            for (const auto& s : g.subs) matchNames.push_back(s);
            break;
        }
    }

    const char* indexPath = "/.crosspoint/ao3_library_index.bin";
    HalFile f;
    if (!Storage.openFileForRead("TMG", indexPath, f)) return;

    char magic[4]; uint8_t version; uint16_t recordCount;
    if (f.read(magic, 4) != 4 || f.read(&version, 1) != 1 ||
        f.read((uint8_t*)&recordCount, 2) != 2 ||
        memcmp(magic, "AO3X", 4) != 0) {
        f.close(); return;
    }
    f.seek(12);

    auto matches = [&matchNames](const char* name) -> bool {
        for (const auto& m : matchNames) if (m == name) return true;
        return false;
    };

    CompactIndexRecord rec;
    for (uint16_t i = 0; i < recordCount; i++) {
        if (f.read((uint8_t*)&rec, sizeof(rec)) != sizeof(rec)) break;
        if (rec.flags & 0x01) continue;
        if (!matches(rec.fandom)) continue;
        for (const char* rel : {rec.relationship1, rec.relationship2}) {
            if (!rel[0]) continue;
            bool found = false;
            for (const auto& s : out) if (s == rel) { found = true; break; }
            if (!found) out.push_back(rel);
        }
    }
    f.close();
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return strcasecmp(a.c_str(), b.c_str()) < 0;
    });
}

void Ao3TagMergeActivity::rebuildDisplayItems() {
    displayItems.clear();

    // Relationship tab with no fandom selected: show fandom navigation list
    if (selectedTabIndex == 1 && relFandomFilter_.empty()) {
        std::vector<std::string> fandoms;
        loadFandomsForRelTab(fandoms);
        for (const auto& f : fandoms) displayItems.push_back({f, 0});
        return;
    }

    const auto& groups = activeGroups();

    std::vector<const std::string*> allSubPtrs;
    for (const auto& g : groups)
        for (const auto& s : g.subs) allSubPtrs.push_back(&s);

    auto isSub = [&](const std::string& name) -> bool {
        for (auto* p : allSubPtrs) if (*p == name) return true;
        return false;
    };
    auto findGroup = [&](const std::string& name) -> const MergeGroup* {
        for (const auto& g : groups) if (g.master == name) return &g;
        return nullptr;
    };

    std::vector<std::string> allTags;
    if (selectedTabIndex == 1 && !relFandomFilter_.empty()) {
        loadRelationshipsForFandom(relFandomFilter_, allTags);
    } else {
        loadAllTagsForActiveTab(allTags);
    }

    for (const auto& tag : allTags) {
        if (isSub(tag)) continue;
        const MergeGroup* g = findGroup(tag);
        displayItems.push_back({tag, g ? (int)g->subs.size() : 0});
    }
}

void Ao3TagMergeActivity::openSecondaryPicker(const std::string& masterName) {
    editingMaster = masterName;
    pickerItems.clear();
    pickerSelectorIndex = 0;

    const auto& groups = activeGroups();

    // Current subs of this master (pre-select them)
    std::vector<std::string> currentSubs;
    for (const auto& g : groups)
        if (g.master == masterName) { currentSubs = g.subs; break; }

    // Tags that belong to OTHER groups (excluded from picker)
    std::vector<std::string> otherMasters, otherSubs;
    for (const auto& g : groups) {
        if (g.master == masterName) continue;
        otherMasters.push_back(g.master);
        for (const auto& s : g.subs) otherSubs.push_back(s);
    }

    auto isExcluded = [&](const std::string& name) -> bool {
        if (name == masterName) return true;
        for (const auto& m : otherMasters) if (m == name) return true;
        for (const auto& s : otherSubs)   if (s == name) return true;
        return false;
    };
    auto isCurrentSub = [&](const std::string& name) -> bool {
        for (const auto& s : currentSubs) if (s == name) return true;
        return false;
    };

    std::vector<std::string> allTags;
    if (selectedTabIndex == 1 && !relFandomFilter_.empty()) {
         loadRelationshipsForFandom(relFandomFilter_, allTags);
    } else {
     loadAllTagsForActiveTab(allTags);
    }
    for (const auto& tag : allTags) {
        if (isExcluded(tag)) continue;
        pickerItems.push_back({tag, isCurrentSub(tag)});
    }

    screen = Screen::SECONDARY;
    requestUpdate(true);
}

void Ao3TagMergeActivity::commitSecondaryPicker() {
    std::vector<std::string> selected;
    for (const auto& item : pickerItems)
        if (item.selected) selected.push_back(item.name);

    auto& groups = activeGroups();
    auto it = std::find_if(groups.begin(), groups.end(),
        [this](const MergeGroup& g) { return g.master == editingMaster; });

    if (selected.empty()) {
        if (it != groups.end()) groups.erase(it); // dissolve group
    } else {
        if (it != groups.end()) it->subs = selected;
        else groups.push_back({editingMaster, selected});
    }

    dirty = true;
    screen = Screen::MAIN;
    rebuildDisplayItems();

    // Clamp the cursor so it doesn't point to a ghost entry if the list shrank
    const int maxIndex = static_cast<int>(displayItems.size());
    if (selectedSettingIndex > maxIndex) {
        selectedSettingIndex = maxIndex;
    }

    requestUpdate(true);
}

// ---------------------------------------------------------------------------
//  Activity lifecycle
// ---------------------------------------------------------------------------

void Ao3TagMergeActivity::onEnter() {
    Activity::onEnter();
    selectedTabIndex     = 0;
    selectedSettingIndex = 0;
    screen = Screen::MAIN;
    dirty  = false;
    loadMergeData();
    rebuildDisplayItems();
    requestUpdate();
}

void Ao3TagMergeActivity::onExit() {
    Activity::onExit();
    if (dirty) saveMergeData();
    displayItems.clear(); displayItems.shrink_to_fit();
    pickerItems.clear();  pickerItems.shrink_to_fit();
    fandomGroups.clear(); relGroups.clear();
}

// ---------------------------------------------------------------------------
//  loop
// ---------------------------------------------------------------------------

void Ao3TagMergeActivity::loop() {
    // --- SECONDARY SCREEN ---
    if (screen == Screen::SECONDARY) {
        const int total = static_cast<int>(pickerItems.size());

        if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
            commitSecondaryPicker();
            return;
        }
        if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && total > 0) {
            pickerItems[pickerSelectorIndex].selected = !pickerItems[pickerSelectorIndex].selected;
            requestUpdate(true);
            return;
        }
        if (total > 0) {
            buttonNavigator.onNextRelease([this, total] {
                pickerSelectorIndex = (pickerSelectorIndex + 1) % total;
                requestUpdate();
            });
            buttonNavigator.onPreviousRelease([this, total] {
                pickerSelectorIndex = (pickerSelectorIndex + total - 1) % total;
                requestUpdate();
            });
            buttonNavigator.onNextContinuous([this, total] {
                pickerSelectorIndex = (pickerSelectorIndex + 2) % total;
                requestUpdate();
            });
            buttonNavigator.onPreviousContinuous([this, total] {
                pickerSelectorIndex = (pickerSelectorIndex + total - 2) % total;
                requestUpdate();
            });
        }
        return;
    }
    

    // --- MAIN SCREEN ---
    bool tabChanged = false;

    // Confirm: cycle tab (when on tab bar) or open secondary picker (when in list)
    // Uses wasPressed to match SettingsActivity pattern
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        if (selectedSettingIndex == 0) {
            selectedTabIndex = (selectedTabIndex + 1) % kTabCount;
            tabChanged = true;
            requestUpdate();
            // fall through — no early return, matches SettingsActivity
        } else {
            const int idx = selectedSettingIndex - 1;
            if (idx >= 0 && idx < static_cast<int>(displayItems.size())) {
                if (selectedTabIndex == 1 && relFandomFilter_.empty()) {
                    // Fandom selected in relationship tab — drill into its relationships
                    relFandomFilter_ = displayItems[idx].name;
                    selectedSettingIndex = 1;
                    rebuildDisplayItems();
                    requestUpdate(true);
                } else {
                    openSecondaryPicker(displayItems[idx].name);
                }
            }
            return;
        }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        if (selectedTabIndex == 1 && !relFandomFilter_.empty()) {
         // Return to fandom list from relationship list
            relFandomFilter_.clear();
         selectedSettingIndex = 1;
         rebuildDisplayItems();
         requestUpdate(true);
        } else if (selectedSettingIndex > 0) {
          selectedSettingIndex = 0;
         requestUpdate();
        } else {
          if (dirty) saveMergeData();
         finish();
        }
        return;
    }

    const int listSize = static_cast<int>(displayItems.size());

    buttonNavigator.onNextRelease([this, listSize] {
        selectedSettingIndex = ButtonNavigator::nextIndex(selectedSettingIndex, listSize + 1);
        requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this, listSize] {
        selectedSettingIndex = ButtonNavigator::previousIndex(selectedSettingIndex, listSize + 1);
        requestUpdate();
    });
    // Side buttons held: cycle tabs
    buttonNavigator.onSideNextContinuous([this, &tabChanged] {
        selectedTabIndex = ButtonNavigator::nextIndex(selectedTabIndex, kTabCount);
        tabChanged = true;
        requestUpdate();
    });
    buttonNavigator.onSidePreviousContinuous([this, &tabChanged] {
        selectedTabIndex = ButtonNavigator::previousIndex(selectedTabIndex, kTabCount);
        tabChanged = true;
        requestUpdate();
    });
    // Front buttons held: skip 2 rows
    buttonNavigator.onFrontNextContinuous([this, listSize] {
        if (selectedSettingIndex == 0) selectedSettingIndex = 2;
        else selectedSettingIndex = std::min(selectedSettingIndex + 2, listSize);
        requestUpdate();
    });
    buttonNavigator.onFrontPreviousContinuous([this, listSize] {
        if (selectedSettingIndex == 0) selectedSettingIndex = listSize;
        else selectedSettingIndex = std::max(selectedSettingIndex - 2, 1);
        requestUpdate();
    });

    if (tabChanged) {
        selectedSettingIndex = (selectedSettingIndex == 0) ? 0 : 1;
        relFandomFilter_.clear();
        rebuildDisplayItems();
        }
    }

// ---------------------------------------------------------------------------
//  render
// ---------------------------------------------------------------------------

void Ao3TagMergeActivity::render(RenderLock&&) {
    if (screen == Screen::SECONDARY) { renderSecondary(); return; }
    renderMain();
}

void Ao3TagMergeActivity::renderMain() {
    renderer.clearScreen();
    const auto pageWidth  = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();
    const auto& metrics   = UITheme::getInstance().getMetrics();

    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   "Merge Similar Tags");

    std::vector<TabInfo> tabs = {
        {"Fandoms",       selectedTabIndex == 0},
        {"Relationships", selectedTabIndex == 1}
    };
    GUI.drawTabBar(renderer,
                   Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                   tabs, selectedSettingIndex == 0);

    const int listTop = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
    const int listH   = pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int listCount = static_cast<int>(displayItems.size());
    
    if (listCount == 0) {
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "No tags indexed yet.");
    } else {
        GUI.drawList(renderer, Rect{0, listTop, pageWidth, listH},
                     listCount, selectedSettingIndex - 1,
                     [this](int i) { return displayItems[i].name; },
                     nullptr, nullptr,
                     [this](int i) -> std::string {
                         return displayItems[i].subCount > 0
                             ? std::to_string(displayItems[i].subCount)
                             : "";
                     }, false);
    }

    // Confirm label cycles to the other tab name when on tab bar
    const char* confirmLabel = (selectedSettingIndex == 0)
        ? (selectedTabIndex == 0 ? "Ships" : "Fandoms")
        : "Edit";
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

    renderer.displayBuffer();
}

void Ao3TagMergeActivity::renderSecondary() {
    renderer.clearScreen();
    const auto pageWidth  = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();
    const auto& metrics   = UITheme::getInstance().getMetrics();

    // Header shows which tag is the master
    std::string headerTitle = "Add to: " + editingMaster;
    if (headerTitle.length() > 30) headerTitle = headerTitle.substr(0, 28) + "..";
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   headerTitle.c_str());

    const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int listH   = pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int total   = static_cast<int>(pickerItems.size());

    if (total == 0) {
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "No available tags.");
    } else {
        GUI.drawList(renderer, Rect{0, listTop, pageWidth, listH},
                     total, pickerSelectorIndex,
                     [this](int i) { return pickerItems[i].name; },
                     nullptr, nullptr,
                     [this](int i) -> std::string { return pickerItems[i].selected ? "•" : ""; },
                     false);
    }

    const auto labels = mappedInput.mapLabels("Save", "Toggle", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

    renderer.displayBuffer();
}