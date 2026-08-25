#pragma once
#include <string>

namespace Ao3ArchiveHelper {

constexpr char READ_FOLDER_ROOT[] = "/read";

inline bool isInReadFolder(const std::string& path) {
    constexpr size_t prefixLen = 6; // "/read/"
    return path.size() > prefixLen &&
           path.compare(0, prefixLen, "/read/") == 0;
}

// Builds the mirrored destination path under /read/<ao3FolderName>/.
// Creates intermediate directories. Returns empty string on failure.
std::string buildDestinationPath(const std::string& srcPath);

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

}