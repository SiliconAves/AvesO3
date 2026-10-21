#include "FileBrowserActivity.h"
#include "BookActionActivity.h"
#include "Ao3Librarian.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include "../../../lib/Memory/Memory.h"

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr size_t NAME_BUFFER_SIZE = 500;
}  // namespace

std::string getFileName(std::string filename);
std::string getFileExtension(const std::string& filename);

FileBrowserActivity::FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         std::string initialPath, const Mode mode)
    : UiListActivity("FileBrowser", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      mode(mode),
      basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}

void FileBrowserActivity::loadFiles() {
  files.clear();
  visibleStatusCache.clear();

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    rebuildRowItems(); 
    return;
  }

  root.rewindDirectory();

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    root.close();
    rebuildRowItems();
    return;
  }

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
  rebuildRowItems();
}

void FileBrowserActivity::rebuildRowItems() {
  rowsUseFileIcons = UITheme::getInstance().getTheme().showsFileIcons();
  rowNames.resize(files.size());
  rowExtensions.resize(files.size());
  rowItems.clear();
  rowItems.reserve(files.size());
  
  std::string cleanBasePath = basepath;
  if (cleanBasePath.back() != '/') cleanBasePath += "/";

  for (size_t i = 0; i < files.size(); i++) {
    rowNames[i] = getFileName(files[i]);
    rowExtensions[i] = getFileExtension(files[i]);
    fui::ListItem item;
    item.label = rowNames[i].c_str();
    if (!rowExtensions[i].empty()) item.value = rowExtensions[i].c_str();
    
    const bool isDirectory = (files[i].back() == '/');

    // === AO3 STATUS ICON HOOK ===
    if (mode == Mode::Books && !isDirectory) {
      if (!visibleStatusCache.count(i)) {
         visibleStatusCache[i] = getBookStatus(cleanBasePath + files[i]);
      }
      // TODO: Replace listIconFor with your pre-baked icons based on visibleStatusCache[i]
      item.icon = listIconFor(UITheme::getFileIcon(files[i])); 
    } else {
      item.icon = listIconFor(UITheme::getFileIcon(files[i]));
    }

    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }

  struct PrewarmCtx {
    const std::vector<std::string>* names;
    const std::string* path;
  } prewarmCtx{&rowNames, &basepath};
  renderer.prewarmFallbackText(
      uiScaleSpec().smallFontId,
      [](const void* ctx, uint32_t i) -> const char* {
        const auto* c = static_cast<const PrewarmCtx*>(ctx);
        return i < c->names->size() ? (*c->names)[i].c_str() : c->path->c_str();
      },
      &prewarmCtx, static_cast<uint32_t>(rowNames.size()) + 1);
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
  rowNames.clear();
  rowExtensions.clear();
  rowItems.clear();
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
  (void)index;
  app.clearTapFlash();
  activateSelected(true);
}

void FileBrowserActivity::activateSelected(const bool forceDelete) {
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
              visibleStatusCache[nav.selected] = actionRes->newStatus;
              RenderLock lock(*this);
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
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;
        {
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

std::string getFileName(std::string filename) {
  if (filename.back() == '/') {
    filename.pop_back();
    if (!UITheme::getInstance().getTheme().showsFileIcons()) {
      return "[" + filename + "]";
    }
    return filename;
  }
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
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

  if (rowsUseFileIcons != UITheme::getInstance().getTheme().showsFileIcons()) {
    rebuildRowItems();
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.valueInset = 8;  
  
  // bodyText natively carries Index 1, defeating the textStyleUnset zero-check
  fui::TextStyle label = screen.theme().bodyText; 
  label.maxLines = 1;                              // Enforce truncation and consistent row height
  props.labelText = label;
  props.valueText = label;                         // Ensure file extensions match the 10pt size
  props.balanceWrappedLabelWithValue = false;
  props.partialTrailingRow = false;                // Fixed grid

  syncListViewport(screen, props);                 // Needed for base touch support
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
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i;
  return 0;
}