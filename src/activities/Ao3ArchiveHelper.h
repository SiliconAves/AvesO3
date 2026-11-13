#pragma once
#include <string>

namespace Ao3ArchiveHelper {

constexpr char READ_FOLDER_ROOT[] = "/read";

inline bool isInReadFolder(const std::string& path) {
    constexpr size_t prefixLen = 6; // "/read/"
    return path.size() > prefixLen &&
           path.compare(0, prefixLen, "/read/") == 0;
}

// rootOverride (optional): build under this root instead of /read/<ao3FolderName>
// (used by bookmarks). Relative structure and collision suffixing are identical.
std::string buildDestinationPath(const std::string& srcPath, const char* rootOverride = nullptr);

// Full archive sequence:
//   1. tombstone index record
//   2. rename .epub file
//   3. re-key cache directory
//   4. update recents entry to new path
//   5. remove from dashboard stores
// Returns the new path on success, empty string on failure.
std::string archiveFic(const std::string& srcPath);

// Restores an archived fic to its original path (read from the ao3_library_info sidecar).
// Re-keys the cache dir, updates recents and APP_STATE.
// Returns the restored path on success, empty string on failure.
std::string restoreFic(const std::string& srcPath);

// Moves an indexed fic into /read/AO3 Bookmarks and transfers its index record to the
// bookmarks index. Returns the new path, or empty string on failure (nothing is changed).
std::string bookmarkFic(const std::string& srcPath);

// Moves a bookmarked fic back under ao3Folder (mirrored path) and transfers its record to
// the main index. Returns the restored path, or empty string on failure (nothing is changed).
std::string restoreBookmark(const std::string& srcPath);

}