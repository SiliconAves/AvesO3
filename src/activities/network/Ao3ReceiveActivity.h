#pragma once

#include <functional>
#include <memory>
#include <string>

#include "activities/Activity.h"
#include "network/CrossPointWebServer.h"

enum class Ao3ReceiveState {
    WIFI_SELECTION,
    SERVER_STARTING,
    SERVER_RUNNING,
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
    Ao3ReceiveState state = Ao3ReceiveState::WIFI_SELECTION;

    std::unique_ptr<CrossPointWebServer> webServer;
    std::string connectedIP;
    std::string connectedSSID;

    unsigned long lastHandleClientTime     = 0;
    size_t        lastProgressReceived     = 0;
    size_t        lastProgressTotal        = 0;
    std::string   currentUploadName;
    std::string   lastCompleteName;
    unsigned long lastCompleteAt           = 0;
    unsigned long lastProcessedCompleteAt  = 0;

    bool exitRequested = false;
    bool ficReceived   = false;   // true if at least one file landed successfully

    void onWifiSelectionComplete(bool connected);
    void startWebServer();
    void stopWebServer();

 public:
    explicit Ao3ReceiveActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
        : Activity("Ao3Receive", renderer, mappedInput) {}

    void onEnter() override;
    void onExit() override;
    void loop() override;
    void render(RenderLock&&) override;

    bool skipLoopDelay()    override { return webServer && webServer->isRunning(); }
    bool preventAutoSleep() override { return webServer && webServer->isRunning(); }
};