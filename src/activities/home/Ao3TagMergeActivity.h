#pragma once
#include <string>
#include <vector>
#include "../Activity.h"
#include "../../util/ButtonNavigator.h"

class Ao3TagMergeActivity final : public Activity {
public:
    explicit Ao3TagMergeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
        : Activity("Ao3TagMerge", renderer, mappedInput) {}

    void onEnter() override;
    void onExit() override;
    void loop() override;
    void render(RenderLock&&) override;

private:
    static constexpr int kTabCount = 2;

    // Tab + list selection — mirrors SettingsActivity pattern
    int selectedTabIndex     = 0; // 0 = Fandoms, 1 = Relationships
    int selectedSettingIndex = 0; // 0 = tab bar focused, 1..n = list item

    enum class Screen { MAIN, SECONDARY };
    Screen screen = Screen::MAIN;
    ButtonNavigator buttonNavigator;

    // Main list
    struct DisplayItem {
        std::string name;
        int subCount; // 0 = plain tag, >0 = master with N subs
    };
    std::vector<DisplayItem> displayItems;

    // Secondary (sub-picker) screen
    struct PickerItem {
        std::string name;
        bool selected;
    };
    std::vector<PickerItem> pickerItems;
    int pickerSelectorIndex = 0;
    std::string editingMaster;
    std::string relFandomFilter_; // empty = fandom list visible in rel tab

    // In-memory merge data
    struct MergeGroup {
        std::string master;
        std::vector<std::string> subs;
    };
    std::vector<MergeGroup> fandomGroups;
    std::vector<MergeGroup> relGroups;
    bool dirty = false;

    std::vector<MergeGroup>& activeGroups();

    void loadMergeData();
    void saveMergeData();
    void rebuildDisplayItems();
    void loadAllTagsForActiveTab(std::vector<std::string>& out) const;
    void openSecondaryPicker(const std::string& masterName);
    void commitSecondaryPicker();
    void loadFandomsForRelTab(std::vector<std::string>& out) const;
    void loadRelationshipsForFandom(const std::string& fandom, std::vector<std::string>& out) const;

    void renderMain();
    void renderSecondary();
};