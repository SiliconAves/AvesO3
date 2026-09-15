#include "Ao3PageQrActivity.h"

#include <Epub.h>
#include <HalStorage.h>
#include <I18n.h>
#include <algorithm>
#include <cstring>
#include <vector>

#include "Ao3LibraryMetadata.h"
#include "network/AO3SyncActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/QrUtils.h"

// ---------------------------------------------------------------------------
//  loadMetadata
// ---------------------------------------------------------------------------

void Ao3PageQrActivity::loadMetadata() {
    metaLoaded = false;
    workId.clear();
    storyUrl.clear();
    memset(title,       0, sizeof(title));
    memset(author,      0, sizeof(author));
    memset(tags,        0, sizeof(tags));
    memset(updatedDate, 0, sizeof(updatedDate));
    chapterCount = 0;

    // --- Read ao3_library_info sidecar ---
    const std::string cachePath = "/.crosspoint/epub_" +
        std::to_string(std::hash<std::string>{}(filePath));

    HalFile f;
    if (Storage.openFileForRead("QRA", cachePath + "/ao3_library_info", f)) {
        Ao3LibraryMetadata meta;
        if (f.read((uint8_t*)&meta, sizeof(meta)) == sizeof(meta) && meta.isValid()) {
            strncpy(title,  meta.title,  sizeof(title)  - 1);
            strncpy(author, meta.author, sizeof(author) - 1);
            for (int i = 0; i < 4; i++)
                strncpy(tags[i], meta.tags[i], sizeof(tags[i]) - 1);
            chapterCount = meta.chapterCount;
            strncpy(updatedDate, meta.updatedDate, sizeof(updatedDate) - 1);
            metaLoaded = true;
        }
        f.close();
    }

    // --- Get work ID via Epub ---
    Epub epub(filePath, "/.crosspoint");
    if (epub.hasAo3Info()) {
        workId = epub.getAo3WorkId();
    }

    if (!workId.empty()) {
        storyUrl = "https://archiveofourown.org/works/" + workId;
    }
}

// ---------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------

void Ao3PageQrActivity::onEnter() {
    Activity::onEnter();
    loadMetadata();
    requestUpdate();
}

void Ao3PageQrActivity::loop() {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        finish();
        return;
    }

    // Open the fic in the reader on Confirm
    if (!filePath.empty() && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        activityManager.goToReader(filePath);
        return;
    }

    // Update via AO3Sync (causes fragmentation; needs silent reboot strategy)
    // if (!workId.empty() && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    //     startActivityForResult(
    //         std::make_unique<AO3SyncActivity>(renderer, mappedInput,
    //             workId, std::string(updatedDate), filePath),
    //         [this](const ActivityResult&) { requestUpdate(true); });
    // }
}

// ---------------------------------------------------------------------------
//  render
// ---------------------------------------------------------------------------

void Ao3PageQrActivity::render(RenderLock&&) {
    renderer.clearScreen();
    const auto& metrics  = UITheme::getInstance().getMetrics();
    const int   pageWidth  = renderer.getScreenWidth();
    const int   pageHeight = renderer.getScreenHeight();

    GUI.drawHeader(renderer,
        Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
        "AO3 Page QR");

    // --- 1. Prepare Top Info Block Content & Metrics ---
    std::vector<std::string> titleLines;
    if (title[0]) {
        titleLines = renderer.wrappedText(UI_12_FONT_ID, title, pageWidth - 40, 2, EpdFontFamily::BOLD);
    }

    std::string authorStr;
    if (author[0]) {
        authorStr = renderer.truncatedText(UI_12_FONT_ID, (std::string("by ") + author).c_str(), pageWidth - 40);
    }

    std::string infoStr = "Chapters on Device: " + std::to_string(chapterCount);
    if (updatedDate[0]) {
        infoStr += " • Updated on " + std::string(updatedDate);
    }

    // Measure info line width to constrain tags
    const int infoWidth = renderer.getTextWidth(UI_10_FONT_ID, infoStr.c_str());
    const int maxTagWidth = (infoWidth > 0 && infoWidth < pageWidth - 40) ? infoWidth : (pageWidth - 40);

    // Calculate fitting tags within info line width
    int fittingCount = 0;
    int actualTotalW = 0;
    int tagWidths[4] = {};

    for (int i = 0; i < 4; i++) {
        if (!tags[i][0]) break;
        int w = renderer.getTextWidth(SMALL_FONT_ID, tags[i]) + 16;
        int nextW = actualTotalW + (fittingCount > 0 ? 8 : 0) + w;
        if (nextW > maxTagWidth) break;

        tagWidths[fittingCount] = w;
        actualTotalW = nextW;
        fittingCount++;
    }

    // --- 2. Calculate Heights & Gaps ---
    const int titleH  = titleLines.empty() ? 0 : (titleLines.size() * renderer.getLineHeight(UI_12_FONT_ID) + (titleLines.size() - 1) * 2);
    const int authorH = authorStr.empty() ? 0 : renderer.getLineHeight(UI_12_FONT_ID);
    const int tagsH   = (fittingCount > 0) ? 20 : 0;
    const int infoH   = renderer.getLineHeight(UI_10_FONT_ID);

    const int gapTitleToAuthor = (!titleLines.empty() && !authorStr.empty()) ? 6 : 0;
    const int gapAuthorToTags  = (!authorStr.empty() && fittingCount > 0) ? 20 : 0;
    const int gapTagsToInfo    = 20;

    // Sub-block height for Title + Author + Tags
    const int subBlockH = titleH + gapTitleToAuthor + authorH + gapAuthorToTags + tagsH;
    const int topBlockH = subBlockH + gapTagsToInfo + infoH;

    constexpr int QR_SIZE = 227;
    const int qrBlockH    = storyUrl.empty() ? 80 : QR_SIZE;

    const int urlH        = renderer.getLineHeight(UI_10_FONT_ID);
    const int scanH       = renderer.getLineHeight(SMALL_FONT_ID);
    const int bottomBlockH = storyUrl.empty() ? 0 : (urlH + 10 + scanH);

    // --- 3. Compute Spacing & True Vertical Centering ---
    const int topLimit     = metrics.topPadding + metrics.headerHeight;
    const int bottomLimit  = pageHeight - metrics.buttonHintsHeight;
    const int availableH   = bottomLimit - topLimit;
    const int totalContent = topBlockH + qrBlockH + bottomBlockH;

    const int numGaps = storyUrl.empty() ? 2 : 4;
    const int baseGap = std::max(6, (availableH - totalContent) / numGaps);

    const int gapInfoToQr   = storyUrl.empty() ? baseGap : std::max(4, baseGap - 6);
    const int gapQrToBottom = std::max(4, baseGap - 6);

    // Total height of all elements combined including their internal spacing
    const int totalBlockHeight = topBlockH + gapInfoToQr + qrBlockH + (storyUrl.empty() ? 0 : (gapQrToBottom + bottomBlockH));

    // Vertically center the entire block as a whole
    const int subBlockY = topLimit + std::max(0, (availableH - totalBlockHeight) / 2);
    const int infoY     = subBlockY + subBlockH + gapTagsToInfo;

    int curY = subBlockY;

    // --- 4. Render Top Sub-Block (Title, Author, Tags) ---
    if (!titleLines.empty()) {
        for (size_t i = 0; i < titleLines.size(); i++) {
            renderer.drawCenteredText(UI_12_FONT_ID, curY, titleLines[i].c_str(), true, EpdFontFamily::BOLD);
            curY += renderer.getLineHeight(UI_12_FONT_ID) + (i + 1 < titleLines.size() - 1 ? 2 : 0);
        }
        curY += gapTitleToAuthor;
    }

    if (!authorStr.empty()) {
        renderer.drawCenteredText(UI_12_FONT_ID, curY, authorStr.c_str());
        curY += authorH + gapAuthorToTags;
    }

    if (fittingCount > 0) {
        int tagX = (pageWidth - actualTotalW) / 2;
        for (int i = 0; i < fittingCount; i++) {
            renderer.drawRoundedRect(tagX, curY, tagWidths[i], 20, 1, 6, true);
            renderer.drawText(SMALL_FONT_ID, tagX + 8, curY - 2, tags[i]);
            tagX += tagWidths[i] + 8;
        }
    }

    // --- Render Info Line ---
    renderer.drawCenteredText(UI_10_FONT_ID, infoY, infoStr.c_str());

    // --- 5. Render Middle Block (QR Code) ---
    curY = infoY + infoH + gapInfoToQr;

    if (!storyUrl.empty()) {
        const Rect qrBounds{(pageWidth - QR_SIZE) / 2, curY, QR_SIZE, QR_SIZE};
        QrUtils::drawQrCode(renderer, qrBounds, storyUrl);
        curY += QR_SIZE;
    } else {
        renderer.drawCenteredText(UI_10_FONT_ID, curY + 20, "Work ID not found in epub.");
        renderer.drawCenteredText(SMALL_FONT_ID, curY + 48, "Re-index the book to resolve this.");
        curY += 80;
    }

    // --- 6. Render Bottom Messages Block ---
    if (!storyUrl.empty()) {
        curY += gapQrToBottom;

        const auto urlText = renderer.truncatedText(UI_10_FONT_ID, storyUrl.c_str(), pageWidth - 40);
        renderer.drawCenteredText(UI_10_FONT_ID, curY, urlText.c_str());
        curY += urlH + 10;

        renderer.drawCenteredText(SMALL_FONT_ID, curY, "Scan with your phone to open the AO3 page");
    }

    // --- Button hints ---
    const char* confirmLabel = filePath.empty() ? "" : tr(STR_OPEN);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

    renderer.displayBuffer();
}