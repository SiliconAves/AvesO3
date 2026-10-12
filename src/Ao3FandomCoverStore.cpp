#include "Ao3FandomCoverStore.h"
#include <ArduinoJson.h>
#include <HalStorage.h>

namespace { constexpr char PATH[] = "/.crosspoint/ao3_fandom_covers.json"; }

void Ao3FandomCoverStore::load() {
  loaded = true;
  entries.clear();
  if (!Storage.exists(PATH)) return;
  String json = Storage.readFile(PATH);
  if (json.isEmpty()) return;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return;
  for (JsonPair kv : doc.as<JsonObject>()) {
    const char* v = kv.value().as<const char*>();
    if (v && v[0]) entries.emplace_back(key(kv.key().c_str()), v);
  }
}

bool Ao3FandomCoverStore::save() const {
  Storage.mkdir("/.crosspoint");
  JsonDocument doc;
  for (const auto& e : entries) doc[e.first] = e.second;
  String json;
  serializeJson(doc, json);
  return Storage.writeFile(PATH, json);
}

const std::string* Ao3FandomCoverStore::find(const char* fandom) {
  if (!loaded) load();
  const std::string k = key(fandom);
  for (const auto& e : entries) if (e.first == k) return &e.second;
  return nullptr;
}

void Ao3FandomCoverStore::assign(const char* fandom, const std::string& file) {
  if (!loaded) load();
  const std::string k = key(fandom);
  for (auto it = entries.begin(); it != entries.end(); ++it) {
    if (it->first == k) {
      if (file.empty()) entries.erase(it); else it->second = file;
      save();
      return;
    }
  }
  if (!file.empty()) { entries.emplace_back(k, file); save(); }
}