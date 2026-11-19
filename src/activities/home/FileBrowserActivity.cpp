#include "FileBrowserActivity.h"
#include "BookActionActivity.h"
#include "Ao3Librarian.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
<<<<<<< HEAD
#include "../../../lib/Memory/Memory.h"
=======
#include <Memory.h>
#include <Utf8.h>
>>>>>>> 1.6.5

#include <algorithm>
#include <functional>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/BookmarkUtil.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr size_t NAME_BUFFER_SIZE = 500;

std::string getBookCachePath(const std::string& path) {
  const char* prefix = nullptr;
  if (FsHelpers::hasEpubExtension(path)) {
    prefix = "epub_";
  } else if (FsHelpers::hasXtcExtension(path)) {
    prefix = "xtc_";
  } else if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    prefix = "txt_";
  } else {
    return "";
  }
  return std::string("/.crosspoint/") + prefix + std::to_string(std::hash<std::string>{}(path));
}

bool moveStatePath(const std::string& oldPath, const std::string& newPath, bool& moved) {
  moved = false;
  if (oldPath.empty() || !Storage.exists(oldPath.c_str())) return true;
  if (Storage.exists(newPath.c_str())) {
    LOG_ERR("FileBrowser", "Rename state target already exists: %s", newPath.c_str());
    return false;
  }
  moved = Storage.rename(oldPath.c_str(), newPath.c_str());
  if (!moved) LOG_ERR("FileBrowser", "Failed to move rename state: %s -> %s", oldPath.c_str(), newPath.c_str());
  return moved;
}

void rollBackStatePath(const std::string& oldPath, const std::string& newPath, const bool moved) {
  if (moved && !Storage.rename(newPath.c_str(), oldPath.c_str())) {
    LOG_ERR("FileBrowser", "Failed to roll back rename state: %s -> %s", newPath.c_str(), oldPath.c_str());
  }
}
}  // namespace

std::string getFileExtension(const std::string& filename);

void formatFileName(const std::string& filename, char* buffer, const size_t bufferSize) {
  if (filename.empty()) {
    buffer[0] = '\0';
    return;
  }
  const bool isDirectory = filename.back() == '/';
  const size_t dot = isDirectory ? filename.size() - 1 : filename.rfind('.');
  const int length = static_cast<int>(dot == std::string::npos ? filename.size() : dot);
  const char* format = isDirectory && !UITheme::getInstance().getTheme().showsFileIcons() ? "[%.*s]" : "%.*s";
  snprintf(buffer, bufferSize, format, length, filename.c_str());
  // Compose only the display copy; filesystem lookup needs the raw entry bytes.
  utf8ComposeNfcInPlace(buffer);
}

void formatFileExtension(const std::string& filename, char* buffer, const size_t bufferSize) {
  buffer[0] = '\0';
  if (filename.empty() || filename.back() == '/') return;
  if (const char* extension = strrchr(filename.c_str(), '.')) snprintf(buffer, bufferSize, "%s", extension);
}

FileBrowserActivity::FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         std::string initialPath, const Mode mode)
    : UiListActivity("FileBrowser", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      mode(mode),
      basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}

void FileBrowserActivity::loadFiles() {
  files.clear();
<<<<<<< HEAD
  visibleStatusCache.clear();

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    rebuildRowItems(); 
=======
  prewarmedStart = -1;  // new folder contents: re-prewarm the visible window

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
>>>>>>> 1.6.5
    return;
  }

  root.rewindDirectory();

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    root.close();
    return;
  }

  // Size `files` once before the fill loop: doubling growth on a large folder
  // holds the old and new string blocks live across the final realloc. The
  // count-only pass reads no names, so filtered-out entries just over-reserve
  // by a few slots.
  size_t entryCount = 0;
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) ++entryCount;
  root.rewindDirectory();
  files.reserve(entryCount);

  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
    const bool isDirectory = file.isDirectory();
    if ((!SETTINGS.showHiddenFiles && fileNameBuffer[0] == '.') ||
        strcmp(fileNameBuffer.get(), "System Volume Information") == 0) {
      continue;
    }

    if (isDirectory) {
      files.emplace_back(std::string(fileNameBuffer.get()) + "/");
    } else {
      std::string_view filename{fileNameBuffer.get()};
      if (mode == Mode::PickFirmware) {
        if (FsHelpers::checkFileExtension(filename, ".bin")) {
          files.emplace_back(filename);
        }
      } else if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
                 FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename) ||
                 FsHelpers::hasBmpExtension(filename) || FsHelpers::hasPngExtension(filename)) {
        files.emplace_back(filename);
      }
    }
  }
  root.close();
  FsHelpers::sortFileList(files);
}

<<<<<<< HEAD
void FileBrowserActivity::refreshStatusIcons(const int visibleCount) {
  if (mode != Mode::Books || rowItems.empty()) return;

  const int first = std::max(0, nav.top);
  const int last = std::min(static_cast<int>(rowItems.size()), first + std::max(visibleCount + 2, 12));

  std::string base = basepath;
  if (base.back() != '/') base += "/";

  for (int i = first; i < last; i++) {
    if (files[i].back() == '/') continue;
    auto it = visibleStatusCache.find(i);
    if (it == visibleStatusCache.end()) {
      it = visibleStatusCache.emplace(i, getBookStatus(base + files[i])).first;
    }
    // START keeps whatever rebuildRowItems() assigned (book/text/image/...).
    if (it->second != BookStatus::START) rowItems[i].icon = bookStatusIconFor(it->second);
  }
}

void FileBrowserActivity::rebuildRowItems() {
  rowsUseFileIcons = UITheme::getInstance().getTheme().showsFileIcons();
  rowNames.resize(files.size());
  rowExtensions.resize(files.size());
  rowItems.clear();
  rowItems.reserve(files.size());

  for (size_t i = 0; i < files.size(); i++) {
    rowNames[i] = getFileName(files[i]);
    rowExtensions[i] = getFileExtension(files[i]);
    fui::ListItem item;
    item.label = rowNames[i].c_str();
    if (!rowExtensions[i].empty()) item.value = rowExtensions[i].c_str();

    item.icon = listIconFor(UITheme::getFileIcon(files[i]));
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
=======
// fui::ListProps::rowProvider — formats row `index` from files[index] into the
// activity's scratch buffers on demand. Runs on the render task only for the
// rows the list actually lays out, so nothing per-file is materialized beyond
// `files` itself. The label/value pointers stay valid until the next call,
// which is all the provider contract requires. The "[folder]" format is
// theme-dependent, but it's re-derived here on every
// repaint, so a theme change picked up while this activity was paused
// underneath another screen needs no cache invalidation.
void FileBrowserActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<FileBrowserActivity*>(ctx);
  if (index >= self->files.size()) return;
  const std::string& entry = self->files[index];
  formatFileName(entry, self->rowNameBuf, sizeof(self->rowNameBuf));
  item.label = self->rowNameBuf;
  formatFileExtension(entry, self->rowExtBuf, sizeof(self->rowExtBuf));
  if (self->rowExtBuf[0] != '\0') {
    item.value = self->rowExtBuf;
>>>>>>> 1.6.5
  }
  item.icon = listIconFor(UITheme::getFileIcon(entry));
  item.actionValue = static_cast<int16_t>(index);
}

// Batch-prewarm the CJK fallback glyphs for a bounded window of display names
// starting at the viewport top — one SD pass per list page (the reader TOC's
// refreshTocWindow pattern) instead of one unbounded pass over the whole
// folder. Getter form: no concatenated copy (a bare-new string append aborts
// under heap pressure). The last index covers the bottom path band: basepath
// (possibly a CJK folder name) draws in the same small font, so it must live
// in the same batch or it would evict the rows' glyphs when the heap gate
// disables union merging. (prewarmFallbackText appends the truncation
// ellipsis.)
void FileBrowserActivity::prewarmRowGlyphs(const int start) {
  const int total = static_cast<int>(files.size());
  int clamped = start;
  if (clamped > total - PREWARM_WINDOW) clamped = total - PREWARM_WINDOW;
  if (clamped < 0) clamped = 0;
  if (clamped == prewarmedStart) return;
  prewarmedStart = clamped;
  const int count = total - clamped < PREWARM_WINDOW ? total - clamped : PREWARM_WINDOW;

  struct PrewarmCtx {
    FileBrowserActivity* self;
    int first;
    int count;
  } prewarmCtx{this, clamped, count};
  renderer.prewarmFallbackText(
      uiScaleSpec().smallFontId,
      [](const void* ctx, uint32_t i) -> const char* {
        auto* c = const_cast<PrewarmCtx*>(static_cast<const PrewarmCtx*>(ctx));
        if (i < static_cast<uint32_t>(c->count)) {
          formatFileName(c->self->files[c->first + i], c->self->rowNameBuf, sizeof(c->self->rowNameBuf));
          return c->self->rowNameBuf;
        }
        return c->self->basepath.c_str();
      },
      &prewarmCtx, static_cast<uint32_t>(count) + 1);
}

void FileBrowserActivity::onEnter() {
  UiListActivity::onEnter();

  // Map FONT_BODY (Index 1) to 10pt for the list items, safely mirroring Ao3LibrarySettingsActivity
  uiTarget.setFont(freeink::ui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);

  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "malloc failed for name buffer");
    return;
  }

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles();
  } else if (!root.isDirectory()) {
    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    nav.selected = static_cast<int>(findEntry(fileName));
  } else {
    loadFiles();
  }
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  files.clear();
  fileNameBuffer.reset();
}

bool FileBrowserActivity::removeDirFile(const std::string& fullPath) {
  auto file = Storage.open(fullPath.c_str());
  if (!file) {
    LOG_ERR("FileBrowser", "Failed to open for metadata clearing: %s", fullPath.c_str());
    return false;
  }

  if (!file.isDirectory()) {
    file.close();
    clearBookCache(fullPath);
    return Storage.remove(fullPath.c_str());
  }
  file.close();

  if (!fileNameBuffer) return false;

  std::vector<std::pair<std::string, bool>> stack;
  stack.reserve(16);
  stack.push_back({fullPath, false});

  while (!stack.empty()) {
    auto [currentPath, postOrder] = std::move(stack.back());
    stack.pop_back();

    if (postOrder) {
      if (!Storage.rmdir(currentPath.c_str())) return false;
      continue;
    }

    auto dir = Storage.open(currentPath.c_str());
    if (!dir || !dir.isDirectory()) return false;

    stack.push_back({currentPath, true});
    dir.rewindDirectory();
    
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
      if (strcmp(fileNameBuffer.get(), ".") == 0 || strcmp(fileNameBuffer.get(), "..") == 0) continue;
      
      std::string entryPath = currentPath;
      if (entryPath.back() != '/') entryPath += "/";
      entryPath += fileNameBuffer.get();

      const bool isDir = entry.isDirectory();
      entry.close();

      if (isDir) {
        stack.push_back({std::move(entryPath), false});
      } else {
        clearBookCache(entryPath);
        if (!Storage.remove(entryPath.c_str())) return false;
      }
    }
  }
  return true;
}

void FileBrowserActivity::activateIndex(const int index) {
  (void)index;
  app.clearTapFlash();
  activateSelected();
}

void FileBrowserActivity::onRowLongPress(const int index) {
<<<<<<< HEAD
  (void)index;
  app.clearTapFlash();
  activateSelected(true);
=======
  (void)index;  // base already synced nav.selected to the pressed row
  app.clearTapFlash();
  if (mode == Mode::Books) {
    showEntryActions();
  } else {
    activateSelected();
  }
>>>>>>> 1.6.5
}

void FileBrowserActivity::activateSelected() {
  if (files.empty()) return;
  if (nav.selected < 0 || nav.selected >= listCount()) return;

  const std::string& entry = files[nav.selected];
  bool isDirectory = (entry.back() == '/');

  if (mode == Mode::PickFirmware && !isDirectory) {
    std::string cleanBasePath = basepath;
    if (cleanBasePath.back() != '/') cleanBasePath += "/";
    ActivityResult res{FilePathResult{cleanBasePath + entry}};
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

<<<<<<< HEAD
  if (mode == Mode::Books && (forceDelete || mappedInput.getHeldTime() >= GO_HOME_MS)) {
    std::string cleanBasePath = basepath;
    if (cleanBasePath.back() != '/') cleanBasePath += "/";
    const std::string fullPath = cleanBasePath + entry;

    if (isDirectory) {
      // === DELETE DIRECTORY ===
      auto handler = [this, fullPath](const ActivityResult& res) {
        if (!res.isCancelled) {
          if (removeDirFile(fullPath)) {
            RenderLock lock(*this);
            loadFiles();
            if (files.empty()) {
              nav.selected = 0;
            } else if (nav.selected >= listCount()) {
              nav.selected = listCount() - 1;
            }
            nav.follow(listCount());
            lock.unlock();
            requestUpdate(true);
          }
        }
      };
      std::string heading = tr(STR_DELETE) + std::string("? ");
      startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, entry), handler);
      return;
    } else {
      // === OPEN AO3 BOOK ACTION MENU ===
      auto handler = [this, fullPath, entry](const ActivityResult& res) {
        if (const auto* actionRes = std::get_if<BookActionResult>(&res.data)) {
          if (actionRes->modified) {
            if (actionRes->deleted) {
              Ao3Librarian::tombstoneRecord(fullPath);
              clearBookCache(fullPath);
              if (Storage.remove(fullPath.c_str())) {
                RenderLock lock(*this);
                loadFiles();
                if (files.empty()) {
                  nav.selected = 0;
                } else if (nav.selected >= listCount()) {
                  nav.selected = listCount() - 1;
                }
                nav.follow(listCount());
              }
            } else if (actionRes->archived || actionRes->indexingCompleted) {
              RenderLock lock(*this);
              loadFiles();
            } else {
              RenderLock lock(*this);
              visibleStatusCache[nav.selected] = actionRes->newStatus;
              rebuildRowItems();
            }
            requestUpdate(true);
          }
        }
      };
      startActivityForResult(std::make_unique<BookActionActivity>(renderer, mappedInput, fullPath, entry), handler);
      return;
    }
  } else {
    // --- SHORT PRESS ACTION: OPEN/NAVIGATE ---
    RenderLock lock(*this);
    if (basepath.back() != '/') basepath += "/";

    if (isDirectory) {
      basepath += entry.substr(0, entry.length() - 1);
      loadFiles();
      nav.selected = 0;
      nav.top = 0;
      lock.unlock();
      requestUpdate();
    } else {
      const std::string fullPath = basepath + entry;
      lock.unlock(); 
      onSelectBook(fullPath);
    }
  }
}

bool FileBrowserActivity::handleCustomInput() {
  if (mode == Mode::Books && mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() >= GO_HOME_MS && basepath != "/") {
    {
=======
  // buildScreen() runs on the render task and reads basepath plus `files`
  // through the row provider; mutate only under the render lock.
  RenderLock lock(*this);
  if (basepath.back() != '/') basepath += "/";

  if (isDirectory) {
    basepath += entry.substr(0, entry.length() - 1);
    loadFiles();
    nav.selected = 0;
    nav.top = 0;
    lock.unlock();
    requestUpdate();
  } else {
    const std::string fullPath = basepath + entry;
    lock.unlock();  // onSelectBook launches an activity; don't hold the lock across it
    onSelectBook(fullPath);
  }
}

void FileBrowserActivity::showEntryActions() {
  if (mode != Mode::Books || files.empty() || optionPopup.isActive() || nav.selected < 0 ||
      nav.selected >= listCount()) {
    return;
  }

  const bool isDirectory = files[nav.selected].back() == '/';
  static constexpr StrId FILE_OPTIONS[] = {StrId::STR_OPEN, StrId::STR_DELETE, StrId::STR_RENAME};
  static constexpr StrId DIRECTORY_OPTIONS[] = {StrId::STR_OPEN, StrId::STR_DELETE};
  optionPopup.show(StrId::STR_FILENAME, isDirectory ? DIRECTORY_OPTIONS : FILE_OPTIONS, isDirectory ? 2 : 3, 0,
                   [this](const int index) {
                     if (index == 0) {
                       activateSelected();
                     } else if (index == 1) {
                       deleteSelected();
                     } else if (index == 2) {
                       startRename();
                     }
                   });
  requestUpdate();
}

void FileBrowserActivity::deleteSelected() {
  if (files.empty() || nav.selected < 0 || nav.selected >= listCount()) return;

  std::string cleanBasePath = basepath;
  if (cleanBasePath.back() != '/') cleanBasePath += "/";
  const std::string entry = files[nav.selected];
  const std::string fullPath = cleanBasePath + entry;

  auto handler = [this, fullPath](const ActivityResult& res) {
    if (res.isCancelled) {
      LOG_DBG("FileBrowser", "Delete cancelled by user");
      return;
    }

    LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
    if (!removeDirFile(fullPath)) {
      LOG_ERR("FileBrowser", "Failed to delete: %s", fullPath.c_str());
      return;
    }

    LOG_DBG("FileBrowser", "Deleted successfully");
    {
      RenderLock lock(*this);
      loadFiles();
      if (files.empty()) {
        nav.selected = 0;
      } else if (nav.selected >= listCount()) {
        nav.selected = listCount() - 1;
      }
      nav.follow(listCount());
    }
    requestUpdate(true);
  };

  const std::string heading = tr(STR_DELETE) + std::string("? ");
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, heading, utf8ComposeNfc(entry));
  if (!confirmation) {
    LOG_ERR("FileBrowser", "OOM: delete confirmation");
    return;
  }
  startActivityForResult(std::move(confirmation), std::move(handler));
}

void FileBrowserActivity::startRename() {
  if (files.empty() || nav.selected < 0 || nav.selected >= listCount()) return;

  const std::string oldEntry = files[nav.selected];
  if (oldEntry.back() == '/') return;

  std::string cleanBasePath = basepath;
  if (cleanBasePath.back() != '/') cleanBasePath += "/";
  const std::string oldPath = cleanBasePath + oldEntry;
  const std::string extension = getFileExtension(oldEntry);
  const std::string initialStem = utf8ComposeNfc(oldEntry.substr(0, oldEntry.size() - extension.size()));
  const size_t maxStemLength = NAME_BUFFER_SIZE - extension.size() - 1;
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_RENAME), initialStem,
                                                           maxStemLength, InputType::Text);
  if (!keyboard) {
    LOG_ERR("FileBrowser", "OOM: rename keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this, oldPath, oldEntry, extension](const ActivityResult& result) {
    if (result.isCancelled) return;
    renameSelectedFile(oldPath, oldEntry, std::get<KeyboardResult>(result.data).text, extension);
  });
}

void FileBrowserActivity::renameSelectedFile(const std::string& oldPath, const std::string& oldEntry,
                                             const std::string& newStem, const std::string& extension) {
  const std::string newEntry = newStem + extension;
  if (newStem.empty() || newEntry.size() >= NAME_BUFFER_SIZE || !FsHelpers::isSafePathComponent(newEntry)) {
    LOG_ERR("FileBrowser", "Invalid rename target: %s", newEntry.c_str());
    return;
  }
  if (newEntry == utf8ComposeNfc(oldEntry)) return;

  const std::string parentPath = FsHelpers::extractFolderPath(oldPath);
  const std::string newPath = (parentPath == "/" ? parentPath : parentPath + "/") + newEntry;
  if (Storage.exists(newPath.c_str())) {
    LOG_ERR("FileBrowser", "Rename target already exists: %s", newPath.c_str());
    return;
  }

  const std::string oldCachePath = getBookCachePath(oldPath);
  const std::string newCachePath = getBookCachePath(newPath);
  const bool isEpub = FsHelpers::hasEpubExtension(oldPath);
  const std::string oldBookmarkPath = isEpub ? BookmarkUtil::getBookmarkPath(oldPath) : "";
  const std::string newBookmarkPath = isEpub ? BookmarkUtil::getBookmarkPath(newPath) : "";
  bool cacheMoved = false;
  bool bookmarksMoved = false;
  if (!moveStatePath(oldCachePath, newCachePath, cacheMoved)) return;
  if (!moveStatePath(oldBookmarkPath, newBookmarkPath, bookmarksMoved)) {
    rollBackStatePath(oldCachePath, newCachePath, cacheMoved);
    return;
  }
  if (!Storage.rename(oldPath.c_str(), newPath.c_str())) {
    LOG_ERR("FileBrowser", "Failed to rename file: %s -> %s", oldPath.c_str(), newPath.c_str());
    rollBackStatePath(oldBookmarkPath, newBookmarkPath, bookmarksMoved);
    rollBackStatePath(oldCachePath, newCachePath, cacheMoved);
    return;
  }

  RECENT_BOOKS.updatePath(oldPath, newPath, oldCachePath, newCachePath);
  if (APP_STATE.openEpubPath == oldPath) {
    APP_STATE.openEpubPath = newPath;
    if (!APP_STATE.saveToFile()) LOG_ERR("FileBrowser", "Failed to save renamed open-book path");
  }

  {
    RenderLock lock(*this);
    loadFiles();
    nav.selected = static_cast<int>(findEntry(newEntry));
    nav.follow(listCount());
  }
  requestUpdate(true);
}

bool FileBrowserActivity::handleCustomInput() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;

  // Long press BACK (1s+) goes to root folder (Books mode only).
  // In firmware-pick mode we keep navigation simple: short Back = up dir / cancel.
  if (mode == Mode::Books && mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() >= GO_HOME_MS && basepath != "/") {
    {
      // buildScreen() runs on the render task and reads basepath plus `files`
      // through the row provider; mutate only under the render lock.
>>>>>>> 1.6.5
      RenderLock lock(*this);
      basepath = "/";
      loadFiles();
      nav.selected = 0;
      nav.top = 0;
    }
    requestUpdate();
    return true;
  }
  return false;
}

void FileBrowserActivity::navigateButtons() {
  const int count = listCount();
  if (count <= 0) return;

  auto& n = activeNav();

  // Helper to instantly snap the viewport to the page boundary of the new selection
  auto stepAndPaginate = [&](const int newIndex) {
    RenderLock lock(*this);
    n.selected = newIndex;
    if (n.pageRows() > 0) {
      n.top = (n.selected / n.pageRows()) * n.pageRows();
    }
  };

  buttonNavigator.onNextRelease([&] { stepAndPaginate(ButtonNavigator::nextIndex(n.selected, count)); requestUpdate(); });
  buttonNavigator.onPreviousRelease([&] { stepAndPaginate(ButtonNavigator::previousIndex(n.selected, count)); requestUpdate(); });

  buttonNavigator.onSideNextContinuous([&] { stepAndPaginate(ButtonNavigator::nextPageIndex(n.selected, count, n.pageRows())); requestUpdate(); });
  buttonNavigator.onSidePreviousContinuous([&] { stepAndPaginate(ButtonNavigator::previousPageIndex(n.selected, count, n.pageRows())); requestUpdate(); });

  buttonNavigator.onFrontNextContinuous([&] { stepAndPaginate(ButtonNavigator::nextFourIndex(n.selected, count)); requestUpdate(); });
  buttonNavigator.onFrontPreviousContinuous([&] { stepAndPaginate(ButtonNavigator::previousFourIndex(n.selected, count)); requestUpdate(); });
}

bool FileBrowserActivity::handleButtons() {
  if (mode == Mode::Books && mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, GO_HOME_MS)) {
    app.clearTapFlash();
    showEntryActions();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;
        {
<<<<<<< HEAD
=======
          // buildScreen() runs on the render task and reads basepath plus `files`
          // through the row provider; mutate only under the render lock.
>>>>>>> 1.6.5
          RenderLock lock(*this);
          basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
          if (basepath.empty()) basepath = "/";
          loadFiles();

          const auto pos = oldPath.find_last_of('/');
          const std::string dirName = oldPath.substr(pos + 1) + "/";
          nav.selected = static_cast<int>(findEntry(dirName));
          nav.top = 0;
          nav.follow(listCount());
        }
        requestUpdate();
      } else if (mode == Mode::PickFirmware) {
        ActivityResult res;
        res.isCancelled = true;
        setResult(std::move(res));
        finish();
      } else {
        onGoHome();
      }
    }
    return true;
  }
  return false;
}

void FileBrowserActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

std::string getFileExtension(const std::string& filename) {
  if (filename.back() == '/') return "";
  const auto pos = filename.rfind('.');
  return filename.substr(pos);
}

BookStatus FileBrowserActivity::getBookStatus(const std::string& path) {
  const auto pos = path.rfind('.');
  if (pos == std::string::npos) return BookStatus::START;
  const auto ext = path.substr(pos + 1);
  if (ext != "epub" && ext != "EPUB") return BookStatus::START;

  std::string cachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(path));
  HalFile f;
  BookStatus status = BookStatus::START;
  if (Storage.openFileForRead("BROWSER", cachePath + "/progress.bin", f)) {
    uint8_t data[11];
    int dataSize = f.read(data, sizeof(data));
    f.close();

    if (dataSize == 11) {
      status = static_cast<BookStatus>(data[10]);
    } else if (dataSize == 7) {
      status = static_cast<BookStatus>(data[6]);
    }
  }
  return status;
}

void FileBrowserActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  {
    const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect band = screen.takeBottom(static_cast<int16_t>(pathLineHeight + metrics.verticalSpacing));
    screen.target().fill(fui::Rect{band.x, band.y, band.width, 3}, fui::Paint::solid(fui::Color::Black));
    const int pathY =
        band.y + metrics.verticalSpacing / 2 + (band.height - metrics.verticalSpacing / 2 - pathLineHeight) / 2;
    const int pathMaxWidth = band.width - metrics.contentSidePadding * 2;
    const char* pathStr = basepath.c_str();
    const char* pathDisplay = pathStr;
    char leftTruncBuf[256];
    if (renderer.getTextWidth(SMALL_FONT_ID, pathStr) > pathMaxWidth) {
      const char ellipsis[] = "\xe2\x80\xa6";  
      const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
      const int available = pathMaxWidth - ellipsisWidth;
      const char* p = pathStr;
      while (*p) {
        if (renderer.getTextWidth(SMALL_FONT_ID, p) <= available) break;
        ++p;
        while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
      }
      snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
      pathDisplay = leftTruncBuf;
    }
    renderer.drawText(SMALL_FONT_ID, band.x + metrics.contentSidePadding, pathY, pathDisplay);
  }

  if (files.empty()) {
    screen.centeredText(mode == Mode::PickFirmware ? tr(STR_NO_BIN_FILES) : tr(STR_NO_FILES_FOUND),
                        screen.theme().bodyText);
    return;
  }

<<<<<<< HEAD
  if (rowsUseFileIcons != UITheme::getInstance().getTheme().showsFileIcons()) {
    rebuildRowItems();
  }

=======
>>>>>>> 1.6.5
  fui::ListProps props;
  props.rowProvider = &FileBrowserActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(files.size());
  props.action = ACTION_ROW;
<<<<<<< HEAD
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.valueInset = 8;  
  
  // bodyText natively carries Index 1, defeating the textStyleUnset zero-check
  fui::TextStyle label = screen.theme().bodyText; 
  label.maxLines = 1;                              // Enforce truncation and consistent row height
  props.labelText = label;
  props.valueText = label;                         // Ensure file extensions match the 10pt size
  props.balanceWrappedLabelWithValue = false;
  props.partialTrailingRow = mappedInput.hasTouch(); // Fixed grid for button devices, partial row for touch devices

  syncListViewport(screen, props);                 // Needed for base touch support
  refreshStatusIcons(nav.pageRows());              // after top is final, before list() reads items
=======
  // Tap opens/navigates; long-press shows entry actions (physical buttons stay in loop()).
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.valueInset = 8;  // air between the extension and the row edge
  // Names use up to two small-font lines; shared list layout sizes each row.
  fui::TextStyle label = screen.theme().smallText;
  label.maxLines = 2;
  props.labelText = label;

  // The trailing value here is just the short extension: skip the balanced
  // 60%-band wrap cap and let both name lines run the full width before it.
  props.balanceWrappedLabelWithValue = false;
  syncListViewport(screen, props);
  // Prewarm the window at the final viewport (syncListViewport just applied
  // follow/clamping to nav.top) before the list resolves rows through the
  // provider.
  prewarmRowGlyphs(nav.top);
>>>>>>> 1.6.5
  screen.list(props);
}

void FileBrowserActivity::drawChrome() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  std::string folderName =
      (mode == Mode::PickFirmware)
          ? std::string(tr(STR_SELECT_FIRMWARE_FILE))
          : ((basepath == "/") ? std::string(tr(STR_SD_CARD)) : basepath.substr(basepath.rfind('/') + 1));
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());
}

void FileBrowserActivity::drawFooter() {
  const char* backLabel = (basepath == "/") ? (mode == Mode::PickFirmware ? tr(STR_BACK) : tr(STR_HOME)) : tr(STR_BACK);
  const bool selectingFirmwareFile = mode == Mode::PickFirmware && !files.empty() && nav.selected >= 0 &&
                                     nav.selected < listCount() && files[nav.selected].back() != '/';
  const char* confirmLabel = files.empty() ? "" : (selectingFirmwareFile ? tr(STR_SELECT) : tr(STR_OPEN));
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, files.empty() ? "" : tr(STR_DIR_UP),
                                            files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
<<<<<<< HEAD
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i;
  return 0;
}
=======
  const auto entry = std::find(files.begin(), files.end(), name);
  return entry != files.end() ? static_cast<size_t>(entry - files.begin()) : 0;
}
>>>>>>> 1.6.5
