#include "Ao3WipsStore.h"

#include <ArduinoJson.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

namespace {
constexpr char WIPS_FILE[] = "/.crosspoint/wips.json";
}  // namespace

Ao3WipsStore Ao3WipsStore::instance;

void Ao3WipsStore::sortAlphabetically() {
    std::sort(entries.begin(), entries.end(), [](const Ao3WipEntry& a, const Ao3WipEntry& b) {
        return a.title < b.title;
    });
}

void Ao3WipsStore::addBook(const std::string& path, const std::string& title, const std::string& author) {
    removeBook(path); // Remove existing to prevent duplicates
    entries.push_back({path, title, author});
    sortAlphabetically();
    saveToFile();
}

bool Ao3WipsStore::removeBook(const std::string& path) {
    auto it = std::remove_if(entries.begin(), entries.end(),
        [&path](const Ao3WipEntry& entry) { return entry.path == path; });
        
    if (it != entries.end()) {
        entries.erase(it, entries.end());
        if (!saveToFile()) {
            LOG_ERR("WIPs", "Failed to persist removal: %s", path.c_str());
        }
        return true;
    }
    return false;
}

bool Ao3WipsStore::pruneMissing() {
    const size_t before = entries.size();
    entries.erase(
        std::remove_if(entries.begin(), entries.end(), [](const Ao3WipEntry& e) {
            return !Storage.exists(e.path.c_str());
        }),
        entries.end()
    );
    return entries.size() != before;
}

bool Ao3WipsStore::saveToFile() const {
    Storage.mkdir("/.crosspoint");

    JsonDocument doc;
    JsonArray array = doc.to<JsonArray>();

    for (const auto& entry : entries) {
        JsonObject obj = array.add<JsonObject>();
        obj["path"] = entry.path;
        obj["title"] = entry.title;
        obj["author"] = entry.author;
    }

    String json;
    serializeJson(doc, json);
    const bool ok = Storage.writeFile(WIPS_FILE, json);
    if (!ok) {
        LOG_ERR("WIPs", "Failed to write %s", WIPS_FILE);
    }
    return ok;
}

bool Ao3WipsStore::loadFromFile() {
    if (!Storage.exists(WIPS_FILE)) return false;

    String raw = Storage.readFile(WIPS_FILE);
    if (raw.isEmpty()) return false;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, raw);
    if (error) {
        LOG_ERR("WIPs", "Failed to parse wips.json: %s", error.c_str());
        return false;
    }

    entries.clear();
    JsonArray array = doc.as<JsonArray>();
    for (JsonObject obj : array) {
        Ao3WipEntry entry;
        entry.path = obj["path"] | std::string("");
        entry.title = obj["title"] | std::string("Unknown Title");
        entry.author = obj["author"] | std::string("Unknown Author");
        if (!entry.path.empty()) {
            entries.push_back(std::move(entry));
        }
    }

    sortAlphabetically();
    LOG_DBG("WIPs", "WIPs loaded from file (%d entries)", static_cast<int>(entries.size()));
    return true;
}