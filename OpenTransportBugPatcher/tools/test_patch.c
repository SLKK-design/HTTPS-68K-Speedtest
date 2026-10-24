/* test_patch.c: runs patch.c's FindBranch and WriteBranch on resource
   forks saved as plain files, on a modern computer.

     cc -I tools/hoststubs -I src tools/test_patch.c src/patch.c -o test_patch
     ./test_patch <work copy of the fork> [expect: original|patched|unknown]

   It reports what it finds, and for an original it patches the work copy,
   checks it, and restores it. */
#include <stdio.h>
#include <string.h>
#include "Files.h"
#include "patch.h"

static FILE* sFiles[8];

OSErr FSpOpenRF(const FSSpec* spec, signed char perm, short* ref)
{
    for (short i = 1; i < 8; i++)
        if (!sFiles[i]) {
            sFiles[i] = fopen(spec->path, perm == fsRdPerm ? "rb" : "r+b");
            if (!sFiles[i]) return fnfErr;
            *ref = i;
            return noErr;
        }
    return ioErr;
}
OSErr SetFPos(short ref, short mode, long offset) { (void)mode; return fseek(sFiles[ref], offset, SEEK_SET) ? ioErr : noErr; }
OSErr FSRead(short ref, long* count, void* buf) { size_t n = fread(buf, 1, (size_t)*count, sFiles[ref]); OSErr e = (long)n == *count ? noErr : eofErr; *count = (long)n; return e; }
OSErr FSWrite(short ref, long* count, const void* buf) { size_t n = fwrite(buf, 1, (size_t)*count, sFiles[ref]); *count = (long)n; return n ? noErr : ioErr; }
OSErr FSClose(short ref) { fclose(sFiles[ref]); sFiles[ref] = NULL; return noErr; }
OSErr FlushVol(void* name, short vRefNum) { (void)name; (void)vRefNum; return noErr; }

static const char* Name(short s) { return s == kOriginalCode ? "original" : s == kPatchedCode ? "patched" : "unknown"; }

int main(int argc, char** argv)
{
    FSSpec spec = { 0, argv[1] };
    long at = 0;
    OSErr err;
    short s = FindBranch(&spec, &at, &err);
    printf("%s: %s", argv[1], Name(s));
    if (s != kUnknown) printf(" at fork offset 0x%lX", at);
    printf(" (err %d)\n", err);
    if (argc > 2 && strcmp(argv[2], Name(s)) != 0) { printf("  FAIL: expected %s\n", argv[2]); return 1; }
    if (s == kOriginalCode) {
        long at2;
        if (WriteBranch(&spec, at, kBranchPatched) != noErr) { printf("  FAIL: write\n"); return 1; }
        s = FindBranch(&spec, &at2, &err);
        printf("  after patching: %s at 0x%lX\n", Name(s), at2);
        if (s != kPatchedCode || at2 != at) { printf("  FAIL\n"); return 1; }
        if (argc > 3) return 0;   /* leave it patched, to compare */
        WriteBranch(&spec, at, kBranchOriginal);
        s = FindBranch(&spec, &at2, &err);
        printf("  after restoring: %s\n", Name(s));
        if (s != kOriginalCode) { printf("  FAIL\n"); return 1; }
    }
    return 0;
}
