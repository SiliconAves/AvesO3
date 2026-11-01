#pragma once

#include <string>
#include "activities/Activity.h"
#include "components/UiAppHost.h"

enum class AO3SyncState {
    INITIALIZING,
    CONNECTING_WIFI,
    SEARCHING,
    UP_TO_DATE,
    UPDATE_FOUND,
    DOWNLOADING,
    UPDATE_SUCCESSFUL,
    ERROR
};

class AO3SyncActivity final : public Activity, private UiAppHost {
private:
    AO3SyncState state = AO3SyncState::INITIALIZING;
    std::string workId;
    std::string currentLocalDate;

    // Results from scraping
    std::string scrapedDate;
    bool scrapedIsCompleted = false;
    std::string errorMessage;
    std::string bookPath;

    // Download progress
    size_t downloadProgress = 0;
    size_t downloadTotal = 0;

    // Internal timing/status
    unsigned long searchStartTime = 0;
    size_t bytesProcessed = 0;
    bool searchFinished = false;
    bool usingOrgFallback = false;

    void onWifiSelectionComplete(bool success);
    void performSearch();
    void performDownload();
    void renderInitializing() const;
    void renderSearching() const;
    void renderDownloading() const;
    void renderResult() const;
    void renderError() const;

    // --- FreeInkUI & touch actions ---
    static constexpr freeink::ui::ActionId ACTION_BACK = 1;
    static constexpr freeink::ui::ActionId ACTION_CONFIRM = 2;
    void handleInput(bool back, bool confirm);
    void onAction(const freeink::ui::ActionEvent& event);
    void buildScreen(UiScreen& screen);

public:
    AO3SyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                    const std::string& workId, const std::string& currentLocalDate,
                    const std::string& bookPath)
        : Activity("AO3Sync", renderer, mappedInput),
          UiAppHost(renderer),
          workId(workId), currentLocalDate(currentLocalDate), bookPath(bookPath) {}

    void onEnter() override;
    void onExit() override;
    void loop() override;
    void render(RenderLock&& lock) override;
};
