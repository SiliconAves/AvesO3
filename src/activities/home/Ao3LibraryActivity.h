#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "../../Ao3LibraryMetadata.h"
#include "../../BookStatus.h"
#include "../../util/ButtonNavigator.h"

class Ao3LibraryActivity final : public Activity {
 public:
  explicit Ao3LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Ao3Library", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Entry {
    std::string path;
    std::string title;
    std::string author;
    char rating;
    char warning;
    bool completed;
    std::string cachePath;
  };

  std::vector<Entry> entries;
  size_t selectorIndex = 0;
  bool isLoaded = false;
  ButtonNavigator buttonNavigator;

  // Page cache: holds full metadata + status for the 3 currently-visible entries.
  // Loaded once per page turn — 0 SD ops during normal navigation within a page.
  Ao3LibraryMetadata pageCache[3];
  BookStatus pageCacheStatus[3] = {BookStatus::START, BookStatus::START, BookStatus::START};
  std::vector<std::string> wrappedSummary[3]; // Pre-computed per page load
  int cachedPage = -1;
  bool buttonsSetup = false;

  void loadEntries();
  void loadPageCache(int page);
  BookStatus getBookStatus(const std::string& path);

  void renderEntry(RenderLock& lock, int y, const Entry& entry, int cacheSlot, bool selected);
  void drawAo3Square(RenderLock& lock, int x, int y, const Entry& entry, int s, BookStatus status);

  void renderSymbol(int x, int y, int s, char c, bool tl, bool tr, bool bl, bool br, int yOffset = 0);
  void renderStatusSymbol(int x, int y, int s, BookStatus status, bool tl, bool tr, bool bl, bool br, int yOffset = 0);
  void renderWarningSymbol(int x, int y, int s, char warning, bool tl, bool tr, bool bl, bool br, int yOffset = 0);
  void renderCompletionSymbol(int x, int y, int s, bool completed, bool tl, bool tr, bool bl, bool br, int yOffset = 0);
};
