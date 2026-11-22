#include "PinLockActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "fontIds.h"

namespace {
constexpr size_t PIN_LENGTH = 4;
}  // namespace

PinLockActivity::PinLockActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const Mode mode,
                                 std::string resumePath, const bool resumeReader, const bool needsWakeRefresh,
                                 const bool allowFastInitialReaderRefresh, const bool rebootedFromPanic)
    : Activity("PinLock", renderer, mappedInput),
      mode(mode),
      stage(mode == Mode::Unlock              ? Stage::Unlock
            : SETTINGS.parentalPin[0] == '\0' ? Stage::EnterNew
                                              : Stage::VerifyCurrent),
      resumePath(std::move(resumePath)),
      resumeReader(resumeReader),
      needsWakeRefresh(needsWakeRefresh),
      allowFastInitialReaderRefresh(allowFastInitialReaderRefresh),
      rebootedFromPanic(rebootedFromPanic) {}

void PinLockActivity::onEnter() {
  Activity::onEnter();
  const bool choosingNewPin = mode != Mode::Unlock && stage != Stage::VerifyCurrent;
  launchKeyboard(choosingNewPin ? StrId::STR_SET_PIN_PROMPT : StrId::STR_ENTER_PIN);
}

void PinLockActivity::loop() {
  if (keyboardLaunchFailed && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    launchKeyboard(titleId);
  }
}

void PinLockActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, I18N.get(titleId));
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void PinLockActivity::launchKeyboard(const StrId title) {
  titleId = title;
  keyboardLaunchFailed = false;
  // While locking, the keyboard must also refuse global shortcuts, or the
  // power button could leave the lock from inside the keyboard.
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, I18N.get(title), "", PIN_LENGTH,
                                                           InputType::Pin, mode == Mode::Unlock);
  if (!keyboard) {
    LOG_ERR("PIN", "OOM: PIN keyboard activity");
    keyboardLaunchFailed = true;
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) { onKeyboardResult(result); });
}

void PinLockActivity::onKeyboardResult(const ActivityResult& result) {
  const auto* entry = std::get_if<KeyboardResult>(&result.data);

  if (mode == Mode::Unlock) {
    if (result.isCancelled || !entry || entry->text != SETTINGS.parentalPin) {
      launchKeyboard(result.isCancelled ? StrId::STR_ENTER_PIN : StrId::STR_INCORRECT_PIN);
      return;
    }
    continueAfterUnlock();
    return;
  }

  if (result.isCancelled || !entry) {
    setResult(ActivityResult::cancel());
    finish();
    return;
  }

  switch (stage) {
    case Stage::VerifyCurrent:
      if (entry->text == SETTINGS.parentalPin) {
        stage = Stage::EnterNew;
        launchKeyboard(StrId::STR_SET_PIN_PROMPT);
      } else {
        launchKeyboard(StrId::STR_INCORRECT_PIN);
      }
      return;

    case Stage::EnterNew:
      if (entry->text.size() != PIN_LENGTH) {
        launchKeyboard(StrId::STR_PIN_MUST_BE_4_DIGITS);
        return;
      }
      newPin = entry->text;
      stage = Stage::ConfirmNew;
      launchKeyboard(StrId::STR_CONFIRM_PIN);
      return;

    case Stage::ConfirmNew:
      if (entry->text.size() != PIN_LENGTH || newPin.size() != PIN_LENGTH || entry->text != newPin) {
        newPin.clear();
        stage = Stage::EnterNew;
        launchKeyboard(StrId::STR_INCORRECT_PIN);
        return;
      }
      strncpy(SETTINGS.parentalPin, newPin.c_str(), sizeof(SETTINGS.parentalPin));
      SETTINGS.parentalPin[sizeof(SETTINGS.parentalPin) - 1] = '\0';
      SETTINGS.pinLockEnabled = 1;
      SETTINGS.saveToFile();
      setResult(ActivityResult());
      finish();
      return;

    case Stage::Unlock:
      return;
  }
}

// Mirrors the tail of main.cpp's boot routing, which deferred to this lock.
void PinLockActivity::continueAfterUnlock() {
  if (resumeReader) {
    // Clear app state to avoid a boot loop if the epub doesn't load.
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    activityManager.goToReader(resumePath, allowFastInitialReaderRefresh);
  } else if (rebootedFromPanic) {
    activityManager.goToCrashReport();
  } else {
    activityManager.goHome(HomeMenuItem::NONE, needsWakeRefresh);
  }
}
