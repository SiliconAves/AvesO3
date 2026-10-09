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

#include "Ao3LibraryMetadata.h"
#include "BookStatus.h"
#include "Ao3NewChaptersStore.h"
#include "Ao3WipsStore.h"
#include "Ao3MarkedForLaterStore.h"
#include <Epub.h>

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
    errorMessage.clear();

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
        Storage.writeFile("/.crosspoint/pending_ao3_scan", "");

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
    if (mode == Ao3ReceiveMode::UPDATE_SINGLE) {
        webServer->setPreserveBookCacheOnUpload(true);
    }
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

void Ao3ReceiveActivity::handleUpdateComplete(const std::string& receivedPath) {
    stopWebServer();

    if (receivedPath.empty()) {
        errorMessage = "File path missing. Please try again.";
        state = Ao3ReceiveState::ERROR;
        requestUpdate();
        return;
    }

    if (!Storage.exists(receivedPath.c_str())) {
        errorMessage = "Received file not found on SD card.";
        state = Ao3ReceiveState::ERROR;
        requestUpdate();
        return;
    }

    // --- Safe atomic swap: backup → rename → delete backup ---
    // Skip the swap when the file was already uploaded directly to the target
    // path (e.g. HTTP POST overwrite to the same location). In that case the
    // new content is already in place; running the swap would mistakenly move
    // it to .bak and then fail to rename it back, producing a spurious error
    // even though the update succeeded.
    if (receivedPath != targetPath) {
        const std::string backupPath = targetPath + ".bak";

        // Step 1: move original to backup (preserves it if rename fails)
        bool hadOriginal = Storage.exists(targetPath.c_str());
        if (hadOriginal) {
            if (!Storage.rename(targetPath.c_str(), backupPath.c_str())) {
                errorMessage = "Could not move original file. SD card issue?";
                state = Ao3ReceiveState::ERROR;
                requestUpdate();
                return;
            }
        }

        // Step 2: move received file to target path
        if (!Storage.rename(receivedPath.c_str(), targetPath.c_str())) {
            // Restore original from backup
            if (hadOriginal) {
                Storage.rename(backupPath.c_str(), targetPath.c_str());
            }
            errorMessage = "Could not move received file. SD card issue?";
            state = Ao3ReceiveState::ERROR;
            requestUpdate();
            return;
        }

        // Step 3: delete backup now that swap succeeded
        if (hadOriginal) {
            Storage.remove(backupPath.c_str());
        }
    }

    // --- Cache invalidation (BMP preserved) ---
    Epub epub(targetPath, "/.crosspoint");
    const std::string cachePath = epub.getCachePath();
    Storage.remove((cachePath + "/book.bin").c_str());
    Storage.removeDir((cachePath + "/sections").c_str());
    Storage.removeDir((cachePath + "/html").c_str());
    Storage.remove((cachePath + "/ao3_info").c_str());

    // --- Read author from sidecar for store updates ---
    std::string author;
    {
        HalFile f;
        if (Storage.openFileForRead("AO3R", (cachePath + "/ao3_library_info").c_str(), f)) {
            Ao3LibraryMetadata meta;
            if (f.read((uint8_t*)&meta, sizeof(meta)) == sizeof(meta) && meta.isValid()) {
                author = meta.author;
            }
            f.close();
        }
    }

    // --- Update progress.bin to NEW_CHAPTER_AVAILABLE ---
    {
        const std::string progressPath = cachePath + "/progress.bin";
        HalFile f;
        uint8_t data[11] = {};
        int bytesRead = 0;
        if (Storage.openFileForRead("AO3R", progressPath.c_str(), f)) {
            bytesRead = f.read(data, sizeof(data));
            f.close();
        }
        if (bytesRead < 7) {
            bytesRead = 7;
        }
        if (bytesRead == 7) {
            data[6] = static_cast<uint8_t>(BookStatus::NEW_CHAPTER_AVAILABLE);
        } else {
            data[10] = static_cast<uint8_t>(BookStatus::NEW_CHAPTER_AVAILABLE);
        }
        if (Storage.openFileForWrite("AO3R", progressPath.c_str(), f)) {
            f.write(data, bytesRead);
            f.close();
        }
    }

    // --- Store updates ---
    NEW_CHAPTERS_STORE.loadFromFile();
    NEW_CHAPTERS_STORE.addBook(targetPath, updateTitle, author);
    NEW_CHAPTERS_STORE.saveToFile();
    AO3_WIPS_STORE.loadFromFile();
    AO3_WIPS_STORE.removeBook(targetPath);
    AO3_WIPS_STORE.clearEntries();
    MARKED_FOR_LATER_STORE.loadFromFile();
    MARKED_FOR_LATER_STORE.removeByPath(targetPath);
    MARKED_FOR_LATER_STORE.clearEntries();

    state = Ao3ReceiveState::FILE_RECEIVED;
    requestUpdate();
}

// ─────────────────────────────────────────────
//  Loop
// ─────────────────────────────────────────────

void Ao3ReceiveActivity::loop() {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        exitRequested = true;
    }

    if (state == Ao3ReceiveState::FILE_RECEIVED) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        finish();
    }
        return;
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

            lastProcessedCompleteAt = status.lastCompleteAt;

            if (mode == Ao3ReceiveMode::UPDATE_SINGLE) {
                // Handle immediately — stop server and do the swap
                handleUpdateComplete(status.lastCompletePath);
                return;
            } else {
                lastCompleteAt   = status.lastCompleteAt;
                lastCompleteName = status.lastCompleteName;
                changed = true;
            }
        }

        if (mode == Ao3ReceiveMode::RECEIVE_NEW &&
            lastCompleteAt > 0 && (millis() - lastCompleteAt) >= 6000) {
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
    const auto pageWidth  = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();

    renderer.clearScreen();

    // FILE_RECEIVED has its own full-screen layout
    if (state == Ao3ReceiveState::FILE_RECEIVED) {
        GUI.drawHeader(renderer,
            Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
            "Update Complete");
        renderFileReceived();
        renderer.displayBuffer();
        return;
    }

    GUI.drawHeader(renderer,
        Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
        mode == Ao3ReceiveMode::UPDATE_SINGLE ? "Send to AvesO3 Update" : "Send to AvesO3");

    const auto height = renderer.getLineHeight(UI_10_FONT_ID);
    const auto top    = (pageHeight - height) / 2;

    if (state == Ao3ReceiveState::SERVER_STARTING) {
        renderer.drawCenteredText(UI_10_FONT_ID, top, "Starting server…");
        renderer.displayBuffer();
        return;
    }

    if (state == Ao3ReceiveState::ERROR) {
        const char* msg = errorMessage.empty()
            ? "Server failed to start."
            : errorMessage.c_str();
        const char* sub = errorMessage.empty()
            ? "Try again or check Wi-Fi."
            : "Press Back to exit.";

        renderer.drawCenteredText(UI_12_FONT_ID, top - 16, msg, true, EpdFontFamily::BOLD);
        renderer.drawCenteredText(UI_10_FONT_ID, top + 16, sub);
        const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
        GUI.drawButtonHints(renderer, labels.btn1, "", "", "");
        renderer.displayBuffer();
        return;
    }

    if (state == Ao3ReceiveState::SERVER_RUNNING) {
        GUI.drawSubHeader(renderer,
            Rect{0, metrics.topPadding + metrics.headerHeight,
                 pageWidth, metrics.tabBarHeight},
            connectedSSID.c_str(),
            (std::string("http://") + connectedIP).c_str());

        if (mode == Ao3ReceiveMode::UPDATE_SINGLE) {
            renderUpdateRunning();
        } else {
            renderServerRunning();   // existing RECEIVE_NEW render
        }
    }

    renderer.displayBuffer();
}

void Ao3ReceiveActivity::renderServerRunning() const {
    const auto& metrics  = UITheme::getInstance().getMetrics();
    const auto pageWidth  = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();

    int y = metrics.topPadding
            + metrics.headerHeight
            + metrics.tabBarHeight
            + metrics.verticalSpacing * 4;

    const int heightText12 = renderer.getTextHeight(UI_12_FONT_ID);
    const int heightText10 = renderer.getLineHeight(UI_10_FONT_ID);
    const int heightSmall  = renderer.getLineHeight(SMALL_FONT_ID);

    // ── Setup section ──
    renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, y,
        "Setup", true, EpdFontFamily::BOLD);
    y += heightText12 + metrics.verticalSpacing * 2;

    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y,
        "1) Install \"Send to AvesO3\" extension for Firefox");
    y += heightSmall;
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y,
        "2) Access AO3 from your pc or phone");
    y += heightSmall;
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y,
        "3) Press the BIRD button to send fics to your ereader");
    y += heightSmall;
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y,
        "\"Keep this screen open while sending\"");
    y += heightSmall + metrics.verticalSpacing * 4;

    // ── Status section ──
    renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, y,
        "Status", true, EpdFontFamily::BOLD);
    y += heightText12 + metrics.verticalSpacing * 2;

    // Check if an upload is currently active (relies on flag rather than total size)
    const auto status = webServer->getWsUploadStatus();

    if (lastCompleteAt > 0 && (millis() - lastCompleteAt) < 6000) {
        // Success flash: "Received: [Title]"
        std::string name = !lastCompleteName.empty() ? lastCompleteName.c_str() : "Fic";
        std::string completionText = "Received: " + name;
        
        std::string truncatedCompletion = renderer.truncatedText(SMALL_FONT_ID,
            completionText.c_str(),
            pageWidth - metrics.contentSidePadding * 2);
            
        renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y,
            truncatedCompletion.c_str(), true, EpdFontFamily::BOLD);
    }

    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, "", "", "");
}

void Ao3ReceiveActivity::renderUpdateRunning() const {
    const auto& metrics   = UITheme::getInstance().getMetrics();
    const int   pageWidth  = renderer.getScreenWidth();
    const int   pageHeight = renderer.getScreenHeight();

    const int contentTop = metrics.topPadding + metrics.headerHeight
                         + metrics.tabBarHeight + metrics.verticalSpacing * 3;
    const int bottomLimit = pageHeight - metrics.buttonHintsHeight;

    const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
    constexpr int QR_SIZE = 170; // Increased by 10px

    // --- Setup section (Unchanged) ---
    int y = contentTop;
    renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, y,
        "Setup", true, EpdFontFamily::BOLD);
    y += renderer.getTextHeight(UI_12_FONT_ID) + metrics.verticalSpacing * 2;

    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y,
        "1) Install the \"Send to AvesO3\" Firefox extension");
    y += smallH;
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y,
        "2) Scan the QR code below");
    y += smallH;
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y,
        "3) Press the BIRD button and send the fic to any folder");
    y += smallH;
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y,
        "4) The new file will automatically overwrite the outdated version");
    y += smallH + metrics.verticalSpacing * 3;

    // --- Fic Metadata & QR Code section ---
    if (!updateStoryUrl.empty()) {
        
        // 1. Fetch Author from the existing sidecar file
        std::string author;
        Epub epub(targetPath, "/.crosspoint");
        const std::string cachePath = epub.getCachePath();
        HalFile f;
        if (Storage.openFileForRead("AO3R", (cachePath + "/ao3_library_info").c_str(), f)) {
            Ao3LibraryMetadata meta;
            if (f.read((uint8_t*)&meta, sizeof(meta)) == sizeof(meta) && meta.isValid()) {
                author = meta.author;
            }
            f.close();
        }

        // 2. Draw Title (12pt Bold)
        if (!updateTitle.empty()) {
            std::string truncTitle = renderer.truncatedText(UI_12_FONT_ID, updateTitle.c_str(), pageWidth - 40);
            renderer.drawCenteredText(UI_12_FONT_ID, y, truncTitle.c_str(), true, EpdFontFamily::BOLD);
            y += renderer.getLineHeight(UI_12_FONT_ID) + 4; // Minor gap below title
        }

        // 3. Draw Author (10pt Regular)
        if (!author.empty()) {
            std::string authorStr = author;
            std::string truncAuthor = renderer.truncatedText(UI_10_FONT_ID, authorStr.c_str(), pageWidth - 40);
            renderer.drawCenteredText(UI_10_FONT_ID, y, truncAuthor.c_str());
            y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing * 2;
        } else {
            y += metrics.verticalSpacing * 2; // Maintain spacing even if author is missing
        }

        // 4. Draw QR Code
        const Rect qrBounds{(pageWidth - QR_SIZE) / 2, y, QR_SIZE, QR_SIZE};
        QrUtils::drawQrCode(renderer, qrBounds, updateStoryUrl);
        y += QR_SIZE + metrics.verticalSpacing * 2;

        // 5. Draw AO3 Link (Small Font)
        std::string truncUrl = renderer.truncatedText(SMALL_FONT_ID, updateStoryUrl.c_str(), pageWidth - 40);
        renderer.drawCenteredText(SMALL_FONT_ID, y, truncUrl.c_str());
    }

    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, "", "", "");
}

void Ao3ReceiveActivity::renderFileReceived() const {
    const auto& metrics   = UITheme::getInstance().getMetrics();
    const int   pageWidth  = renderer.getScreenWidth();
    const int   pageHeight = renderer.getScreenHeight();

    const int mid = pageHeight / 2;
    const int lh  = renderer.getLineHeight(UI_10_FONT_ID);

    renderer.drawCenteredText(UI_12_FONT_ID, mid - lh,
        "Update success!", true, EpdFontFamily::BOLD);

    if (!targetPath.empty()) {
        // Extract the filename with extension from the full path
        std::string filename = targetPath.substr(targetPath.find_last_of('/') + 1);
        
        std::string truncated = renderer.truncatedText(
            UI_10_FONT_ID, filename.c_str(), pageWidth - 40);
        renderer.drawCenteredText(UI_10_FONT_ID, mid + 8, truncated.c_str());
    }
    
    const auto labels = mappedInput.mapLabels("", tr(STR_DONE), "", "");
    GUI.drawButtonHints(renderer, "", labels.btn2, "", "");
}