#include "HomeCoverCache.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>
#include "RecentBooksStore.h"


#include "UITheme.h"

#include "fontIds.h"
#include "icons/coverao3.h"

namespace fui = freeink::ui;

void HomeCoverCache::begin() {
  // Cover regions do not overlap. Each may widen by one physical byte per row.
  coverCacheCapacity = renderer.getRegionByteSize(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()) +
                       MAX_COVERS * std::max(renderer.getScreenWidth(), renderer.getScreenHeight());
  coverCache = HalMemory::allocatePsram(coverCacheCapacity);
  if (!coverCache) {
    LOG_ERR("HOME", "PSRAM cover cache unavailable (%u bytes); rendering uncached", unsigned(coverCacheCapacity));
    coverCacheCapacity = 0;
  }
}

void HomeCoverCache::invalidate() {
  coverCacheUsed = 0;
  cachedCovers.fill(CachedCover{});
}

void HomeCoverCache::invalidate(size_t index) {
  if (index < cachedCovers.size()) cachedCovers[index].valid = false;
}

void HomeCoverCache::prepare() {
  const int orientation = static_cast<int>(renderer.getOrientation());
  if (coverCacheOrientation != orientation) {
    invalidate();
    coverCacheOrientation = orientation;
  }
}

bool HomeCoverCache::paint(fui::Rect rect, size_t index, const std::string& path, bool isAo3,
                           const RecentBook* book) {
  if (index >= cachedCovers.size()) return false;
  auto& cached = cachedCovers[index];
  uint8_t* const cacheData = coverCache.get();
  if (coverCache && cached.valid && cached.rect.x == rect.x && cached.rect.y == rect.y &&
      cached.rect.width == rect.width && cached.rect.height == rect.height &&
      renderer.copyBufferToRegion(rect.x, rect.y, rect.width, rect.height, cacheData + cached.offset, cached.bytes)) {
    return true;
  }
  cached.valid = false;
  bool drawn = false;
  if (!path.empty() && Storage.openFileForRead("HOME", path, coverFile)) {
    if (coverBitmap.parseHeaders() == BmpReaderError::Ok && coverBitmap.getWidth() > 0 && coverBitmap.getHeight() > 0) {
      // The art nudges a few px right of center; the spine below hugs its
      // left edge either way.
      constexpr int ART_SHIFT = 3;
      drawn = GUI.drawCoverThumbFill(renderer, coverBitmap, Rect{rect.x, rect.y, rect.width, rect.height}, ART_SHIFT);
      if (drawn) {
        renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
        // False spine glued to the art's left edge (centered art means that
        // edge moves with each cover's fit margin): a dark band with a
        // dithered crease makes every cover read as a bound book. Drawn here
        // so the PSRAM snapshot below captures it.
        const int artLeft = std::max<int>(rect.x, rect.x + (rect.width - coverBitmap.getWidth()) / 2 + ART_SHIFT);
        renderer.fillRect(artLeft, rect.y, 3, rect.height);
        renderer.fillRectDither(artLeft + 3, rect.y, 2, rect.height, Color::LightGray);
      }
    }
    coverFile.close();
  }
  if (!drawn) {
    GUI.drawCoverPlaceholder(renderer, Rect{rect.x, rect.y, rect.width, rect.height}, isAo3 ? Ao3Icon : nullptr);
    if (isAo3 && book) {
      constexpr int PAD = 10;  // inside the black area, all four sides
      constexpr int GAP = 4;  // minimum space between the title block and the author
      const int maxW = rect.width - 2 * PAD;
      const int lh = renderer.getLineHeight(SMALL_FONT_ID);
      const int titleTop = rect.y + rect.height / 3 + PAD;  // the black area starts at height / 3
      const int bottom = rect.y + rect.height - PAD;
      const bool hasAuthor = !book->author.empty();
      const int authorY = bottom - lh - GAP;  // fixed, so it is at the same height on every cover

      const auto drawCentered = [&](const std::string& s, int y) {
        const int tw = renderer.getTextWidth(SMALL_FONT_ID, s.c_str());
        renderer.drawText(SMALL_FONT_ID, rect.x + (rect.width - tw) / 2, y, s.c_str(), false);
      };

      // The title gets the room above the author, up to 3 lines (wrappedText adds the ".." itself).
      const int titleBottom = hasAuthor ? authorY - GAP : bottom;
      const int maxLines = std::clamp((titleBottom - titleTop) / lh, 1, 3);
      int curY = titleTop;
      for (const auto& line : renderer.wrappedText(SMALL_FONT_ID, book->title.c_str(), maxW, maxLines)) {
        drawCentered(line, curY);
        curY += lh;
      }
      if (hasAuthor && authorY >= curY) {  // skipped only if the cover is too short to fit both
        drawCentered(renderer.truncatedText(SMALL_FONT_ID, book->author.c_str(), maxW), authorY);
      }
    }
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  }
  if (coverCache) {
    const size_t needed = renderer.getRegionByteSize(rect.x, rect.y, rect.width, rect.height);
    if (needed > cached.bytes && needed <= coverCacheCapacity - coverCacheUsed) {
      cached.offset = coverCacheUsed;
      cached.bytes = needed;
      coverCacheUsed += needed;
    }
    if (needed > 0 && needed <= cached.bytes) {
      cached.rect = rect;
      cached.valid =
          renderer.copyRegionToBuffer(rect.x, rect.y, rect.width, rect.height, cacheData + cached.offset, cached.bytes);
    }
  }
  return true;  // The cover slot is painted, including fallback art.
}
