#include "Ao3EndOfBookActivity.h"

#include <cstdint>
#include <string>
#include <variant>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "I18n.h"
#include "ReaderUtils.h"
#include "EpubReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "HalStorage.h"

#include "Ao3Librarian.h"
#include "Ao3MarkedForLaterStore.h"
#include "activities/network/AO3SyncActivity.h"
#include "Ao3EndOfBookSeriesActivity.h"
#include "../home/Ao3LibraryActivity.h"
#include "activities/Ao3PageQrActivity.h"
#include "activities/Ao3ArchiveHelper.h"
#include "activities/util/ConfirmationActivity.h"

#include <FreeInkUIIcon.h>
#include "components/icons/ao3EndOfBookIcons32.h"

namespace fui = freeink::ui;

namespace {
inline uint32_t fnv1a_local(const char* text) {
  uint32_t hash = 2166136261u;
  while (*text) {
    hash ^= static_cast<uint8_t>(*text++);
    hash *= 16777619u;
  }
  return hash;
}

// Action button geometry. Width is not fixed: it is derived from the widest
// label (icon column + text gap + label + right padding), capped to the screen.
constexpr int16_t BUTTON_HEIGHT = 48;
constexpr int16_t BUTTON_GAP = 14;        // between buttons
constexpr int16_t BUTTON_ICON_SIZE = 32;
constexpr int16_t BUTTON_ICON_PAD = 12;   // left edge -> icon
constexpr int16_t BUTTON_TEXT_GAP = 12;   // icon -> label
constexpr int16_t BUTTON_RIGHT_PAD = 20;  // label -> right edge
constexpr uint8_t BUTTON_RADIUS = 4;

// Screen layout. TITLE_TOP_REF is measured on a REF_SCREEN_HEIGHT screen and
// scaled linearly on other panels.
constexpr unsigned long BACK_LONG_PRESS_MS = 1000;  // hold Back -> library / file browser
constexpr int TITLE_TOP_REF = 55;
constexpr int REF_SCREEN_HEIGHT = 800;
constexpr int16_t TEXT_SIDE_MARGIN = 30;  // title / series / author / status
constexpr int16_t AUTHOR_GAP = 15;   // series (or title) -> author
constexpr int16_t BANNER_LIFT = 12;  // extra space under the kudos banner

// Fits text into at most two wrapped lines. When it overflows, the text is cut
// on a UTF-8 boundary and ".." is appended. Measuring uses maxLines = 3 so an
// overflow shows up as a third line instead of being silently ellipsized.
std::string fitTwoLines(const fui::DrawTarget& target, const std::string& text, fui::TextStyle style,
                        int16_t width) {
  style.maxLines = 3;
  const int16_t maxHeight = static_cast<int16_t>(2 * target.lineHeight(style.font));
  auto fits = [&](const std::string& candidate) {
    return fui::measureWrappedText(target, candidate.c_str(), style, width).height <= maxHeight;
  };
  if (fits(text)) return text;

  auto build = [&](size_t length) {
    size_t cut = length;
    while (cut > 0 && (static_cast<uint8_t>(text[cut]) & 0xC0) == 0x80) --cut;  // stay on a character start
    std::string candidate = text.substr(0, cut);
    while (!candidate.empty() && candidate.back() == ' ') candidate.pop_back();
    candidate += "..";
    return candidate;
  };

  size_t lo = 0;
  size_t hi = text.size();
  while (lo < hi) {  // largest prefix that still fits together with ".."
    const size_t mid = (lo + hi + 1) / 2;
    if (fits(build(mid))) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  return build(lo);
}
} // namespace

// ---------------------------------------------------------------------------
//  Constructor & Setup
// ---------------------------------------------------------------------------

Ao3EndOfBookActivity::Ao3EndOfBookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           std::string bookPath, bool isCompleted,
                                           std::string seriesName, int seriesPart,
                                           std::string ao3WorkId, std::string ao3UpdateDate,
                                           int spineCountBeforeDownload, bool hadAfterword,
                                           int ao3LibraryReturnIndex)
    : UiListActivity("Ao3EndOfBook", renderer, mappedInput, false),
      bookPath(std::move(bookPath)),
      isCompleted(isCompleted),
      seriesName(std::move(seriesName)),
      seriesPart(seriesPart),
      ao3WorkId(std::move(ao3WorkId)),
      ao3UpdateDate(std::move(ao3UpdateDate)),
      spineCountBeforeDownload(spineCountBeforeDownload),
      hadAfterword(hadAfterword),
      ao3LibraryReturnIndex(ao3LibraryReturnIndex) {
          
  // Slots: TITLE = 12pt, BODY = 10pt, SMALL = 8pt. Weight is a TextStyle flag.
  uiTarget.setFont(fui::GfxRendererTarget::FONT_TITLE, UI_12_FONT_ID);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  
  buildActiveRows();
}

void Ao3EndOfBookActivity::buildActiveRows() {
  activeRows.clear();
  activeRows.push_back(ROW_GO_HOME);
  activeRows.push_back(ROW_AO3_PAGE);
  if (!seriesName.empty()) {
    activeRows.push_back(ROW_SERIES);
  }
  if (!isCompleted) {
    activeRows.push_back(ROW_UPDATE);
  }
  if (!Ao3ArchiveHelper::isInReadFolder(bookPath)) {
    activeRows.push_back(ROW_ARCHIVE);
  }
}

void Ao3EndOfBookActivity::loadBookInfo() {
  // Metadata comes from the book's cache, so no caller needs to pass it in.
  auto epub = std::make_unique<Epub>(bookPath, "/.crosspoint");
  if (epub->load(false, true)) {
    bookTitle = epub->getTitle();
    bookAuthor = epub->getAuthor();
  }
}

void Ao3EndOfBookActivity::onEnter() {
  loadBookInfo();
  UiListActivity::onEnter();
  requestUpdate(true);
}

int Ao3EndOfBookActivity::listCount() const {
  return activeRows.size();
}

void Ao3EndOfBookActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---------------------------------------------------------------------------
//  UI Layout & Rendering
// ---------------------------------------------------------------------------

void Ao3EndOfBookActivity::buildScreen(UiScreen& uiScreen) {
  const auto& metrics = UITheme::getInstance().getMetrics();

  fui::DrawTarget& target = uiScreen.target();
  const fui::ThemeTokens& theme = uiScreen.theme();

  // Title sits TITLE_TOP_REF px from the top on the reference screen height.
  const int16_t titleTop =
      static_cast<int16_t>(TITLE_TOP_REF * uiScreen.device().height / REF_SCREEN_HEIGHT);
  uiScreen.setContentMarginFromScreen(fui::Insets{
      titleTop,
      0,
      static_cast<int16_t>(metrics.buttonHintsHeight + theme.spaceMd + BANNER_LIFT),
      0});

  // --- Text roles (size comes from the font slot, weight from the flag) ---
  fui::TextStyle titleStyle = theme.titleText;  // 12pt bold
  titleStyle.align = fui::TextAlign::Center;
  titleStyle.bold = true;
  titleStyle.maxLines = 2;

  fui::TextStyle authorStyle = theme.bodyText;  // 10pt regular
  authorStyle.align = fui::TextAlign::Center;

  fui::TextStyle statusStyle = theme.bodyText;  // 10pt bold
  statusStyle.align = fui::TextAlign::Center;
  statusStyle.bold = true;

  fui::TextStyle hintStyle = theme.smallText;  // 8pt regular
  hintStyle.align = fui::TextAlign::Center;
  hintStyle.maxLines = 1;

  fui::TextStyle bannerStyle = theme.smallText;  // 8pt regular, left aligned
  bannerStyle.align = fui::TextAlign::Left;
  bannerStyle.maxLines = 3;

  auto iconFor = [this](int i) -> fui::BitmapRef {
    switch (activeRows[i]) {
      case ROW_GO_HOME:  return fui::bitmapFromIcon(icon_go_home_32);
      case ROW_AO3_PAGE: return fui::bitmapFromIcon(icon_ao3_page_32);
      case ROW_SERIES:   return fui::bitmapFromIcon(icon_series_fic_32);
      case ROW_UPDATE:   return fui::bitmapFromIcon(icon_update_fic_32);
      case ROW_ARCHIVE:  return fui::bitmapFromIcon(icon_archive_fic_32);
    }
    return {};
  };

  fui::TextStyle buttonText = theme.bodyText;  // 10pt regular, left aligned
  buttonText.align = fui::TextAlign::Left;

  // --- Bottom: kudos banner, sized from its measured text ---
  static const char* const kKudosText = "Remember to leave kudos and a comment on the AO3 story page. :)";
  const fui::Rect content = uiScreen.body();
  const fui::Rect bannerArea = content.inset(fui::makeInsets(0, theme.spaceLg, 0, theme.spaceLg));
  const fui::Insets bannerPad = fui::makeInsets(theme.spaceMd);
  const fui::Size bannerText = fui::measureWrappedText(
      target, kKudosText, bannerStyle, static_cast<int16_t>(bannerArea.width - bannerPad.left - bannerPad.right));
  const int16_t bannerHeight = static_cast<int16_t>(bannerText.height + bannerPad.top + bannerPad.bottom);

  const fui::Rect bottomRect = uiScreen.takeBottom(bannerHeight);
  const fui::Rect bannerBox{bannerArea.x, bottomRect.y, bannerArea.width, bannerHeight};

  const fui::Paint borderPaint = fui::Paint::solid(fui::Color::Black);
  target.fill(bannerBox, fui::Paint::dither(fui::Color::LightGray), BUTTON_RADIUS);
  target.stroke(bannerBox, borderPaint, 1, BUTTON_RADIUS);
  target.text(bannerBox.inset(bannerPad), kKudosText, bannerStyle);

  // --- Measure every block first so the free space can be split evenly ---
  const fui::Rect topArea =
      uiScreen.body().inset(fui::makeInsets(0, TEXT_SIDE_MARGIN, 0, TEXT_SIDE_MARGIN));
  const fui::Rect body = uiScreen.body();

  static const char* const kHintLine1 = "Scan AO3 Page QR or select Update";
  static const char* const kHintLine2 = "to check for new chapters.";
  const char* statusText = isCompleted ? "End of Book" : "Current End Reached.";

  // Title, series and author: max two lines each, truncated with "..".
  const std::string titleText = fitTwoLines(target, bookTitle, titleStyle, topArea.width);
  std::string seriesText;
  if (!seriesName.empty()) {
    seriesText = fitTwoLines(target, "Book " + std::to_string(seriesPart) + " of " + seriesName, authorStyle,
                             topArea.width);
  }
  std::string authorText;
  if (!bookAuthor.empty()) {
    authorText = fitTwoLines(target, "by " + bookAuthor, authorStyle, topArea.width);
  }

  auto heightOf = [&](const char* text, const fui::TextStyle& style) {
    return fui::measureWrappedText(target, text, style, topArea.width).height;
  };
  const int16_t titleH = heightOf(titleText.c_str(), titleStyle);
  const int16_t seriesH = seriesText.empty() ? 0 : heightOf(seriesText.c_str(), authorStyle);
  const int16_t authorH = authorText.empty() ? 0 : heightOf(authorText.c_str(), authorStyle);
  const int16_t statusH = heightOf(statusText, statusStyle);
  const int16_t hintLineH = heightOf(kHintLine1, hintStyle);
  const int16_t hintH = isCompleted ? 0 : static_cast<int16_t>(2 * hintLineH);
  const int16_t statusBlockH = static_cast<int16_t>(statusH + (isCompleted ? 0 : theme.spaceSm + hintH));

  auto labelFor = [this](int i) -> const char* {
    switch (activeRows[i]) {
      case ROW_AO3_PAGE: return "AO3 Page";
      case ROW_SERIES:   return "Series";
      case ROW_UPDATE:   return "Update";
      case ROW_ARCHIVE:  return "Archive";
      case ROW_GO_HOME:  return "Go Home";
    }
    return "";
  };
  const int count = listCount();
  int16_t widest = 0;
  for (int i = 0; i < count; i++) {
    const int16_t w = target.measureText(buttonText.font, labelFor(i), buttonText).width;
    if (w > widest) widest = w;
  }
  const int16_t textOffset = BUTTON_ICON_PAD + BUTTON_ICON_SIZE + BUTTON_TEXT_GAP;
  int16_t btnWidth = static_cast<int16_t>(textOffset + widest + BUTTON_RIGHT_PAD);
  if (btnWidth > body.width) btnWidth = body.width;
  const int16_t buttonsH =
      static_cast<int16_t>(count * BUTTON_HEIGHT + (count > 0 ? (count - 1) * BUTTON_GAP : 0));

  // --- Top block: title, optional series, author ---
  int16_t y = topArea.y;
  auto drawLine = [&](const char* text, const fui::TextStyle& style, int16_t h, int16_t gapAfter) {
    target.text(fui::Rect{topArea.x, y, topArea.width, h}, text, style);
    y = static_cast<int16_t>(y + h + gapAfter);
  };
  drawLine(titleText.c_str(), titleStyle, titleH, 0);
  if (!seriesText.empty()) {
    y = static_cast<int16_t>(y + theme.spaceSm);  // title -> series
    drawLine(seriesText.c_str(), authorStyle, seriesH, 0);
  }
  if (!authorText.empty()) {
    y = static_cast<int16_t>(y + AUTHOR_GAP);  // series (or title) -> author
    drawLine(authorText.c_str(), authorStyle, authorH, 0);
  }
  const int16_t topBlockBottom = y;

  // Status block and buttons share the space between the author line and the
  // banner: three equal gaps puts the status block midway between the title
  // block and the buttons, and the buttons midway between the status block
  // and the banner.
  int16_t freeSpace = static_cast<int16_t>(bannerBox.y - topBlockBottom - statusBlockH - buttonsH);
  if (freeSpace < 0) freeSpace = 0;
  const int16_t gap = static_cast<int16_t>(freeSpace / 3);

  // --- Status block ---
  y = static_cast<int16_t>(topBlockBottom + gap);
  if (isCompleted) {
    drawLine(statusText, statusStyle, statusH, 0);
  } else {
    drawLine(statusText, statusStyle, statusH, theme.spaceSm);
    drawLine(kHintLine1, hintStyle, hintLineH, 0);
    drawLine(kHintLine2, hintStyle, hintLineH, 0);
  }

  // --- Buttons ---
  int16_t btnY = static_cast<int16_t>(topBlockBottom + gap + statusBlockH + gap);
  const int16_t startX = static_cast<int16_t>(body.x + (body.width - btnWidth) / 2);

  fui::StyleSet buttonStyles = fui::outlinedButtonStyles(BUTTON_RADIUS);
  buttonStyles.selected.background = fui::Paint::dither(fui::Color::LightGray);

  for (int i = 0; i < count; i++) {
    const fui::Rect btnRect{startX, btnY, btnWidth, BUTTON_HEIGHT};
    // Touch: register the button's hit area; a tap arrives as ACTION_ROW with
    // value = i and the base class selects the row and calls activateIndex().
    uiScreen.frame().hit(btnRect, ACTION_ROW, static_cast<int16_t>(i), fui::InputTouch);

    const fui::BoxStyle& box = (i == nav.selected) ? buttonStyles.selected : buttonStyles.normal;
    target.fill(btnRect, box.background, box.radius);
    target.stroke(btnRect, box.border, box.borderWidth, box.radius);

    const fui::Rect iconRect{static_cast<int16_t>(btnRect.x + BUTTON_ICON_PAD),
                             static_cast<int16_t>(btnRect.y + (BUTTON_HEIGHT - BUTTON_ICON_SIZE) / 2),
                             BUTTON_ICON_SIZE, BUTTON_ICON_SIZE};
    target.bitmap(iconRect, iconFor(i), fui::BitmapMode::Center);

    const fui::Rect textRect{static_cast<int16_t>(btnRect.x + textOffset), btnRect.y,
                             static_cast<int16_t>(btnRect.width - textOffset - 4), btnRect.height};
    target.text(textRect, labelFor(i), buttonText);

    btnY = static_cast<int16_t>(btnY + BUTTON_HEIGHT + BUTTON_GAP);
  }
}

// ---------------------------------------------------------------------------
//  Input Handlers
// ---------------------------------------------------------------------------

void Ao3EndOfBookActivity::stepSelection(int delta) {
  const int count = listCount();
  if (count <= 0) return;
  {
    RenderLock lock(*this);  // the render task reads nav while building the screen
    nav.selected = (nav.selected + delta % count + count) % count;
  }
  requestUpdate(true);
}

void Ao3EndOfBookActivity::navigateButtons() {
  buttonNavigator.onNext([this] { stepSelection(1); });
  buttonNavigator.onPrevious([this] { stepSelection(-1); });
}

bool Ao3EndOfBookActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }

  // Back: holding it fires the AO3 library / file browser jump as soon as the
  // hold time is reached, without waiting for the release. A tap (released
  // before that) returns to the reader.
  // wasLongPressed() fires once per hold and marks that hold's release as
  // suppressed in MappedInputManager, so the release can't leak into the screen
  // we jump to.
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Back, BACK_LONG_PRESS_MS)) {
    backHoldHandled = true;
    returnToLibraryOrBrowser();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (backHoldHandled) {  // the hold already acted; swallow the release
      backHoldHandled = false;
    } else {
      finish();
    }
    return true;
  }

  // Page-turn keys scroll the button list instead of flipping pages. Tilt is
  // ignored so a tilt can't move the selection by accident.
  auto [prevTrig, nextTrig, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  if (!fromTilt && (prevTrig || nextTrig)) {
    stepSelection(nextTrig ? 1 : -1);
    return true;
  }

  // Touch is not handled here: the base loop routes taps to the hit areas
  // registered in buildScreen(), so edge taps no longer flip pages.
  return false;
}

void Ao3EndOfBookActivity::activateIndex(int index) {
  if (index < 0 || index >= static_cast<int>(activeRows.size())) return;
  app.clearTapFlash();

  switch (activeRows[index]) {
    case ROW_AO3_PAGE:
      // Stacked on top of this screen (not replacing it), so Back returns here.
      startActivityForResult(std::make_unique<Ao3PageQrActivity>(renderer, mappedInput, bookPath),
                             [this](const ActivityResult&) { requestUpdate(true); });
      break;

    case ROW_SERIES:
      launchSeriesActivity();
      break;

    case ROW_UPDATE: {
      const auto path = bookPath;
      const auto spineCount = spineCountBeforeDownload;
      const auto afterword = hadAfterword;
      
      startActivityForResult(std::make_unique<AO3SyncActivity>(renderer, mappedInput, ao3WorkId, ao3UpdateDate, path),
        [this, path, spineCount, afterword](const ActivityResult& res) {
          auto epub = std::make_unique<Epub>(path, "/.crosspoint");

          if (!res.isCancelled && std::holds_alternative<AO3Result>(res.data)) {
            const auto& ao3Res = std::get<AO3Result>(res.data);
            if (ao3Res.downloaded) {
              epub->saveAo3Info(epub->getAo3WorkId(), ao3Res.scrapedDate, ao3Res.isCompleted);
              std::string cachePath = epub->getCachePath();
              Storage.remove((cachePath + "/book.bin").c_str());
              Storage.removeDir((cachePath + "/sections").c_str());
              Storage.removeDir((cachePath + "/html").c_str());

              MARKED_FOR_LATER_STORE.loadFromFile();
              MARKED_FOR_LATER_STORE.removeByPath(path);
              MARKED_FOR_LATER_STORE.clearEntries();

              int firstNewChapter = spineCount;
              if (afterword && spineCount > 0) {
                firstNewChapter -= 1;
              }
              EpubReaderUtils::saveProgress(*epub, firstNewChapter, 0, spineCount, std::nullopt, BookStatus::NEW_CHAPTER_AVAILABLE);
            } else {
              if (ao3Res.updateFound) {
                EpubReaderUtils::saveProgress(*epub, spineCount, 0, spineCount, std::nullopt, BookStatus::NEW_CHAPTER_AVAILABLE);
              } else {
                EpubReaderUtils::saveProgress(*epub, spineCount, 0, spineCount, std::nullopt, BookStatus::WAITING_FOR_CHAPTER);
              }
            }
          } else {
            EpubReaderUtils::saveProgress(*epub, spineCount, 0, spineCount, std::nullopt, BookStatus::WAITING_FOR_CHAPTER);
          }
          activityManager.goHome();
        });
      break;
    }

    case ROW_ARCHIVE: {
      // Same flow as "Move to Read Folder" in BookActionActivity.
      auto handler = [this](const ActivityResult& res) {
        if (res.isCancelled) {
          requestUpdate(true);
          return;
        }
        if (Ao3ArchiveHelper::archiveFic(bookPath).empty()) {  // empty = move failed
          requestUpdate(true);
          return;
        }
        // The fic left the library, so this screen has nothing left to show.
        returnToLibraryOrBrowser();
      };
      startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, "Move to Read Folder?",
                                                                    "The fic will leave the AO3 Library."),
                             handler);
      break;
    }

    case ROW_GO_HOME:
      onGoHome();
      break;
  }
}

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

void Ao3EndOfBookActivity::launchSeriesActivity() {
  const uint32_t originHash = static_cast<uint32_t>(std::hash<std::string>{}(bookPath));
  char truncatedName[32];
  strncpy(truncatedName, seriesName.c_str(), 31);
  truncatedName[31] = '\0';
  const uint32_t seriesHash = fnv1a_local(truncatedName);

  activityManager.replaceActivity(std::make_unique<Ao3EndOfBookSeriesActivity>(
      renderer, mappedInput,
      seriesName,
      seriesHash,
      originHash,
      bookPath));
}

void Ao3EndOfBookActivity::returnToLibraryOrBrowser() {
  if (ao3LibraryReturnIndex >= 0) {
    activityManager.goToAo3Library(static_cast<size_t>(ao3LibraryReturnIndex));
  } else {
    activityManager.goToFileBrowser(bookPath);
  }
}