#include "BookActionActivity.h"

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
                                       std::string filePath, std::string fileName)
    : Activity("BookAction", renderer, mappedInput),
      filePath(std::move(filePath)),
      fileName(std::move(fileName)) {}

// ---------------------------------------------------------------------------
//  logicalRow / visibleRowCount helpers
// ---------------------------------------------------------------------------

int BookActionActivity::logicalRow(int visual) const {
  int idx = 0;

  // Row 0 — Status cycle (epub and xtc only)
  if (isEpub || isXtc) {
    if (visual == idx) return 0;
    idx++;
  }

  // Row 1 — Mark for Later toggle (AO3 books only)
  if (hasAo3LibraryInfo) {
    if (visual == idx) return 1;
    idx++;
  }

  // Row 2 — Index Book (epub only)
  if (isEpub) {
    if (visual == idx) return 2;
    idx++;
  }

  // Row 3 — Delete (always present)
  if (visual == idx) return 3;

  return -1;
}

int BookActionActivity::visibleRowCount() const {
  int n = 1;                      // Delete is always present
  if (isEpub || isXtc) n++;      // Status cycle
  if (hasAo3LibraryInfo) n++;    // Mark for Later toggle
  if (isEpub) n++;               // Index Book
  return n;
}

// ---------------------------------------------------------------------------
//  onEnter
// ---------------------------------------------------------------------------

void BookActionActivity::onEnter() {
  Activity::onEnter();

  // Load current status from progress.bin
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

  // Detect file type and current store membership
  isEpub = FsHelpers::hasEpubExtension(filePath);
  isXtc  = FsHelpers::hasXtcExtension(filePath);
  isMarkedForLater = MARKED_FOR_LATER_STORE.contains(filePath);

  // Clamp selectorIndex in case it was left from a different file
  const int maxIdx = visibleRowCount() - 1;
  if (selectorIndex > maxIdx) selectorIndex = maxIdx;

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
      case 3: return std::string(tr(STR_DELETE));
      default: return "";
    }
  };

  auto rowValue = [this](int index) -> std::string {
  if (logicalRow(index) == 1) {
    return std::to_string(MARKED_FOR_LATER_STORE.getCount()) + "/10";
  }
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
  // --- Back: commit status change if needed and exit ---
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

  // --- Confirm: act on the currently selected logical row ---
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    switch (logicalRow(selectorIndex)) {

      case 0: {
        // Status cycle — locked while MARKED_FOR_LATER; guard keeps it safe
        // even if the cursor somehow lands here.
        if (currentStatus == BookStatus::MARKED_FOR_LATER) break;
        uint8_t s = (static_cast<uint8_t>(currentStatus) + 1) % 5;
        currentStatus = static_cast<BookStatus>(s);
        requestUpdate(true);
        break;
      }

      case 1: {
        // Mark for Later toggle
        if (isMarkedForLater) {
          // Remove from store; if the status is still MARKED_FOR_LATER, revert
          // it to START so it no longer shows the special icon.
          MARKED_FOR_LATER_STORE.removeByPath(filePath);
          if (currentStatus == BookStatus::MARKED_FOR_LATER) {
            currentStatus = BookStatus::START;
            saveStatus();
          }
          isMarkedForLater = false;
        } else if (MARKED_FOR_LATER_STORE.isFull()) {
          // List is at capacity — the "10/10" pill is already visible; no-op.
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
        // Index / Reindex Book — launches Ao3IndexActivity in SINGLE mode
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
        // Delete — confirm before destroying file
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

      default:
        break;
    }
    return;
  }

  // --- Navigation: skip the dimmed Status row when MARKED_FOR_LATER ---

  // Returns the next visual index that the cursor should stop on.
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
//  saveStatus
// ---------------------------------------------------------------------------

void BookActionActivity::saveStatus() {
  std::string cachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(filePath));
  HalFile f;

  uint8_t data[7] = {0, 0, 0, 0, 0, 0, static_cast<uint8_t>(currentStatus)};

  // Preserve existing position bytes if the file already exists
  if (Storage.openFileForRead("BROWSER", cachePath + "/progress.bin", f)) {
    f.read(data, 6);
    f.close();
  }

  if (Storage.openFileForWrite("BROWSER", cachePath + "/progress.bin", f)) {
    f.write(data, 7);
    f.close();
  }

  // Sync the finished flag into the AO3 compact index (boundary crossing only)
  if (hasAo3LibraryInfo) {
    bool isNowFinished = (currentStatus == BookStatus::FINISHED);
    bool wasFinished   = (initialStatus  == BookStatus::FINISHED);
    if (isNowFinished != wasFinished) {
      Ao3Librarian::setRecordFinished(filePath, isNowFinished);
    }
  }

  // MARKED_FOR_LATER is managed exclusively by the toggle handler above.
  // The store insertion/removal already happened there, so we must not fire
  // the tab-store hooks for this status — return early.
  if (currentStatus == BookStatus::MARKED_FOR_LATER) {
    return;
  }

  // Tab store hooks — mirror status transitions into the Dashboard stores.
  if (currentStatus == BookStatus::NEW_CHAPTER_AVAILABLE) {
    Epub epub(filePath, "/.crosspoint");
    epub.load(false, true);
    NEW_CHAPTERS_STORE.addBook(filePath, epub.getTitle(), epub.getAuthor());
    AO3_WIPS_STORE.removeBook(filePath);
  } else if (currentStatus == BookStatus::WAITING_FOR_CHAPTER ||
             currentStatus == BookStatus::FINISHED) {
    NEW_CHAPTERS_STORE.removeByPath(filePath);
    if (currentStatus == BookStatus::WAITING_FOR_CHAPTER) {
      Epub epub(filePath, "/.crosspoint");
      epub.load(false, true);
      AO3_WIPS_STORE.addBook(filePath, epub.getTitle(), epub.getAuthor());
    } else {
      // FINISHED: evict from WIPs and Marked for Later
      AO3_WIPS_STORE.removeBook(filePath);
      MARKED_FOR_LATER_STORE.removeByPath(filePath);
    }
  }
}