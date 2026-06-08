#include "Ao3Librarian.h"
#include <memory>
#include <Epub.h>
#include <HalStorage.h>
#include <Logging.h>
#include <functional>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace {

/**
 * @brief Internal stream consumer that parses AO3 metadata on the fly.
 * Uses fixed-size buffers to avoid heap fragmentation during heavy indexing.
 */
class HtmlScraper : public Print {
 public:
  Ao3LibraryMetadata& meta;
  char buffer[1024];
  size_t bufferSize = 0;
  bool inSummary = false;
  bool inTag = false;
  size_t summaryBytes = 0;
  bool isInProgress = false;
  std::string scrapedWorkId;
  std::string scrapedDate;
  bool hasUpdatedDate = false;

  explicit HtmlScraper(Ao3LibraryMetadata& m) : meta(m), scrapedWorkId(""), scrapedDate(""), hasUpdatedDate(false) {
    memset(buffer, 0, sizeof(buffer));
    bufferSize = 0;
    summaryBytes = 0;
    inSummary = false;
    inTag = false;
    isInProgress = false;
  }

  size_t write(uint8_t b) override {
    char c = (char)b;
    
    if (inSummary) {
      if (c == '<') inTag = true;
      if (!inTag && summaryBytes < 511) {
        meta.summary[summaryBytes++] = c;
      }
      if (c == '>') {
        inTag = false;
        if (bufferSize > 6 && (strstr(buffer + bufferSize - 6, "</p>") || strstr(buffer + bufferSize - 6, "</div>"))) {
          if (summaryBytes > 30) inSummary = false;
        }
      }
    }

    if (bufferSize < sizeof(buffer) - 1) {
      buffer[bufferSize++] = (char)c;
      buffer[bufferSize] = 0;
    }

    if (bufferSize > 800) {
      processBuffer();
      int overlap = bufferSize - 400;
      memmove(buffer, buffer + 400, overlap);
      bufferSize = overlap;
      buffer[bufferSize] = 0;
      yield();
    }
    return 1;
  }

  std::string extractDate(const char* anchor) {
    const char* pos = strstr(buffer, anchor);
    if (!pos) return "";

    const char* scan = pos + strlen(anchor);
    bool inHtm = false;
    while (scan < buffer + bufferSize) {
      if (*scan == '<') {
        inHtm = true;
        scan++;
        continue;
      }
      if (*scan == '>') {
        inHtm = false;
        scan++;
        continue;
      }
      if (inHtm) {
        scan++;
        continue;
      }
      if (isdigit(static_cast<unsigned char>(*scan))) {
        if (scan + 10 <= buffer + bufferSize) {
          bool valid = true;
          for (int i = 0; i < 10; i++) {
            if (i == 4 || i == 7) {
              if (scan[i] != '-') { valid = false; break; }
            } else {
              if (!isdigit(static_cast<unsigned char>(scan[i]))) { valid = false; break; }
            }
          }
          if (valid) {
            return std::string(scan, 10);
          }
        }
      }
      scan++;
    }
    return "";
  }

  void processBuffer() {
    if (meta.rating == '-') {
      findFuzzyField("Rating:", [this](const char* val) {
        meta.rating = Ao3Librarian::mapRating(val);
      });
    }

    if (meta.warning == 0 || meta.warning == '-') {
      findFuzzyField("Warnings:", [this](const char* val) {
        char w = Ao3Librarian::mapWarning(val);
        if (w != '-' || meta.warning == 0) meta.warning = w;
      });
      findFuzzyField("Archive Warning:", [this](const char* val) {
        char w = Ao3Librarian::mapWarning(val);
        if (w != '-' || meta.warning == 0) meta.warning = w;
      });
    }

    findFuzzyField("Words:", [this](const char* val) {
      char clean[32] = {0};
      int j = 0;
      for(int i=0; val[i] && j < 31; i++) if(isdigit(val[i])) clean[j++] = val[i];
      else if(val[i] != ',') break;
      meta.wordCount = strtoul(clean, nullptr, 10);
    });

    if (strstr(buffer, "In-Progress")) isInProgress = true;

    findFuzzyField("Chapters:", [this](const char* val) {
      const char* slash = strchr(val, '/');
      if (slash) {
        unsigned published = 0;
        for (const char* p = val; p < slash && isdigit(static_cast<unsigned char>(*p)); ++p) {
          published = published * 10u + static_cast<unsigned>(*p - '0');
          if (published > 65535u) break;
        }
        meta.chapterCount = static_cast<uint16_t>(published);
        const char* after = slash + 1;
        bool okTotal = (*after != '\0');
        for (const char* p = after; okTotal && *p; ++p) {
          if (!isdigit(static_cast<unsigned char>(*p))) okTotal = false;
        }
        unsigned total = 0;
        if (okTotal) total = static_cast<unsigned>(atoi(after));
        meta.isCompleted = okTotal && total > 0 && meta.chapterCount == static_cast<uint16_t>(total);
      } else {
        meta.chapterCount = static_cast<uint16_t>(atoi(val));
        meta.isCompleted = !isInProgress;
      }
    });

    if (strstr(buffer, "Genre:")) extractTagsFromAnchor("Genre:");
    if (!meta.tags[0][0] && strstr(buffer, "Additional Tags:")) {
      extractTagsFromAnchor("Additional Tags:");
    }
    if (strstr(buffer, "Series:")) extractSeries();

    const char* sumPos = strstr(buffer, "Summary:");
    if (sumPos && !inSummary && meta.summary[0] == '\0') {
      const char* scan = sumPos + (sizeof("Summary:") - 1);
      bool htm = false;
      while (scan < buffer + bufferSize) {
        if (*scan == '<') htm = true;
        else if (*scan == '>') htm = false;
        else if (!htm && !isspace(static_cast<unsigned char>(*scan)) && *scan != ':' && *scan != '-' && *scan != '/') {
            inSummary = true;
            while (scan < buffer + bufferSize && summaryBytes < 511) {
                if (*scan == '<') htm = true;
                else if (*scan == '>') htm = false;
                else if (!htm) meta.summary[summaryBytes++] = *scan;
                scan++;
            }
            break;
        }
        scan++;
      }
    }

    if (meta.summary[0] == '\0') tryExtractNativeSummary();

    // Native AO3: extract work ID from works/ URL in HTML
    const char* pos = strstr(buffer, "archiveofourown.org/works/");
    if (pos) {
      const char* p = pos + 26;
      std::string extractedId = "";
      while (*p && isdigit(static_cast<unsigned char>(*p))) {
        extractedId += *p;
        p++;
      }
      if (extractedId.size() > scrapedWorkId.size()) {
        scrapedWorkId = extractedId;
      }
    }

    // Extract last updated or published date with Updated having absolute precedence
    std::string tempDate = extractDate("Updated:");
    if (!tempDate.empty()) {
      scrapedDate = tempDate;
      hasUpdatedDate = true;
    } else if (!hasUpdatedDate) {
      tempDate = extractDate("Published:");
      if (!tempDate.empty()) {
        scrapedDate = tempDate;
      }
    }
  }

  void tryExtractNativeSummary() {
    const char* mark = strstr(buffer, ">Summary</");
    if (!mark) mark = strstr(buffer, ">Summary<");
    if (!mark) return;
    const char* bq = strstr(mark, "<blockquote");
    if (!bq) return;
    const char* gt = strchr(bq, '>');
    if (!gt) return;
    const char* scan = gt + 1;
    bool inTag = false;
    size_t base = strlen(meta.summary);
    while (scan < buffer + bufferSize && base < 511) {
      if (*scan == '<') {
        if (static_cast<size_t>(buffer + bufferSize - scan) >= 13 &&
            strncmp(scan, "</blockquote>", 13) == 0) {
          break;
        }
        inTag = true;
      } else if (*scan == '>') {
        inTag = false;
      } else if (!inTag) {
        meta.summary[base++] = *scan;
      }
      scan++;
    }
    meta.summary[base] = '\0';
  }

  void findFuzzyField(const char* anchor, std::function<void(const char*)> callback) {
    const char* pos = strstr(buffer, anchor);
    if (!pos) return;

    const char* scan = pos + strlen(anchor);
    bool inHtm = false;
    const char* valStart = nullptr;

    while (scan < buffer + bufferSize) {
      if (*scan == '<') { inHtm = true; scan++; continue; }
      if (*scan == '>') { inHtm = false; scan++; continue; }
      if (!inHtm && !isspace(*scan) && *scan != ':' && *scan != '-' && *scan != '/' && *scan != 's' && *scan != 'S') {
        valStart = scan;
        break;
      }
      scan++;
    }

    if (!valStart) return;

    const char* valEnd = strchr(valStart, '<');
    if (valEnd) {
      char temp[128];
      size_t len = std::min((size_t)(valEnd - valStart), sizeof(temp) - 1);
      strncpy(temp, valStart, len);
      temp[len] = 0;
      while (len > 0 && isspace(temp[len-1])) temp[--len] = 0;
      callback(temp);
    }
  }

  void extractSeries() {
    const char* pos = strstr(buffer, "Series:");
    if (!pos) return;

    const char* scan = pos + (sizeof("Series:") - 1);
    // Skip to the start of the value
    while (scan < buffer + bufferSize) {
      if (*scan == '<') {
        while (scan < buffer + bufferSize && *scan != '>') scan++;
        if (scan < buffer + bufferSize) scan++;
      } else if (isspace(*scan) || *scan == ':' || *scan == '/' || *scan == '-' || *scan == '_') {
        scan++;
      } else {
        break;
      }
    }

    if (scan >= buffer + bufferSize) return;

    // Read characters, ignoring HTML tags, until we hit a newline or EOF.
    char seriesText[256];
    size_t outIdx = 0;
    while (scan < buffer + bufferSize && *scan != '\n' && *scan != '\r' && outIdx < sizeof(seriesText) - 1) {
      if (*scan == '<') {
        while (scan < buffer + bufferSize && *scan != '>') scan++;
      } else if (*scan != '>') {
        seriesText[outIdx++] = *scan;
      }
      scan++;
    }
    seriesText[outIdx] = 0;

    // Clean up trailing spaces
    while (outIdx > 0 && isspace(seriesText[outIdx - 1])) {
      seriesText[--outIdx] = 0;
    }

    // Native AO3: "part N of Series Title"
    {
      char* p = seriesText;
      while (*p && isspace(static_cast<unsigned char>(*p))) p++;
      if (strncasecmp(p, "part ", 5) == 0) {
        const char* num = p + 5;
        int part = atoi(num);
        while (*num && isdigit(static_cast<unsigned char>(*num))) num++;
        while (*num && isspace(static_cast<unsigned char>(*num))) num++;
        if (strncasecmp(num, "of ", 3) == 0) {
          num += 3;
          while (*num && isspace(static_cast<unsigned char>(*num))) num++;
          if (part > 0 && *num) {
            meta.seriesPart = static_cast<uint16_t>(part);
            strncpy(meta.seriesName, num, sizeof(meta.seriesName) - 1);
            meta.seriesName[sizeof(meta.seriesName) - 1] = '\0';
            return;
          }
        }
      }
    }

    // FanFicFare: "Title [N]"
    char* bracketPos = strrchr(seriesText, '[');
    if (bracketPos) {
      int part = atoi(bracketPos + 1);
      if (part > 0) meta.seriesPart = part;
      
      *bracketPos = 0; // Terminate name before bracket
      size_t nameLen = strlen(seriesText);
      while (nameLen > 0 && isspace(seriesText[nameLen - 1])) {
        seriesText[--nameLen] = 0;
      }
      strncpy(meta.seriesName, seriesText, sizeof(meta.seriesName) - 1);
      meta.seriesName[sizeof(meta.seriesName) - 1] = '\0';
    } else {
      strncpy(meta.seriesName, seriesText, sizeof(meta.seriesName) - 1);
      meta.seriesName[sizeof(meta.seriesName) - 1] = '\0';
    }
  }

  void extractTagsFromAnchor(const char* anchor) {
    const char* pos = strstr(buffer, anchor);
    if (!pos) return;

    const char* scan = pos + strlen(anchor);
    // Properly skip whole HTML tags and delimiters, so </b> doesn't leave 'b>' behind
    while (scan < buffer + bufferSize) {
      if (*scan == '<') {
        while (scan < buffer + bufferSize && *scan != '>') scan++;
        if (scan < buffer + bufferSize) scan++; // skip '>'
      } else if (isspace(*scan) || *scan == ':' || *scan == '/' || *scan == '-' || *scan == '_') {
        scan++;
      } else {
        break;
      }
    }

    int tagIdx = 0;
    while (tagIdx < 4 && scan < buffer + bufferSize) {
      const char* comma = strchr(scan, ',');
      const char* lt = strchr(scan, '<');
      const char* actualEnd = (comma && lt) ? std::min(comma, lt) : (comma ? comma : lt);
      if (!actualEnd) break;

      if (lt && lt == scan) {
        // Native AO3: scan is sitting on an <a href="...">, skip into its text content
        while (scan < buffer + bufferSize && *scan != '>') scan++;
        if (scan < buffer + bufferSize) scan++; // skip '>'
        // Recalculate lt and actualEnd now that we're inside the tag text
        lt = strchr(scan, '<');
        if (!lt) break;
        actualEnd = lt;
      }

      size_t rawLen = (size_t)(actualEnd - scan);
      if (rawLen == 0) { scan = lt; continue; } // empty text node, skip
      bool truncated = rawLen > 15;
      size_t len = std::min(rawLen, truncated ? (size_t)14 : (size_t)15);
      strncpy(meta.tags[tagIdx], scan, len);
      meta.tags[tagIdx][len] = 0;
      if (truncated) {
        // Avoid "...word ." — turn trailing whitespace in the copy into dots before the truncation mark
        size_t i = len;
        while (i > 0 && isspace(static_cast<unsigned char>(meta.tags[tagIdx][i - 1]))) {
          meta.tags[tagIdx][i - 1] = '.';
          i--;
        }
        meta.tags[tagIdx][len] = '.';
        meta.tags[tagIdx][len + 1] = 0;
      }
      tagIdx++;

      if (actualEnd == lt) {
        // Native AO3: skip past </a> closing tag and the ", " separator (including quotes)
        scan = lt;
        while (scan < buffer + bufferSize && *scan != '>') scan++;
        if (scan < buffer + bufferSize) scan++; // skip '>'
        while (scan < buffer + bufferSize && (isspace(*scan) || *scan == ',' || *scan == '"')) scan++;
      } else {
        // FFF: plain comma-separated text, just advance past the comma and whitespace
        scan = actualEnd + 1;
        while (scan < buffer + bufferSize && (isspace(*scan) || *scan == ',')) scan++;
      }
    }
  }
};

} // namespace

bool Ao3Librarian::scrape(const Epub& epub, bool force) {
  const std::string infoPath = epub.getCachePath() + "/ao3_library_info";
  
  if (!force && Storage.exists(infoPath.c_str())) {
    auto existing = std::unique_ptr<Ao3LibraryMetadata>(new Ao3LibraryMetadata());
    if (getLibraryInfo(epub, *existing)) {
      if (existing->version == 8 && existing->rating != '-' && existing->warning != 0 && existing->summary[0] != 0 && existing->wordCount > 0) {
        // If ao3-info.bin is missing but we already have cached library info,
        // we can still attempt to rebuild ao3-info.bin if we sniff a native preface
        if (!epub.hasAo3Info() && sniffNativeAo3Preface(epub)) {
          auto meta = std::unique_ptr<Ao3LibraryMetadata>(new Ao3LibraryMetadata());
          std::string scrapedWorkId = "";
          std::string scrapedDate = "";
          if (parseTitlePage(epub, *meta, scrapedWorkId, scrapedDate)) {
            if (!scrapedWorkId.empty()) {
              epub.saveAo3Info(scrapedWorkId, scrapedDate, meta->isCompleted);
            }
          }
        }
        return true; 
      }
    }
  }

  auto meta = std::unique_ptr<Ao3LibraryMetadata>(new Ao3LibraryMetadata());
  std::string scrapedWorkId = "";
  std::string scrapedDate = "";
  if (!parseTitlePage(epub, *meta, scrapedWorkId, scrapedDate)) return false;

  strncpy(meta->filepath, epub.getPath().c_str(), 255);
  strncpy(meta->title, epub.getTitle().c_str(), 127);
  strncpy(meta->author, epub.getAuthor().c_str(), 127);
  strncpy(meta->updatedDate, scrapedDate.c_str(), sizeof(meta->updatedDate) - 1);
  meta->updatedDate[sizeof(meta->updatedDate) - 1] = '\0';

  // Generate the ao3-info.bin sidecar if missing and we extracted a valid Work ID
  if (!epub.hasAo3Info() && !scrapedWorkId.empty()) {
    epub.saveAo3Info(scrapedWorkId, scrapedDate, meta->isCompleted);
  }

  FsFile f;
  if (Storage.openFileForWrite("AO3L", infoPath, f)) {
    f.write((uint8_t*)meta.get(), sizeof(Ao3LibraryMetadata));
    f.close();
    return true;
  }
  return false;
}

bool Ao3Librarian::getLibraryInfo(const Epub& epub, Ao3LibraryMetadata& meta) {
  const std::string infoPath = epub.getCachePath() + "/ao3_library_info";
  FsFile f;
  if (Storage.openFileForRead("AO3L", infoPath, f)) {
    if (f.read((uint8_t*)&meta, sizeof(meta)) == sizeof(meta)) {
      f.close();
      return meta.isValid();
    }
    f.close();
  }
  return false;
}

bool Ao3Librarian::parseTitlePage(const Epub& epub, Ao3LibraryMetadata& meta, std::string& scrapedWorkId, std::string& scrapedDate) {
  if (epub.getSpineItemsCount() == 0) return false;

  // Check up to the first 3 spine items to account for internal cover pages
  int itemsToCheck = std::min(3, epub.getSpineItemsCount());
  
  for (int i = 0; i < itemsToCheck; i++) {
    std::string href = epub.getSpineItem(i).href;
    auto scraper = std::unique_ptr<HtmlScraper>(new HtmlScraper(meta));

    if (epub.readItemContentsToStream(href, *scraper, 8192)) {
      scraper->processBuffer();
      // Propagate work ID and date if found in this spine item
      if (scrapedWorkId.empty() && !scraper->scrapedWorkId.empty()) {
        scrapedWorkId = scraper->scrapedWorkId;
      }
      if (scrapedDate.empty() && !scraper->scrapedDate.empty()) {
        scrapedDate = scraper->scrapedDate;
      }
    }
  }

  return meta.wordCount > 0;
}

bool Ao3Librarian::sniffNativeAo3Preface(const Epub& epub) {
  if (epub.getSpineItemsCount() <= 0) return false;

  struct PrefaceSniffer final : public Print {
    char buf[2400];
    size_t n = 0;
    bool match = false;
    size_t write(uint8_t b) override {
      if (match) return 1;
      if (n + 1 < sizeof(buf)) {
        buf[n++] = static_cast<char>(b);
        buf[n] = '\0';
      }
      if (n >= 40 && !match) {
        if (strstr(buf, "archiveofourown.org/works/")) match = true;
        else if (strstr(buf, "Posted originally on") && strstr(buf, "archiveofourown")) match = true;
      }
      return 1;
    }
  };

  auto sniffer = std::unique_ptr<PrefaceSniffer>(new PrefaceSniffer());
  epub.readItemContentsToStream(epub.getSpineItem(0).href, *sniffer, 8192);
  return sniffer->match;
}

char Ao3Librarian::mapRating(const char* s) {
  if (strcasestr(s, "General")) return 'G';
  if (strcasestr(s, "Teen")) return 'T';
  if (strcasestr(s, "Mature")) return 'M';
  if (strcasestr(s, "Explicit")) return 'E';
  return '-';
}

char Ao3Librarian::mapWarning(const char* s) {
  if (strcasestr(s, "Creator Chose Not To Use") || strcasestr(s, "Choose Not To Use Archive Warnings")) return 'B';
  if (strcasestr(s, "No Archive Warnings Apply")) return '-';
  if (strlen(s) > 3) return '!';
  return '-';
}

void Ao3Librarian::scanGlobalLibrary(std::vector<Ao3LibraryMetadata>& out) {
  const char* cacheRoot = "/.crosspoint";
  FsFile root = Storage.open(cacheRoot);
  if (!root || !root.isDirectory()) return;

  FsFile entry;
  while (entry = root.openNextFile()) {
    char name[64];
    entry.getName(name, sizeof(name));
    if (entry.isDirectory() && strncmp(name, "epub_", 5) == 0) {
      std::string infoPath = std::string(cacheRoot) + "/" + name + "/ao3_library_info";
      if (Storage.exists(infoPath.c_str())) {
        FsFile f;
        if (Storage.openFileForRead("AO3L", infoPath, f)) {
          Ao3LibraryMetadata meta;
          if (f.read((uint8_t*)&meta, sizeof(meta)) == sizeof(meta)) {
            if (meta.isValid() && meta.version == 8) {
              out.push_back(meta);
            }
          }
          f.close();
        }
      }
    }
    entry.close();
    yield();
  }
  root.close();
}

bool Ao3Librarian::hasAnyAo3Fics() {
  const char* cacheRoot = "/.crosspoint";
  FsFile root = Storage.open(cacheRoot);
  if (!root || !root.isDirectory()) return false;

  bool found = false;
  FsFile entry;
  while (entry = root.openNextFile()) {
    char name[64];
    entry.getName(name, sizeof(name));
    if (entry.isDirectory() && strncmp(name, "epub_", 5) == 0) {
      std::string infoPath = std::string(cacheRoot) + "/" + name + "/ao3_library_info";
      if (Storage.exists(infoPath.c_str())) {
        found = true;
        entry.close();
        break;
      }
    }
    entry.close();
    yield();
  }
  root.close();
  return found;
}
