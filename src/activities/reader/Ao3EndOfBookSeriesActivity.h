#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "../../Ao3LibraryMetadata.h"
#include "../../BookStatus.h"
#include "../../Ao3ViewEntry.h"
#include "../../util/ButtonNavigator.h"

class Ao3EndOfBookSeriesActivity final : public Activity {
 public:
  Ao3EndOfBookSeriesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                              std::string seriesName, uint32_t seriesHash,
                              uint32_t originCacheHash, std::string originEpubPath)
      : Activity("Ao3EndOfBookSeries", renderer, mappedInput),
        seriesName_(std::move(seriesName)),
        seriesHash_(seriesHash),
        originCacheHash_(originCacheHash),
        originEpubPath_(std::move(originEpubPath)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string seriesName_;
  uint32_t    seriesHash_;
  uint32_t    originCacheHash_;
  std::string originEpubPath_;

  std::vector<ViewEntry> viewEntries;
  size_t selectorIndex = 0;

  Ao3LibraryMetadata       pageCache[3];
  BookStatus               pageCacheStatus[3] = {BookStatus::START, BookStatus::START, BookStatus::START};
  std::vector<std::string> wrappedSummary[3];
  int cachedPage = -1;

  ButtonNavigator buttonNavigator;

  void loadViewEntries();
  void loadPageCache(int page);
  BookStatus getBookStatus(uint32_t cacheHash);

  void renderEntry(RenderLock& lock, int y, const ViewEntry& ve, int cacheSlot, bool selected);
  void drawAo3Square(RenderLock& lock, int x, int y, int s,
                     char rating, char warning, bool completed, BookStatus status);
  void renderSymbol(int x, int y, int s, char c, bool tl, bool tr, bool bl, bool br, int yOffset = 0);
  void renderStatusSymbol(int x, int y, int s, BookStatus status, bool tl, bool tr, bool bl, bool br, int yOffset = 0);
  void renderWarningSymbol(int x, int y, int s, char warning, bool tl, bool tr, bool bl, bool br, int yOffset = 0);
  void renderCompletionSymbol(int x, int y, int s, bool completed, bool tl, bool tr, bool bl, bool br, int yOffset = 0);
};