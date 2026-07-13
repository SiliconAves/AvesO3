#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include "BookStatus.h"


struct WifiResult {
  bool connected = false;
  std::string ssid;
  std::string ip;
};

struct KeyboardResult {
  std::string text;
};

struct MenuResult {
  int action = -1;
  uint8_t orientation = 0;
  uint8_t pageTurnOption = 0;
  BookStatus status = BookStatus::READING;
};

struct ChapterResult {
  int spineIndex = 0;
};

struct PercentResult {
  int percent = 0;
};

struct PageResult {
  uint32_t page = 0;
};

struct SyncResult {
  int spineIndex = 0;
  int page = 0;
};

enum class NetworkMode;

struct NetworkModeResult {
  NetworkMode mode;
};

struct FootnoteResult {
  std::string href;
};

struct FilePathResult {
  std::string path;
};

struct BookActionResult {
  bool deleted = false;
  bool modified = false;
  BookStatus newStatus = BookStatus::START;
  bool indexingCompleted = false;
};

struct AO3Result {
  std::string scrapedDate;
  bool isCompleted = false;
  bool updateFound = false;
  bool downloaded = false;
};

struct FolderPickerResult {
  std::string singlePath;              // populated in SINGLE mode
  std::vector<std::string> multiPaths; // populated in MULTI mode
  bool isMulti = false;
};

struct Ao3IndexResult {
  bool indexingCompleted = false;
  bool successfullyIndexed = false;
};

// --- Updated Variant containing your custom results ---
using ResultVariant = std::variant<std::monostate, WifiResult, KeyboardResult, MenuResult, ChapterResult, PercentResult,
                                   PageResult, SyncResult, NetworkModeResult, FootnoteResult, FilePathResult, 
                                   BookActionResult, AO3Result, FolderPickerResult, Ao3IndexResult>;

struct ActivityResult {
  bool isCancelled = false;
  ResultVariant data;

  static ActivityResult cancel() {
    ActivityResult r;
    r.isCancelled = true;
    return r;
  }

  explicit ActivityResult() = default;

  template <typename ResultType>
    requires std::is_constructible_v<ResultVariant, ResultType&&>
  // cppcheck-suppress noExplicitConstructor
  ActivityResult(ResultType&& result) : data{std::forward<ResultType>(result)} {}
};

using ActivityResultHandler = std::function<void(const ActivityResult&)>;
