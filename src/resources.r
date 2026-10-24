/* resources.r: Speed Test's menus, window, About alert, memory size and icon. */

#include "Processes.r"
#include "Menus.r"
#include "Windows.r"
#include "MacTypes.r"
#include "Dialogs.r"
#include "Finder.r"

resource 'MENU' (128) {
    128, textMenuProc;
    allEnabled, enabled;
    apple;
    {
        "About Speed Test\0xC9", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (129) {
    129, textMenuProc;
    allEnabled, enabled;
    "File";
    {
        "Run Everything", noIcon, "R", noMark, plain;           /* 1 */
        "-", noIcon, noKey, noMark, plain;
        "Crypto Only (no network)", noIcon, noKey, noMark, plain; /* 3 */
        "Live: Today\0xD5s Engine", noIcon, noKey, noMark, plain; /* 4 */
        "Live: Compare Servers", noIcon, "T", noMark, plain;     /* 5 */
        "Live: Each Chart Row", noIcon, noKey, noMark, plain;     /* 6 */
        "Live: Each Chart Row, Test Server", noIcon, "L", noMark, plain; /* 7 */
        "Live: Grouped Rows, Test Server", noIcon, "G", noMark, plain;   /* 8 */
        "Live: Less Work First, Test Server", noIcon, "F", noMark, plain; /* 9 */
        "-", noIcon, noKey, noMark, plain;
        "Quit", noIcon, "Q", noMark, plain;                       /* 11 */
    }
};

resource 'MENU' (130) {
    130, textMenuProc;
    allEnabled, enabled;
    "Edit";
    {
        "Undo", noIcon, "Z", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Cut", noIcon, "X", noMark, plain;
        "Copy", noIcon, "C", noMark, plain;
        "Paste", noIcon, "V", noMark, plain;
        "Clear", noIcon, noKey, noMark, plain;
    }
};

resource 'MBAR' (128) {
    { 128, 129, 130 };
};

/* Fits a 512 x 342 screen (SE/30, Classic II) */
resource 'WIND' (128) {
    {40, 4, 336, 508}, noGrowDocProc;
    invisible;
    goAway;
    0, "Speed Test";
    noAutoCenter;
};

resource 'ALRT' (128) {
    {60, 60, 220, 452}, 128,
    {
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
    },
    alertPositionMainScreen
};

resource 'DITL' (128) {
    {
        {126, 310, 146, 378}, Button { enabled, "OK" };
        {12, 16, 116, 378}, StaticText { disabled,
            "Speed Test\r"
            "Times every TLS handshake method Notion Classic and Claude 68 "
            "have used, on this Mac: the crypto on its own, and real "
            "connections to api.notion.com.\r"
            "Lin van der Slikke, 2026, with Claude Code."
        };
    }
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
    2560 * 1024,
    2048 * 1024
};

/* Finder icon: Notion's "stopwatch" icon, redrawn as 1-bit pixels. The BNDL gives the
   Finder the icon for the creator; hasBundle and hasCustomIcon (set by
   tools/set_finder_flags.py after the build) make it use them. -16455 is
   the file's own custom icon. Each icon is its bitmap, then its mask. */

#define STOPWATCH_ICN \
    $"00000000 00000000 00000000 000000C0" \
    $"0007E1E0 0007E0F0 0007E078 003FFCFC" \
    $"007FFFF8 00FE7FC0 01FE7F80 03FE7FC0" \
    $"03FE7FC0 03FE7FC0 07FE7FE0 07FE7FE0" \
    $"07FC3FE0 07FC3FE0 07FC3FE0 07FC3FE0" \
    $"03FE7FC0 03FE7FC0 03FE7FC0 01FFFF80" \
    $"00FFFF00 007FFE00 003FFC00 0007E000" \
    $"00000000 00000000 00000000 00000000" \
    $"00000000 00000000 00000000 000000C0" \
    $"0007E1E0 0007E0F0 0007E078 003FFCFC" \
    $"007FFFF8 00FFFFC0 01FFFF80 03FFFFC0" \
    $"03FFFFC0 03FFFFC0 07FFFFE0 07FFFFE0" \
    $"07FFFFE0 07FFFFE0 07FFFFE0 07FFFFE0" \
    $"03FFFFC0 03FFFFC0 03FFFFC0 01FFFF80" \
    $"00FFFF00 007FFE00 003FFC00 0007E000" \
    $"00000000 00000000 00000000 00000000"

#define STOPWATCH_ICS \
    $"0000 0000 03B0 07DC" \
    $"0FFC 1EF0 3EF8 3EF8" \
    $"3C78 3C78 3EF8 1FF0" \
    $"0FE0 07C0 0000 0000" \
    $"0000 0000 03B0 07DC" \
    $"0FFC 1FF0 3FF8 3FF8" \
    $"3FF8 3FF8 3FF8 1FF0" \
    $"0FE0 07C0 0000 0000"

data 'SpTs' (0) {
};

resource 'FREF' (128) {
    'APPL', 0, ""
};

resource 'BNDL' (128) {
    'SpTs', 0,
    {
        'FREF', { 0, 128 };
        'ICN#', { 0, 128 }
    }
};

data 'ICN#' (128) { STOPWATCH_ICN };
data 'ics#' (128) { STOPWATCH_ICS };
data 'ICN#' (-16455) { STOPWATCH_ICN };
data 'ics#' (-16455) { STOPWATCH_ICS };
