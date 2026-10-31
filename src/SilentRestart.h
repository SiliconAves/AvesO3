#pragma once
#include <cstdint>

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session.

// AFTER
void silentRestart();          // home screen
void silentRestartToReader();  // currently-open EPUB (APP_STATE.openEpubPath)

// Return hint for silentRestartToHint(). Default (target 0) = Home.
// RULE: set before launching a screen that can end in a silent restart
// (BookAction -> Ao3Receive); clear in that launch's result handler AND in
// the launcher's onExit(). Only silentRestartToHint() consumes it.
constexpr uint8_t SILENT_TARGET_DASHBOARD   = 2;
constexpr uint8_t SILENT_TARGET_AO3_LIBRARY = 3;

struct SilentReturnHint {
  uint8_t target = 0;   // 0 = none/Home
  uint8_t tab = 0;      // dashboard tab
  int16_t index = -1;   // dashboard row (0-based, -1 = none) or library selectorIndex
};
inline SilentReturnHint g_silentReturnHint;

void silentRestartToHint();  // restart to the hinted screen, or Home if no hint

// Reboots immediately after an activity releases exclusive raw storage. The
// RTC target ensures setup() lands on Home instead of resuming a reader.
void restartToHomeAfterStorageHandoff();
