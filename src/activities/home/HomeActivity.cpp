#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../../Ao3Librarian.h"

int HomeActivity::getMenuItemCount() const {
  int count = 4;  // File Browser, Recents, File transfer, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
if (hasOpdsServers || hasAo3Library) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  // Pass 1: Add pinned books
  for (const RecentBook& book : books) {
    if (recentBooks.size() >= maxBooks) {
      break;
    }

if (book.pinned) {
      if (RecentBooksStore::isMissing(book)) {
        continue;
      }
      recentBooks.push_back(book);
    }
  }

  // Pass 2: Add unpinned books
  for (const RecentBook& book : books) {
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    if (!book.pinned) {
      if (!Storage.exists(book.path.c_str())) {
        continue;
      }
      recentBooks.push_back(book);
    }
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          // 1. Build the cache if missing in an isolated scope
          //    This runs any AO3 scraping and frees all heavy memory immediately upon exit
          {
            Epub tempEpub(book.path, "/.crosspoint");
            tempEpub.load(true, true, true);
          }

          // 2. Open a fresh instance to generate the thumbnail with a clean heap
          Epub epub(book.path, "/.crosspoint");
          epub.load(false, true, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

hasAo3Library = true; 

  selectorIndex = 0;
  recentsLoaded = false;
  recentsLoading = false;
  firstRenderDone = false;
  coverRendered = false;
  coverBufferStored = false;
  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  const auto base = static_cast<int>(recentBooks.size());
  selectorIndex = 0;

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();

  if (SETTINGS.uiTheme == CrossPointSettings::LYRA_3_COVERS) {
    const int booksCount = static_cast<int>(recentBooks.size());

    // Horizontal (Front buttons) - Linear (Release) | Jump to Menu (Hold Right)
    buttonNavigator.onRelease(ButtonNavigator::getFrontNextButtons(), [this, menuCount] {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
      requestUpdate();
    });

    buttonNavigator.onFrontNextContinuous([this, booksCount, menuCount] {
      if (selectorIndex < booksCount) {
        selectorIndex = booksCount;  // Jump to menu
      } else {
        selectorIndex = (selectorIndex + 2) % menuCount;
      }
      requestUpdate();
    });

    buttonNavigator.onRelease(ButtonNavigator::getFrontPreviousButtons(), [this, menuCount] {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      requestUpdate();
    });

    buttonNavigator.onFrontPreviousContinuous([this, menuCount] {
      selectorIndex = (selectorIndex - 2 + menuCount) % menuCount;
      requestUpdate();
    });

    // Vertical (Side buttons) - Section jumping
    buttonNavigator.onRelease(ButtonNavigator::getSideNextButtons(), [this, booksCount, menuCount] {
      if (selectorIndex < booksCount) {
        selectorIndex = booksCount;  // Jump to menu
      } else {
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount); // Advance 1 item normally
      }
      requestUpdate();
    });

    buttonNavigator.onSideNextContinuous([this, booksCount, menuCount] {
      if (selectorIndex < booksCount) {
        selectorIndex = booksCount;  // Jump to menu
      } else {
        selectorIndex = (selectorIndex + 2) % menuCount; // Skip 2 menu entries
      }
      requestUpdate();
    });

    buttonNavigator.onRelease(ButtonNavigator::getSidePreviousButtons(), [this, booksCount, menuCount] {
      if (selectorIndex == booksCount && booksCount > 0) {
        selectorIndex = 0;  // Jump to first book
      } else {
        selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      }
      requestUpdate();
    });

    buttonNavigator.onSidePreviousContinuous([this, booksCount, menuCount] {
      if (selectorIndex == booksCount && booksCount > 0) {
        selectorIndex = 0;  // Jump to first book
      } else {
        selectorIndex = (selectorIndex - 2 + menuCount) % menuCount;
      }
      requestUpdate();
    });
  } else {
    // Original linear behavior for Classic/Lyra themes
    buttonNavigator.onNextRelease([this, menuCount] {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
      requestUpdate();
    });

    buttonNavigator.onPreviousRelease([this, menuCount] {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      requestUpdate();
    });
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
// Calculate dynamic indices based on which options are available
    int idx = 0;
    int menuSelectedIndex = selectorIndex - static_cast<int>(recentBooks.size());
    const int libraryHubIdx = (hasOpdsServers || hasAo3Library) ? idx++ : -1;
    const int recentsIdx = idx++;
    const int fileBrowserIdx = idx++;
    const int fileTransferIdx = idx++;
    const int settingsIdx = idx;

    if (selectorIndex < recentBooks.size()) {
      onSelectBook(recentBooks[selectorIndex].path);
    } else if (menuSelectedIndex == fileBrowserIdx) {
      onFileBrowserOpen();
    } else if (menuSelectedIndex == recentsIdx) {
      onRecentsOpen();
    } else if (menuSelectedIndex == libraryHubIdx) {
      onAo3LibraryOpen();
    } else if (menuSelectedIndex == fileTransferIdx) {
      onFileTransferOpen();
    } else if (menuSelectedIndex == settingsIdx) {
      onSettingsOpen();
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (selectorIndex < static_cast<int>(recentBooks.size())) {
      const RecentBook& selectedBook = recentBooks[selectorIndex];
      const int maxPinned = UITheme::getInstance().getMetrics().homeRecentBooksCount;
      if (selectedBook.pinned || RECENT_BOOKS.getPinnedCount() < maxPinned) {
        std::string toggledPath = selectedBook.path;
        RECENT_BOOKS.togglePinned(toggledPath);
        loadRecentBooks(maxPinned);

        // Track the book's new position so the selection cursor follows it
        for (int i = 0; i < static_cast<int>(recentBooks.size()); ++i) {
          if (recentBooks[i].path == toggledPath) {
            selectorIndex = i;
            break;
          }
        }

        // Invalidate the cover buffer so it re-renders in the correct order
        freeCoverBuffer();
        coverRendered = false;

        requestUpdate();
      }
    }
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);

  // Record the tile rect so storeCoverBuffer (called from the theme) knows
  // which sub-region of the framebuffer to snapshot. ~16 KB in Portrait
  // instead of the 48 KB full framebuffer the previous bind captured.
  coverRectX = 0;
  coverRectY = metrics.homeTopPadding;
  coverRectW = pageWidth;
  coverRectH = metrics.homeCoverTileHeight;

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  // Build menu items dynamically
  std::vector<const char*> menuItems = {tr(STR_MENU_RECENT_BOOKS), tr(STR_BROWSE_FILES), tr(STR_FILE_TRANSFER),
                                        tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Recent, Folder, Transfer, Settings};

  if (hasOpdsServers && hasAo3Library) {
    menuItems.insert(menuItems.begin(), "Libraries");
    menuIcons.insert(menuIcons.begin(), Library);
  } else if (hasOpdsServers) {
    menuItems.insert(menuItems.begin(), tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin(), Library);
  } else if (hasAo3Library) {
    menuItems.insert(menuItems.begin(), "AO3 Library");
    menuIcons.insert(menuIcons.begin(), Library);
  }

  if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
    // Insert Continue Reading at the top if enabled in theme
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
    menuIcons.insert(menuIcons.begin(), Book);
  }

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset, pageWidth,
           pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing +
                         metrics.homeMenuTopOffset + metrics.buttonHintsHeight)},
      static_cast<int>(menuItems.size()),
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  const char* backLabel = "";
  if (selectorIndex < static_cast<int>(recentBooks.size())) {
    if (recentBooks[selectorIndex].pinned) {
      backLabel = tr(STR_UNPIN);
    } else if (RECENT_BOOKS.getPinnedCount() < metrics.homeRecentBooksCount) {
      backLabel = tr(STR_PIN);
    }
  }

  const auto labels = mappedInput.mapLabels(backLabel, tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
void HomeActivity::onAo3LibraryOpen() { activityManager.goToAo3Library(); }
