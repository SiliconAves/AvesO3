#pragma once

#include "activities/UiListActivity.h"
#include <string>
#include <vector>

class Ao3EndOfBookActivity final : public UiListActivity {
 public:
    Ao3EndOfBookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                       std::string bookPath, bool isCompleted,
                       std::string seriesName, int seriesPart,
                       std::string ao3WorkId, std::string ao3UpdateDate,
                       int spineCountBeforeDownload, bool hadAfterword,
                       int ao3LibraryReturnIndex,
                       std::string bookTitle = "", std::string bookAuthor = "");

  void onEnter() override;

 protected:
  // --- UiListActivity Overrides ---
  int listCount() const override;
  void buildScreen(UiScreen& uiScreen) override;
  void activateIndex(int index) override;
  bool handleButtons() override;
  void navigateButtons() override;

  // --- Chrome & Footer Overrides ---
  const char* headerTitle() const override { return nullptr; }  // nullptr = no header band
  void drawFooter() override;

 private:
  std::string bookPath;
  std::string bookTitle;
  std::string bookAuthor;
  bool isCompleted;
  std::string seriesName;
  int seriesPart;
  std::string ao3WorkId;
  std::string ao3UpdateDate;
  int spineCountBeforeDownload;
  bool hadAfterword;
  int ao3LibraryReturnIndex;
  bool backHoldHandled = false;  // Back-hold already fired; ignore its release
  unsigned long enterMs = 0;   // input is ignored briefly after entry (leftover page-turn input)

  enum ActionRow {
    ROW_GO_HOME = 0,
    ROW_AO3_PAGE,
    ROW_SERIES,
    ROW_UPDATE,
    ROW_ARCHIVE
  };
  std::vector<ActionRow> activeRows;

  void loadBookInfo();
  void stepSelection(int delta);
  void buildActiveRows();
  void launchSeriesActivity();
  void returnToLibraryOrBrowser();
};