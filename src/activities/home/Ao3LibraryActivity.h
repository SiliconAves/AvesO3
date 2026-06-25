#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "../../Ao3LibraryMetadata.h"
#include "../../BookStatus.h"
#include "../../Ao3ViewEntry.h"
#include "../../Ao3SortFilterState.h"
#include "../../util/ButtonNavigator.h"

struct FilterHashes {
    bool     fandomActive;
    uint32_t fandomHash;
    bool     relationshipActive;
    uint32_t relationshipHash;
    bool     relationshipNoneOnly;
};

inline FilterHashes computeFilterHashes(const SortFilterState& state) {
    FilterHashes h;
    h.fandomActive         = state.fandom[0] != '\0';
    h.fandomHash           = h.fandomActive ? fnv1a(state.fandom) : 0;
    h.relationshipActive   = state.relationship[0] != '\0';
    h.relationshipHash     = h.relationshipActive ? fnv1a(state.relationship) : 0;
    h.relationshipNoneOnly = state.relationshipNoneOnly;
    return h;
}

class Ao3LibraryActivity final : public Activity {
 public:
  explicit Ao3LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Ao3Library", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class IndexState {
    UNKNOWN,  // not yet loaded
    OK,       // index file valid
    MISSING,  // ao3_library_index.bin does not exist
    CORRUPT   // file exists but failed magic/version/OOM sanity check
  };

  enum class ScreenState {
    LIBRARY,
    FILTER_PANEL,
    FANDOM_PICKER,
    RELATIONSHIP_PICKER
  };

  std::vector<ViewEntry> viewEntries;
  size_t     selectorIndex = 0;
  IndexState indexState    = IndexState::UNKNOWN;
  ScreenState screenState  = ScreenState::LIBRARY;
  ButtonNavigator buttonNavigator;

  // Page cache
  Ao3LibraryMetadata      pageCache[3];
  BookStatus              pageCacheStatus[3] = {BookStatus::START, BookStatus::START, BookStatus::START};
  std::vector<std::string> wrappedSummary[3];
  int  cachedPage  = -1;
  bool buttonsSetup = false;

  // Filter & Sort State
  SortFilterState activeState;
  SortFilterState pendingState;
  int overlayRowIndex = 0; // 0=Fandom, 1=Relationship, 2=Sort By, 3=Order, 4=Confirm

  // Pickers support
  std::vector<std::string> uniqueFandoms;
  std::vector<std::string> pickerItems;
  size_t pickerSelectedIndex = 0;
  bool pickerHasNone = false;

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

  // Sorting & Filtering helpers
  void loadSortFilterState();
  void saveSortFilterState() const;
  void resortViewEntries();
  void rebuildViewEntries();
  void applyStateChange(const SortFilterState& prev, const SortFilterState& next);
  bool passesFilter(const ViewEntry& v, const FilterHashes& h) const;

  void buildFandomList(std::vector<std::string>& out) const;
  void buildRelationshipList(const char* fandom, std::vector<std::string>& out, bool& hasNoneEntries) const;

  // Rendering subsets
  void renderLibrary(RenderLock& lock);
  void renderFilterOverlay();
  void renderPicker();
};
