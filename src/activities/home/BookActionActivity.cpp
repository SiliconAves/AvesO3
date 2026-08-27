#include "BookActionActivity.h"
#include "../Ao3ArchiveHelper.h"

#include <Epub.h>
#include <HalStorage.h>
#include <I18n.h>
#include <FsHelpers.h>

#include "../../components/UITheme.h"
#include "../util/ConfirmationActivity.h"
#include "Ao3IndexActivity.h"
#include "../../Ao3Librarian.h"
#include "Ao3NewChaptersStore.h"
#include "Ao3WipsStore.h"
#include "Ao3MarkedForLaterStore.h"

// ---------------------------------------------------------------------------
//  Constructor
// ---------------------------------------------------------------------------

BookActionActivity::BookActionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       std::string filePath, std::string fileName,
                                       BookActionMode mode)
    : Activity("BookAction", renderer, mappedInput),
      filePath(std::move(filePath)),
      fileName(std::move(fileName)),
      mode(mode) {}

// ---------------------------------------------------------------------------
//  Logical row constants
//   0 = Status cycle
//   1 = Mark for Later toggle   (FULL, AO3 only)
//   2 = Index Book              (FULL, epub only)
//   3 = Delete                  (FULL only)
//   5 = Remove from List        (DASHBOARD only)
// ---------------------------------------------------------------------------

int BookActionActivity::logicalRow(int visual) const {
  if (mode == BookActionMode::DASHBOARD) {
    if (visual == 0) return 0;  // Status cycle
    if (visual == 1) return 5;  // Remove from List
    return -1;
  }

  // FULL mode
  int idx = 0;
  if (isEpub || isXtc)                      { if (visual == idx) return 0; idx++; }
  if (hasAo3LibraryInfo)                    { if (visual == idx) return 1; idx++; }
  if (isEpub)                               { if (visual == idx) return 2; idx++; }
  if (hasAo3LibraryInfo)                    { if (visual == idx) return 3; idx++; }
  if (visual == idx)       return 4;
  return -1;
}

int BookActionActivity::visibleRowCount() const {
  if (mode == BookActionMode::DASHBOARD) return 2;

  int n = 1;                      // Delete always present
  if (isEpub || isXtc)      n++;  // Status
  if (hasAo3LibraryInfo)    n++;  // Mark for Later
  if (isEpub)               n++;  // Index Book
  if (hasAo3LibraryInfo)    n++;  // Move to Read Folder / Restore
  return n;
}

// ---------------------------------------------------------------------------
//  onEnter
// ---------------------------------------------------------------------------

void BookActionActivity::onEnter() {
  Activity::onEnter();

  std::string cachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(filePath));
  HalFile f;
  if (Storage.openFileForRead("BROWSER", cachePath + "/progress.bin", f)) {
    uint8_t data[7];
    if (f.read(data, 7) >= 7) {
      currentStatus = static_cast<BookStatus>(data[6]);
      initialStatus = currentStatus;
    }
    f.close();
  }

  hasAo3LibraryInfo = Storage.exists((cachePath + "/ao3_library_info").c_str());
  isAlreadyArchived = Ao3ArchiveHelper::isInReadFolder(filePath);
  isEpub = FsHelpers::hasEpubExtension(filePath);
  isXtc  = FsHelpers::hasXtcExtension(filePath);
  if (MARKED_FOR_LATER_STORE.getCount() == 0) {
    MARKED_FOR_LATER_STORE.loadFromFile();
  }
  isMarkedForLater = MARKED_FOR_LATER_STORE.contains(filePath);

  const int maxIdx = visibleRowCount() - 1;
  if (selectorIndex > maxIdx) selectorIndex = maxIdx;

  // Skip dimmed Status row if MARKED_FOR_LATER
  if (logicalRow(selectorIndex) == 0 && currentStatus == BookStatus::MARKED_FOR_LATER)
    selectorIndex = std::min(selectorIndex + 1, maxIdx);

  // Swallow the Confirm release that fired the long-press in the Dashboard
  skipFirstConfirmRelease = (mode == BookActionMode::DASHBOARD);

  requestUpdate(true);
}

// ---------------------------------------------------------------------------
//  render
// ---------------------------------------------------------------------------

void BookActionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer,
                 Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 fileName.c_str());

  auto rowTitle = [this](int index) -> std::string {
    switch (logicalRow(index)) {
      case 0: return std::string("Book Status: ") + getStatusLabel(currentStatus);
      case 1: return isMarkedForLater ? "Remove from Later List" : "Mark for Later";
      case 2: return hasAo3LibraryInfo ? "Reindex Book" : "Index Book";
      case 3: return isAlreadyArchived ? "Restore to AO3 Library" : "Move to Read Folder";
      case 4: return std::string(tr(STR_DELETE));
      case 5: return "Remove from List";
      default: return "";
    }
  };

  auto rowValue = [this](int index) -> std::string {
    if (logicalRow(index) == 1)
      return std::to_string(MARKED_FOR_LATER_STORE.getCount()) + "/10";
    return "";
  };

  auto rowDimmed = [this](int index) -> bool {
    return logicalRow(index) == 0 && currentStatus == BookStatus::MARKED_FOR_LATER;
  };

  GUI.drawList(renderer,
               Rect{0,
                    metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing,
                    renderer.getScreenWidth(),
                    renderer.getScreenHeight() - metrics.headerHeight -
                        metrics.buttonHintsHeight - metrics.verticalSpacing * 2},
               visibleRowCount(), selectorIndex,
               rowTitle, nullptr, nullptr, rowValue, true, rowDimmed);

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

// ---------------------------------------------------------------------------
//  loop
// ---------------------------------------------------------------------------

void BookActionActivity::loop() {
  // Swallow the Confirm release that opened this menu via long-press
  if (skipFirstConfirmRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm))
      skipFirstConfirmRelease = false;
    // Still allow Back so the user can dismiss immediately
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (currentStatus != initialStatus) {
      saveStatus();
      BookActionResult res;
      res.modified  = true;
      res.newStatus = currentStatus;
      setResult(ActivityResult(std::move(res)));
    }
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    switch (logicalRow(selectorIndex)) {

      case 0: {
        if (currentStatus == BookStatus::MARKED_FOR_LATER) break;
        uint8_t s = (static_cast<uint8_t>(currentStatus) + 1) % 5;
        currentStatus = static_cast<BookStatus>(s);
        requestUpdate(true);
        break;
      }

      case 1: {
        // Mark for Later toggle (FULL only)
        if (isMarkedForLater) {
          MARKED_FOR_LATER_STORE.removeByPath(filePath);
          if (currentStatus == BookStatus::MARKED_FOR_LATER) {
            currentStatus = BookStatus::START;
            saveStatus();
          }
          isMarkedForLater = false;
        } else if (MARKED_FOR_LATER_STORE.isFull()) {
          // cap visible in "10/10" pill — no-op
        } else {
          Epub epub(filePath, "/.crosspoint");
          epub.load(false, true);
          MARKED_FOR_LATER_STORE.addBook(filePath, epub.getTitle(), epub.getAuthor());
          currentStatus   = BookStatus::MARKED_FOR_LATER;
          saveStatus();
          isMarkedForLater = true;
        }
        requestUpdate(true);
        break;
      }

      case 2: {
        auto handler = [this](const ActivityResult& res) {
          if (const auto* indexRes = std::get_if<Ao3IndexResult>(&res.data)) {
            if (indexRes->successfullyIndexed) {
              BookActionResult result;
              result.modified          = true;
              result.indexingCompleted = true;
              setResult(ActivityResult(std::move(result)));
              finish();
              return;
            }
          }
          requestUpdate(true);
        };
        startActivityForResult(
            std::make_unique<Ao3IndexActivity>(renderer, mappedInput, Ao3IndexMode::SINGLE, filePath),
            handler);
        break;
      }

      case 3: {
        if (isAlreadyArchived) {
            // Restore: move file back then re-index.
            const std::string restoredPath = Ao3ArchiveHelper::restoreFic(filePath);
            if (!restoredPath.empty()) {
                auto handler = [this](const ActivityResult& res) {
                    BookActionResult result;
                    result.modified = true;
                    result.indexingCompleted = true;
                    setResult(ActivityResult(std::move(result)));
                    finish();
                };
                    startActivityForResult(
                    std::make_unique<Ao3IndexActivity>(
                    renderer, mappedInput, Ao3IndexMode::SINGLE, restoredPath),
                    handler);
            } else {
              requestUpdate(true);  // restore failed, stay open
            }
        } else {
          // Archive: move to read folder.
          auto handler = [this](const ActivityResult& res) {
            if (!res.isCancelled) {
                const std::string newPath = Ao3ArchiveHelper::archiveFic(filePath);
                if (!newPath.empty()) {
                    BookActionResult result;
                    result.modified = true;
                    result.archived = true;
                    result.newPath  = newPath;
                    setResult(ActivityResult(std::move(result)));
                    finish();
                } else {
                    requestUpdate(true);
                }
            } else {
                requestUpdate(true);
            }
          };
          startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                "Move to Read Folder?", "The fic will leave the AO3 Library."),
                handler);
        }
        break;
      }

      case 4: {
        auto handler = [this](const ActivityResult& res) {
          if (!res.isCancelled) {
            BookActionResult result;
            result.deleted  = true;
            result.modified = true;
            setResult(ActivityResult(std::move(result)));
            finish();
          } else {
            requestUpdate(true);
          }
        };
        std::string heading = std::string(tr(STR_DELETE)) + "?";
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, fileName),
            handler);
        break;
      }

      case 5: {
        // Remove from List — no confirmation, caller handles store eviction
        BookActionResult res;
        res.removedFromList = true;
        setResult(ActivityResult(std::move(res)));
        finish();
        break;
      }

      default:
        break;
    }
    return;
  }

  auto nextVisual = [this](int current) -> int {
    const int total = visibleRowCount();
    int next = (current + 1) % total;
    if (logicalRow(next) == 0 && currentStatus == BookStatus::MARKED_FOR_LATER)
      next = (next + 1) % total;
    return next;
  };

  auto prevVisual = [this](int current) -> int {
    const int total = visibleRowCount();
    int prev = (current + total - 1) % total;
    if (logicalRow(prev) == 0 && currentStatus == BookStatus::MARKED_FOR_LATER)
      prev = (prev + total - 1) % total;
    return prev;
  };

  buttonNavigator.onNext([this, &nextVisual] {
    selectorIndex = nextVisual(selectorIndex);
    requestUpdate(true);
  });

  buttonNavigator.onPrevious([this, &prevVisual] {
    selectorIndex = prevVisual(selectorIndex);
    requestUpdate(true);
  });
}

// ---------------------------------------------------------------------------
//  saveStatus — unchanged from starting state
// ---------------------------------------------------------------------------

void BookActionActivity::saveStatus() {
  std::string cachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(filePath));
  HalFile f;

  uint8_t data[7] = {0, 0, 0, 0, 0, 0, static_cast<uint8_t>(currentStatus)};
  if (Storage.openFileForRead("BROWSER", cachePath + "/progress.bin", f)) {
    f.read(data, 6);
    f.close();
  }
  if (Storage.openFileForWrite("BROWSER", cachePath + "/progress.bin", f)) {
    f.write(data, 7);
    f.close();
  }

  if (hasAo3LibraryInfo) {
    bool isNowFinished = (currentStatus == BookStatus::FINISHED);
    bool wasFinished   = (initialStatus  == BookStatus::FINISHED);
    if (isNowFinished != wasFinished)
      Ao3Librarian::setRecordFinished(filePath, isNowFinished);
  }

  if (currentStatus == BookStatus::MARKED_FOR_LATER) return;

  if (currentStatus != BookStatus::READING) {
    MARKED_FOR_LATER_STORE.loadFromFile();
    MARKED_FOR_LATER_STORE.removeByPath(filePath);
    MARKED_FOR_LATER_STORE.clearEntries();
  }
    
  if (currentStatus == BookStatus::NEW_CHAPTER_AVAILABLE) {
    Epub epub(filePath, "/.crosspoint");
    epub.load(false, true);
    NEW_CHAPTERS_STORE.loadFromFile();
    NEW_CHAPTERS_STORE.addBook(filePath, epub.getTitle(), epub.getAuthor());
    NEW_CHAPTERS_STORE.clearEntries();
    AO3_WIPS_STORE.loadFromFile();
    AO3_WIPS_STORE.removeBook(filePath);
    AO3_WIPS_STORE.clearEntries();
  } else if (currentStatus == BookStatus::WAITING_FOR_CHAPTER || currentStatus == BookStatus::FINISHED) {
    NEW_CHAPTERS_STORE.loadFromFile();          
    NEW_CHAPTERS_STORE.removeByPath(filePath);
    NEW_CHAPTERS_STORE.clearEntries();
    if (currentStatus == BookStatus::WAITING_FOR_CHAPTER) {
      Epub epub(filePath, "/.crosspoint");
      epub.load(false, true);
      AO3_WIPS_STORE.loadFromFile();
      AO3_WIPS_STORE.addBook(filePath, epub.getTitle(), epub.getAuthor());
      AO3_WIPS_STORE.clearEntries();
    } else {
      AO3_WIPS_STORE.loadFromFile();
      AO3_WIPS_STORE.removeBook(filePath);
      AO3_WIPS_STORE.clearEntries();
    }
  }
}