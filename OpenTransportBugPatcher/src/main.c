/* main.c: Open Transport Bug Patcher.

   Open Transport 1.3 offers TCP window scaling (RFC 1323) on every
   connection the Mac opens, then ignores the scale factor the server picks.
   Cloudflare picks 13, so its 128 KB window reads as 16 bytes: the Mac's
   TCP stalls about 3 s on every connection, then sends 16 bytes per round
   trip. This app changes 2 bytes in "Open Tpt Inet 68K Library" so the Mac
   stops offering window scaling, and changes them back on request.

   It patches nothing it doesn't recognise: it searches the library's code
   for Open Transport 1.3's exact instructions around the option, and only
   writes those 2 bytes, straight into the file. */

#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Files.h>
#include <Folders.h>
#include <StandardFile.h>
#include <Gestalt.h>
#include <ShutDown.h>
#include <MacMemory.h>
#include <TextUtils.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "patch.h"

enum {
    kAlertPatch = 128,     /* Patch / Quit */
    kAlertRestore = 129,   /* Restore / Quit */
    kAlertChoose = 130,    /* Choose File… / Quit */
    kAlertRestart = 131,   /* Restart / Later */
    kAlertInfo = 132       /* OK */
};


#define kLibName "\pOpen Tpt Inet 68K Library"

/* ------------------------------------------------------------------ */

static short ShowAlert(short id, const char* text1, const char* text2)
{
    Str255 a, b;
    size_t n1 = strlen(text1), n2 = strlen(text2);
    if (n1 > 255) n1 = 255;
    if (n2 > 255) n2 = 255;
    a[0] = (unsigned char)n1;
    memcpy(a + 1, text1, n1);
    b[0] = (unsigned char)n2;
    memcpy(b + 1, text2, n2);
    ParamText(a, b, "\p", "\p");
    InitCursor();
    return Alert(id, NULL);
}

static bool FindInExtensions(FSSpec* spec)
{
    short vRefNum;
    long dirID;
    if (FindFolder(kOnSystemDisk, kExtensionFolderType, kDontCreateFolder, &vRefNum, &dirID) != noErr)
        return false;
    return FSMakeFSSpec(vRefNum, dirID, kLibName, spec) == noErr;
}

static bool ChooseFile(FSSpec* spec)
{
    StandardFileReply reply;
    SFTypeList types = { 'libr' };
    StandardGetFile(NULL, 1, types, &reply);
    if (!reply.sfGood)
        return false;
    *spec = reply.sfFile;
    return true;
}

static const char* ErrorText(OSErr err)
{
    switch (err) {
        case wPrErr:  return "The disk is locked.";
        case fLckdErr: return "The file is locked (Get Info).";
        case opWrErr: return "Something else has the file open for writing.";
        case permErr: return "The file can't be opened for writing.";
        default:      return "";
    }
}

int main(void)
{
    FSSpec spec;
    long branch = 0, sysArch = 0;
    OSErr err;
    char msg[256];

    MaxApplZone();
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    FlushEvents(everyEvent, 0);

    if (Gestalt(gestaltSysArchitecture, &sysArch) == noErr && sysArch != gestalt68k) {
        ShowAlert(kAlertInfo, "This fix is for 68K Macs. A Power Mac runs Open Transport's TCP "
                              "from a different library, which this app doesn't change.", "");
        return 0;
    }

    if (!FindInExtensions(&spec)) {
        if (ShowAlert(kAlertChoose, "Open Tpt Inet 68K Library isn't in this Mac's Extensions "
                                    "folder. Choose it yourself?", "") != 1 ||
            !ChooseFile(&spec))
            return 0;
    }

    for (;;) {
        short status = FindBranch(&spec, &branch, &err);
        short choice;

        if (err != noErr) {
            snprintf(msg, sizeof(msg), "Couldn't read the library (error %d). %s", err, ErrorText(err));
            ShowAlert(kAlertInfo, msg, "");
            return 0;
        }

        if (status == kOriginalCode) {
            choice = ShowAlert(kAlertPatch,
                "Open Transport 1.3 offers TCP window scaling, then ignores the scale the "
                "server picks. Behind Cloudflare (Notion, Anthropic, and many more) a 128 KB "
                "window reads as 16 bytes: about 3 s lost per connection, and uploads crawl.",
                "This changes 2 bytes so the Mac stops offering it. Restore puts them back.");
            if (choice != 1)
                return 0;
            err = WriteBranch(&spec, branch, kBranchPatched);
        } else if (status == kPatchedCode) {
            choice = ShowAlert(kAlertRestore,
                "This Open Transport is already fixed: it doesn't offer TCP window scaling.",
                "Restore the original 2 bytes?");
            if (choice != 1)
                return 0;
            err = WriteBranch(&spec, branch, kBranchOriginal);
        } else {
            choice = ShowAlert(kAlertChoose,
                "This copy of Open Tpt Inet 68K Library isn't one this app knows. It looks for "
                "Open Transport 1.3's code, so nothing was changed.",
                "Choose another copy?");
            if (choice != 1 || !ChooseFile(&spec))
                return 0;
            continue;
        }

        if (err != noErr) {
            snprintf(msg, sizeof(msg), "Couldn't change the library (error %d). %s", err, ErrorText(err));
            ShowAlert(kAlertInfo, msg, "Nothing was changed.");
            return 0;
        }

        /* Read it back, so the message says what's really on disk */
        status = FindBranch(&spec, &branch, &err);
        if (status == kUnknown) {
            ShowAlert(kAlertInfo, "The change couldn't be checked afterwards. Run this app "
                                  "again to see the library's state.", "");
            return 0;
        }
        if (ShowAlert(kAlertRestart,
                      status == kPatchedCode ? "Done: Open Transport no longer offers TCP window scaling."
                                             : "Done: the original 2 bytes are back.",
                      "Open Transport reads the library at startup. Restart now?") == 1)
            ShutDwnStart();
        return 0;
    }
}
