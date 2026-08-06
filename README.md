# What's Different in AvesO3
> [!WARNING]
> **NOT COMPATIBLE WITH DEVICES WITH THE NEW DISPLAY CONTROLLER** New x3/x4 batches are shipped  with a new display controller. This will be fixed in a future release.

- [x] **AO3-style library browser**, complete with summary and metadata square. You can add **up to 400 books** to the AO3 library.
- [x] Compatible with **Calibre+FanFicFare epubs** with *<u>*unmodified metadata spine*</u>* and **epubs from AO3**
- [x] AO3 Library **sorting and filtering** with 2 modes (Automatic & Folder Tree)
- [x] **Download new chapters** directly from AO3
- [x] **Auto index ao3 stories** in chunks
- [x] **Track the status** of each fic directly from the File Browser (introduces 5 statuses, with a corresponding icon: Unread, Reading, Waiting for Chapter, New Chapter Available, Finished)
- [x] **Pin your longfics** to the homescreen, so they aren't pushed off by your oneshots
- [x] **Added Noto Sans 10pt** to replicate the experience of reading AO3 on a phone
- [x] **Improved menu navigation**: hold left/right to skip multiple lines in File Browser, Reader Menu and Homescreen
- [x] Support for **line breaks**

What's Next in AvesO3 1.4.0
- [ ] Series continous reading
- [ ] Quick Access Menu: Marked for Later, Followed Fics (Subscriptions)
- [ ] AO3 max book cap increase
- [ ] Separate AO3 font and book font

What may come in the future:
- [ ] Locked fics support
- [ ] Bookmarked fics
- [ ] Online backend that can host your library online
- [ ] More!

# v.1.3.0 Features
**1. Automatic Sort and Filter**
<img src="docs/images/AvesO3/sortFilter.jpg" width="300" align="left" style="margin-right: 30px; border: 1px solid black;"/>
<br><br>Automatically Filter by:
+ **Fandom**
+ **Relationship** (up to 2 relationships per fic in Automatic Mode)<br><br> 

Sort by:
+ **Title**
+ **Author**
+ **Word Count**
+ **Series**
+ **Date Added**<br><br>

**NAVIGATION:** 
+ **SIDE DOWN** button opens the filter panel.
+ Inside the panel, **holding Next/Down** will bring you to the confirm button instantly.
<br clear="left"/> <br>

**2. Library Auto-Index**
<img src="docs/images/AvesO3/manageLibrary.jpg" width="300" align="left" style="margin-right: 30px; border: 1px solid black;"/><br><br>
Select **Index New Books** to start indexing ao3 books automatically, in batches of 10, 15 or 20 books depending on your choice (you can change this in **AO3 Library Settings**).<br><br>
Remember to **set up your AO3 folder in the AO3 Settings menu below.**
<br><br>
Indexing/Force Reindexing a **Single Book** is available from the  **long press Confirm** menu in File Browser/AO3 Library.<br><br>
**NAVIGATION:**<br>
Press **SIDE UP** to access this menu. 
<br clear="left"/> <br>

**3. AO3 Library Settings**
<img src="docs/images/AvesO3/librarySettings.jpg" width="300" align="left" style="margin-right: 30px; border: 1px solid black;"/><br><br>
**NAVIGATION:**<br>
Select **AO3 Library Settings** from manage panel.<br><br>
Available settings:<br>
+ **Your AO3 Folder**: select the folder where you put your AO3 books. This folder will be scanned for new epubs during indexing.
+ **Ignored Folders**: multi-select all the folders you never want to index. (useful if you keep your AO3 stories in root/ alongside other folders, such as Books, Comics, ...)
+ **Index Batch Size**: Books will be indexed in batches. You can choose the size of the batch here.
+ **Filter Mode**: Choose between AUTOMATIC/FOLDER TREE.<br>
+ **Library Cleanup**: Use this option to remove ghost book entries or non-functional entries in your AO3 Library.
<br clear="left"/> <br>
**FOLDER TREE MODE**: Choose this mode if you keep your AO3 stories organized **in this exact manner**:<br><br> **YourAO3Folder/Fandom/Relationship**<br><br>
Your folder names will be shown when you filter your library by fandom/relationships. This is especially useful if you use the auto-folders on transfer feature from Calibre. All epubs outside of the Relationship folders will be ignored. You can name the Fandom and Relationship folders as you please, as long as you keep the structure intact.
<br><br>
**AUTOMATIC MODE**: Choose this mode if you DO NOT organize your books as described above, or you don't organize them at all. Fandom and Relationship will be **detected automatically**. This mode supports **up to two relationships** during filtering.

# Feature Showcase

**1. AO3-Style Library**
<img src="docs/images/AvesO3/ao3Library.jpg" width="300" align="left" style="margin-right: 15px; border: 1px solid black;"/>

AO3-style library with summary and metadata, accessible from the Homescreen. 
In the AO3 square, the top right area has been changed to display the reading status (see point 3) instead of the relationship type. <br><br>
**Reading Status (Top Right of AO3 square):** <br>
White - : Unread book<br>
▲ : Waiting for Chapter<br>
▲+●: New Chapter Available<br>
R + Grey: Currently reading<br>
F + Black: Finished reading<br><br> 
**Navigation**: hold right/down and left/up to skip a page. <br><br>
**Warning:** in order for the stories to appear in this menu, they need to be **indexed first**.
<br clear="left"/> 

**2. Update Chapters On-Device**
<img src="docs/images/AvesO3/newEndOfBook.jpg" width="300" align="left" style="margin-right: 15px; border: 1px solid black;"/>
<br><br>**New End of Book screen:** when the story is marked as in-progress, a new End of Book screen appears. While connected to wi-fi, pressing **Search** will start a story update check. If it's successfull, you can choose to download the updated story directly from AO3. <br><br>
**Warning:** locked fics are currently unsupported.<br><br>
**Your local file will be overwritten with the one you downloaded from AO3.**
<br clear="left"/>

**3. File Browser Status Tracking**
<img src="docs/images/AvesO3/statusIcons.jpg" width="300" align="left" style="margin-right: 15px; border: 1px solid black;"/>
<br><br>**Status tracking:** The epub icon now displays one of **5 new statuses**. <br><br>
**Icon Meaning:** <br>
White Book: Unread book<br>
▲ : Waiting for Chapter<br>
▲+●: New Chapter Available<br>
Grey Book: Currently reading<br>
Black with Checkmark: Finished reading<br>

**Note:** Statuses are assigned automatically. If you wish, you can also change them manually through the reader menu or by **long-pressing the Confirm Button** in the File Browser, the AO3 Library or the Reader Menu.
<br clear="left"/>

**4. Book Pinning**
<img src="docs/images/AvesO3/homescreen.jpg" width="300" align="left" style="margin-right: 15px; border: 1px solid black;"/>
<br><br>Pin your longfics to the Homescreen by pressing the **Back Button**, so they are never pushed off when you read oneshots.
<br><br>Pinned fics are marked with a **bullet dot [·]** before the title.
<br clear="left"/>

## Installing (DO NOT INSTALL ON DEVICES WITH THE NEW DISPLAY CONTROLLER)
1. Download the .bin file from the Releases section
2. Connect your Xteink X4 to your computer via USB-C and wake/unlock the device
3. Go to https://xteink.dve.al/ and click "Flash CrossPoint firmware"

To revert back to the official firmware, you can flash the latest official firmware from https://xteink.dve.al/, or swap
back to the other partition using the "Swap boot partition" button here https://xteink.dve.al/debug.

### Data caching

The first time chapters of a book are loaded, they are cached to the SD card. Subsequent loads are served from the
cache. This cache directory exists at `.crosspoint` on the SD card. The structure is as follows:

```text
.crosspoint/
├── epub_12471232/               # Each EPUB is cached to a subdirectory named `epub_<hash>`
│   ├── progress.bin             # Stores reading progress (chapter, page, etc.)
|   ├── ao3-info.bin             # Stores information for chapter syncing
|   ├── ao3-library-info.bin     # Stores information for displaying the AO3 Library
│   ├── cover.bmp                # Book cover image (once generated)
│   ├── book.bin                 # Book metadata (title, author, spine, table of contents, etc.)
│   └── sections/                # All chapter data is stored in the sections subdirectory
│       ├── 0.bin                # Chapter data (screen count, all text layout info, etc.)
│       ├── 1.bin                #     files are named by their index in the spine
│       └── ...
├── settings.json        # device settings
├── state.json           # resume/runtime state
└── recent.json          # recent books list
```

Removing `/.crosspoint` clears all cached metadata and forces a full regeneration on next open. Book deletes, overwrites, and moves done through the firmware or web UI clear or re-key matching caches; manual SD-card edits may leave stale cache directories behind.

