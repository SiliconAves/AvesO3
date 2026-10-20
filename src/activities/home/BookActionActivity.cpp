#include "BookActionActivity.h"
#include "../Ao3ArchiveHelper.h"

#include <Epub.h>
#include <HalStorage.h>
#include <I18n.h>
#include <FsHelpers.h>

#include "components/UITheme.h"
#include "activities/util/ConfirmationActivity.h"
#include "Ao3IndexActivity.h"
#include "Ao3Librarian.h"
#include "../Ao3PageQrActivity.h"
#include "Ao3NewChaptersStore.h"
#include "Ao3WipsStore.h"
#include "Ao3MarkedForLaterStore.h"

namespace fui = freeink::ui;

// ---------------------------------------------------------------------------
//  Constructor
// ---------------------------------------------------------------------------

BookActionActivity::BookActionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       std::string filePath, std::string fileName,
                                       BookActionMode mode)
    : UiListActivity("BookAction", renderer, mappedInput, false), // wantsTouchLongPress = false
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
//   6 = Show AO3 Page QR        (AO3 only)
// ---------------------------------------------------------------------------

int BookActionActivity::logicalRow(int visual) const {
  if (mode == BookActionMode::DASHBOARD) {
    int idx = 0;
    if (visual == idx) return 0;  // Status cycle
    idx++;
    if (hasAo3LibraryInfo) {
      if (visual == idx) return 6;  // Show AO3 Page QR (below Status cycle)
      idx++;
    }
    if (visual == idx) return 5;  // Remove from List
    return -1;
  }

  // FULL mode
  int idx = 0;
  if (isEpub || isXtc)   { if (visual == idx) return 0; idx++; } // Status cycle
  if (hasAo3LibraryInfo) { if (visual == idx) return 1; idx++; } // Mark for Later
  if (hasAo3LibraryInfo) { if (visual == idx) return 6; idx++; } // Show AO3 Page QR
  if (isEpub && !isAlreadyArchived) { if (visual == idx) return 2; idx++; } // Index Book
  if (hasAo3LibraryInfo) { if (visual == idx) return 3; idx++; } // Move to Read Folder / Restore
  if (visual == idx)     return 4;                               // Delete
  return -1;
}

int BookActionActivity::listCount() const {
  if (mode == BookActionMode::DASHBOARD) {
    int n = 2; // Status cycle + Remove from List
    if (hasAo3LibraryInfo) n++; // Show AO3 Page QR
    return n;
  }

  int n = 1;                      // Delete always present
  if (isEpub || isXtc)      n++;  // Status
  if (hasAo3LibraryInfo)    n++;  // Mark for Later
  if (hasAo3LibraryInfo)    n++;  // Show AO3 Page QR
  if (isEpub && !isAlreadyArchived) n++;  // Index Book
  if (hasAo3LibraryInfo)    n++;  // Move to Read Folder / Restore
  return n;
}

// ---------------------------------------------------------------------------
//  onEnter
// ---------------------------------------------------------------------------

void BookActionActivity::onEnter() {
  UiListActivity::onEnter();

  std::string cachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(filePath));
  HalFile f;
   if (Storage.openFileForRead("BROWSER", cachePath + "/progress.bin", f)) {
    uint8_t data[11];
    int dataSize = f.read(data, sizeof(data));
    f.close();
    if (dataSize == 7)  { currentStatus = static_cast<BookStatus>(data[6]);  initialStatus = currentStatus; }
    if (dataSize == 11) { currentStatus = static_cast<BookStatus>(data[10]); initialStatus = currentStatus; }
  }

  hasAo3LibraryInfo = Storage.exists((cachePath + "/ao3_library_info").c_str());
  isAlreadyArchived = Ao3ArchiveHelper::isInReadFolder(filePath);
  isEpub = FsHelpers::hasEpubExtension(filePath);
  isXtc  = FsHelpers::hasXtcExtension(filePath);
  
  if (MARKED_FOR_LATER_STORE.getCount() == 0) {
    MARKED_FOR_LATER_STORE.loadFromFile();
  }
  isMarkedForLater = MARKED_FOR_LATER_STORE.contains(filePath);

  const int maxIdx = listCount() - 1;
  if (nav.selected > maxIdx) nav.selected = maxIdx;

  // Skip dimmed Status row if MARKED_FOR_LATER
  if (logicalRow(nav.selected) == 0 && currentStatus == BookStatus::MARKED_FOR_LATER)
    nav.selected = std::min(nav.selected + 1, maxIdx);

  // Swallow the Confirm release that fired the long-press in the Dashboard
  skipFirstConfirmRelease = (mode == BookActionMode::DASHBOARD);

  requestUpdate(true);
}

// ---------------------------------------------------------------------------
//  UI Builders & Overrides
// ---------------------------------------------------------------------------

void BookActionActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BookActionActivity::buildScreen(UiScreen& uiScreen) {
  fui::ListProps props;
  syncListViewport(uiScreen, props, false);

  int count = listCount();
  std::vector<fui::ListItem> items(count);
  std::vector<std::string> titles(count);
  std::vector<std::string> values(count);

  for (int i = 0; i < count; ++i) {
    switch (logicalRow(i)) {
      case 0: titles[i] = std::string("Book Status: ") + getStatusLabel(currentStatus); break;
      case 1: titles[i] = isMarkedForLater ? "Remove from Later List" : "Mark for Later"; break;
      case 2: titles[i] = hasAo3LibraryInfo ? "Reindex Book" : "Index Book"; break;
      case 3: titles[i] = isAlreadyArchived ? "Restore to AO3 Library" : "Move to Read Folder"; break;
      case 4: titles[i] = std::string(tr(STR_DELETE)); break;
      case 5: titles[i] = "Remove from List"; break;
      case 6: titles[i] = "Show AO3 Page QR"; break;
    }

    if (logicalRow(i) == 1) {
      values[i] = std::to_string(MARKED_FOR_LATER_STORE.getCount()) + "/10";
    }

    items[i].label = titles[i].c_str();
    if (!values[i].empty()) items[i].value = values[i].c_str();

    // The 1.6 framework natively dims the row grey and disables touches if enabled = false
    if (logicalRow(i) == 0 && currentStatus == BookStatus::MARKED_FOR_LATER) {
      items[i].enabled = false;
    }
  }

  props.valueText = uiScreen.theme().smallText;
  props.items = items.data();
  props.count = count;
  uiScreen.list(props);
}

// ---------------------------------------------------------------------------
//  Input Handlers
// ---------------------------------------------------------------------------

bool BookActionActivity::handleCustomInput() {
  // Swallow the Confirm release that opened this menu via long-press
  if (skipFirstConfirmRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm))
      skipFirstConfirmRelease = false;
    // Still allow Back so the user can dismiss immediately
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return true;
  }
  return false;
}

bool BookActionActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    saveStatusIfModified();
    BookActionResult res;
    res.modified  = true;
    res.newStatus = currentStatus;
    setResult(ActivityResult(std::move(res)));
    finish();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }
  return false;
}

void BookActionActivity::navigateButtons() {
  // We override navigateButtons to natively support skipping the disabled row
  auto nextVisual = [this](int current) -> int {
    const int total = listCount();
    if (total == 0) return 0;
    int next = (current + 1) % total;
    if (logicalRow(next) == 0 && currentStatus == BookStatus::MARKED_FOR_LATER)
      next = (next + 1) % total;
    return next;
  };

  auto prevVisual = [this](int current) -> int {
    const int total = listCount();
    if (total == 0) return 0;
    int prev = (current + total - 1) % total;
    if (logicalRow(prev) == 0 && currentStatus == BookStatus::MARKED_FOR_LATER)
      prev = (prev + total - 1) % total;
    return prev;
  };

  buttonNavigator.onNext([this, nextVisual] {
    nav.selected = nextVisual(nav.selected);
    nav.follow(listCount());
    requestUpdate(true);
  });

  buttonNavigator.onPrevious([this, prevVisual] {
    nav.selected = prevVisual(nav.selected);
    nav.follow(listCount());
    requestUpdate(true);
  });
}

// ---------------------------------------------------------------------------
//  Row Activation Logic
// ---------------------------------------------------------------------------

void BookActionActivity::activateIndex(int index) {
  switch (logicalRow(index)) {
    case 0: {
      if (currentStatus == BookStatus::MARKED_FOR_LATER) break;
      uint8_t s = (static_cast<uint8_t>(currentStatus) + 1) % 5;
      currentStatus = static_cast<BookStatus>(s);
      requestUpdate(true);
      break;
    }

    case 1: {
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
      saveStatusIfModified();
      auto handler = [this](const ActivityResult& res) {
        if (const auto* indexRes = std::get_if<Ao3IndexResult>(&res.data)) {
          if (indexRes->successfullyIndexed) {
            BookActionResult result;
            result.modified          = true;
            result.newStatus         = currentStatus;
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
          saveStatusIfModified();
          const std::string restoredPath = Ao3ArchiveHelper::restoreFic(filePath);
          if (!restoredPath.empty()) {
              auto handler = [this](const ActivityResult& res) {
                  BookActionResult result;
                  result.modified = true;
                  result.newStatus = currentStatus;
                  result.indexingCompleted = true;
                  setResult(ActivityResult(std::move(result)));
                  finish();
              };
              startActivityForResult(
                  std::make_unique<Ao3IndexActivity>(
                  renderer, mappedInput, Ao3IndexMode::SINGLE, restoredPath),
                  handler);
          } else {
            startActivityForResult(
                std::make_unique<ConfirmationActivity>(
                    renderer, mappedInput,
                    "Cannot Restore: Outdated Metadata",
                    "Reindex /read, or move the file to your AO3 folder."),
                [this](const ActivityResult&) { requestUpdate(true); });
          }
      } else {
        auto handler = [this](const ActivityResult& res) {
          if (!res.isCancelled) {
              saveStatusIfModified();
              const std::string newPath = Ao3ArchiveHelper::archiveFic(filePath);
              if (!newPath.empty()) {
                  BookActionResult result;
                  result.modified = true;
                  result.archived = true;
                  result.newPath  = newPath;
                  result.newStatus = currentStatus;
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
          saveStatusIfModified();
          BookActionResult result;
          result.deleted  = true;
          result.modified = true;
          result.newStatus = currentStatus;
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
      saveStatusIfModified();
      BookActionResult res;
      res.removedFromList = true;
      res.modified        = true;
      res.newStatus       = currentStatus;
      setResult(ActivityResult(std::move(res)));
      finish();
      break;
    }

    case 6: {
      startActivityForResult(
          std::make_unique<Ao3PageQrActivity>(renderer, mappedInput, filePath),
          [this](const ActivityResult&) { requestUpdate(true); });
      break;
    }

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
//  Status Persistence
// ---------------------------------------------------------------------------

void BookActionActivity::saveStatus() {
  std::string cachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(filePath));
  HalFile f;

  uint8_t data[11] = {0};
  data[10] = static_cast<uint8_t>(currentStatus);  // byte 10 = status
  if (Storage.openFileForRead("BROWSER", cachePath + "/progress.bin", f)) {
    f.read(data, 10);  // preserve bytes 0-9
    f.close();
  }
  data[10] = static_cast<uint8_t>(currentStatus);
  if (Storage.openFileForWrite("BROWSER", cachePath + "/progress.bin", f)) {
    f.write(data, 11);
    f.close();
  }

  if (hasAo3LibraryInfo) {
    bool isNowFinished = (currentStatus == BookStatus::FINISHED);
    bool wasFinished   = (initialStatus  == BookStatus::FINISHED);
    if (isNowFinished != wasFinished)
      Ao3Librarian::setRecordFinished(filePath, isNowFinished);
  }

  if (currentStatus == BookStatus::MARKED_FOR_LATER) {
    initialStatus = currentStatus;
    return;
  }

  if (currentStatus != BookStatus::READING) {
    MARKED_FOR_LATER_STORE.loadFromFile();
    MARKED_FOR_LATER_STORE.removeByPath(filePath);
    MARKED_FOR_LATER_STORE.saveToFile();
  }
    
  if (currentStatus == BookStatus::NEW_CHAPTER_AVAILABLE) {
    Epub epub(filePath, "/.crosspoint");
    epub.load(false, true);
    NEW_CHAPTERS_STORE.loadFromFile();
    NEW_CHAPTERS_STORE.addBook(filePath, epub.getTitle(), epub.getAuthor());
    NEW_CHAPTERS_STORE.saveToFile();
    AO3_WIPS_STORE.loadFromFile();
    AO3_WIPS_STORE.removeBook(filePath);
    AO3_WIPS_STORE.saveToFile();
  } else if (currentStatus == BookStatus::WAITING_FOR_CHAPTER || currentStatus == BookStatus::FINISHED) {
    NEW_CHAPTERS_STORE.loadFromFile();          
    NEW_CHAPTERS_STORE.removeByPath(filePath);
    NEW_CHAPTERS_STORE.saveToFile();
    if (currentStatus == BookStatus::WAITING_FOR_CHAPTER) {
      Epub epub(filePath, "/.crosspoint");
      epub.load(false, true);
      AO3_WIPS_STORE.loadFromFile();
      AO3_WIPS_STORE.addBook(filePath, epub.getTitle(), epub.getAuthor());
      AO3_WIPS_STORE.saveToFile();
    } else {
      AO3_WIPS_STORE.loadFromFile();
      AO3_WIPS_STORE.removeBook(filePath);
      AO3_WIPS_STORE.saveToFile();
    }
  }

  initialStatus = currentStatus;
}

void BookActionActivity::saveStatusIfModified() {
  if (currentStatus != initialStatus) {
    saveStatus();
  }
}