#include "Ao3IndexActivity.h"
#include <HalStorage.h>
#include <ArduinoJson.h>
#include <Epub.h>
#include <Logging.h>
#include <I18n.h>
#include "../../components/UITheme.h"
#include "../../fontIds.h"

namespace {
bool isLibraryFull() {
  const char* indexPath = "/.crosspoint/ao3_library_index.bin";
  if (!Storage.exists(indexPath)) return false;
  FsFile f;
  if (Storage.openFileForRead("AO3L", indexPath, f)) {
    char magic[4];
    uint8_t version;
    uint16_t recordCount;
    if (f.read(magic, 4) == 4 && f.read(&version, 1) == 1 && f.read((uint8_t*)&recordCount, 2) == 2) {
      f.close();
      return recordCount >= 400;
    }
    f.close();
  }
  return false;
}
} // namespace

void Ao3IndexActivity::onEnter() {
  Activity::onEnter();
  state = State::HEAP_CHECK;
  initialized = false;
  requestUpdate(true);
}

void Ao3IndexActivity::runHeapCheck() {
  if (ESP.getFreeHeap() < 80 * 1024) {
    state = State::ERROR;
    errorMessage = "Insufficient memory to run indexing (need 80KB free heap).";
    return;
  }

  if (mode == Ao3IndexMode::SINGLE) {
    if (isLibraryFull()) {
      // Build index hashes to check if this specific file is already indexed
      buildIndexedHashes();
      uint32_t hash = static_cast<uint32_t>(std::hash<std::string>{}(targetPath));
      bool isExistingBook = std::binary_search(indexedHashes.begin(), indexedHashes.end(), hash);

      if (!isExistingBook) {
        state = State::ERROR;
        errorMessage = "AO3 library full (400 books).";
        return;
      }
    }
    state = State::SINGLE_SNIFFING;
  } else {
    // Directory mode: Block immediately if full (no need to waste time scanning folders)
    if (isLibraryFull()) {
      state = State::ERROR;
      errorMessage = "AO3 library full (400 books).";
      return;
    }
    state = State::DIR_LOAD_SETTINGS;
  }
}

void Ao3IndexActivity::loadSettings() {
  ao3Folder = "";
  excludedFolders.clear();
  const char* path = "/.crosspoint/ao3_settings.json";
  if (!Storage.exists(path)) return;
  String json = Storage.readFile(path);
  if (json.isEmpty()) return;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return;
  ao3Folder = doc["ao3Folder"] | "";
  batchSize = doc["batchSize"] | 10;
  JsonArray arr = doc["excludedFolders"];
  if (!arr.isNull()) {
    for (JsonVariant val : arr) {
      excludedFolders.push_back(val.as<std::string>());
    }
  }
}

void Ao3IndexActivity::buildIndexedHashes() {
  indexedHashes.clear();
  const char* indexPath = "/.crosspoint/ao3_library_index.bin";
  if (!Storage.exists(indexPath)) return;

  FsFile f;
  if (!Storage.openFileForRead("AO3L", indexPath, f)) return;

  char magic[4];
  uint8_t version;
  uint16_t recordCount;
  if (f.read(magic, 4) == 4 && f.read(&version, 1) == 1 && f.read((uint8_t*)&recordCount, 2) == 2) {
    f.seek(12); // Seek past header
    CompactIndexRecord rec;
    for (uint16_t i = 0; i < recordCount; i++) {
      if (f.read((uint8_t*)&rec, sizeof(rec)) != sizeof(rec)) break;
      if (!(rec.flags & 0x01)) {
        indexedHashes.push_back(rec.cacheHash);
      }
    }
  }
  f.close();
  std::sort(indexedHashes.begin(), indexedHashes.end());
}

bool Ao3IndexActivity::isExcluded(const std::string& path) const {
  for (const auto& excl : excludedFolders) {
    if (path == excl) return true;
  }
  return false;
}

void Ao3IndexActivity::loop() {
  // Common error or completion back/confirm navigation
  if (state == State::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      finish();
    }
    return;
  }
  if (state == State::DIR_COMPLETE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!failedBooks.empty()) {
        state = State::DIR_FAILED_LIST;
        requestUpdate(true);
      } else {
        finish();
      }
    }
    return;
  }
  if (state == State::DIR_FAILED_LIST) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      finish();
    }
    return;
  }
  if (state == State::SINGLE_COMPLETE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      Ao3IndexResult res;
      res.indexingCompleted = true;
      res.successfullyIndexed = true;
      setResult(ActivityResult(std::move(res)));
      finish();
    }
    return;
  }

  switch (state) {
    case State::HEAP_CHECK:
      runHeapCheck();
      requestUpdate(true);
      break;

    case State::SINGLE_SNIFFING:
      tickSingleSniffing();
      break;

    case State::SINGLE_SCRAPING:
      tickSingleScraping();
      break;

    case State::DIR_LOAD_SETTINGS:
      loadSettings();
      if (ao3Folder.empty()) {
        state = State::ERROR;
        errorMessage = "No AO3 folder configured. Please configure your AO3 folder in Settings first.";
        requestUpdate(true);
      } else {
        state = State::DIR_DISCOVERY;
        initialized = false;
        requestUpdate(true);
      }
      break;

    case State::DIR_DISCOVERY:
      tickDirDiscovery();
      break;

    case State::DIR_DISCOVERY_CONFIRM:
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        successCount = 0;
        failureCount = 0;
        failedBooks.clear();
        startDirIndexing();
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        finish(); // user declined; return to library
      }
      break;

    case State::DIR_INDEXING:
      tickDirIndexing();
      break;

    case State::DIR_BATCH_COMPLETE:
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        startDirIndexing();
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        finish(); // user stopped early; still triggers result handler
      }
      break;

    default:
      break;
  }
}

void Ao3IndexActivity::tickSingleSniffing() {
  Epub epub(targetPath, "/.crosspoint");
  std::string pub = epub.sniffPublisher();

  // Transform to lowercase for case-insensitive comparison
  std::string pubLower = pub;
  std::transform(pubLower.begin(), pubLower.end(), pubLower.begin(), ::tolower);

  if (pubLower == "archive of our own" || pubLower.find("archiveofourown") != std::string::npos) {
    state = State::SINGLE_SCRAPING;
  } else {
    state = State::ERROR;
    errorMessage = "Not an AO3 book (publisher: " + (pub.empty() ? "Unknown" : pub) + ")";
  }
  requestUpdate(true);
}

void Ao3IndexActivity::tickSingleScraping() {
  if (ESP.getFreeHeap() < 80 * 1024) {
    yield();
    return; // defer tick
  }

  Epub epub(targetPath, "/.crosspoint");
  if (!epub.load(true, true, true)) {
    state = State::ERROR;
    errorMessage = "Failed to load epub file structure.";
    requestUpdate(true);
    return;
  }

  bool success = Ao3Librarian::scrape(epub, /*force=*/true);
  if (success) {
    state = State::SINGLE_COMPLETE;
  } else {
    state = State::ERROR;
    errorMessage = "Scraping/Indexing failed.";
  }
  requestUpdate(true);
}

void Ao3IndexActivity::tickDirDiscovery() {
  if (!initialized) {
    buildIndexedHashes();
    dirQueue.clear();
    dirQueue.push_back({ ao3Folder, 0 });
    unindexedCount = 0;
    initialized = true;
    requestUpdate(true);
    return;
  }

  if (dirQueue.empty()) {
    if (unindexedCount == 0) {
      state = State::DIR_COMPLETE;
    } else {
      state = State::DIR_DISCOVERY_CONFIRM;
    }
    requestUpdate(true);
    return;
  }

  QueueEntry entry = dirQueue.back();
  dirQueue.pop_back();

  // Skip exclusions, hidden folders, crosspoint dirs
  if (isExcluded(entry.path) || entry.path.find("/.") != std::string::npos || entry.path.find(".crosspoint") != std::string::npos) {
    yield();
    return;
  }

  auto root = Storage.open(entry.path.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    yield();
    return;
  }

  root.rewindDirectory();
  char name[256];
  FsFile file;
  while (file = root.openNextFile()) {
    file.getName(name, sizeof(name));

    std::string fullChildPath = entry.path;
    if (fullChildPath.back() != '/') fullChildPath += "/";
    fullChildPath += name;

    if (file.isDirectory()) {
      if (name[0] != '.' && entry.depth < 5 && strcmp(name, "System Volume Information") != 0 && strcmp(name, ".crosspoint") != 0) {
        dirQueue.push_back({ fullChildPath, entry.depth + 1 });
      }
    } else {
      std::string nameStr(name);
      std::string ext = "";
      size_t dotPos = nameStr.find_last_of('.');
      if (dotPos != std::string::npos) {
        ext = nameStr.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      }
      if (ext == "epub") {
        uint32_t hash = static_cast<uint32_t>(std::hash<std::string>{}(fullChildPath));
        if (!std::binary_search(indexedHashes.begin(), indexedHashes.end(), hash)) {
          unindexedCount++;
        }
      }
    }
    file.close();
  }
  root.close();
  requestUpdate(true);
}

void Ao3IndexActivity::startDirIndexing() {
  // Rebuild indexed hashes so books successfully indexed in previous batches are excluded.
  buildIndexedHashes();
  // Merge in any books that failed this session so subsequent batch walks
  // don't retry them endlessly. failedBooks paths are preserved for the
  // failed list screen — only their hashes are inserted here, in memory only.
  for (const auto& path : failedBooks) {
    uint32_t hash = static_cast<uint32_t>(std::hash<std::string>{}(path));
    auto it = std::lower_bound(indexedHashes.begin(), indexedHashes.end(), hash);
    if (it == indexedHashes.end() || *it != hash) {
      indexedHashes.insert(it, hash);
    }
  }

  // Collect up to batchSize unindexed paths. This walk is synchronous because it does
  // no epub loading — just filename hashing — and is bounded by batchSize entries.
  pendingBooks.clear();
  pendingBooks.reserve(batchSize);

  std::vector<QueueEntry> queue;
  queue.push_back({ ao3Folder, 0 });

  while (!queue.empty() && (int)pendingBooks.size() < batchSize) {
    QueueEntry entry = queue.back();
    queue.pop_back();

    if (isExcluded(entry.path) || entry.path.find("/.") != std::string::npos || entry.path.find(".crosspoint") != std::string::npos) {
      continue;
    }

    auto root = Storage.open(entry.path.c_str());
    if (!root || !root.isDirectory()) {
      if (root) root.close();
      continue;
    }

    root.rewindDirectory();
    char name[256];
    FsFile file;
    while (file = root.openNextFile()) {
      file.getName(name, sizeof(name));

      std::string fullChildPath = entry.path;
      if (fullChildPath.back() != '/') fullChildPath += "/";
      fullChildPath += name;

      if (file.isDirectory()) {
        if (name[0] != '.' && entry.depth < 5 && strcmp(name, "System Volume Information") != 0 && strcmp(name, ".crosspoint") != 0) {
          queue.push_back({ fullChildPath, entry.depth + 1 });
        }
      } else {
        std::string nameStr(name);
        std::string ext = "";
        size_t dotPos = nameStr.find_last_of('.');
        if (dotPos != std::string::npos) {
          ext = nameStr.substr(dotPos + 1);
          std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        }
        if (ext == "epub") {
          uint32_t hash = static_cast<uint32_t>(std::hash<std::string>{}(fullChildPath));
          if (!std::binary_search(indexedHashes.begin(), indexedHashes.end(), hash)) {
            pendingBooks.push_back(fullChildPath);
          }
        }
      }
      file.close();
      if ((int)pendingBooks.size() >= batchSize) break;
    }
    root.close();
    yield(); // give the watchdog a breath between directories
  }

  if (pendingBooks.empty()) {
    // No more unindexed books remain — all done.
    state = State::DIR_COMPLETE;
    requestUpdate(true);
    return;
  }

  pendingBooks.shrink_to_fit();
  currentBookIndex = 0;
  batchStartIndex = 0;
  batchCount = pendingBooks.size();
  state = State::DIR_INDEXING;
  requestUpdate(true);
}

void Ao3IndexActivity::tickDirIndexing() {
  if (ESP.getFreeHeap() < 80 * 1024) {
    yield();
    return; // defer
  }

  // Batch exhausted. If we got fewer books than requested, this was the last batch.
  // Otherwise there may be more unindexed books — show the batch complete prompt.
  if (currentBookIndex >= pendingBooks.size()) {
    if ((int)pendingBooks.size() < batchSize) {
      state = State::DIR_COMPLETE;
    } else {
      state = State::DIR_BATCH_COMPLETE;
    }
    requestUpdate(true);
    return;
  }

  if (isLibraryFull()) {
    state = State::DIR_COMPLETE;
    errorMessage = "AO3 library full (400 books).";
    requestUpdate(true);
    return;
  }

  // Move the path out and immediately free its heap buffer.
  std::string filePath = std::move(pendingBooks[currentBookIndex]);
  pendingBooks[currentBookIndex].shrink_to_fit();
  Epub epub(filePath, "/.crosspoint");

  if (epub.load(true, true, true)) {
    currentBookTitle = epub.getTitle();
    bool success = Ao3Librarian::scrape(epub, /*force=*/true);
    if (success) {
      successCount++;
    } else {
      failureCount++;
      if (failedBooks.size() < 10) failedBooks.push_back(filePath);
    }
  } else {
    failureCount++;
    if (failedBooks.size() < 10) failedBooks.push_back(filePath);
  }

  currentBookIndex++;
  requestUpdate(true);
}

void Ao3IndexActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string titleStr = (mode == Ao3IndexMode::SINGLE) ? "Index Book" : "Index AO3 Library";
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, titleStr.c_str());

  int contentTop = metrics.topPadding + metrics.headerHeight + 40;

  if (state == State::HEAP_CHECK) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Checking free memory...");
  }
  else if (state == State::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, "Error");
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  else if (state == State::SINGLE_SNIFFING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Checking publisher...");
  }
  else if (state == State::SINGLE_SCRAPING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Parsing AO3 metadata...");
  }
  else if (state == State::SINGLE_COMPLETE) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, "Indexing complete!");
    const auto labels = mappedInput.mapLabels("", "Done", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  else if (state == State::DIR_DISCOVERY) {
    renderer.drawCenteredText(UI_12_FONT_ID, contentTop, "Scanning AO3 folder...", true, EpdFontFamily::BOLD);
    char buf[128];
    sprintf(buf, "%zu books found", unindexedCount);
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + 40, buf);
  }
  else if (state == State::DIR_DISCOVERY_CONFIRM) {
    char buf[128];
    sprintf(buf, "%zu unindexed book/s found. Index now?", unindexedCount);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, buf);
    const auto labels = mappedInput.mapLabels("Cancel", "Index", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  else if (state == State::DIR_INDEXING) {
    int usableAreaTop = metrics.topPadding + metrics.headerHeight;
    int usableAreaBottom = pageHeight - metrics.buttonHintsHeight;
    int centeredContentTop = usableAreaTop + ((usableAreaBottom - usableAreaTop) - 162) / 2;

    renderer.drawCenteredText(UI_12_FONT_ID, centeredContentTop, "Building AO3 Library", true);

    // Progress bar
    int progressBarWidth = pageWidth - 120;
    int progressBarX = (pageWidth - progressBarWidth) / 2;
    size_t processed = currentBookIndex - batchStartIndex;
    GUI.drawProgressBar(renderer, Rect{progressBarX, centeredContentTop + 47, progressBarWidth, 20}, processed, batchCount);

    char buf[128];
    sprintf(buf, "%zu / %zu books", processed, batchCount);
    renderer.drawCenteredText(UI_10_FONT_ID, centeredContentTop + 117, buf);

    // Current book title
    if (!currentBookTitle.empty()) {
      std::string truncatedTitle = currentBookTitle;
      if (truncatedTitle.length() > 30) truncatedTitle = truncatedTitle.substr(0, 28) + "..";
      renderer.drawCenteredText(SMALL_FONT_ID, centeredContentTop + 157, truncatedTitle.c_str(), true, EpdFontFamily::ITALIC);
    }
  }
  else if (state == State::DIR_BATCH_COMPLETE) {
    char buf[128];
    sprintf(buf, "%zu / %zu books indexed. Index next %d?", successCount + failureCount, unindexedCount, batchSize);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, buf);

    sprintf(buf, "%zu succeeded, %zu failed", successCount, failureCount);
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 10, buf);

    const auto labels = mappedInput.mapLabels("Cancel", "Continue", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  else if (state == State::DIR_COMPLETE) {
    if (!errorMessage.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, errorMessage.c_str());
    } else if (unindexedCount == 0) {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, "No new books found in your AO3 directory.");
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, "Indexing Complete!");
    }

    char buf[128];
    sprintf(buf, "%zu book/s processed (%zu succeeded, %zu failed).", successCount + failureCount, successCount, failureCount);
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 10, buf);

    const auto labels = mappedInput.mapLabels("", "Done", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  else if (state == State::DIR_FAILED_LIST) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Failed Books");

    const int entryHeight = 40;
    const int startY = metrics.topPadding + metrics.headerHeight + 10;

    for (size_t i = 0; i < failedBooks.size(); i++) {
        const std::string& fullPath = failedBooks[i];

        // Extract filename and directory from path
        std::string filename = fullPath;
        std::string dirPath = fullPath;
        size_t lastSlash = fullPath.find_last_of('/');
        if (lastSlash != std::string::npos) {
            filename = fullPath.substr(lastSlash + 1);
            dirPath = fullPath.substr(0, lastSlash + 1); // Keep the trailing slash to indicate a directory
        }

        // Truncate if needed
        const int maxWidth = pageWidth - 30;
        std::string displayName = renderer.truncatedText(UI_10_FONT_ID, filename.c_str(), maxWidth);
        std::string displayPath = renderer.truncatedText(SMALL_FONT_ID, dirPath.c_str(), maxWidth);

        const int y = startY + static_cast<int>(i) * entryHeight;
        renderer.drawText(UI_10_FONT_ID, 15, y, displayName.c_str());
        renderer.drawText(SMALL_FONT_ID,  15, y + 18, displayPath.c_str());
    }

    const auto labels = mappedInput.mapLabels("", "Done", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
