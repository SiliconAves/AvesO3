#include "Ao3TagMergeStore.h"
#include <HalStorage.h>
#include <ArduinoJson.h>
#include <algorithm>
#include "Ao3ViewEntry.h"  // for fnv1a

std::vector<std::pair<uint32_t, uint32_t>> Ao3TagMergeStore::fandomMap_;
std::vector<std::pair<uint32_t, uint32_t>> Ao3TagMergeStore::relMap_;

namespace {
void buildMap(JsonArray arr, std::vector<std::pair<uint32_t, uint32_t>>& out) {
    out.clear();
    for (JsonObject group : arr) {
        const char* master = group["master"] | "";
        if (!master[0]) continue;
        const uint32_t masterHash = fnv1a(master);
        for (const char* sub : group["subs"].as<JsonArray>()) {
            if (sub && sub[0]) out.push_back({fnv1a(sub), masterHash});
        }
    }
    std::sort(out.begin(), out.end());
}

uint32_t resolveFrom(const std::vector<std::pair<uint32_t, uint32_t>>& map, uint32_t hash) {
    const auto it = std::lower_bound(map.begin(), map.end(), std::make_pair(hash, uint32_t(0)));
    return (it != map.end() && it->first == hash) ? it->second : hash;
}

bool isSubIn(const std::vector<std::pair<uint32_t, uint32_t>>& map, uint32_t hash) {
    const auto it = std::lower_bound(map.begin(), map.end(), std::make_pair(hash, uint32_t(0)));
    return it != map.end() && it->first == hash;
}
} // namespace

void Ao3TagMergeStore::load() {
    fandomMap_.clear();
    relMap_.clear();
    if (!Storage.exists(kPath)) return;
    String json = Storage.readFile(kPath);
    if (json.isEmpty()) return;
    JsonDocument doc;
    if (deserializeJson(doc, json)) return;
    buildMap(doc["fandoms"].as<JsonArray>(), fandomMap_);
    buildMap(doc["relationships"].as<JsonArray>(), relMap_);
}

void Ao3TagMergeStore::unload() {
    fandomMap_.clear(); fandomMap_.shrink_to_fit();
    relMap_.clear();    relMap_.shrink_to_fit();
}

uint32_t Ao3TagMergeStore::resolveFandom(uint32_t hash)       { return resolveFrom(fandomMap_, hash); }
uint32_t Ao3TagMergeStore::resolveRelationship(uint32_t hash) { return resolveFrom(relMap_,    hash); }
bool Ao3TagMergeStore::isSubFandom(uint32_t hash)       { return isSubIn(fandomMap_, hash); }
bool Ao3TagMergeStore::isSubRelationship(uint32_t hash) { return isSubIn(relMap_,    hash); }