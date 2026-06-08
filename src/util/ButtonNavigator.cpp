#include "ButtonNavigator.h"

const MappedInputManager* ButtonNavigator::mappedInput = nullptr;

void ButtonNavigator::onNext(const Callback& callback) {
  onNextPress(callback);
  onNextContinuous(callback);
}

void ButtonNavigator::onPrevious(const Callback& callback) {
  onPreviousPress(callback);
  onPreviousContinuous(callback);
}

void ButtonNavigator::onFrontNext(const Callback& callback) {
  onPress(getFrontNextButtons(), callback);
  onFrontNextContinuous(callback);
}

void ButtonNavigator::onFrontPrevious(const Callback& callback) {
  onPress(getFrontPreviousButtons(), callback);
  onFrontPreviousContinuous(callback);
}

void ButtonNavigator::onSideNext(const Callback& callback) {
  onPress(getSideNextButtons(), callback);
  onSideNextContinuous(callback);
}

void ButtonNavigator::onSidePrevious(const Callback& callback) {
  onPress(getSidePreviousButtons(), callback);
  onSidePreviousContinuous(callback);
}

void ButtonNavigator::onFrontNextPress(const Callback& callback) { onPress(getFrontNextButtons(), callback); }

void ButtonNavigator::onFrontPreviousPress(const Callback& callback) { onPress(getFrontPreviousButtons(), callback); }

void ButtonNavigator::onSideNextPress(const Callback& callback) { onPress(getSideNextButtons(), callback); }

void ButtonNavigator::onSidePreviousPress(const Callback& callback) { onPress(getSidePreviousButtons(), callback); }

void ButtonNavigator::onPressAndContinuous(const Buttons& buttons, const Callback& callback) {
  onPress(buttons, callback);
  onContinuous(buttons, callback);
}

void ButtonNavigator::onNextPress(const Callback& callback) { onPress(getNextButtons(), callback); }

void ButtonNavigator::onPreviousPress(const Callback& callback) { onPress(getPreviousButtons(), callback); }

void ButtonNavigator::onNextRelease(const Callback& callback) { onRelease(getNextButtons(), callback); }

void ButtonNavigator::onPreviousRelease(const Callback& callback) { onRelease(getPreviousButtons(), callback); }

void ButtonNavigator::onNextContinuous(const Callback& callback) { onContinuous(getNextButtons(), callback); }

void ButtonNavigator::onPreviousContinuous(const Callback& callback) { onContinuous(getPreviousButtons(), callback); }

void ButtonNavigator::onFrontNextContinuous(const Callback& callback) { onContinuous(getFrontNextButtons(), callback); }

void ButtonNavigator::onFrontPreviousContinuous(const Callback& callback) {
  onContinuous(getFrontPreviousButtons(), callback);
}

void ButtonNavigator::onSideNextContinuous(const Callback& callback) { onContinuous(getSideNextButtons(), callback); }

void ButtonNavigator::onSidePreviousContinuous(const Callback& callback) { onContinuous(getSidePreviousButtons(), callback); }

void ButtonNavigator::onPress(const Buttons& buttons, const Callback& callback) {
  const bool wasPressed = std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->wasPressed(button);
  });

  if (wasPressed) {
    callback();
  }
}

void ButtonNavigator::onRelease(const Buttons& buttons, const Callback& callback) {
  const bool wasReleased = std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->wasReleased(button);
  });

  if (wasReleased) {
    if (lastContinuousNavTime == 0) {
      callback();
    }

    lastContinuousNavTime = 0;
  }
}

void ButtonNavigator::onContinuous(const Buttons& buttons, const Callback& callback) {
  const bool isPressed = std::any_of(buttons.begin(), buttons.end(), [this](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->isPressed(button) && shouldNavigateContinuously();
  });

  if (isPressed) {
    callback();
    lastContinuousNavTime = millis();
  }
}

bool ButtonNavigator::shouldNavigateContinuously() const {
  if (!mappedInput) return false;

  const bool buttonHeldLongEnough = mappedInput->getHeldTime() > continuousStartMs;
  const bool navigationIntervalElapsed = (millis() - lastContinuousNavTime) > continuousIntervalMs;

  return buttonHeldLongEnough && navigationIntervalElapsed;
}

int ButtonNavigator::nextIndex(const int currentIndex, const int totalItems) {
  if (totalItems <= 0) return 0;

  // Calculate the next index with wrap-around
  return (currentIndex + 1) % totalItems;
}

int ButtonNavigator::previousIndex(const int currentIndex, const int totalItems) {
  if (totalItems <= 0) return 0;

  // Calculate the previous index with wrap-around
  return (currentIndex + totalItems - 1) % totalItems;
}

int ButtonNavigator::nextPageIndex(const int currentIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0 || itemsPerPage <= 0) return 0;

  // When items fit on one page, use index navigation instead
  if (totalItems <= itemsPerPage) {
    return nextIndex(currentIndex, totalItems);
  }

  const int lastPageIndex = (totalItems - 1) / itemsPerPage;
  const int currentPageIndex = currentIndex / itemsPerPage;

  if (currentPageIndex < lastPageIndex) {
    return (currentPageIndex + 1) * itemsPerPage;
  }

  return 0;
}

int ButtonNavigator::previousPageIndex(const int currentIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0 || itemsPerPage <= 0) return 0;

  // When items fit on one page, use index navigation instead
  if (totalItems <= itemsPerPage) {
    return previousIndex(currentIndex, totalItems);
  }

  const int lastPageIndex = (totalItems - 1) / itemsPerPage;
  const int currentPageIndex = currentIndex / itemsPerPage;

  if (currentPageIndex > 0) {
    return (currentPageIndex - 1) * itemsPerPage;
  }

  return lastPageIndex * itemsPerPage;
}

int ButtonNavigator::nextFourIndex(const int currentIndex, const int totalItems) {
  if (totalItems <= 0) return 0;
  return (currentIndex + 4) % totalItems;
}

int ButtonNavigator::previousFourIndex(const int currentIndex, const int totalItems) {
  if (totalItems <= 0) return 0;
  return (currentIndex + totalItems - (4 % totalItems)) % totalItems;
}

int ButtonNavigator::nextThreeIndex(const int currentIndex, const int totalItems) {
  if (totalItems <= 0) return 0;
  return (currentIndex + 3) % totalItems;
}

int ButtonNavigator::previousThreeIndex(const int currentIndex, const int totalItems) {
  if (totalItems <= 0) return 0;
  return (currentIndex + totalItems - (3 % totalItems)) % totalItems;
}

