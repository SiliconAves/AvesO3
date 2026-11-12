#pragma once
#include <cstdint>
#include <string>

namespace Ao3Paths {

constexpr char MAIN_INDEX[] = "/.crosspoint/ao3_library_index.bin";
constexpr char BOOKMARKS_INDEX[] = "/.crosspoint/ao3_bookmarks_index.bin";

// MAIN keeps the existing filename: it is the file users already have on their SD card.
constexpr char MAIN_SORT_STATE[] = "/.crosspoint/ao3SortFilterState.json";
constexpr char BOOKMARKS_SORT_STATE[] = "/.crosspoint/ao3_bookmarks_sort.json";

// Bookmarked fics live in the read folder, next to the archive subfolder.
constexpr char BOOKMARKS_FOLDER[] = "/read/AO3 Bookmarks";   // no trailing slash
constexpr char BOOKMARKS_PREFIX[] = "/read/AO3 Bookmarks/";

inline bool isInBookmarks(const std::string& path) {
  constexpr size_t len = sizeof(BOOKMARKS_PREFIX) - 1;
  return path.size() > len && path.compare(0, len, BOOKMARKS_PREFIX) == 0;
}

// Which index file owns a fic, decided purely by where its file lives.
inline const char* indexPathFor(const std::string& path) {
  return isInBookmarks(path) ? BOOKMARKS_INDEX : MAIN_INDEX;
}

}  // namespace Ao3Paths

enum class Ao3LibraryMode : uint8_t { REGULAR = 0, BOOKMARKS = 1 };