/* log.h: the results, shown in the window and appended to
   "Speed Test Results" next to the application. */
#pragma once
#include <Windows.h>
#include <stdint.h>

void LogInit(WindowPtr win);
void LogDraw();                       /* update event */
void LogText(const char* text);       /* \r ends a line */
void LogF(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

/* The files next to the app, one pair per build and kind of Mac:
   "SP - 10241725 - 16-bit" (the results) and "... gaps" (raw timer gaps).
   10241725 is the build's month, day, hour and minute; 16-bit or 32-bit
   is the Mac's data bus. */
const char* LogFileName();
const char* LogGapsFileName();
extern "C" const char kBuildStamp[];   /* "Oct 24 2026 17:25:03" */

/* raw numbers for analysis elsewhere: appended to the gaps file */
OSErr LogGaps(const char* header, const unsigned short* gaps, unsigned long n);

/* "12.345 s" */
const char* FmtSec(uint32_t us);

/* Cmd-period since StopReset(): ends the run after the current step */
void StopReset();
bool StopRequested();

/* tests.cpp */
void RunCryptoTests();
void RunLiveToday();
void RunLiveRows();
void RunLiveRowsLab();
void RunLiveRowsLabGrouped();
void RunLiveRowsLabLessWork();
void RunSendTest();
void LogMachine();
