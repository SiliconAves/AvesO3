#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "BookStatus.h"
#include "RecentBooksStore.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class FileBrowserActivity final : public UiListActivity {
 public:
  // Books = standard reader browser; PickFirmware = filter to .bin only and return path via ActivityResult.
  enum class Mode { Books, PickFirmware };

 private:
  // File actions
  bool removeDirFile(const std::string& fullPath);
  void showEntryActions();  // hold on a row: directory -> delete confirm, file -> BookActionActivity
  void deleteSelected();

  Mode mode = Mode::Books;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  std::unique_ptr<char[]> fileNameBuffer;

  // Pull-based rows: the SDK list resolves each drawn row on demand through
  // provideRow() (fui::ListProps::rowProvider), so the only per-file
  // residency is `files` itself — no full-length rowNames/rowExtensions/
  // rowItems arrays (a 1000-file folder used to pin ~100KB of vectors plus a
  // heap copy of every display name, which aborted under -fno-exceptions
  // when the contiguous blocks no longer fit). The label/value strings for
  // the row being laid out live in these scratch buffers; the provider
  // contract only needs them valid until the next provideRow() call.
  static constexpr size_t ROW_NAME_BUF_SIZE = 512;  // NAME_BUFFER_SIZE + "[]" + terminator slack
  char rowNameBuf[ROW_NAME_BUF_SIZE]{};
  char rowExtBuf[16]{};
  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);

  // CJK fallback glyphs are prewarmed for a bounded window of rows around the
  // viewport (one SD pass per list page, like the reader TOC) instead of the
  // whole folder. -1 = nothing prewarmed; reset by loadFiles().
  static constexpr int PREWARM_WINDOW = 24;
  int prewarmedStart = -1;
  void prewarmRowGlyphs(int start);

  int listCount() const override { return static_cast<int>(files.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  // Long-press BACK goes to root; short Back goes up a directory (home/cancel at
  // root), and Confirm activates on release while a hold opens file actions.
  bool handleCustomInput() override;
  bool handleButtons() override;
  void navigateButtons() override; // [ADDED] Override the base navigation loop

  void drawChrome() override;
  void drawFooter() override;
  void activateSelected();

  // Data loading
  BookStatus getBookStatus(const std::string& path);
  // Status icons (fork). Direct-mapped cache of progress.bin statuses, filled lazily by provideRow()
  // for the rows actually drawn. A slot is valid only while its stored index matches (index % N).
  // 64 slots x 8 bytes = 512 B regardless of folder size. Cleared by loadFiles().
  struct StatusSlot {
    int index = -1;
    uint8_t status = 0;
  };
  static constexpr int STATUS_SLOTS = 64;
  StatusSlot statusSlots[STATUS_SLOTS];
  BookStatus statusFor(int index);
  void cacheStatus(int index, BookStatus status);
  void clearStatusCache();
  void loadFiles();
  size_t findEntry(const std::string& name) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books);
  void onEnter() override;
  void onExit() override;
};
