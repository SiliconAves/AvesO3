#include "IntervalSelectionActivity.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "components/UITheme.h"
#include "components/UiSliderDialog.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_SLIDER = 1;
constexpr fui::ActionId ACTION_STEP = 2;
constexpr fui::ActionId ACTION_CANCEL = 3;
constexpr fui::ActionId ACTION_OK = 4;
}  // namespace

IntervalSelectionActivity::IntervalSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     const char* activityName, const StrId titleId,
                                                     const int initialValue, const int minValue, const int maxValue,
                                                     const int smallStep, const int largeStep,
                                                     const StrId valueFormatId, const bool readerActivity,
                                                     const StrId maxBoundaryLabelId)
    : Activity(activityName, renderer, mappedInput),
      UiAppHost(renderer),
      titleId(titleId),
      valueFormatId(valueFormatId),
      maxBoundaryLabelId(maxBoundaryLabelId),
      value(initialValue),
      minValue(minValue),
      maxValue(maxValue),
      smallStep(smallStep),
      largeStep(largeStep),
      readerActivity(readerActivity) {}

int IntervalSelectionActivity::clampedValue(const int candidate) const {
  return std::clamp(candidate, minValue, maxValue);
}

void IntervalSelectionActivity::onEnter() {
  Activity::onEnter();
  value = clampedValue(value);
  resetUi();
  app.on(ACTION_SLIDER, &IntervalSelectionActivity::onSliderEvent, this);
  app.on(ACTION_STEP, &IntervalSelectionActivity::onStepEvent, this);
  app.on(ACTION_CANCEL, &IntervalSelectionActivity::onCancelEvent, this);
  app.on(ACTION_OK, &IntervalSelectionActivity::onOkEvent, this);
  app.setScreen(&IntervalSelectionActivity::intervalScreen, this);
  requestUpdate();
}

void IntervalSelectionActivity::adjustValue(const int delta) {
  value = clampedValue(value + delta);
  requestUpdate();
}

void IntervalSelectionActivity::setValue(const int candidate) {
  const int clamped = clampedValue(candidate);
  if (clamped == value) return;
  value = clamped;
  requestUpdate();
}

void IntervalSelectionActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void IntervalSelectionActivity::confirm() {
  setResult(IntervalResult{static_cast<uint32_t>(value)});
  finish();
}

void IntervalSelectionActivity::onSliderEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<IntervalSelectionActivity*>(user);
  if (event.dragPermille < 0) return;
  const int range = std::max(1, self->maxValue - self->minValue);
  self->setValue(self->minValue + (static_cast<int>(event.dragPermille) * range + 500) / 1000);
}

void IntervalSelectionActivity::onStepEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<IntervalSelectionActivity*>(user);
  self->adjustValue(event.value * self->smallStep);
}

void IntervalSelectionActivity::onCancelEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<IntervalSelectionActivity*>(user);
  self->app.clearTapFlash();  // the tap leaves this screen
  self->cancel();
}

void IntervalSelectionActivity::onOkEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<IntervalSelectionActivity*>(user);
  self->app.clearTapFlash();  // the tap leaves this screen
  self->confirm();
}

void IntervalSelectionActivity::loop() {
  // Touch goes through the FreeInkApp: render() registered the slider, -/+ zones,
  // and Cancel/OK hit rects; the slider follows the finger via InputDrag. Runs
  // before the Back handler because the release of a drag can also register as a
  // swipe (e.g. the left-edge rightward back gesture) — the drag must consume it
  // so it can't cancel the dialog.
  const auto route = routeTouch(mappedInput, false, /*routeHeld=*/true);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) {
    if (route.event.dragPermille >= 0) draggingSlider = true;
    return;
  }
  if (routingReady() && draggingSlider) {
    // Drag ended (possibly off the slider): swallow the tap/swipe events it produced.
    if (!route.snap.touchHeld) draggingSlider = false;
    return;
  }

  int tx = 0;
  int ty = 0;
  const int screenWidth = renderer.getScreenWidth();
  const int barWidth = std::min(360, std::max(0, screenWidth - 40));
  constexpr int barHeight = 16;
  const int barX = std::max(0, (screenWidth - barWidth) / 2);
  const int barY = 140;

  // Live drag on the slider: once a touch lands on the bar, the value follows the
  // finger until release. Runs before the Back/Confirm handlers because the release
  // of a drag can also register as a swipe (e.g. the left-edge rightward back
  // gesture) — the drag must consume it so it can't cancel or confirm the dialog.
  if (mappedInput.isScreenTouchHeld(tx, ty)) {
    if (draggingBar || (ty >= barY - 20 && ty < barY + barHeight + 20 && tx >= barX && tx < barX + barWidth)) {
      draggingBar = true;
      const int range = std::max(1, maxValue - minValue);
      const int dragged =
          clampedValue(minValue + std::clamp(tx - barX, 0, barWidth - 1) * range / std::max(1, barWidth - 1));
      if (dragged != value) {
        value = dragged;
        requestUpdate();
      }
      return;
    }
  } else if (draggingBar) {
    // Release frame of a drag: swallow the tap/swipe events it produced.
    draggingBar = false;
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancel();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirm();
    return;
  }

  if (mappedInput.wasScreenTapped(tx, ty)) {
    if (ty >= barY - 20 && ty < barY + barHeight + 20 && tx >= barX && tx < barX + barWidth) {
      const int range = std::max(1, maxValue - minValue);
      value = clampedValue(minValue + (tx - barX) * range / std::max(1, barWidth - 1));
      requestUpdate();
      return;
    }
    if (ty >= renderer.getScreenHeight() - 80) {
      if (tx < renderer.getScreenWidth() / 3) {
        ActivityResult result;
        result.isCancelled = true;
        setResult(std::move(result));
        finish();
      } else if (tx > renderer.getScreenWidth() * 2 / 3) {
        setResult(IntervalResult{static_cast<uint32_t>(value)});
        finish();
      }
      return;
    }
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustValue(-smallStep); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustValue(smallStep); });

  // On edge-button boards (X3, X4 Pro) the side buttons sit on the left/right edges of the screen rather
  // than as a vertical up/down rocker (X4), so BTN_UP is physically the left button and BTN_DOWN the right
  // one. Flip the large-step direction there so the left button decreases and the right button increases.
  const int upDelta = gpio.hasEdgeSideButtons() ? -largeStep : largeStep;
  const int downDelta = gpio.hasEdgeSideButtons() ? largeStep : -largeStep;
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this, upDelta] { adjustValue(upDelta); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down},
                                       [this, downDelta] { adjustValue(downDelta); });
}

void IntervalSelectionActivity::formatValue(char* buffer, const size_t size, const int forValue) const {
  if (maxBoundaryLabelId != StrId::STR_NONE_OPT && forValue == maxValue) {
    snprintf(buffer, size, "%s", I18N.get(maxBoundaryLabelId));
  } else if (valueFormatId != StrId::STR_NONE_OPT) {
    snprintf(buffer, size, I18N.get(valueFormatId), static_cast<unsigned int>(forValue));
  } else {
    snprintf(buffer, size, "%d", forValue);
  }
}

void IntervalSelectionActivity::intervalScreen(UiScreen& screen, void* user) {
  static_cast<IntervalSelectionActivity*>(user)->buildIntervalScreen(screen);
}

void IntervalSelectionActivity::buildIntervalScreen(UiScreen& screen) {
  char readout[64];
  formatValue(readout, sizeof(readout), value);

  // Step hints: front buttons do the small step, side buttons the large step. Built from
  // separate label + value strings (rather than splitting one localized sentence) so the layout
  // doesn't depend on translators preserving a hidden separator.
  char hints[2][64];
  char stepText[24];
  int hintIndex = 0;
  for (const auto& [labelId, step] :
       {std::pair{StrId::STR_STEP_HINT_FRONT, smallStep}, std::pair{StrId::STR_STEP_HINT_SIDE, largeStep}}) {
    if (valueFormatId != StrId::STR_NONE_OPT) {
      snprintf(stepText, sizeof(stepText), I18N.get(valueFormatId), static_cast<unsigned int>(step));
    } else {
      snprintf(stepText, sizeof(stepText), "%d", step);
    }
    snprintf(hints[hintIndex], sizeof(hints[hintIndex]), "%s %s", I18N.get(labelId), stepText);
    hintIndex++;
  }

  UiSliderDialogSpec spec;
  spec.readout = readout;
  spec.value = value - minValue;
  spec.max = std::max(1, maxValue - minValue);
  spec.sliderAction = ACTION_SLIDER;
  spec.stepAction = ACTION_STEP;
  spec.cancelAction = ACTION_CANCEL;
  spec.okAction = ACTION_OK;
  spec.hintLine1 = hints[0];
  spec.hintLine2 = hints[1];
  buildSliderDialogScreen(screen, renderer, mappedInput, spec);
}

void IntervalSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  renderer.drawCenteredText(UI_12_FONT_ID, 15, I18N.get(titleId), true, EpdFontFamily::BOLD);

  // Value readout, slider, hints, and the touch Cancel/OK pair render through the
  // app so the interactive elements register touch hit rects.
  renderUi();

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
