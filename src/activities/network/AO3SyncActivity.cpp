#include "AO3SyncActivity.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <esp_crt_bundle.h>
#include <Logging.h>
#include <I18n.h>
#include <ZipFile.h>
#include <Epub.h>

#include "Ao3NewChaptersStore.h"
#include "Ao3WipsStore.h"
#include "Ao3LibraryMetadata.h"
#include "Ao3MarkedForLaterStore.h"
#include "BookStatus.h"
#include "SilentRestart.h"

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

    if (ESP.getMaxAllocHeap() < 50 * 1024) {
        errorMessage = "Not enough memory. Please reboot the device.";
        state = AO3SyncState::ERROR;
        requestUpdate();
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

    usingOrgFallback = false;
    static const char* const kDomains[] = { "archiveofourown.gay", "archiveofourown.org" };

    int status_code = 0;
    for (int urlIdx = 0; urlIdx < 2; urlIdx++) {
        if (urlIdx == 1) {
            usingOrgFallback = true;
            requestUpdateAndWait();
            delay(1000);
        }

        char currentUrl[128];
        snprintf(currentUrl, sizeof(currentUrl),
                 "https://%s/works/%s?view_adult=true",
                 kDomains[urlIdx], cleanWorkId.c_str());

    int max_retries = 3;
    HTTPClient http;
    std::unique_ptr<NetworkClient> netClient;
    bool firstAttempt = true;

    while (max_retries > 0) {
        // On retries only: free the old TLS context before allocating a new one.
        // Skipped on first attempt because http.end() before http.begin() is unsafe.
        if (!firstAttempt) {
            http.end();
            netClient.reset();
        }
        firstAttempt = false;

        auto* secureClient = new NetworkClientSecure();
        if (!secureClient) {
            errorMessage = "Out of Memory";
            state = AO3SyncState::ERROR;
            return;
        }
        secureClient->setInsecure();
        secureClient->setTimeout(20);
        const char* alpn_protos[] = {"http/1.1", nullptr};
        secureClient->setAlpnProtocols(alpn_protos);

        netClient.reset(secureClient);

        http.begin(*netClient, currentUrl);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.setTimeout(20000);
        http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        http.addHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8");
        http.addHeader("Accept-Language", "en-US,en;q=0.5");
        http.addHeader("Connection", "keep-alive");

        status_code = http.GET();

        if (status_code == HTTP_CODE_OK || status_code == 403 || status_code == 404) {
        break;
        }

        LOG_INF("AO3", "HTTP error %d, retries left: %d", status_code, max_retries - 1);
        max_retries--;

        if (max_retries > 0) {
            mappedInput.update();
            if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
                errorMessage = "Search Aborted";
                state = AO3SyncState::ERROR;
                http.end();
                netClient.reset();
                requestUpdate();
                return;
            }
            delay(1500);
        }
    }

    if (status_code == 403) {
        http.end();
        if (urlIdx == 0) continue; // try .org
        errorMessage = tr(STR_AO3_ERROR_LOCKED);
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

    char* buffer  = (char*)malloc(1024);
    char* htmlAcc = (char*)malloc(2049);
    if (!buffer || !htmlAcc) {
        free(buffer);
        free(htmlAcc);
        errorMessage = "Out of Memory";
        http.end();
        state = AO3SyncState::ERROR;
        return;
    }
    htmlAcc[0] = '\0';
    size_t htmlAccLen = 0;

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

                // Maintain small window for markers (Fast Discard)
                if (htmlAccLen + (size_t)read > 2048) {
                    size_t keep = (htmlAccLen >= 1024) ? 1024 : htmlAccLen;
                    memmove(htmlAcc, htmlAcc + (htmlAccLen - keep), keep);
                    htmlAccLen = keep;
                }
                size_t space  = 2048 - htmlAccLen;
                size_t toCopy = ((size_t)read < space) ? (size_t)read : space;
                memcpy(htmlAcc + htmlAccLen, buffer, toCopy);
                htmlAccLen += toCopy;
                htmlAcc[htmlAccLen] = '\0';

                // Search for date
                if (!foundDate) {
                    const char* pos = strstr(htmlAcc, "<dd class=\"status\">");
                    if (pos) {
                        const char* end = strstr(pos + 19, "</dd>");
                        if (end) {
                            char dateBuf[32] = {};
                            size_t len = (size_t)(end - (pos + 19));
                            if (len > 31) len = 31;
                            strncpy(dateBuf, pos + 19, len);
                            scrapedDate = dateBuf;
                            foundDate = true;
                        }
                    }
                }

                // Search for chapters
                if (!foundChapters) {
                    const char* pos = strstr(htmlAcc, "<dd class=\"chapters\">");
                    if (pos) {
                        const char* end = strstr(pos + 21, "</dd>");
                        if (end) {
                            char chapStr[32] = {};
                            size_t len = (size_t)(end - (pos + 21));
                            if (len > 31) len = 31;
                            strncpy(chapStr, pos + 21, len);
                            const char* slash = strchr(chapStr, '/');
                            if (slash) {
                                const char* total = slash + 1;
                                scrapedIsCompleted = (strcmp(total, "?") != 0 &&
                                                      atoi(total) > 0 &&
                                                      atoi(chapStr) == atoi(total));
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
    free(htmlAcc);
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
        break;
    }
}

void AO3SyncActivity::performDownload() {
    state = AO3SyncState::DOWNLOADING;
    errorMessage = "";
    downloadProgress = 0;
    downloadTotal = 0;
    requestUpdate();

    std::string downloadUrl = "https://archiveofourown.gay/downloads/" + workId + "/work.epub?v=" + scrapedDate;
    std::string tempPath = bookPath + ".tmp";

    LOG_INF("AO3", "Downloading: %s -> %s", downloadUrl.c_str(), tempPath.c_str());

        auto result = HttpDownloader::downloadToFile(downloadUrl, tempPath, [this](size_t downloaded, size_t total) {
        downloadProgress = downloaded;
        downloadTotal = total;
        requestUpdate(true);
    });

    if (result == HttpDownloader::HTTP_ERROR) {
        usingOrgFallback = true;
        requestUpdateAndWait();
        delay(1000);
        downloadUrl = "https://archiveofourown.org/downloads/" + workId + "/work.epub?v=" + scrapedDate;
        result = HttpDownloader::downloadToFile(downloadUrl, tempPath, [this](size_t downloaded, size_t total) {
            downloadProgress = downloaded;
            downloadTotal = total;
            requestUpdate(true);
        });
    }

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

            // Tab 1 hook: insert into New Chapters store.
            // Read title/author from existing sidecar metadata file instead of
            // loading the entire EPUB ZIP to keep peak memory minimal.
            {
                const uint32_t hash = static_cast<uint32_t>(std::hash<std::string>{}(bookPath));
                const std::string infoPath = "/.crosspoint/epub_" + std::to_string(hash) + "/ao3_library_info";

                auto meta = std::make_unique<Ao3LibraryMetadata>();
                bool hasMeta = false;
                {
                    HalFile f;
                    if (Storage.openFileForRead("AO3L", infoPath, f)) {
                        hasMeta = (f.read((uint8_t*)meta.get(), sizeof(*meta)) == sizeof(*meta))
                                   && meta->isValid();
                        f.close();
                    }
                }
                const char* title  = hasMeta ? meta->title  : "";
                const char* author = hasMeta ? meta->author : "";

                NEW_CHAPTERS_STORE.loadFromFile();
                NEW_CHAPTERS_STORE.addBook(bookPath, title, author);
                NEW_CHAPTERS_STORE.clearEntries();
                AO3_WIPS_STORE.loadFromFile();
                AO3_WIPS_STORE.removeBook(bookPath);
                AO3_WIPS_STORE.clearEntries();
            }

            // Update AO3 sidecar info and invalidate EPUB section/book cache
            {
                Epub epub(bookPath, "/.crosspoint");
                epub.saveAo3Info(workId, scrapedDate, scrapedIsCompleted);
                const std::string cachePath = epub.getCachePath();
                Storage.remove((cachePath + "/book.bin").c_str());
                Storage.removeDir((cachePath + "/sections").c_str());
                Storage.removeDir((cachePath + "/html").c_str());

                // Update BookStatus in progress.bin to NEW_CHAPTER_AVAILABLE so UI reads correct status
                const std::string progressPath = cachePath + "/progress.bin";
                HalFile f;
                if (Storage.openFileForRead("AO3", progressPath, f)) {
                    uint8_t data[11] = {};
                    int bytesRead = f.read(data, sizeof(data));
                    f.close();
                    if (bytesRead >= 7) {
                        if (bytesRead == 7) {
                            data[6] = static_cast<uint8_t>(BookStatus::NEW_CHAPTER_AVAILABLE);
                        } else if (bytesRead >= 11) {
                            data[10] = static_cast<uint8_t>(BookStatus::NEW_CHAPTER_AVAILABLE);
                        }
                        if (Storage.openFileForWrite("AO3", progressPath, f)) {
                            f.write(data, bytesRead);
                            f.close();
                        }
                    }
                }

                MARKED_FOR_LATER_STORE.loadFromFile();
                MARKED_FOR_LATER_STORE.removeByPath(bookPath);
                MARKED_FOR_LATER_STORE.clearEntries();
            }

            state = AO3SyncState::UPDATE_SUCCESSFUL;
            requestUpdate();
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
    if (state == AO3SyncState::UPDATE_SUCCESSFUL) {
        if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
            mappedInput.wasReleased(MappedInputManager::Button::Back)) {
            silentRestart();
        }
    } else if (state == AO3SyncState::UPDATE_FOUND || state == AO3SyncState::UP_TO_DATE) {
        if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
            if (state == AO3SyncState::UPDATE_FOUND) {
                performDownload();
            } else {
                silentRestart();
            }
        } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
            if (state == AO3SyncState::UPDATE_FOUND) {
                // Signal the update exists so status becomes NEW_CHAPTER_AVAILABLE
                AO3Result res;
                res.updateFound = true;
                setResult(ActivityResult(res));
            }
            silentRestartToReader();
        }
    } else if (state == AO3SyncState::ERROR) {
        if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
            mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
            silentRestartToReader();
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

    if (usingOrgFallback) {
        renderer.drawCenteredText(UI_10_FONT_ID, top, "Retrying on .org domain...");
    } else {
        renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_AO3_SEARCHING));
    }
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

    if (state == AO3SyncState::UPDATE_SUCCESSFUL) {
        renderer.drawCenteredText(UI_10_FONT_ID, top, "Update successful!", true, EpdFontFamily::BOLD);
        GUI.drawButtonHints(renderer, "", tr(STR_DONE), "", "");
    } else if (state == AO3SyncState::UP_TO_DATE) {
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
    if (errorMessage == "Not enough memory. Please reboot the device.") {
        GUI.drawButtonHints(renderer, tr(STR_BACK), "Reboot", "", "");
    } else {
        GUI.drawButtonHints(renderer, tr(STR_BACK), tr(STR_RETRY), "", "");
    }
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
        case AO3SyncState::UPDATE_SUCCESSFUL:
            renderResult();
            break;
        case AO3SyncState::ERROR:
            renderError();
            break;
    }

    renderer.displayBuffer();
}
