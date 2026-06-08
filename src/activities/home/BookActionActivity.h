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

 public:
  BookActionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath,
                     std::string fileName);

  void onEnter() override;
  void render(RenderLock&& lock) override;
  void loop() override;

 private:
  void saveStatus();
};
