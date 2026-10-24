/* main.cpp: Speed Test, a window, a menu and the runs in tests.cpp.

   Times every TLS handshake method Notion Classic and Claude 68 have used,
   on the Mac it runs on. Results go to the window and to "Speed Test
   Results" next to the app. Cmd-period stops a run after the current step. */

#include <MacMemory.h>
#include <Menus.h>
#include <Fonts.h>
#include <Dialogs.h>
#include <Windows.h>
#include <TextEdit.h>
#include <Events.h>
#include <Quickdraw.h>
#include <ToolUtils.h>
#include <Devices.h>
#include <Resources.h>
#include <Sound.h>
#include "methods.h"
#include "log.h"

enum { kMenuApple = 128, kMenuFile = 129, kMenuEdit = 130 };
enum { kItemAbout = 1 };
enum { kItemRunAll = 1, kItemCrypto = 3, kItemToday = 4, kItemSends = 5, kItemRows = 6, kItemRowsLab = 7,
       kItemRowsLabGrouped = 8, kItemRowsLabLessWork = 9, kItemQuit = 11 };
enum { kAboutAlert = 128 };

static WindowPtr sWin;
static bool sQuit = false;

/* fast_alloc.c reports its pool use after each run; not needed here */
extern "C" void fast_alloc_log(const char* func, const char* msg)
{
    (void)func;
    (void)msg;
}

static void InitApp()
{
    /* Stack to 128 KB before the heap takes the room, as Claude 68 does:
       mbedTLS and the verify code use a lot of it */
    SetApplLimit(GetApplLimit() - (104L * 1024L));
    MaxApplZone();
    for (int i = 0; i < 10; i++)
        MoreMasters();

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    FlushEvents(everyEvent, 0);

    SetMenuBar(GetNewMBar(128));
    AppendResMenu(GetMenuHandle(kMenuApple), 'DRVR');
    DrawMenuBar();

    sWin = GetNewWindow(128, NULL, (WindowPtr)-1);
    SetPort(sWin);
    LogInit(sWin);
    ShowWindow(sWin);
}

static void Run(short item)
{
    StopReset();
    SetCursor(*GetCursor(watchCursor));
    LogMachine();
    if (item == kItemRunAll || item == kItemCrypto)
        RunCryptoTests();
    if ((item == kItemRunAll || item == kItemToday) && !StopRequested())
        RunLiveToday();
    if ((item == kItemRunAll || item == kItemSends) && !StopRequested())
        RunSendTest();
    if ((item == kItemRunAll || item == kItemRows) && !StopRequested())
        RunLiveRows();
    if (item == kItemRowsLab)
        RunLiveRowsLab();   /* needs tools/lab_server.py, so not in Run Everything */
    if (item == kItemRowsLabGrouped)
        RunLiveRowsLabGrouped();
    if (item == kItemRowsLabLessWork)
        RunLiveRowsLabLessWork();
    LogText(StopRequested() ? "\rStopped.\r" : "\rDone.\r");
    InitCursor();
    SysBeep(10);
}

static void DoMenu(long choice)
{
    short menu = HiWord(choice), item = LoWord(choice);
    HiliteMenu(0);   /* before a run, which can take a while */
    if (menu == kMenuApple) {
        if (item == kItemAbout) {
            Alert(kAboutAlert, NULL);
        } else {
            Str255 name;
            GetMenuItemText(GetMenuHandle(kMenuApple), item, name);
            OpenDeskAcc(name);
        }
    } else if (menu == kMenuFile) {
        if (item == kItemQuit)
            sQuit = true;
        else
            Run(item);
    } else if (menu == kMenuEdit) {
        SystemEdit(item - 1);
    }
}

int main()
{
    InitApp();
    bench_methods_today(&gMethods);
    LogText("Speed Test times every TLS handshake method Notion Classic and\r"
            "Claude 68 have used, on this Mac.\r\r"
            "File > Run Everything (Cmd-R) runs all four parts, one after another.\r");
    LogF("Results also go to \"%s\" next to this app.\r", LogFileName());
    LogText("Cmd-period stops a run after the current step.\r");

    while (!sQuit) {
        EventRecord e;
        if (!WaitNextEvent(everyEvent, &e, 10, NULL))
            continue;
        switch (e.what) {
            case mouseDown: {
                WindowPtr win;
                short part = FindWindow(e.where, &win);
                if (part == inMenuBar)
                    DoMenu(MenuSelect(e.where));
                else if (part == inSysWindow)
                    SystemClick(&e, win);
                else if (part == inDrag)
                    DragWindow(win, e.where, &qd.screenBits.bounds);
                else if (part == inGoAway && TrackGoAway(win, e.where))
                    sQuit = true;
                else if (part == inContent && win != FrontWindow())
                    SelectWindow(win);
                break;
            }
            case keyDown:
            case autoKey:
                if (e.modifiers & cmdKey)
                    DoMenu(MenuKey((char)(e.message & charCodeMask)));
                break;
            case updateEvt:
                BeginUpdate((WindowPtr)e.message);
                if ((WindowPtr)e.message == sWin)
                    LogDraw();
                EndUpdate((WindowPtr)e.message);
                break;
        }
    }
    return 0;
}
