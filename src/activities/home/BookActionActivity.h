#pragma once

#include <string>
#include <vector>

#include "BookStatus.h"
#include "activities/Activity.h"

#include "util/ButtonNavigator.h"

class BookActionActivity final : public Activity {
  std::string filePath;
  std::string fileName;
  int selectorIndex = 0;
  BookStatus currentStatus = BookStatus::START;
  BookStatus initialStatus = BookStatus::START;
  ButtonNavigator buttonNavigator;
  bool hasAo3LibraryInfo = false;

  // Marked for Later / file-type state, populated in onEnter()
  bool isMarkedForLater = false;
  bool isEpub = false;
  bool isXtc  = false;

 public:
  BookActionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath,
                     std::string fileName);

  void onEnter() override;
  void render(RenderLock&& lock) override;
  void loop() override;

 private:
  void saveStatus();

  // Maps a visual list index to a logical row constant:
  //   0 = Status cycle
  //   1 = Mark for Later toggle
  //   2 = Index Book
  //   3 = Delete
  // Returns -1 if the visual index is out of range.
  int logicalRow(int visualIndex) const;

  // Returns the number of rows that should be visible for the current file
  // and store state.
  int visibleRowCount() const;
};