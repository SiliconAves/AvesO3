#pragma once
#include <string>
#include <vector>
#include "../Activity.h"
#include "../../util/ButtonNavigator.h"

class Ao3CoverAssignmentActivity final : public Activity {
 public:
  explicit Ao3CoverAssignmentActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Ao3CoverAssign", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Screen { FANDOMS, FILES };
  Screen screen = Screen::FANDOMS;
  ButtonNavigator buttonNavigator;

  std::vector<std::string> fandoms;   // raw names from the index
  std::vector<std::string> files;     // files[0] = "None"
  int selector = 0;                   // 0-based (no tab bar row)
  int fileSelector = 0;

  void loadFandoms();
  void loadFiles();
  void renderFandoms();
  void renderFiles();
};