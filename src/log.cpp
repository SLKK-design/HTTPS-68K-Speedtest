/* log.cpp: results in a TextEdit window and in "SP - <build> - <bus>".

   The file is the record: it is appended to and flushed line by line, so
   a crash or a power-off keeps everything up to the last finished step.
   The window only shows the tail (TextEdit holds 32 KB). */

#include <Files.h>
#include <Gestalt.h>
#include <Events.h>
#include <Fonts.h>
#include <TextEdit.h>
#include <Script.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "log.h"

static char  sResultsC[32];    /* "SP - 10241725 - 16-bit"      */
static char  sGapsC[32];       /* "SP - 10241725 - 16-bit gaps" */
static Str63 sResultsName;
static Str63 sGapsName;
static FSSpec sGapsSpec;       /* found at launch, in the app's folder */
static bool   sGapsSpecOK = false;

static WindowPtr sWin = NULL;
static TEHandle  sTE = NULL;
static short     sRef = 0;
static bool      sStop = false;

/* The Mac's data bus: 16-bit on the 68000 Macs and on the cost-reduced
   68020/030 ones (LC, LC II, Classic II, Color Classic), 32-bit otherwise. */
static const char* BusName()
{
    long mach = 0, proc = 0;
    Gestalt(gestaltMachineType, &mach);
    Gestalt(gestaltProcessorType, &proc);
    if (proc == gestalt68000)
        return "16-bit";
    switch (mach) {
    case gestaltClassicII:
    case gestaltMacLC:
    case gestaltMacLCII:
    case gestaltMacColorClassic:
        return "16-bit";
    }
    return "32-bit";
}

/* "Oct 24 2026 17:25:03" -> "10241725" */
static void BuildNumber(char out[9])
{
    static const char kMonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int month = 0;
    for (int m = 0; m < 12; m++)
        if (memcmp(kBuildStamp, kMonths + m * 3, 3) == 0)
            month = m + 1;
    out[0] = (char)('0' + month / 10);
    out[1] = (char)('0' + month % 10);
    out[2] = kBuildStamp[4] == ' ' ? '0' : kBuildStamp[4];
    out[3] = kBuildStamp[5];
    out[4] = kBuildStamp[12];
    out[5] = kBuildStamp[13];
    out[6] = kBuildStamp[15];
    out[7] = kBuildStamp[16];
    out[8] = '\0';
}

static void ToPascal(const char* c, Str63 p)
{
    size_t n = strlen(c);
    if (n > 31)
        n = 31;   /* HFS file names */
    p[0] = (unsigned char)n;
    memcpy(p + 1, c, n);
}

const char* LogFileName()     { return sResultsC; }
const char* LogGapsFileName() { return sGapsC; }

void LogInit(WindowPtr win)
{
    char build[9];
    BuildNumber(build);
    snprintf(sResultsC, sizeof(sResultsC), "SP - %s - %s", build, BusName());
    snprintf(sGapsC, sizeof(sGapsC), "%s gaps", sResultsC);
    ToPascal(sResultsC, sResultsName);
    ToPascal(sGapsC, sGapsName);

    sWin = win;
    SetPort(win);
    TextFont(kFontIDMonaco);
    TextSize(9);
    Rect r = win->portRect;
    InsetRect(&r, 4, 4);
    sTE = TENew(&r, &r);

    /* At launch the default directory is the application's folder */
    short vRef;
    long dirID;
    FSSpec spec;
    if (HGetVol(NULL, &vRef, &dirID) != noErr)
        return;
    /* An FSSpec holds the real volume, not the launch working directory,
       which may be gone by the time the gaps are written. */
    OSErr gerr = FSMakeFSSpec(vRef, dirID, sGapsName, &sGapsSpec);
    sGapsSpecOK = (gerr == noErr || gerr == fnfErr);
    OSErr err = FSMakeFSSpec(vRef, dirID, sResultsName, &spec);
    if (err == fnfErr)
        err = FSpCreate(&spec, 'ttxt', 'TEXT', smSystemScript);
    if (err == noErr && FSpOpenDF(&spec, fsRdWrPerm, &sRef) == noErr)
        SetFPos(sRef, fsFromLEOF, 0);
    else
        sRef = 0;
}

void LogDraw()
{
    if (!sTE)
        return;
    SetPort(sWin);
    EraseRect(&sWin->portRect);
    TEUpdate(&sWin->portRect, sTE);
}

static void ScrollToEnd()
{
    short lines = (*sTE)->nLines;
    short lineH = (*sTE)->lineHeight;
    short visible = ((*sTE)->viewRect.bottom - (*sTE)->viewRect.top) / lineH;
    short top = ((*sTE)->viewRect.top - (*sTE)->destRect.top) / lineH;
    short want = lines > visible ? lines - visible : 0;
    if (want != top)
        TEScroll(0, (top - want) * lineH, sTE);
}

void LogText(const char* text)
{
    long len = (long)strlen(text);
    if (len == 0)
        return;

    if (sRef) {
        long count = len;
        FSWrite(sRef, &count, text);
        ParamBlockRec pb;
        memset(&pb, 0, sizeof(pb));
        pb.ioParam.ioRefNum = sRef;
        PBFlushFileSync(&pb);
    }

    if (sTE) {
        SetPort(sWin);
        if ((*sTE)->teLength + len > 24000) {   /* keep the tail */
            TESetSelect(0, 8000, sTE);
            TEDelete(sTE);
        }
        TESetSelect(32767, 32767, sTE);
        TEInsert(text, len, sTE);
        ScrollToEnd();
    }
}

void LogF(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    LogText(buf);
}

const char* FmtSec(uint32_t us)
{
    static char bufs[4][16];
    static int next = 0;
    char* b = bufs[next];
    next = (next + 1) & 3;
    uint32_t ms = (us + 500) / 1000;
    snprintf(b, 16, "%lu.%03lu s", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
    return b;
}

void StopReset()
{
    sStop = false;
}

bool StopRequested()
{
    EventRecord e;
    while (!sStop && GetNextEvent(keyDownMask | autoKeyMask, &e)) {
        char c = (char)(e.message & charCodeMask);
        if ((e.modifiers & cmdKey) && c == '.')
            sStop = true;
    }
    return sStop;
}

/* Appends raw timer gaps to the gaps file next to the app: a header
   line, then one number per line, in a single write (the folder may be on
   a file server). If that file can't be written, the same lines go into
   the results file instead (not the window). Returns the gaps file's
   error, noErr when it worked. */
OSErr LogGaps(const char* header, const unsigned short* gaps, unsigned long n)
{
    static char buf[40000];
    short ref = 0;
    long len, count;
    OSErr err = fnfErr;

    len = snprintf(buf, sizeof(buf), "%s\r", header);
    for (unsigned long i = 0; i < n && len < (long)sizeof(buf) - 8; i++)
        len += snprintf(buf + len, sizeof(buf) - len, "%u\r", (unsigned)gaps[i]);

    if (sGapsSpecOK) {
        err = FSpCreate(&sGapsSpec, 'ttxt', 'TEXT', smSystemScript);
        if (err == dupFNErr)
            err = noErr;
        if (err == noErr)
            err = FSpOpenDF(&sGapsSpec, fsRdWrPerm, &ref);
        if (err == noErr) {
            SetFPos(ref, fsFromLEOF, 0);
            count = len;
            err = FSWrite(ref, &count, buf);
            FSClose(ref);
        }
    }
    if (err != noErr && sRef) {
        SetFPos(sRef, fsFromLEOF, 0);
        count = len;
        FSWrite(sRef, &count, buf);
    }
    return err;
}
