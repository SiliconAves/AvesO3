#include "Ao3TagMergeActivity.h"
#include <HalStorage.h>
#include <ArduinoJson.h>
#include <Logging.h>
#include <I18n.h>
#include <algorithm>
#include <cstring>
#include "Ao3CompactIndexRecord.h"
#include "Ao3TagMergeStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

// ---------------------------------------------------------------------------
//  Helpers & Data Loading
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
    f.seek(12);

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

    // Build the header title once when we enter the screen
    headerTitleStr = "Add to: " + editingMaster;
    if (headerTitleStr.length() > 30) headerTitleStr = headerTitleStr.substr(0, 28) + "..";

    pickerItems.clear();
    secondaryNav.reset();

    const auto& groups = activeGroups();
    std::vector<std::string> currentSubs;
    for (const auto& g : groups)
        if (g.master == masterName) { currentSubs = g.subs; break; }

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

    // Clamp viewport to prevent ghost pointers if list shrank
    int listSize = static_cast<int>(displayItems.size());
    if (ringPos() > listSize) {
        moveRingTo(listSize);
    }
    requestUpdate(true);
}

// ---------------------------------------------------------------------------
//  Activity Lifecycle & Overrides
// ---------------------------------------------------------------------------

// Add this helper include at the top of the file if not present
#include "components/UiAppHelpers.h"

void Ao3TagMergeActivity::onEnter() {
    UiTabListActivity::onEnter();
    // Map FONT_BODY to 10pt for the list items globally
    uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
    
    selectedTabIndex = 0;
    secondaryNav.reset();
    screen = Screen::MAIN;
    dirty = false;
    loadMergeData();
    rebuildDisplayItems();
    requestUpdate();
}

// Implement touch routing for both screens
void Ao3TagMergeActivity::onRowAction(const fui::ActionEvent& event) {
    if (screen == Screen::SECONDARY) {
        // Secondary screen uses a standard list, no tab bar offset
        activeNav().selected = event.value;
        activateIndex(event.value);
    } else {
        // Main screen uses UiTabListActivity layout, where row 0 is the tab bar
        activeNav().selected = event.value + 1;
        activateIndex(event.value);
    }
}

void Ao3TagMergeActivity::onExit() {
    UiTabListActivity::onExit();
    if (dirty) saveMergeData();
    displayItems.clear(); displayItems.shrink_to_fit();
    pickerItems.clear();  pickerItems.shrink_to_fit();
    fandomGroups.clear(); relGroups.clear();
}

fui::ListNav& Ao3TagMergeActivity::activeNav() {
    return (screen == Screen::SECONDARY) ? secondaryNav : UiTabListActivity::activeNav();
}

int Ao3TagMergeActivity::listCount() const {
    return screen == Screen::SECONDARY ? static_cast<int>(pickerItems.size()) 
                                       : static_cast<int>(displayItems.size());
}

const char* Ao3TagMergeActivity::headerTitle() const {
    if (screen == Screen::SECONDARY) {
        return headerTitleStr.c_str();
    }
    return "Merge Similar Tags";
}

void Ao3TagMergeActivity::drawFooter() {
    if (screen == Screen::SECONDARY) {
        const auto labels = mappedInput.mapLabels("Save", "Toggle", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    } else {
        const char* confirmLabel = (ringPos() == 0) 
            ? (selectedTabIndex == 0 ? "Ships" : "Fandoms") 
            : "Edit";
        const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
}

// ---------------------------------------------------------------------------
//  Tab & Input Handling
// ---------------------------------------------------------------------------

void Ao3TagMergeActivity::onTabAction(int index) {
    selectedTabIndex = index;
    relFandomFilter_.clear();
    rebuildDisplayItems();
    moveRingTo(0);
    requestUpdate(true);
}

void Ao3TagMergeActivity::stepTab(int direction) {
    selectedTabIndex = direction > 0 ? ButtonNavigator::nextIndex(selectedTabIndex, kTabCount)
                                     : ButtonNavigator::previousIndex(selectedTabIndex, kTabCount);
    relFandomFilter_.clear();
    rebuildDisplayItems();
    moveRingTo(0);
    requestUpdate(true);
}

bool Ao3TagMergeActivity::handleButtons() {
    if (screen == Screen::SECONDARY) {
        if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
            commitSecondaryPicker();
            return true;
        }
        if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && !pickerItems.empty()) {
            activateIndex(secondaryNav.selected); // <-- Routes through the safe function now
            return true;
        }
        return false;
    }

    // MAIN Screen
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        if (ringPos() == 0) {
            stepTab(1); // Cycle tab
        } else {
            activateIndex(ringPos() - 1); // Drill down
        }
        return true;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        if (selectedTabIndex == 1 && !relFandomFilter_.empty()) {
            relFandomFilter_.clear();
            rebuildDisplayItems();
            moveRingTo(1);
            requestUpdate(true);
        } else if (ringPos() > 0) {
            moveRingTo(0);
            requestUpdate(true);
        } else {
            if (dirty) saveMergeData();
            finish();
        }
        return true;
    }

    return false;
}

void Ao3TagMergeActivity::navigateButtons() {
    if (screen == Screen::SECONDARY) {
        const int total = static_cast<int>(pickerItems.size());
        if (total == 0) return;

        buttonNavigator.onNextRelease([this, total] {
            secondaryNav.selected = ButtonNavigator::nextIndex(secondaryNav.selected, total);
            secondaryNav.follow(total);
            requestUpdate();
        });
        buttonNavigator.onPreviousRelease([this, total] {
            secondaryNav.selected = ButtonNavigator::previousIndex(secondaryNav.selected, total);
            secondaryNav.follow(total);
            requestUpdate();
        });
        buttonNavigator.onNextContinuous([this, total] {
            secondaryNav.selected = (secondaryNav.selected + 2) % total;
            secondaryNav.follow(total);
            requestUpdate();
        });
        buttonNavigator.onPreviousContinuous([this, total] {
            secondaryNav.selected = (secondaryNav.selected + total - 2) % total;
            secondaryNav.follow(total);
            requestUpdate();
        });
    } else {
        const int listSize = static_cast<int>(displayItems.size());
        
        buttonNavigator.onNextRelease([this, listSize] {
            moveRingTo(ButtonNavigator::nextIndex(ringPos(), listSize + 1));
            requestUpdate(true);
        });
        buttonNavigator.onPreviousRelease([this, listSize] {
            moveRingTo(ButtonNavigator::previousIndex(ringPos(), listSize + 1));
            requestUpdate(true);
        });
        // Side buttons continuously held: cycle tabs
        buttonNavigator.onSideNextContinuous([this] {
            stepTab(1);
        });
        buttonNavigator.onSidePreviousContinuous([this] {
            stepTab(-1);
        });
        // Front buttons continuously held: skip 2 rows
        buttonNavigator.onFrontNextContinuous([this, listSize] {
            int current = ringPos();
            if (current == 0) moveRingTo(std::min(2, listSize));
            else moveRingTo(std::min(current + 2, listSize));
            requestUpdate(true);
        });
        buttonNavigator.onFrontPreviousContinuous([this, listSize] {
            int current = ringPos();
            if (current == 0) moveRingTo(listSize);
            else moveRingTo(std::max(current - 2, 1));
            requestUpdate(true);
        });
    }
}

void Ao3TagMergeActivity::activateIndex(int index) {
    // Safely intercept touches on the secondary picker screen
    if (screen == Screen::SECONDARY) {
        if (index >= 0 && index < static_cast<int>(pickerItems.size())) {
            pickerItems[index].selected = !pickerItems[index].selected;
            requestUpdate(true);
        }
        return;
    }

    // Main screen routing
    if (index >= 0 && index < static_cast<int>(displayItems.size())) {
        if (selectedTabIndex == 1 && relFandomFilter_.empty()) {
            relFandomFilter_ = displayItems[index].name;
            rebuildDisplayItems();
            moveRingTo(1);
            requestUpdate(true);
        } else {
            openSecondaryPicker(displayItems[index].name);
        }
    }
}

// ---------------------------------------------------------------------------
//  Screen Builders
// ---------------------------------------------------------------------------

void Ao3TagMergeActivity::buildScreen(UiScreen& uiScreen) {
    if (screen == Screen::SECONDARY) {
        buildSecondaryScreen(uiScreen);
    } else {
        buildMainScreen(uiScreen);
    }
}

void Ao3TagMergeActivity::buildMainScreen(UiScreen& uiScreen) {
    // 1. Force the FUI Small Font slot to 10pt so the Tab Bar renders at the correct size
    uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_10_FONT_ID);

    const auto& metrics = UITheme::getInstance().getMetrics();
    uiScreen.setContentMarginFromScreen(fui::Insets{
        static_cast<int16_t>(metrics.topPadding + metrics.headerHeight),
        0,
        static_cast<int16_t>(metrics.buttonHintsHeight),
        0});
        
    buildTabBar(uiScreen);

    if (displayItems.empty()) {
        uiScreen.spacer(20);
        uiScreen.centeredText("No tags indexed yet.", uiScreen.theme().bodyText);
        return;
    }

    uiScreen.insetContent(fui::Insets{-6, 0, 0, 0});

    fui::ListProps props;
    syncTabListViewport(uiScreen, props, false);

    int count = static_cast<int>(displayItems.size());
    std::vector<fui::ListItem> items(count);
    std::vector<std::string> values(count);

    for (int i = 0; i < count; i++) {
        items[i].label = displayItems[i].name.c_str();
        items[i].actionValue = i; // Ensure FUI knows the row index for touches
        if (displayItems[i].subCount > 0) {
            values[i] = std::to_string(displayItems[i].subCount);
            items[i].value = values[i].c_str(); 
        }
    }

    // Bind FUI touch interactions natively
    props.action = ACTION_ROW;
    props.inputMask = fui::InputTouch;

    props.labelText = uiScreen.theme().bodyText;
    props.valueText = uiScreen.theme().smallText;
    props.items = items.data();
    props.count = count;

    // 2. Switch the FUI Small Font slot back to 8pt right before drawing the list values
    uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);

    uiScreen.list(props);
}

void Ao3TagMergeActivity::buildSecondaryScreen(UiScreen& uiScreen) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    
    // Secondary screen has no tabs, add verticalSpacing to clear the header cleanly
    uiScreen.setContentMarginFromScreen(fui::Insets{
        static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing),
        0,
        static_cast<int16_t>(metrics.buttonHintsHeight),
        0});

    if (pickerItems.empty()) {
        uiScreen.spacer(20);
        uiScreen.centeredText("No available tags.", uiScreen.theme().bodyText);
        return;
    }

    fui::ListProps props;
    syncListViewport(uiScreen, props, false);

    int count = static_cast<int>(pickerItems.size());
    std::vector<fui::ListItem> items(count);

    for (int i = 0; i < count; i++) {
        items[i].label = pickerItems[i].name.c_str();
        items[i].actionValue = i; // Ensure FUI knows the row index for touches
        if (pickerItems[i].selected) {
            items[i].value = "•";
        }
    }

    // Bind FUI touch interactions natively
    props.action = ACTION_ROW;
    props.inputMask = fui::InputTouch;

    props.labelText = uiScreen.theme().bodyText;
    props.valueText = uiScreen.theme().smallText;
    props.items = items.data();
    props.count = count;
    
    // Ensure value bullet point uses the 8pt small font
    uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);

    uiScreen.list(props);
}