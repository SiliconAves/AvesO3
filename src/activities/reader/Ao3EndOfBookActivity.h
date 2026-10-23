#pragma once

#include "activities/Activity.h"
#include <string>

class Ao3EndOfBookActivity : public Activity {
 public:
  Ao3EndOfBookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                       std::string bookPath, bool isCompleted,
                       std::string seriesName, int seriesPart,
                       std::string ao3WorkId, std::string ao3UpdateDate,
                       int spineCountBeforeDownload, bool hadAfterword,
                       int ao3LibraryReturnIndex);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  std::string bookPath;
  bool isCompleted;
  std::string seriesName;
  int seriesPart;
  std::string ao3WorkId;
  std::string ao3UpdateDate;
  int spineCountBeforeDownload;
  bool hadAfterword;
  int ao3LibraryReturnIndex;

  void launchSeriesActivity();
  void returnToLibraryOrBrowser();
};