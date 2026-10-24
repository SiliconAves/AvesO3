#pragma once
#include <string>
#include "activities/Activity.h"
#include "components/UiAppHost.h"

class Ao3PageQrActivity final : public Activity, private UiAppHost {
public:
    explicit Ao3PageQrActivity(GfxRenderer& renderer,
                               MappedInputManager& mappedInput,
                               std::string filePath)
        : Activity("Ao3PageQr", renderer, mappedInput),
          UiAppHost(renderer),
          filePath(std::move(filePath)) {}

    void onEnter() override;
    void loop()    override;
    void render(RenderLock&&) override;

private:
    std::string filePath;

    // Loaded from ao3_library_info sidecar
    char     title[128]   = {};
    char     author[128]  = {};
    char     tags[4][16]  = {};
    uint16_t chapterCount = 0;
    char     updatedDate[12] = {};   // used as currentLocalDate for AO3Sync

    // Derived
    std::string workId;
    std::string storyUrl;
    bool        metaLoaded = false;

    void loadMetadata();

    // --- FreeInkUI & Action Handlers ---
    static constexpr freeink::ui::ActionId ACTION_BACK = 1;
    static constexpr freeink::ui::ActionId ACTION_OPEN = 2;
    static constexpr freeink::ui::ActionId ACTION_UPDATE = 3;

    static void buildScreenTrampoline(UiScreen& screen, void* user);
    void buildScreen(UiScreen& screen);

    static void onActionTrampoline(const freeink::ui::ActionEvent& event, void* user);
    void onAction(const freeink::ui::ActionEvent& event);

    void doBack();
    void doOpen();
    void doUpdate();
};