#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include "../Activity.h"
#include "../../Ao3CompactIndexRecord.h"
#include "../../Ao3Librarian.h"
#include "../../util/ButtonNavigator.h"

enum class Ao3IndexMode {
  SINGLE,
  DIRECTORY
};

class Ao3IndexActivity final : public Activity {
 public:
  enum class State {
    HEAP_CHECK,
    
    // Single mode states
    SINGLE_SNIFFING,
    SINGLE_SCRAPING,
    SINGLE_COMPLETE,
    
    // Directory mode states
    DIR_LOAD_SETTINGS,
    DIR_DISCOVERY,
    DIR_DISCOVERY_CONFIRM,
    DIR_INDEXING,
    DIR_BATCH_COMPLETE,
    DIR_COMPLETE,
    
    ERROR
  };

 private:
  Ao3IndexMode mode;
  std::string targetPath; // file path for SINGLE mode
  State state = State::HEAP_CHECK;
  std::string errorMessage;
  ButtonNavigator buttonNavigator;

  // Settings
  std::string ao3Folder;
  std::vector<std::string> excludedFolders;

  // Discovery / Scan state variables
  struct QueueEntry {
    std::string path;
    int depth;
  };
  std::vector<QueueEntry> dirQueue;
  std::vector<uint32_t> indexedHashes;
  std::vector<std::string> pendingBooks;
  
  // Progress variables
  size_t currentBookIndex = 0;
  size_t batchStartIndex = 0;
  size_t batchCount = 0;
  size_t successCount = 0;
  size_t failureCount = 0;
  std::string currentBookTitle;

  int batchSize = 10;
  bool initialized = false;

  void runHeapCheck();
  void startSingleSniffing();
  void tickSingleSniffing();
  void tickSingleScraping();
  
  void startDirLoadSettings();
  void tickDirDiscovery();
  void startDirIndexing();
  void tickDirIndexing();

  void loadSettings();
  void buildIndexedHashes();
  bool isExcluded(const std::string& path) const;

 public:
  Ao3IndexActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Ao3IndexMode mode, std::string targetPath = "")
      : Activity("Ao3Index", renderer, mappedInput), mode(mode), targetPath(std::move(targetPath)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool preventAutoSleep() override { return true; }
};
