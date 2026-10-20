#pragma once

#include <string>
#include "BookStatus.h"
#include "activities/UiListActivity.h"
#include "../Ao3ArchiveHelper.h"

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

 public:
  BookActionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                     std::string filePath, std::string fileName,
                     BookActionMode mode = BookActionMode::FULL);

  void onEnter() override;

 protected:
  // --- UiListActivity Overrides ---
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;

  bool handleButtons() override;
  void navigateButtons() override;

  // --- Chrome & Footer Overrides ---
  const char* headerTitle() const override { return fileName.c_str(); }
  void drawFooter() override;

 private:
  void saveStatus();
  void saveStatusIfModified();
  int  logicalRow(int visualIndex) const;
};