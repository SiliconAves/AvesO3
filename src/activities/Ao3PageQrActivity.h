#pragma once
#include <string>
#include "activities/Activity.h"

class Ao3PageQrActivity final : public Activity {
public:
    explicit Ao3PageQrActivity(GfxRenderer& renderer,
                               MappedInputManager& mappedInput,
                               std::string filePath)
        : Activity("Ao3PageQr", renderer, mappedInput),
          filePath(std::move(filePath)) {}

    void onEnter() override;
    void loop()    override;
    void render(RenderLock&&) override;

private:
    std::string filePath;

    // Loaded from ao3_library_info sidecar
    char     title[128]   = {};
    char     author[128]  = {};
    char     tags[4][16]  = {};
    uint16_t chapterCount = 0;
    char     updatedDate[12] = {};   // used as currentLocalDate for AO3Sync

    // Derived
    std::string workId;
    std::string storyUrl;
    bool        metaLoaded = false;

    void loadMetadata();
};