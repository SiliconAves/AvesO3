#include "Ao3EndOfBookActivity.h"

#include <variant>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "I18n.h"
#include "ReaderUtils.h"
#include "EpubReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "HalStorage.h"

#include "Ao3Librarian.h"
#include "Ao3MarkedForLaterStore.h"
#include "activities/network/AO3SyncActivity.h"
#include "Ao3EndOfBookSeriesActivity.h"
#include "../home/Ao3LibraryActivity.h"

namespace {
inline uint32_t fnv1a_local(const char* text) {
  uint32_t hash = 2166136261u;
  while (*text) {
    hash ^= static_cast<uint8_t>(*text++);
    hash *= 16777619u;
  }
  return hash;
}
} // namespace

Ao3EndOfBookActivity::Ao3EndOfBookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           std::string bookPath, bool isCompleted,
                                           std::string seriesName, int seriesPart,
                                           std::string ao3WorkId, std::string ao3UpdateDate,
                                           int spineCountBeforeDownload, bool hadAfterword,
                                           int ao3LibraryReturnIndex)
    : Activity("Ao3EndOfBook", renderer, mappedInput),
      bookPath(std::move(bookPath)),
      isCompleted(isCompleted),
      seriesName(std::move(seriesName)),
      seriesPart(seriesPart),
      ao3WorkId(std::move(ao3WorkId)),
      ao3UpdateDate(std::move(ao3UpdateDate)),
      spineCountBeforeDownload(spineCountBeforeDownload),
      hadAfterword(hadAfterword),
      ao3LibraryReturnIndex(ao3LibraryReturnIndex) {}

void Ao3EndOfBookActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void Ao3EndOfBookActivity::loop() {
  // 1. Handle Update Button (Confirm)
  if (!isCompleted && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const auto path = bookPath;
    const auto spineCount = spineCountBeforeDownload;
    const auto afterword = hadAfterword;
    
    startActivityForResult(std::make_unique<AO3SyncActivity>(renderer, mappedInput, ao3WorkId, ao3UpdateDate, path),
      [path, spineCount, afterword](const ActivityResult& res) {
        auto epub = std::make_unique<Epub>(path, "/.crosspoint");

        if (!res.isCancelled && std::holds_alternative<AO3Result>(res.data)) {
          const auto& ao3Res = std::get<AO3Result>(res.data);
          if (ao3Res.downloaded) {
            epub->saveAo3Info(epub->getAo3WorkId(), ao3Res.scrapedDate, ao3Res.isCompleted);
            std::string cachePath = epub->getCachePath();
            Storage.remove((cachePath + "/book.bin").c_str());
            Storage.removeDir((cachePath + "/sections").c_str());
            Storage.removeDir((cachePath + "/html").c_str());

            MARKED_FOR_LATER_STORE.loadFromFile();
            MARKED_FOR_LATER_STORE.removeByPath(path);
            MARKED_FOR_LATER_STORE.clearEntries();

            int firstNewChapter = spineCount;
            if (afterword && spineCount > 0) {
              firstNewChapter -= 1;
            }
            EpubReaderUtils::saveProgress(*epub, firstNewChapter, 0, spineCount, std::nullopt, BookStatus::NEW_CHAPTER_AVAILABLE);
          } else {
            if (ao3Res.updateFound) {
              EpubReaderUtils::saveProgress(*epub, spineCount, 0, spineCount, std::nullopt, BookStatus::NEW_CHAPTER_AVAILABLE);
            } else {
              EpubReaderUtils::saveProgress(*epub, spineCount, 0, spineCount, std::nullopt, BookStatus::WAITING_FOR_CHAPTER);
            }
          }
        } else {
          EpubReaderUtils::saveProgress(*epub, spineCount, 0, spineCount, std::nullopt, BookStatus::WAITING_FOR_CHAPTER);
        }
        activityManager.goHome();
      });
    return;
  }

  // 2. Handle Series / Library Exits (Back)
  const bool backPressed = mappedInput.isPressed(MappedInputManager::Button::Back);
  const bool backReleased = mappedInput.wasReleased(MappedInputManager::Button::Back);
  const unsigned long backHeldTime = mappedInput.getHeldTime();
  const bool hasSeries = !seriesName.empty();

  if (hasSeries && backReleased && backHeldTime < ReaderUtils::GO_BACK_OR_HOME_MS) {
    launchSeriesActivity();
    return;
  }
  if (backPressed && backHeldTime >= ReaderUtils::GO_HOME_MS) {
    if (SETTINGS.backShortToFileBrowser && !hasSeries) onGoHome();
    else returnToLibraryOrBrowser();
    return;
  }
  if (backReleased && backHeldTime < ReaderUtils::GO_BACK_OR_HOME_MS) {
    if (SETTINGS.backShortToFileBrowser) returnToLibraryOrBrowser();
    else onGoHome();
    return;
  }

  // 3. Handle Page Turn Back to Book
  auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  auto [prevTrig, nextTrig, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);

  if (prevTrig || touch.prev) {
    // Calling finish pops this activity off the stack and wakes up EpubReaderActivity
    finish();
    return;
  }
  if (nextTrig || touch.next) {
    onGoHome();
    return;
  }
}

void Ao3EndOfBookActivity::render(RenderLock&& lock) {
  renderer.clearScreen();
  
  const int lh12 = renderer.getLineHeight(UI_12_FONT_ID);
  const int lh10 = renderer.getLineHeight(UI_10_FONT_ID);
  const bool hasSeries = !seriesName.empty();

  if (!isCompleted) {
    const int mainY = 280;
    renderer.drawCenteredText(UI_12_FONT_ID, mainY,      tr(STR_AO3_END_REACHED),      true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_12_FONT_ID, mainY + 40, tr(STR_AO3_LOOK_FOR_UPDATES), true, EpdFontFamily::BOLD);
    if (hasSeries) {
      char seriesBuf[160];
      snprintf(seriesBuf, sizeof(seriesBuf), "Book %d of %s", seriesPart, seriesName.c_str());
      int seriesY = mainY + 40 + lh12 + lh10;
      for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, seriesBuf, renderer.getScreenWidth() - 40, 2)) {
        renderer.drawCenteredText(UI_10_FONT_ID, seriesY, line.c_str());
        seriesY += lh10;
      }
    }
    const auto labels = mappedInput.mapLabels(
        hasSeries ? tr(STR_EOB_SERIES_BUTTON) : "", tr(STR_AO3_SEARCH), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  } else {
    const int mainY = 300;
    renderer.drawCenteredText(UI_12_FONT_ID, mainY, tr(STR_END_OF_BOOK), true, EpdFontFamily::BOLD);
    if (hasSeries) {
      char seriesBuf[160];
      snprintf(seriesBuf, sizeof(seriesBuf), "Book %d of %s", seriesPart, seriesName.c_str());
      int seriesY = mainY + lh12 + lh10;
      for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, seriesBuf, renderer.getScreenWidth() - 40, 2)) {
        renderer.drawCenteredText(UI_10_FONT_ID, seriesY, line.c_str());
        seriesY += lh10;
      }
      const auto labels = mappedInput.mapLabels(hasSeries ? tr(STR_EOB_SERIES_BUTTON) : "", "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  }
  
  renderer.displayBuffer();
}

void Ao3EndOfBookActivity::launchSeriesActivity() {
  const uint32_t originHash = static_cast<uint32_t>(std::hash<std::string>{}(bookPath));
  char truncatedName[32];
  strncpy(truncatedName, seriesName.c_str(), 31);
  truncatedName[31] = '\0';
  const uint32_t seriesHash = fnv1a_local(truncatedName);

  activityManager.replaceActivity(std::make_unique<Ao3EndOfBookSeriesActivity>(
      renderer, mappedInput,
      seriesName,
      seriesHash,
      originHash,
      bookPath));
}

void Ao3EndOfBookActivity::returnToLibraryOrBrowser() {
  if (ao3LibraryReturnIndex >= 0) {
    activityManager.goToAo3Library(static_cast<size_t>(ao3LibraryReturnIndex));
  } else {
    activityManager.goToFileBrowser(bookPath);
  }
}