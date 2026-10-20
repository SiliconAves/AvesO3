#pragma once
#include <string>
#include <vector>
#include "activities/UiTabListActivity.h"
#include "util/ButtonNavigator.h"

class Ao3TagMergeActivity final : public UiTabListActivity {
public:
    explicit Ao3TagMergeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
        : UiTabListActivity("Ao3TagMerge", renderer, mappedInput) {}

    void onEnter() override;
    void onExit() override;

protected:
    // --- UiTabListActivity Overrides ---
    int tabCount() const override { return kTabCount; }
    int activeTab() const override { return selectedTabIndex; }
    const char* tabLabel(int index) const override { return index == 0 ? "Fandoms" : "Relationships"; }
    
    void onTabAction(int index) override;
    void stepTab(int direction) override;
    
    bool handleButtons() override;
    void navigateButtons() override;

    int listCount() const override;
    void buildScreen(UiScreen& screen) override;
    void activateIndex(int index) override;
    freeink::ui::ListNav& activeNav() override;

    // --- Chrome & Footer Overrides ---
    const char* headerTitle() const override;
    void drawFooter() override;

private:
    static constexpr int kTabCount = 2;
    int selectedTabIndex = 0; // 0 = Fandoms, 1 = Relationships

    enum class Screen { MAIN, SECONDARY };
    Screen screen = Screen::MAIN;

    // Custom secondary viewport tracker (MAIN screens use the inherited tabNavs array)
    freeink::ui::ListNav secondaryNav;
    std::string headerTitleStr;

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

    void buildMainScreen(UiScreen& screen);
    void buildSecondaryScreen(UiScreen& screen);
};