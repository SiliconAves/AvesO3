#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/AvesLogo.h"

constexpr int LOGO_W = 160;
constexpr int LOGO_H = 192;

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Place logo slightly above vertical center to leave room for text below
  const int logoX = (pageWidth  - LOGO_W) / 2 - 12;
  const int logoY = (pageHeight - LOGO_H) / 2 - 15;

  renderer.clearScreen();
  renderer.drawImage(AvesLogo, logoX, logoY, LOGO_W, LOGO_H);
  renderer.drawCenteredText(UI_10_FONT_ID, logoY + LOGO_H - 10, "AvesO3", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, logoY + LOGO_H + 15, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
}
