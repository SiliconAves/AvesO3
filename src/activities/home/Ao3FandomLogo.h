#pragma once
#include <string>
class GfxRenderer;
bool drawFandomLogo(const GfxRenderer& renderer, const std::string& fandom,
                    int rectX, int rectY, int rectW, int rectH,
                    int maxLogo = 80, bool black = false, int pad = 8);

bool drawFandomLogoForBook(const GfxRenderer& renderer, const std::string& epubPath,
                           int rectX, int rectY, int rectW, int rectH,
                           int maxLogo = 80, bool black = false, int pad = 8);