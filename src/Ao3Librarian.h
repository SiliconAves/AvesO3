#pragma once

#include <string>
#include <vector>
#include "Ao3LibraryMetadata.h"

#include "Ao3CompactIndexRecord.h"

class Epub;

/**
 * @brief Utility class to scrape AO3 metadata from an EPUB file.
 * Handles FanFicFare-exported AO3 EPUBs and AO3-download (Calibre-style) EPUBs.
 */
class Ao3Librarian {
 public:
  /**
   * @brief True if spine 0 looks like an AO3-download preface (no ao3WorkId in OPF).
   */
  static bool sniffNativeAo3Preface(const Epub& epub);

  /**
   * @brief Scrapes metadata from the given EPUB and saves it to the sidecar.
   * @param epub The EPUB object (must be loaded).
   * @param force If true, overwrites existing library info.
   * @return true if successful or if info already exists.
   */
  static bool scrape(const Epub& epub, bool force = false);

  /**
   * @brief Reads the library info sidecar if it exists.
   * @param epub The EPUB object.
   * @param meta Out parameter for metadata.
   * @return true if metadata was found and is valid.
   */
  static bool getLibraryInfo(const Epub& epub, Ao3LibraryMetadata& meta);

  /**
   * @brief Scans the device cache for all identified AO3 fics.
   * @param out Vector to populate with metadata.
   */
  static void scanGlobalLibrary(std::vector<Ao3LibraryMetadata>& out);

  /**
   * @brief Quick check to see if any AO3 library info exists.
   */
  static bool hasAnyAo3Fics();

  /**
   * @brief Helper to map AO3 string ratings to our char codes.
   */
  static char mapRating(const char* ratingStr);

  /**
   * @brief Helper to map AO3 warning strings to our codes.
   */
  static char mapWarning(const char* warningStr);

  /**
   * @brief Writes a compact record into the unified index file.
   */
  static bool writeIndexRecord(const CompactIndexRecord& rec);

  /**
   * @brief Marks a record as tombstoned (deleted) in the index.
   */
  static bool tombstoneRecord(const std::string& epubPath);

 private:
  /**
   * @brief Internal parser that handles the HTML streaming and anchor searching.
   */
  static bool parseTitlePage(const Epub& epub,
                             Ao3LibraryMetadata& meta,
                             std::string& scrapedWorkId,
                             std::string& scrapedDate,
                             char* scrapedFandom,
                             char* scrapedRel1,
                             char* scrapedRel2);
};

