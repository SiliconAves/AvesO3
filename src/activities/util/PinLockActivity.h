#pragma once
#include <I18n.h>

#include <string>
#include <utility>

#include "activities/Activity.h"

// Parental PIN screen. Setup mode (from Settings) verifies the current PIN when
// one exists, then asks for a new 4-digit PIN twice. Unlock mode (boot/wake)
// asks for the PIN and only then continues to the destination that boot would
// otherwise have opened directly. Digits are entered on the keyboard's number
// pad; this activity only shows the prompt behind it.
class PinLockActivity final : public Activity {
 public:
  enum class Mode { Setup, Unlock };

  explicit PinLockActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode,
                           std::string resumePath = "", bool resumeReader = false, bool needsWakeRefresh = false,
                           bool allowFastInitialReaderRefresh = false, bool rebootedFromPanic = false);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool blocksGlobalShortcuts() const override { return mode == Mode::Unlock; }

 private:
  enum class Stage { VerifyCurrent, EnterNew, ConfirmNew, Unlock };

  void launchKeyboard(StrId title);
  void onKeyboardResult(const ActivityResult& result);
  void continueAfterUnlock();

  Mode mode;
  Stage stage;
  // Where boot was headed before the lock intercepted it (Unlock mode only).
  std::string resumePath;
  // First entry of a new PIN, kept until it is confirmed.
  std::string newPin;
  bool resumeReader;
  bool needsWakeRefresh;
  bool allowFastInitialReaderRefresh;
  bool rebootedFromPanic;
  // Set when the keyboard could not be allocated; Confirm retries.
  bool keyboardLaunchFailed = false;
  StrId titleId = StrId::STR_ENTER_PIN;
};
