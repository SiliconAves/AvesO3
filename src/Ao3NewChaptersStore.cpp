#include "Ao3NewChaptersStore.h"

#include <ArduinoJson.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

namespace {
constexpr char NEW_CHAPTERS_FILE[] = "/.crosspoint/new_chapters.json";
constexpr int  MAX_NEW_CHAPTERS    = 10;
}  // namespace

Ao3NewChaptersStore Ao3NewChaptersStore::instance;

// ---------------------------------------------------------------------------

void Ao3NewChaptersStore::addBook(const std::string& path,
                                  const std::string& title,
                                  const std::string& author) {
  // Move to top if already present.
  auto it = std::find_if(entries.begin(), entries.end(),
                         [&](const Ao3NewChaptersEntry& e) { return e.path == path; });
  if (it != entries.end()) {
    // Update metadata in case title/author changed, then rotate to front.
    it->title  = title;
    it->author = author;
    std::rotate(entries.begin(), it, it + 1);
    saveToFile();
    return;
  }

  // List full → do not add.
  if (static_cast<int>(entries.size()) >= MAX_NEW_CHAPTERS) {
    LOG_DBG("NCS", "New Chapters list full (%d); not adding: %s", MAX_NEW_CHAPTERS, path.c_str());
    return;
  }

  entries.insert(entries.begin(), {path, title, author});
  saveToFile();
}

bool Ao3NewChaptersStore::removeByPath(const std::string& path) {
  auto it = std::find_if(entries.begin(), entries.end(),
                         [&](const Ao3NewChaptersEntry& e) { return e.path == path; });
  if (it == entries.end()) return false;

  entries.erase(it);
  if (!saveToFile()) {
    LOG_ERR("NCS", "Failed to persist removal: %s", path.c_str());
  }
  return true;
}

bool Ao3NewChaptersStore::isMissing(const Ao3NewChaptersEntry& e) {
  return !Storage.exists(e.path.c_str());
}

bool Ao3NewChaptersStore::pruneMissing() {
  const size_t before = entries.size();
  entries.erase(std::remove_if(entries.begin(), entries.end(), &isMissing), entries.end());
  return entries.size() != before;
}

// ---------------------------------------------------------------------------
//  Persistence
// ---------------------------------------------------------------------------

bool Ao3NewChaptersStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& e : entries) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"]   = e.path;
    obj["title"]  = e.title;
    obj["author"] = e.author;
  }

  String json;
  serializeJson(doc, json);
  const bool ok = Storage.writeFile(NEW_CHAPTERS_FILE, json);
  if (!ok) {
    LOG_ERR("NCS", "Failed to write %s", NEW_CHAPTERS_FILE);
  }
  return ok;
}

bool Ao3NewChaptersStore::loadFromFile() {
  if (!Storage.exists(NEW_CHAPTERS_FILE)) return false;

  String raw = Storage.readFile(NEW_CHAPTERS_FILE);
  if (raw.isEmpty()) return false;

  JsonDocument doc;
  auto err = deserializeJson(doc, raw);
  if (err) {
    LOG_ERR("NCS", "JSON parse error: %s", err.c_str());
    return false;
  }

  entries.clear();
  JsonArray arr = doc["books"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (static_cast<int>(entries.size()) >= MAX_NEW_CHAPTERS) break;
    Ao3NewChaptersEntry e;
    e.path   = obj["path"]   | std::string("");
    e.title  = obj["title"]  | std::string("");
    e.author = obj["author"] | std::string("");
    if (!e.path.empty()) {
      entries.push_back(std::move(e));
    }
  }

  LOG_DBG("NCS", "New Chapters loaded from file (%d entries)", getCount());
  return true;
}
