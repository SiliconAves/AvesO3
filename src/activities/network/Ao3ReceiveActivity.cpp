#include "Ao3ReceiveActivity.h"

#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/QrUtils.h"
#include "util/TaskWatchdog.h"

namespace {
constexpr const char* HOSTNAME = "crosspoint";
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────

void Ao3ReceiveActivity::onEnter() {
    Activity::onEnter();

    state                    = Ao3ReceiveState::WIFI_SELECTION;
    connectedIP.clear();
    connectedSSID.clear();
    lastHandleClientTime     = 0;
    lastProgressReceived     = 0;
    lastProgressTotal        = 0;
    currentUploadName.clear();
    lastCompleteName.clear();
    lastCompleteAt           = 0;
    lastProcessedCompleteAt  = 0;
    exitRequested            = false;
    ficReceived              = false;

    requestUpdate();

    if (WiFi.status() != WL_CONNECTED) {
        startActivityForResult(
            std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
            [this](const ActivityResult& result) {
                if (!result.isCancelled) {
                    const auto& wifi = std::get<WifiResult>(result.data);
                    connectedIP   = wifi.ip;
                    connectedSSID = wifi.ssid;
                }
                onWifiSelectionComplete(!result.isCancelled);
            });
    } else {
        connectedIP   = WiFi.localIP().toString().c_str();
        connectedSSID = WiFi.SSID().c_str();
        startWebServer();
    }
}

void Ao3ReceiveActivity::onExit() {
    Activity::onExit();

    MDNS.end();

    if (WiFi.getMode() != WIFI_MODE_NULL) {
        WiFi.disconnect(false);
        delay(30);

        // Only trigger auto-index if something was actually received
        if (ficReceived) {
            Storage.writeFile("/.crosspoint/pending_ao3_scan", "");
        }

        // WiFi fragments the heap — silent restart is mandatory
        silentRestart();
    }
}

// ─────────────────────────────────────────────
//  Server management
// ─────────────────────────────────────────────

void Ao3ReceiveActivity::onWifiSelectionComplete(bool connected) {
    if (!connected) {
        finish();
        return;
    }
    startWebServer();
}

void Ao3ReceiveActivity::startWebServer() {
    state = Ao3ReceiveState::SERVER_STARTING;
    requestUpdate();

    MDNS.end();
    if (MDNS.begin(HOSTNAME)) {
        LOG_DBG("AO3R", "mDNS started: http://%s.local/", HOSTNAME);
    }

    webServer.reset(new CrossPointWebServer());
    webServer->begin();

    if (webServer->isRunning()) {
        state = Ao3ReceiveState::SERVER_RUNNING;
        requestUpdate();
    } else {
        state = Ao3ReceiveState::ERROR;
        requestUpdate();
    }
}

void Ao3ReceiveActivity::stopWebServer() {
    if (webServer) {
        webServer->stop();
        webServer.reset();
    }
}

// ─────────────────────────────────────────────
//  Loop
// ─────────────────────────────────────────────

void Ao3ReceiveActivity::loop() {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        exitRequested = true;
    }

    if (webServer && webServer->isRunning()) {
        const unsigned long gap = millis() - lastHandleClientTime;
        if (lastHandleClientTime > 0 && gap > 100) {
            LOG_DBG("AO3R", "WARNING: %lu ms gap since last handleClient", gap);
        }

        resetTaskWatchdogIfSubscribed();
        constexpr int MAX_ITERATIONS = 80;
        for (int i = 0; i < MAX_ITERATIONS && webServer->isRunning(); i++) {
            webServer->handleClient();
            if ((i & 0x07) == 0x07) resetTaskWatchdogIfSubscribed();
            if ((i & 0x0F) == 0x0F) {
                yield();
                if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
                    exitRequested = true;
                    break;
                }
            }
        }
        lastHandleClientTime = millis();

        // Poll upload status for progress and completion
        const auto status = webServer->getWsUploadStatus();
        bool changed = false;

        if (status.inProgress) {
            if (status.received  != lastProgressReceived ||
                status.total     != lastProgressTotal    ||
                status.filename  != currentUploadName) {
                lastProgressReceived = status.received;
                lastProgressTotal    = status.total;
                currentUploadName    = status.filename;
                changed = true;
            }
        } else if (lastProgressReceived != 0 || lastProgressTotal != 0) {
            lastProgressReceived = 0;
            lastProgressTotal    = 0;
            currentUploadName.clear();
            changed = true;
        }

        if (status.lastCompleteAt != 0 &&
            status.lastCompleteAt != lastProcessedCompleteAt) {
            lastCompleteAt          = status.lastCompleteAt;
            lastCompleteName        = status.lastCompleteName;
            lastProcessedCompleteAt = status.lastCompleteAt;
            ficReceived             = true;   // at least one fic landed
            changed = true;
        }

        if (lastCompleteAt > 0 && (millis() - lastCompleteAt) >= 6000) {
            lastCompleteAt = 0;
            lastCompleteName.clear();
            changed = true;
        }

        if (changed) requestUpdate();
    }

    if (exitRequested) {
        finish();
        return;
    }
}

// ─────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────

void Ao3ReceiveActivity::render(RenderLock&&) {
    const auto& metrics  = UITheme::getInstance().getMetrics();
    const int pageWidth  = renderer.getScreenWidth();
    const int pageHeight = renderer.getScreenHeight();

    renderer.clearScreen();
    GUI.drawHeader(renderer,
        Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
        "AvesO3 Receive");

    if (state == Ao3ReceiveState::SERVER_STARTING) {
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Starting server…");
        renderer.displayBuffer();
        return;
    }

    if (state == Ao3ReceiveState::ERROR) {
        renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 16,
            "Server failed to start.", true, EpdFontFamily::BOLD);
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 16,
            "Try again or check Wi-Fi.");
        const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
        GUI.drawButtonHints(renderer, labels.btn1, "", "", "");
        renderer.displayBuffer();
        return;
    }

    if (state == Ao3ReceiveState::SERVER_RUNNING) {
        // ── Sub-header: SSID + IP ──
        GUI.drawSubHeader(renderer,
            Rect{0, metrics.topPadding + metrics.headerHeight,
                 pageWidth, metrics.tabBarHeight},
            connectedSSID.c_str(),
            (std::string("http://") + connectedIP).c_str());

        const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight;
        const int contentBottom = pageHeight - metrics.buttonHintsHeight;
        const int contentHeight = contentBottom - contentTop;

        // ── QR code (square, upper half of content area) ──
        const int qrSize  = std::min(pageWidth, contentHeight / 2) - 16;
        const int qrLeft  = (pageWidth - qrSize) / 2;
        const int qrTop   = contentTop + 12;

        QrUtils::drawQrCode(renderer,
            Rect{qrLeft, qrTop, qrSize, qrSize},
            std::string("http://") + connectedIP);

        // ── Hostname hint below QR ──
        const int hintY = qrTop + qrSize + 10;
        renderer.drawCenteredText(SMALL_FONT_ID, hintY, "or crosspoint.local");

        // ── Status area ──
        const int statusY = hintY + renderer.getLineHeight(SMALL_FONT_ID) + 16;
        const int lineH   = renderer.getLineHeight(UI_10_FONT_ID);

        if (lastProgressTotal > 0 && lastProgressReceived <= lastProgressTotal) {
            // Transfer in progress
            std::string label = "Receiving";
            if (!currentUploadName.empty()) {
                label += ": " + currentUploadName;
                label = renderer.truncatedText(UI_10_FONT_ID, label.c_str(),
                    pageWidth - metrics.contentSidePadding * 2);
            }
            renderer.drawCenteredText(UI_10_FONT_ID, statusY, label.c_str());

            const int barW = pageWidth - 80;
            const int barX = (pageWidth - barW) / 2;
            const int barY = statusY + lineH + 8;
            GUI.drawProgressBar(renderer,
                Rect{barX, barY, barW, metrics.progressBarHeight},
                lastProgressReceived, lastProgressTotal);

        } else if (lastCompleteAt > 0 && (millis() - lastCompleteAt) < 6000) {
            // Completion flash
            renderer.drawCenteredText(UI_10_FONT_ID, statusY,
                "Received:", true, EpdFontFamily::BOLD);
            std::string name = renderer.truncatedText(UI_10_FONT_ID,
                lastCompleteName.c_str(),
                pageWidth - metrics.contentSidePadding * 2);
            renderer.drawCenteredText(UI_10_FONT_ID, statusY + lineH + 4, name.c_str());

        } else {
            // Idle
            renderer.drawCenteredText(UI_10_FONT_ID, statusY,
                "Waiting for fic…");
        }

        const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", "");
        GUI.drawButtonHints(renderer, labels.btn1, "", "", "");
    }

    renderer.displayBuffer();
}