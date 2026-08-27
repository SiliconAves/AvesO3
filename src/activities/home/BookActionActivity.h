#pragma once

#include <string>
#include "BookStatus.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "../Ao3ArchiveHelper.h"

enum class BookActionMode {
  FULL,      // AO3 Library / file browser long-press
  DASHBOARD  // Dashboard tabs 0-2 long-press
};

class BookActionActivity final : public Activity {
  std::string filePath;
  std::string fileName;
  int selectorIndex = 0;
  BookStatus currentStatus = BookStatus::START;
  BookStatus initialStatus = BookStatus::START;
  ButtonNavigator buttonNavigator;
  bool hasAo3LibraryInfo       = false;
  bool isMarkedForLater        = false;
  bool isEpub                  = false;
  bool isXtc                   = false;
  bool skipFirstConfirmRelease = false;
  bool isAlreadyArchived       = false;
  BookActionMode mode;

 public:
  BookActionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                     std::string filePath, std::string fileName,
                     BookActionMode mode = BookActionMode::FULL);

  void onEnter() override;
  void render(RenderLock&& lock) override;
  void loop() override;

 private:
  void saveStatus();
  int  logicalRow(int visualIndex) const;
  int  visibleRowCount() const;
};