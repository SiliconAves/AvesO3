#pragma once

#include <functional>
#include <memory>
#include <string>

#include "activities/Activity.h"
#include "network/CrossPointWebServer.h"

enum class Ao3ReceiveMode : uint8_t {
    RECEIVE_NEW,
    UPDATE_SINGLE,
};

enum class Ao3ReceiveState {
    WIFI_SELECTION,
    SERVER_STARTING,
    SERVER_RUNNING,
    FILE_RECEIVED,   // UPDATE_SINGLE only
    ERROR
};

/**
 * Ao3ReceiveActivity starts the file transfer web server and displays
 * the device IP and QR code so the AvesO3 browser extension can send
 * EPUB files directly to the device.
 *
 * Structurally identical to CalibreConnectActivity but with AO3-specific
 * UI and auto-index triggering on exit.
 */
class Ao3ReceiveActivity final : public Activity {
public:
    // RECEIVE_NEW (default)
    explicit Ao3ReceiveActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
        : Activity("Ao3Receive", renderer, mappedInput),
          mode(Ao3ReceiveMode::RECEIVE_NEW) {}

    // UPDATE_SINGLE
    Ao3ReceiveActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                       Ao3ReceiveMode mode,
                       std::string targetPath,
                       std::string storyUrl,
                       std::string title)
        : Activity("Ao3Receive", renderer, mappedInput),
          mode(mode),
          targetPath(std::move(targetPath)),
          updateStoryUrl(std::move(storyUrl)),
          updateTitle(std::move(title)) {}

    void onEnter() override;
    void onExit()  override;
    void loop()    override;
    void render(RenderLock&&) override;

private:
    Ao3ReceiveMode mode;
    Ao3ReceiveState state = Ao3ReceiveState::WIFI_SELECTION;

    // Shared
    std::unique_ptr<CrossPointWebServer> webServer;
    std::string connectedIP;
    std::string connectedSSID;
    unsigned long lastHandleClientTime  = 0;
    size_t lastProgressReceived         = 0;
    size_t lastProgressTotal            = 0;
    std::string currentUploadName;
    std::string lastCompleteName;
    unsigned long lastCompleteAt        = 0;
    unsigned long lastProcessedCompleteAt = 0;
    bool exitRequested = false;

    // UPDATE_SINGLE only
    std::string targetPath;
    std::string updateStoryUrl;
    std::string updateTitle;
    std::string errorMessage;

    void onWifiSelectionComplete(bool connected);
    void startWebServer();
    void stopWebServer();
    void handleUpdateComplete(const std::string& receivedPath);

    void renderServerRunning() const;
    void renderUpdateRunning() const;   // UPDATE_SINGLE server screen
    void renderFileReceived()  const;   // UPDATE_SINGLE success screen
};