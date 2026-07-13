#pragma once

#include <functional>
#include <vector>

#include "MappedInputManager.h"

class ButtonNavigator final {
  using Callback = std::function<void()>;
  using Buttons = std::vector<MappedInputManager::Button>;

  const uint16_t continuousStartMs;
  const uint16_t continuousIntervalMs;
  uint32_t lastContinuousNavTime = 0;
  static const MappedInputManager* mappedInput;

  [[nodiscard]] bool shouldNavigateContinuously() const;

 public:
  explicit ButtonNavigator(const uint16_t continuousIntervalMs = 500, const uint16_t continuousStartMs = 500)
      : continuousStartMs(continuousStartMs), continuousIntervalMs(continuousIntervalMs) {}

  static void setMappedInputManager(const MappedInputManager& mappedInputManager) { mappedInput = &mappedInputManager; }

  void onNext(const Callback& callback);
  void onPrevious(const Callback& callback);
  void onFrontNext(const Callback& callback);
  void onFrontPrevious(const Callback& callback);
  void onSideNext(const Callback& callback);
  void onSidePrevious(const Callback& callback);

  void onFrontNextPress(const Callback& callback);
  void onFrontPreviousPress(const Callback& callback);
  void onSideNextPress(const Callback& callback);
  void onSidePreviousPress(const Callback& callback);

  void onPressAndContinuous(const Buttons& buttons, const Callback& callback);

  void onNextPress(const Callback& callback);
  void onPreviousPress(const Callback& callback);
  void onPress(const Buttons& buttons, const Callback& callback);

  void onNextRelease(const Callback& callback);
  void onPreviousRelease(const Callback& callback);
  void onRelease(const Buttons& buttons, const Callback& callback);

  void onNextContinuous(const Callback& callback);
  void onPreviousContinuous(const Callback& callback);
  void onContinuous(const Buttons& buttons, const Callback& callback);

  void onFrontNextContinuous(const Callback& callback);
  void onFrontPreviousContinuous(const Callback& callback);
  void onSideNextContinuous(const Callback& callback);
  void onSidePreviousContinuous(const Callback& callback);

  [[nodiscard]] static int nextIndex(int currentIndex, int totalItems);
  [[nodiscard]] static int previousIndex(int currentIndex, int totalItems);

  [[nodiscard]] static int nextPageIndex(int currentIndex, int totalItems, int itemsPerPage);
  [[nodiscard]] static int previousPageIndex(int currentIndex, int totalItems, int itemsPerPage);

  [[nodiscard]] static int nextFourIndex(int currentIndex, int totalItems);
  [[nodiscard]] static int previousFourIndex(int currentIndex, int totalItems);

  [[nodiscard]] static int nextThreeIndex(int currentIndex, int totalItems);
  [[nodiscard]] static int previousThreeIndex(int currentIndex, int totalItems);


  [[nodiscard]] static Buttons getNextButtons() {
    return {MappedInputManager::Button::Down, MappedInputManager::Button::Right};
  }
  [[nodiscard]] static Buttons getPreviousButtons() {
    return {MappedInputManager::Button::Up, MappedInputManager::Button::Left};
  }

  [[nodiscard]] static Buttons getFrontNextButtons() { return {MappedInputManager::Button::Right}; }
  [[nodiscard]] static Buttons getFrontPreviousButtons() { return {MappedInputManager::Button::Left}; }
  [[nodiscard]] static Buttons getSideNextButtons() { return {MappedInputManager::Button::Down}; }
  [[nodiscard]] static Buttons getSidePreviousButtons() { return {MappedInputManager::Button::Up}; }
};