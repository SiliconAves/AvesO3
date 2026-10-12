#include "Ao3FandomLogo.h"
#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <algorithm>
#include <memory>
#include "Ao3FandomCoverStore.h"
#include "Ao3LibraryMetadata.h"

namespace {
constexpr int MAX_SRC  = 256;   // sanity limit on the source file
constexpr int MAX_LOGO = 80;    // largest displayed size
constexpr int PAD      = 8;     // margin inside the black rect
}

bool drawFandomLogo(const GfxRenderer& renderer, const std::string& fandom,
                    int rectX, int rectY, int rectW, int rectH) {
  if (fandom.empty()) return false;
  const std::string* file = Ao3FandomCoverStore::getInstance().find(fandom.c_str());
  if (!file || file->empty()) return false;

  const int boxW = std::min(MAX_LOGO, rectW - 2 * PAD);
  const int boxH = std::min(MAX_LOGO, rectH - 2 * PAD);
  if (boxW <= 0 || boxH <= 0) return false;

  HalFile f;
  if (!Storage.openFileForRead("LOGO", "/.fandom_covers/" + *file, f)) return false;

  bool drawn = false;
  {
    Bitmap bmp(f);                                     // dithering=false: no ditherer allocated
    // dimensions are valid only after parseHeaders()
    if (bmp.parseHeaders() == BmpReaderError::Ok && bmp.getWidth() <= MAX_SRC && bmp.getHeight() <= MAX_SRC) {
      int dw = bmp.getWidth(), dh = bmp.getHeight();
      if (dw > boxW || dh > boxH) {                    // shrink to fit, keep aspect, never upscale
        if (dw * boxH >= dh * boxW) { dh = dh * boxW / dw; dw = boxW; }
        else                        { dw = dw * boxH / dh; dh = boxH; }
      }
      dw = std::max(dw, 1); dh = std::max(dh, 1);
      renderer.drawBitmap1BitStencil(bmp, rectX + (rectW - dw) / 2, rectY + (rectH - dh) / 2, dw, dh);
      drawn = true;
    }
  }
  f.close();
  return drawn;
}

bool drawFandomLogoForBook(const GfxRenderer& renderer, const std::string& epubPath,
                           int rectX, int rectY, int rectW, int rectH) {
  const std::string info =
      "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(epubPath)) + "/ao3_library_info";
  std::string fandom;
  {
    HalFile f;
    if (!Storage.openFileForRead("LOGO", info, f)) return false;
    auto meta = std::unique_ptr<Ao3LibraryMetadata>(new Ao3LibraryMetadata());   // heap, freed at scope end
    if (f.read((uint8_t*)meta.get(), sizeof(Ao3LibraryMetadata)) == sizeof(Ao3LibraryMetadata) &&
        meta->isValid() && meta->fandom[0]) fandom = meta->fandom;
    f.close();
  }
  return drawFandomLogo(renderer, fandom, rectX, rectY, rectW, rectH);
}