/* Files.h for testing patch.c on a modern computer: the few File Manager
   calls it uses, on top of stdio. An FSSpec here is just a path to a file
   that holds the resource fork's bytes. */
#ifndef HOST_FILES_H
#define HOST_FILES_H
#include <stdio.h>
typedef short OSErr;
typedef char* Ptr;
typedef struct { short vRefNum; const char* path; } FSSpec;
enum { noErr = 0, fnfErr = -43, ioErr = -36, eofErr = -39 };
enum { fsRdPerm = 1, fsRdWrPerm = 3, fsFromStart = 1 };
OSErr FSpOpenRF(const FSSpec* spec, signed char perm, short* ref);
OSErr SetFPos(short ref, short mode, long offset);
OSErr FSRead(short ref, long* count, void* buf);
OSErr FSWrite(short ref, long* count, const void* buf);
OSErr FSClose(short ref);
OSErr FlushVol(void* name, short vRefNum);
#endif
