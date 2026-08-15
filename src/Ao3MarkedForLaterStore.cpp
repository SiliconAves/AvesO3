#include "Ao3MarkedForLaterStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

namespace {
constexpr char MARKED_FOR_LATER_FILE[] = "/.crosspoint/marked_for_later.json";
}  // namespace

Ao3MarkedForLaterStore Ao3MarkedForLaterStore::instance;

// ---------------------------------------------------------------------------

bool Ao3MarkedForLaterStore::addBook(const std::string& path,
                                     const std::string& title,
                                     const std::string& author) {
  // Already in the list — no-op, no reorder.
  if (contains(path)) {
    LOG_DBG("MFLS", "Already marked for later, ignoring: %s", path.c_str());
    return false;
  }

  // List full — caller must show cap message.
  if (isFull()) {
    LOG_DBG("MFLS", "Marked for Later list full (%d); not adding: %s", MAX_ENTRIES, path.c_str());
    return false;
  }

  entries.push_back({path, title, author});
  saveToFile();
  return true;
}

bool Ao3MarkedForLaterStore::contains(const std::string& path) const {
  return std::any_of(entries.begin(), entries.end(),
                     [&](const Ao3MarkedForLaterEntry& e) { return e.path == path; });
}

bool Ao3MarkedForLaterStore::removeByPath(const std::string& path) {
  auto it = std::find_if(entries.begin(), entries.end(),
                         [&](const Ao3MarkedForLaterEntry& e) { return e.path == path; });
  if (it == entries.end()) return false;

  entries.erase(it);
  if (!saveToFile()) {
    LOG_ERR("MFLS", "Failed to persist removal: %s", path.c_str());
  }
  return true;
}

bool Ao3MarkedForLaterStore::isMissing(const Ao3MarkedForLaterEntry& e) {
  return !Storage.exists(e.path.c_str());
}

bool Ao3MarkedForLaterStore::pruneMissing() {
  const size_t before = entries.size();
  entries.erase(std::remove_if(entries.begin(), entries.end(), &isMissing), entries.end());
  return entries.size() != before;
}

int Ao3MarkedForLaterStore::getQueuePosition(const std::string& path) const {
  for (int i = 0; i < static_cast<int>(entries.size()); i++) {
    if (entries[i].path == path) return i + 1;  // 1-based
  }
  return -1;
}

// ---------------------------------------------------------------------------
//  Persistence
// ---------------------------------------------------------------------------

bool Ao3MarkedForLaterStore::saveToFile() const {
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
  const bool ok = Storage.writeFile(MARKED_FOR_LATER_FILE, json);
  if (!ok) {
    LOG_ERR("MFLS", "Failed to write %s", MARKED_FOR_LATER_FILE);
  }
  return ok;
}

bool Ao3MarkedForLaterStore::loadFromFile() {
  if (!Storage.exists(MARKED_FOR_LATER_FILE)) return false;

  String raw = Storage.readFile(MARKED_FOR_LATER_FILE);
  if (raw.isEmpty()) return false;

  JsonDocument doc;
  auto err = deserializeJson(doc, raw);
  if (err) {
    LOG_ERR("MFLS", "JSON parse error: %s", err.c_str());
    return false;
  }

  entries.clear();
  JsonArray arr = doc["books"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (static_cast<int>(entries.size()) >= MAX_ENTRIES) break;
    Ao3MarkedForLaterEntry e;
    e.path   = obj["path"]   | std::string("");
    e.title  = obj["title"]  | std::string("");
    e.author = obj["author"] | std::string("");
    if (!e.path.empty()) {
      entries.push_back(std::move(e));
    }
  }

  LOG_DBG("MFLS", "Marked for Later loaded from file (%d entries)", getCount());
  return true;
}
