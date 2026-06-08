#include "BookActionActivity.h"

#include <Epub.h>
#include <HalStorage.h>
#include <I18n.h>

#include "../../components/UITheme.h"
#include "../util/ConfirmationActivity.h"

BookActionActivity::BookActionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath,
                                       std::string fileName)
    : Activity("BookAction", renderer, mappedInput), filePath(std::move(filePath)), fileName(std::move(fileName)) {}

void BookActionActivity::onEnter() {
  Activity::onEnter();

  // Load current status
  std::string cachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(filePath));
  FsFile f;
  if (Storage.openFileForRead("BROWSER", cachePath + "/progress.bin", f)) {
    uint8_t data[7];
    if (f.read(data, 7) >= 7) {
      currentStatus = static_cast<BookStatus>(data[6]);
      initialStatus = currentStatus;
    }
    f.close();
  }

  requestUpdate(true);
}

void BookActionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 fileName.c_str());

  auto rowTitle = [this](int index) {
    if (index == 0) {
      return std::string("Book Status: ") + getStatusLabel(currentStatus);
    }
    return std::string(tr(STR_DELETE));
  };

  GUI.drawList(renderer,
               Rect{0, metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing, renderer.getScreenWidth(),
                    renderer.getScreenHeight() - metrics.headerHeight - metrics.buttonHintsHeight -
                        metrics.verticalSpacing * 2},
               2, selectorIndex, rowTitle);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void BookActionActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (currentStatus != initialStatus) {
      saveStatus();
      BookActionResult res;
      res.modified = true;
      res.newStatus = currentStatus;
      setResult(ActivityResult(std::move(res)));
    }
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectorIndex == 0) {
      // Cycle status
      uint8_t s = static_cast<uint8_t>(currentStatus);
      s = (s + 1) % 5;
      currentStatus = static_cast<BookStatus>(s);
      requestUpdate(true);
    } else {
      // Trigger delete confirmation
      auto handler = [this](const ActivityResult& res) {
        if (!res.isCancelled) {
          BookActionResult result;
          result.deleted = true;
          result.modified = true;
          setResult(ActivityResult(std::move(result)));
          finish();
        } else {
          requestUpdate(true);
        }
      };
      std::string heading = std::string(tr(STR_DELETE)) + "?";
      startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, fileName), handler);
    }
    return;
  }

  buttonNavigator.onNext([this] {
    selectorIndex = (selectorIndex < 1) ? selectorIndex + 1 : 0;
    requestUpdate(true);
  });

  buttonNavigator.onPrevious([this] {
    selectorIndex = (selectorIndex > 0) ? selectorIndex - 1 : 1;
    requestUpdate(true);
  });
}

void BookActionActivity::saveStatus() {
  std::string cachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(filePath));
  FsFile f;
  
  uint8_t data[7] = {0, 0, 0, 0, 0, 0, static_cast<uint8_t>(currentStatus)};
  
  // Try to preserve position if file exists
  if (Storage.openFileForRead("BROWSER", cachePath + "/progress.bin", f)) {
    f.read(data, 6);
    f.close();
  }

  // Write updated status
  if (Storage.openFileForWrite("BROWSER", cachePath + "/progress.bin", f)) {
    f.write(data, 7);
    f.close();
  }
}

