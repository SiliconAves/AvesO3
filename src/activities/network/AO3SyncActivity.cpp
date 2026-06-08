#include "AO3SyncActivity.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <esp_crt_bundle.h>
#include <Logging.h>
#include <I18n.h>
#include <ZipFile.h>
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/StringUtils.h"
#include "activities/ActivityResult.h"
#include "HalStorage.h"

extern "C" {
extern esp_err_t esp_crt_bundle_attach(void* conf);
}

void AO3SyncActivity::onEnter() {
    Activity::onEnter();
    WiFi.mode(WIFI_STA);
    
    state = AO3SyncState::CONNECTING_WIFI;
    requestUpdate();

    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
        [this](const ActivityResult& res) {
            onWifiSelectionComplete(!res.isCancelled);
        });
}

void AO3SyncActivity::onExit() {
    Activity::onExit();
    WiFi.disconnect(false);
    delay(100);
    WiFi.mode(WIFI_OFF);
}

void AO3SyncActivity::onWifiSelectionComplete(bool success) {
    if (!success) {
        errorMessage = "WiFi Failed";
        state = AO3SyncState::ERROR;
        requestUpdate();
        return;
    }

    if (state == AO3SyncState::DOWNLOADING) {
        // We were trying to retry a download
        requestUpdateAndWait();
        performDownload();
    } else {
        state = AO3SyncState::SEARCHING;
        requestUpdateAndWait();
        performSearch();
    }
}

void AO3SyncActivity::performSearch() {
    if (workId.empty()) {
        errorMessage = "Invalid Work ID";
        state = AO3SyncState::ERROR;
        return;
    }

    std::string cleanWorkId = workId;
    cleanWorkId.erase(0, cleanWorkId.find_first_not_of(" \n\r\t"));
    cleanWorkId.erase(cleanWorkId.find_last_not_of(" \n\r\t") + 1);

    if (cleanWorkId.empty()) {
        errorMessage = "Invalid Work ID";
        state = AO3SyncState::ERROR;
        return;
    }

    std::string currentUrl = "https://archiveofourown.gay/works/" + cleanWorkId + "?view_adult=true";
    
    int status_code = 0;
    int max_retries = 3;
    HTTPClient http;
    std::unique_ptr<NetworkClient> netClient;

    while (max_retries > 0) {
        auto* secureClient = new NetworkClientSecure();
        secureClient->setInsecure(); // Skip strict cert validation
        secureClient->setTimeout(20); // 20s network read timeout
        
        // Set ALPN to http/1.1 to help Cloudflare routing
        const char* alpn_protos[] = {"http/1.1", nullptr};
        secureClient->setAlpnProtocols(alpn_protos);
        
        netClient.reset(secureClient);

        http.begin(*netClient, currentUrl.c_str());
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.setTimeout(20000); // 20 seconds HTTP timeout
        http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"); 
        http.addHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8");
        http.addHeader("Accept-Language", "en-US,en;q=0.5");
        http.addHeader("Connection", "keep-alive");

        status_code = http.GET();

        if (status_code == HTTP_CODE_OK || status_code == 403 || status_code == 404) {
            break; // Success or definite non-retryable error
        }

        LOG_INF("AO3", "HTTP error %d, retries left: %d", status_code, max_retries - 1);
        http.end();
        max_retries--;
        
        if (max_retries > 0) {
            // Check if user wants to cancel while retrying
            mappedInput.update();
            if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
                errorMessage = "Search Aborted";
                state = AO3SyncState::ERROR;
                requestUpdate();
                return;
            }
            delay(1500); // Wait before retry
        }
    }

    if (status_code == 403) {
        errorMessage = tr(STR_AO3_ERROR_LOCKED);
        http.end();
        state = AO3SyncState::ERROR;
        return;
    } else if (status_code == 429) {
        errorMessage = "AO3 Rate Limit: Try later";
        http.end();
        state = AO3SyncState::ERROR;
        return;
    } else if (status_code == 404) {
        errorMessage = "Work Deleted/Not Found";
        http.end();
        state = AO3SyncState::ERROR;
        return;
    } else if (status_code != HTTP_CODE_OK) {
        if (status_code < 0) {
            errorMessage = "Err: " + std::string(http.errorToString(status_code).c_str());
        } else {
            errorMessage = "Error: " + std::to_string(status_code);
        }
        http.end();
        state = AO3SyncState::ERROR;
        return;
    }

    WiFiClient* stream = http.getStreamPtr();
    if (!stream) {
        errorMessage = "Stream Failed";
        http.end();
        state = AO3SyncState::ERROR;
        return;
    }

    char* buffer = (char*)malloc(1024);
    if (!buffer) {
        errorMessage = "Out of Memory";
        http.end();
        state = AO3SyncState::ERROR;
        return;
    }

    std::string htmlAcc;
    bool foundDate = false;
    bool foundChapters = false;
    bytesProcessed = 0;

    while (bytesProcessed < 100000 && http.connected()) {
        // Allow user to abort
        mappedInput.update();
        if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
            LOG_INF("AO3", "Search aborted by user");
            errorMessage = "Search Aborted";
            foundDate = false; // Force failure
            break;
        }

        size_t available = stream->available();
        if (available > 0) {
            int toRead = std::min(available, (size_t)1024);
            int read = stream->read((uint8_t*)buffer, toRead);
            if (read > 0) {
                bytesProcessed += read;
                std::string chunk(buffer, read);
                htmlAcc += chunk;

                // Maintain small window for markers (Fast Discard)
                if (htmlAcc.size() > 2048) {
                    htmlAcc = htmlAcc.substr(htmlAcc.size() - 1024);
                }

                // Search for date
                if (!foundDate) {
                    size_t pos = htmlAcc.find("<dd class=\"status\">");
                    if (pos != std::string::npos) {
                        size_t endPos = htmlAcc.find("</dd>", pos);
                        if (endPos != std::string::npos) {
                            scrapedDate = htmlAcc.substr(pos + 19, endPos - (pos + 19));
                            foundDate = true;
                        }
                    }
                }

                // Search for chapters
                if (!foundChapters) {
                    size_t pos = htmlAcc.find("<dd class=\"chapters\">");
                    if (pos != std::string::npos) {
                        size_t endPos = htmlAcc.find("</dd>", pos);
                        if (endPos != std::string::npos) {
                            std::string chapStr = htmlAcc.substr(pos + 21, endPos - (pos + 21));
                            size_t slashPos = chapStr.find("/");
                            if (slashPos != std::string::npos) {
                                std::string current = chapStr.substr(0, slashPos);
                                std::string total = chapStr.substr(slashPos + 1);
                                if (total != "?" && current == total) {
                                    scrapedIsCompleted = true;
                                } else {
                                    scrapedIsCompleted = false;
                                }
                                foundChapters = true;
                            }
                        }
                    }
                }

                if (foundDate && foundChapters) break;
            }
        } else {
            delay(10); // Wait for more data
        }
    }

    free(buffer);
    http.end();

    if (foundDate && foundChapters) {
        if (scrapedDate > currentLocalDate) {
            state = AO3SyncState::UPDATE_FOUND;
        } else {
            state = AO3SyncState::UP_TO_DATE;
        }
    } else if (errorMessage == "Search Aborted") {
        state = AO3SyncState::ERROR;
    } else {
        errorMessage = tr(STR_AO3_ERROR_GENERIC);
        state = AO3SyncState::ERROR;
    }
    requestUpdate();
}

void AO3SyncActivity::performDownload() {
    state = AO3SyncState::DOWNLOADING;
    errorMessage = "";
    downloadProgress = 0;
    downloadTotal = 0;
    requestUpdate();

    std::string downloadUrl = "https://archiveofourown.gay/downloads/" + workId + "/work.epub";
    std::string tempPath = bookPath + ".tmp";

    LOG_INF("AO3", "Downloading: %s -> %s", downloadUrl.c_str(), tempPath.c_str());

    auto result = HttpDownloader::downloadToFile(downloadUrl, tempPath, [this](size_t downloaded, size_t total) {
        downloadProgress = downloaded;
        downloadTotal = total;
        requestUpdate(true); // Fast update for progress bar
    });

    if (result == HttpDownloader::OK) {
        LOG_INF("AO3", "Download successful, verifying ZIP integrity");
        
        ZipFile zip(tempPath);
        if (!zip.open()) {
            LOG_ERR("AO3", "ZIP Integrity check failed - truncated download?");
            if (Storage.exists(tempPath.c_str())) {
                Storage.remove(tempPath.c_str());
            }
            errorMessage = "Integrity Check Failed";
            state = AO3SyncState::ERROR;
            requestUpdate();
            return;
        }
        zip.close();

        LOG_INF("AO3", "Integrity verified, performing atomic swap");

        // Atomic Swap
        if (Storage.exists(bookPath.c_str())) {
            Storage.remove(bookPath.c_str());
        }
        
        if (Storage.rename(tempPath.c_str(), bookPath.c_str())) {
            LOG_INF("AO3", "Atomic swap complete");
            
            // Success result
            AO3Result res;
            res.scrapedDate = scrapedDate;
            res.isCompleted = scrapedIsCompleted;
            res.updateFound = true;
            res.downloaded = true;
            setResult(ActivityResult(res));
            finish();
        } else {
            errorMessage = "File Swap Failed";
            state = AO3SyncState::ERROR;
        }
    } else {
        LOG_ERR("AO3", "Download failed with error code: %d", result);
        if (Storage.exists(tempPath.c_str())) {
            Storage.remove(tempPath.c_str());
        }
        errorMessage = tr(STR_DOWNLOAD_FAILED);
        state = AO3SyncState::ERROR;
    }
    requestUpdate();
}

void AO3SyncActivity::loop() {
    if (state == AO3SyncState::UPDATE_FOUND || state == AO3SyncState::UP_TO_DATE) {
        if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
            if (state == AO3SyncState::UPDATE_FOUND) {
                performDownload();
            } else {
                AO3Result res;
                res.scrapedDate = scrapedDate;
                res.isCompleted = scrapedIsCompleted;
                res.updateFound = false;
                setResult(ActivityResult(res));
                finish();
            }
        } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
            if (state == AO3SyncState::UPDATE_FOUND) {
                // Signal the update exists so status becomes NEW_CHAPTER_AVAILABLE
                AO3Result res;
                res.updateFound = true;
                setResult(ActivityResult(res));
            }
            finish();
        }
    } else if (state == AO3SyncState::ERROR) {
        if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
            finish();
        } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
            // Check if WiFi is still connected, if not reconnect, otherwise just retry
            if (WiFi.status() == WL_CONNECTED) {
                if (downloadTotal > 0 || !scrapedDate.empty()) {
                    // We already found the update but failed download
                    performDownload();
                } else {
                    state = AO3SyncState::SEARCHING;
                    requestUpdateAndWait();
                    performSearch();
                }
            } else {
                state = AO3SyncState::CONNECTING_WIFI;
                requestUpdate();
                startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                    [this](const ActivityResult& res) {
                        onWifiSelectionComplete(!res.isCancelled);
                    });
            }
        }
    }
}

void AO3SyncActivity::renderInitializing() const {
    const auto pageWidth = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto top = (pageHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;

    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_AO3_CONNECTING_WIFI));
}

void AO3SyncActivity::renderSearching() const {
    const auto pageWidth = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto top = (pageHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;

    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_AO3_SEARCHING));
}

void AO3SyncActivity::renderDownloading() const {
    const auto pageWidth = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();
    const auto& metrics = UITheme::getInstance().getMetrics();
    
    const auto height = renderer.getLineHeight(UI_10_FONT_ID);
    const auto top = pageHeight / 2 - 40;

    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_DOWNLOADING), true, EpdFontFamily::BOLD);
    
    if (downloadTotal > 0) {
        const int barWidth = pageWidth - 100;
        constexpr int barHeight = 20;
        const int barX = 50;
        const int barY = pageHeight / 2;
        GUI.drawProgressBar(renderer, Rect{barX, barY, barWidth, barHeight}, downloadProgress, downloadTotal);
    }
}

void AO3SyncActivity::renderResult() const {
    const auto pageWidth = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();
    const auto& metrics = UITheme::getInstance().getMetrics();
    
    const auto height = renderer.getLineHeight(UI_10_FONT_ID);
    const auto top = (pageHeight - height) / 2;

    if (state == AO3SyncState::UP_TO_DATE) {
        renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_AO3_UP_TO_DATE), true, EpdFontFamily::BOLD);
        GUI.drawButtonHints(renderer, tr(STR_BACK), tr(STR_DONE), "", "");
    } else if (state == AO3SyncState::UPDATE_FOUND) {
        renderer.drawCenteredText(UI_10_FONT_ID, top - 10, tr(STR_AO3_UPDATE_QUERY), true, EpdFontFamily::BOLD);
        renderer.drawCenteredText(UI_10_FONT_ID, top + height + 5, (std::string("New date: ") + scrapedDate).c_str());
        GUI.drawButtonHints(renderer, tr(STR_CANCEL), tr(STR_AO3_DOWNLOAD), "", "");
    }
}

void AO3SyncActivity::renderError() const {
    const auto pageWidth = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto top = (pageHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;

    renderer.drawCenteredText(UI_10_FONT_ID, top, errorMessage.c_str(), true, EpdFontFamily::BOLD);
    GUI.drawButtonHints(renderer, tr(STR_BACK), tr(STR_RETRY), "", "");
}

void AO3SyncActivity::render(RenderLock&& lock) {
    const auto pageWidth = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();
    const auto& metrics = UITheme::getInstance().getMetrics();

    renderer.clearScreen();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_AO3_SEARCH));

    switch (state) {
        case AO3SyncState::INITIALIZING:
        case AO3SyncState::CONNECTING_WIFI:
            renderInitializing();
            break;
        case AO3SyncState::SEARCHING:
            renderSearching();
            break;
        case AO3SyncState::DOWNLOADING:
            renderDownloading();
            break;
        case AO3SyncState::UP_TO_DATE:
        case AO3SyncState::UPDATE_FOUND:
            renderResult();
            break;
        case AO3SyncState::ERROR:
            renderError();
            break;
    }

    renderer.displayBuffer();
}
