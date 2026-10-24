#pragma once

#include <string>
#include "BookStatus.h"
#include "activities/UiListActivity.h"
#include "../Ao3ArchiveHelper.h"
#include "components/OptionPopup.h"

enum class BookActionMode {
  FULL,      // AO3 Library / file browser long-press
  DASHBOARD  // Dashboard tabs 0-2 long-press
};

class BookActionActivity final : public UiListActivity {
  std::string filePath;
  std::string fileName;
  BookStatus currentStatus = BookStatus::START;
  BookStatus initialStatus = BookStatus::START;
  bool hasAo3LibraryInfo       = false;
  bool isMarkedForLater        = false;
  bool isEpub                  = false;
  bool isXtc                   = false;
  bool isAlreadyArchived       = false;
  BookActionMode mode;
  OptionPopup optionPopup;

  // Row storage reused by every buildScreen() pass. Building the rows into
  // member storage (instead of local std::vectors) avoids allocator churn on
  // every repaint. FULL mode shows at most 6 rows, so 8 leaves headroom.
  static constexpr int MAX_ROWS = 8;
  freeink::ui::ListItem rowItems[MAX_ROWS];
  std::string rowTitles[MAX_ROWS];  // backing storage for rowItems[i].label
  std::string rowValues[MAX_ROWS];  // backing storage for rowItems[i].value

  public:
  BookActionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                     std::string filePath, std::string fileName,
                     BookActionMode mode = BookActionMode::FULL);

  void onEnter() override;
  void render(RenderLock&&) override;

 protected:
  // --- UiListActivity Overrides ---
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;

  bool handleCustomInput() override;
  bool handleButtons() override;
  void navigateButtons() override;

  // --- Chrome & Footer Overrides ---
  const char* headerTitle() const override { return fileName.c_str(); }
  void drawFooter() override;

  private:
  void saveStatus();
  void saveStatusIfModified();
  void openStatusPopup();
  int  logicalRow(int visualIndex) const;
};