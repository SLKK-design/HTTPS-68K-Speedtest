/* resources.r: Open Transport Bug Patcher's alerts, version, memory size and icon. */

#include "Processes.r"
#include "Dialogs.r"
#include "MacTypes.r"
#include "Finder.r"

/* All alerts fit a 512 x 342 screen. ^0 and ^1 are the two paragraphs. */
#define ALERT_RECT {50, 40, 270, 470}
#define TEXT_ITEM  {12, 20, 175, 410}, StaticText { disabled, "^0\r\r^1" }

resource 'ALRT' (128) { ALERT_RECT, 128, { OK, visible, sound1; OK, visible, sound1;
    OK, visible, sound1; OK, visible, sound1 }, alertPositionMainScreen };
resource 'DITL' (128) { {
    {185, 330, 205, 410}, Button { enabled, "Patch" };
    {185, 240, 205, 320}, Button { enabled, "Quit" };
    TEXT_ITEM;
} };

resource 'ALRT' (129) { ALERT_RECT, 129, { OK, visible, sound1; OK, visible, sound1;
    OK, visible, sound1; OK, visible, sound1 }, alertPositionMainScreen };
resource 'DITL' (129) { {
    {185, 330, 205, 410}, Button { enabled, "Restore" };
    {185, 240, 205, 320}, Button { enabled, "Quit" };
    TEXT_ITEM;
} };

resource 'ALRT' (130) { ALERT_RECT, 130, { OK, visible, sound1; OK, visible, sound1;
    OK, visible, sound1; OK, visible, sound1 }, alertPositionMainScreen };
resource 'DITL' (130) { {
    {185, 310, 205, 410}, Button { enabled, "Choose File\0xC9" };
    {185, 220, 205, 300}, Button { enabled, "Quit" };
    TEXT_ITEM;
} };

resource 'ALRT' (131) { ALERT_RECT, 131, { OK, visible, sound1; OK, visible, sound1;
    OK, visible, sound1; OK, visible, sound1 }, alertPositionMainScreen };
resource 'DITL' (131) { {
    {185, 330, 205, 410}, Button { enabled, "Restart" };
    {185, 240, 205, 320}, Button { enabled, "Later" };
    TEXT_ITEM;
} };

resource 'ALRT' (132) { ALERT_RECT, 132, { OK, visible, sound1; OK, visible, sound1;
    OK, visible, sound1; OK, visible, sound1 }, alertPositionMainScreen };
resource 'DITL' (132) { {
    {185, 330, 205, 410}, Button { enabled, "OK" };
    TEXT_ITEM;
} };

resource 'vers' (1) {
    0x01, 0x00, release, 0x00, verUS, "1.0",
    "1.0, Lin van der Slikke, 2026, with Claude Code"
};

resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    notDisplayManagerAware,
    reserved,
    reserved,
    512 * 1024,
    384 * 1024
};

/* Finder icon: Notion's "swap-horizontally" icon, redrawn as 1-bit pixels. The BNDL gives the
   Finder the icon for the creator; hasBundle and hasCustomIcon (set by
   tools/set_finder_flags.py after the build) make it use them. -16455 is
   the file's own custom icon. Each icon is its bitmap, then its mask. */

#define SWAP_ICN \
    $"00000000 00000000 00000000 00000000" \
    $"00000000 00000600 00000F00 00000780" \
    $"000003C0 07FFFFE0 07FFFFF0 07FFFFE0" \
    $"000003C0 00000780 00000F00 00000600" \
    $"00000000 00600000 00F00000 01E00000" \
    $"03C00000 07FFFFE0 0FFFFFE0 07FFFFE0" \
    $"03C00000 01E00000 00F00000 00600000" \
    $"00000000 00000000 00000000 00000000" \
    $"00000000 00000000 00000000 00000000" \
    $"00000000 00000600 00000F00 00000780" \
    $"000003C0 07FFFFE0 07FFFFF0 07FFFFE0" \
    $"000003C0 00000780 00000F00 00000600" \
    $"00000000 00600000 00F00000 01E00000" \
    $"03C00000 07FFFFE0 0FFFFFE0 07FFFFE0" \
    $"03C00000 01E00000 00F00000 00600000" \
    $"00000000 00000000 00000000 00000000"

#define SWAP_ICS \
    $"0000 0030 0018 3FFC" \
    $"3FFC 0018 0030 0000" \
    $"0000 0C00 1800 3FFC" \
    $"3FFC 1800 0C00 0000" \
    $"0000 0030 0018 3FFC" \
    $"3FFC 0018 0030 0000" \
    $"0000 0C00 1800 3FFC" \
    $"3FFC 1800 0C00 0000"

data 'OTwF' (0) {
};

resource 'FREF' (128) {
    'APPL', 0, ""
};

resource 'BNDL' (128) {
    'OTwF', 0,
    {
        'FREF', { 0, 128 };
        'ICN#', { 0, 128 }
    }
};

data 'ICN#' (128) { SWAP_ICN };
data 'ics#' (128) { SWAP_ICS };
data 'ICN#' (-16455) { SWAP_ICN };
data 'ics#' (-16455) { SWAP_ICS };
