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
  const auto& metrics = UITheme::getInstance().getMetrics();
  int menuItems = 0;
  if (menuPage == 0) {
    menuItems = 5;
    if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
      menuItems += 1;  // Continue Reading occupies slot 0; no separate book section
    }
  } else {
    menuItems = getPage1ItemCount();
    if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
      menuItems += 1;  // Continue Reading is also prepended on page 1
    }
  }
  // When Continue Reading is embedded in the menu there is no separate book
  // section, so do not add recentBooks.size() again.
  if (metrics.homeContinueReadingInMenu) {
    return menuItems;
  }
  return menuItems + static_cast<int>(recentBooks.size());
}

int HomeActivity::getPage1ItemCount() const {
  int count = 0;
  if (hasOpdsServers) count++;
  // future page-1 entries go here
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

  // hasOpdsServers = OPDS_STORE.hasServers();
  hasOpdsServers = true;
  menuPage = 0; // Reset to first menu page

  hasAo3Library = true; 

  backPressSeen = false;
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
  const auto& metrics = UITheme::getInstance().getMetrics();

auto activateSelection = [this] {
  if (selectorIndex < static_cast<int>(recentBooks.size())) {
    onSelectBook(recentBooks[selectorIndex].path);
    return;
  }

  const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());

  if (menuPage == 1) {
    // NOTE: selectorIndex=0 (Continue Reading) is already caught by the
    // early-return above (selectorIndex < recentBooks.size()). Do NOT prepend
    // it here — that would shift OPDS Browser to index 1 while menuIndex for
    // selectorIndex=1 computes to 0, causing OPDS to open the book instead.
    std::vector<std::function<void()>> page1Actions;
    if (hasOpdsServers) page1Actions.push_back([this] { onOpdsBrowserOpen(); });
    // future page-1 entries go here
    if (menuIndex >= 0 && menuIndex < static_cast<int>(page1Actions.size())) {
      page1Actions[menuIndex]();
    }
    return;
  }

  // Page 0
  std::vector<std::function<void()>> page0Actions = {
    [this] { onAo3LibraryOpen(); },
    [this] { onRecentsOpen(); },
    [this] { onFileBrowserOpen(); },
    [this] { onFileTransferOpen(); },
    [this] { onSettingsOpen(); }
  };

  // NOTE: when homeContinueReadingInMenu is true, selectorIndex=0 is already
  // handled by the early-return above (selectorIndex < recentBooks.size()).
  // Do NOT prepend a Continue Reading action here – that would shift every
  // subsequent action one slot forward and break them all.

  if (menuIndex >= 0 && menuIndex < static_cast<int>(page0Actions.size())) {
    page0Actions[menuIndex]();
  }
};

  if (SETTINGS.uiTheme == CrossPointSettings::LYRA_3_COVERS) {
    const int booksCount = static_cast<int>(recentBooks.size());

    // Horizontal (Front buttons) - Linear (Release) | Jump to Menu / Skip 2 (Hold)
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
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);  // Advance 1 item normally
      }
      requestUpdate();
    });

    buttonNavigator.onSideNextContinuous([this, booksCount, menuCount] {
      if (selectorIndex < booksCount) {
        selectorIndex = booksCount;  // Jump to menu
      } else {
        selectorIndex = (selectorIndex + 2) % menuCount;  // Skip 2 menu entries
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
    // Classic, Lyra, RoundedRaff: same hold-action shortcuts, mapped through
    // the unified NavNext/NavPrevious logical buttons (front + side combined).
    const int booksCount = static_cast<int>(recentBooks.size());

    // Release: step one item at a time
    buttonNavigator.onNextRelease([this, menuCount] {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
      requestUpdate();
    });

    buttonNavigator.onPreviousRelease([this, menuCount] {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      requestUpdate();
    });

    // Hold Next: jump from book section to menu, then skip 2 within menu
    buttonNavigator.onNextContinuous([this, booksCount, menuCount] {
      if (selectorIndex < booksCount) {
        selectorIndex = booksCount;  // Jump to menu
      } else {
        selectorIndex = (selectorIndex + 2) % menuCount;  // Skip 2 menu entries
      }
      requestUpdate();
    });

    // Hold Previous: jump from first menu item back to book, then skip 2 backwards
    buttonNavigator.onPreviousContinuous([this, booksCount, menuCount] {
      if (selectorIndex == booksCount && booksCount > 0) {
        selectorIndex = 0;  // Jump to first book
      } else {
        selectorIndex = (selectorIndex - 2 + menuCount) % menuCount;
      }
      requestUpdate();
    });
  }

  // v1.5 swipe support
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    backPressSeen = true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (backPressSeen) {
      backPressSeen = false;
      const int booksCount = static_cast<int>(recentBooks.size());
      if (selectorIndex < booksCount) {
        // Pin/unpin for recent books
        const RecentBook& selectedBook = recentBooks[selectorIndex];
        const int maxPinned = UITheme::getInstance().getMetrics().homeRecentBooksCount;
        if (selectedBook.pinned || RECENT_BOOKS.getPinnedCount() < maxPinned) {
          std::string toggledPath = selectedBook.path;
          RECENT_BOOKS.togglePinned(toggledPath);
          loadRecentBooks(maxPinned);
          for (int i = 0; i < static_cast<int>(recentBooks.size()); ++i) {
            if (recentBooks[i].path == toggledPath) { selectorIndex = i; break; }
          }
          freeCoverBuffer();
          coverRendered = false;
          requestUpdate();
        }
      } else {
        // Switch menu page
        const int targetPage = (menuPage == 0) ? 1 : 0;
        if (targetPage == 0 || getPage1ItemCount() > 0) {
          menuPage = targetPage;
          const auto& switchMetrics = UITheme::getInstance().getMetrics();
          // In RoundedRaff (homeContinueReadingInMenu) there is no separate
          // book section; the first menu slot is always Continue Reading.
          selectorIndex = (switchMetrics.homeContinueReadingInMenu && !recentBooks.empty())
                              ? 0
                              : booksCount;
          requestUpdate();
        }
      }
    }
    return;
  }

  if (!recentBooks.empty() &&
      mappedInput.wasTapInRect(0, metrics.homeTopPadding, renderer.getScreenWidth(), metrics.homeCoverTileHeight)) {
    selectorIndex = 0;
    activateSelection();
    return;
  }

  const int menuTop = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int renderedMenuSelection =
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size();
  const int renderedMenuCount =
      menuCount - (metrics.homeContinueReadingInMenu ? 0 : static_cast<int>(recentBooks.size()));
  int menuRow = -1;
  const auto menuTouch = mappedInput.rowTouch(menuRow, menuTop, metrics.menuRowHeight + metrics.menuSpacing,
                                              renderedMenuCount, 0, INT32_MAX, metrics.menuRowHeight);
  if (menuTouch != MappedInputManager::RowTouch::None) {
    const int touchedIndex =
        metrics.homeContinueReadingInMenu ? menuRow : menuRow + static_cast<int>(recentBooks.size());
    if (menuTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedIndex) {
        selectorIndex = touchedIndex;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedIndex;
      activateSelection();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
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

  std::vector<const char*> menuItems;
  std::vector<UIIcon> menuIcons;

  if (menuPage == 0) {
    menuItems = { "AO3 Library", "Dashboard", tr(STR_BROWSE_FILES), tr(STR_FILE_TRANSFER), tr(STR_SETTINGS_TITLE) };
    menuIcons = { Library, Recent, Folder, Transfer, Settings };
    if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
      menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
      menuIcons.insert(menuIcons.begin(), Book);
    }
  } else {
    // Page 1: prepend Continue Reading first (same as page 0) so the user
    // always sees it as the first entry regardless of which page they're on.
    if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
      menuItems.push_back(tr(STR_CONTINUE_READING));
      menuIcons.push_back(Book);
    }
    if (hasOpdsServers) {
      menuItems.push_back("OPDS Browser");
      menuIcons.push_back(Library); // swap for dedicated icon later
    }
    // future page-1 entries go here
  }

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset, pageWidth,
           pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing +
                         metrics.homeMenuTopOffset + metrics.buttonHintsHeight)},
      static_cast<int>(menuItems.size()),
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - static_cast<int>(recentBooks.size()),
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  const char* backLabel = "";
  if (selectorIndex < static_cast<int>(recentBooks.size())) {
    if (recentBooks[selectorIndex].pinned) {
      backLabel = tr(STR_UNPIN);
    } else if (RECENT_BOOKS.getPinnedCount() < metrics.homeRecentBooksCount) {
      backLabel = tr(STR_PIN);
    }
  } else {
    if (menuPage == 1 || getPage1ItemCount() > 0) backLabel = "Switch";
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
