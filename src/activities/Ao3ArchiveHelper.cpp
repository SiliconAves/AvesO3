#include "Ao3ArchiveHelper.h"
#include "Ao3LibraryMetadata.h"
#include "CrossPointState.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <functional>
#include <string>

#include "Ao3Librarian.h"
#include "Ao3MarkedForLaterStore.h"
#include "Ao3NewChaptersStore.h"
#include "Ao3WipsStore.h"
#include "RecentBooksStore.h"

namespace {

std::string loadAo3FolderSetting() {
    const char* path = "/.crosspoint/ao3_settings.json";
    if (!Storage.exists(path)) return "";
    String json = Storage.readFile(path);
    if (json.isEmpty()) return "";
    JsonDocument doc;
    if (deserializeJson(doc, json)) return "";
    return doc["ao3Folder"] | "";
}

} // namespace

namespace Ao3ArchiveHelper {

std::string buildDestinationPath(const std::string& srcPath) {
    const std::string ao3Folder = loadAo3FolderSetting();

    // Last component of ao3Folder becomes the archive subfolder name.
    // Fall back to "AO3 Fanfiction" when ao3Folder is empty or root.
    std::string subfolderName = "AO3 Fanfiction";
    if (!ao3Folder.empty() && ao3Folder != "/") {
        const size_t lastSlash = ao3Folder.rfind('/');
        subfolderName = (lastSlash != std::string::npos && lastSlash + 1 < ao3Folder.size())
                            ? ao3Folder.substr(lastSlash + 1)
                            : ao3Folder;
    }

    const std::string archiveRoot = std::string(READ_FOLDER_ROOT) + "/" + subfolderName;

    // Strip ao3Folder prefix to get relative path; fall back to filename only
    // if the book lives outside the configured ao3Folder.
    std::string relPart;
    bool underAo3Folder = false;

    if (!ao3Folder.empty()) {
        if (ao3Folder == "/") {
            if (srcPath.size() > 1 && srcPath[0] == '/') {
                underAo3Folder = true;
                relPart = srcPath.substr(1); // Strip only the leading slash
            }
        } else {
            if (srcPath.size() > ao3Folder.size() &&
                srcPath.compare(0, ao3Folder.size(), ao3Folder) == 0 &&
                srcPath[ao3Folder.size()] == '/') {
                underAo3Folder = true;
                relPart = srcPath.substr(ao3Folder.size() + 1);
            }
        }
    }

    // Fallback: If not under the designated AO3 folder, filename only
    if (!underAo3Folder) {
        const size_t lastSlash = srcPath.rfind('/');
        relPart = (lastSlash != std::string::npos)
                      ? srcPath.substr(lastSlash + 1)
                      : srcPath;
    }

    // Create the destination directory tree (pFlag=true = recursive).
    const size_t relLastSlash = relPart.rfind('/');
    const std::string dstDir =
        (relLastSlash != std::string::npos)
            ? archiveRoot + "/" + relPart.substr(0, relLastSlash)
            : archiveRoot;
    Storage.mkdir(dstDir.c_str(), true);

    // First candidate: no suffix.
    std::string dstPath = archiveRoot + "/" + relPart;
    if (!Storage.exists(dstPath.c_str())) return dstPath;

    // Collision resolution: suffix the filename only, preserve directory.
    const std::string dirPrefix =
        archiveRoot + "/" +
        (relLastSlash != std::string::npos ? relPart.substr(0, relLastSlash + 1) : "");
    const std::string filename =
        (relLastSlash != std::string::npos) ? relPart.substr(relLastSlash + 1) : relPart;
    const size_t dotPos  = filename.rfind('.');
    const std::string base = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
    const std::string ext  = (dotPos != std::string::npos) ? filename.substr(dotPos)    : "";

    int suffix = 2;
    do {
        dstPath = dirPrefix + base + " (" + std::to_string(suffix) + ")" + ext;
        suffix++;
    } while (Storage.exists(dstPath.c_str()) && suffix < 100);

    return dstPath;
}

std::string archiveFic(const std::string& srcPath) {
    const std::string dstPath = buildDestinationPath(srcPath);
    if (dstPath.empty()) {
        LOG_ERR("AO3ARC", "Failed to build destination path for: %s", srcPath.c_str());
        return "";
    }

    // 1. Tombstone before the rename — uses original path hash for lookup.
    Ao3Librarian::tombstoneRecord(srcPath);

    // 2. Move the .epub file.
    if (!Storage.rename(srcPath.c_str(), dstPath.c_str())) {
        LOG_ERR("AO3ARC", "Rename failed: %s -> %s", srcPath.c_str(), dstPath.c_str());
        return "";
    }

    // 3. Re-key cache directory — same pattern as moveFinishedBookToReadFolder.
    const std::string oldCachePath = "/.crosspoint/epub_" +
                                     std::to_string(std::hash<std::string>{}(srcPath));
    const std::string newCachePath = "/.crosspoint/epub_" +
                                     std::to_string(std::hash<std::string>{}(dstPath));
    if (!oldCachePath.empty() && Storage.exists(oldCachePath.c_str())) {
        if (!Storage.rename(oldCachePath.c_str(), newCachePath.c_str())) {
            LOG_ERR("AO3ARC", "Failed to rename cache dir %s -> %s (non-fatal)",
                    oldCachePath.c_str(), newCachePath.c_str());
        }
    }

    // 4. Repoint recents entry to new path.
    RECENT_BOOKS.updatePath(srcPath, dstPath, oldCachePath, newCachePath);

    // 5. Repoint resume pointer if needed.
    if (APP_STATE.openEpubPath == srcPath) {
        APP_STATE.openEpubPath = dstPath;
        APP_STATE.saveToFile();
    }

    // 6. Remove from dashboard stores.
    MARKED_FOR_LATER_STORE.removeByPath(srcPath);
    NEW_CHAPTERS_STORE.removeByPath(srcPath);
    AO3_WIPS_STORE.removeBook(srcPath);

    LOG_INF("AO3ARC", "Archived: %s -> %s", srcPath.c_str(), dstPath.c_str());
    return dstPath;
}

std::string restoreFic(const std::string& srcPath) {
    // Read original path from the stale filepath field in ao3_library_info.
    const std::string currentCachePath = "/.crosspoint/epub_" +
                                         std::to_string(std::hash<std::string>{}(srcPath));
    const std::string infoPath = currentCachePath + "/ao3_library_info";

    if (!Storage.exists(infoPath.c_str())) {
        LOG_ERR("AO3ARC", "ao3_library_info not found for: %s", srcPath.c_str());
        return "";
    }

    Ao3LibraryMetadata meta;
    {
        HalFile f;
        if (!Storage.openFileForRead("AO3ARC", infoPath, f)) {
            LOG_ERR("AO3ARC", "Failed to open ao3_library_info for: %s", srcPath.c_str());
            return "";
        }
        if (f.read((uint8_t*)&meta, sizeof(meta)) != sizeof(meta) || !meta.isValid()) {
            LOG_ERR("AO3ARC", "Invalid ao3_library_info for: %s", srcPath.c_str());
            return "";
        }
    }

    const std::string originalPath(meta.filepath);
    if (originalPath.empty()) {
        LOG_ERR("AO3ARC", "Empty original path in ao3_library_info for: %s", srcPath.c_str());
        return "";
    }

    // Recreate the original directory in case it no longer exists.
    const size_t lastSlash = originalPath.rfind('/');
    if (lastSlash != std::string::npos) {
        const std::string originalDir = originalPath.substr(0, lastSlash);
        Storage.mkdir(originalDir.c_str(), true);
    }

    // Move the file back.
    if (!Storage.rename(srcPath.c_str(), originalPath.c_str())) {
        LOG_ERR("AO3ARC", "Restore rename failed: %s -> %s",
                srcPath.c_str(), originalPath.c_str());
        return "";
    }

    // Re-key cache dir back to the original path hash.
    const std::string newCachePath = "/.crosspoint/epub_" +
                                     std::to_string(std::hash<std::string>{}(originalPath));
    if (Storage.exists(currentCachePath.c_str())) {
        if (!Storage.rename(currentCachePath.c_str(), newCachePath.c_str())) {
            LOG_ERR("AO3ARC", "Failed to restore cache dir %s -> %s (non-fatal)",
                    currentCachePath.c_str(), newCachePath.c_str());
        }
    }

    // Repoint recents entry.
    RECENT_BOOKS.updatePath(srcPath, originalPath, currentCachePath, newCachePath);

    // Repoint resume pointer if needed.
    if (APP_STATE.openEpubPath == srcPath) {
        APP_STATE.openEpubPath = originalPath;
        APP_STATE.saveToFile();
    }

    LOG_INF("AO3ARC", "Restored: %s -> %s", srcPath.c_str(), originalPath.c_str());
    return originalPath;
}

}