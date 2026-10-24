/* patch.c: finds and changes the window-scale branch in Open Transport's
   TCP code, reading the resource fork with the File Manager only, so the
   same code also builds on a modern computer for testing (tools/). */

#include <Files.h>
#include <MacMemory.h>
#include <string.h>
#include "patch.h"

/* The code that builds the options of the Mac's connection request (SYN).
   In Open Transport 1.3 it adds window scaling when the connection is
   SYN_SENT, or when the other side offered it:

     2012       move.l  (a2),d0
     72FE       moveq   #-2,d1          ; SYN_SENT
     B081       cmp.l   d1,d0
     670E       beq.s   add_wscale      ; <- patched to 602A: bra.s skip
     202A 006C  move.l  108(a2),d0
     720C       moveq   #12,d1
     E2A8       lsr.l   d1,d0
     7201       moveq   #1,d1
     C081       and.l   d1,d0
     661C       bne.s   skip
     7603       moveq   #3,d3           ; add_wscale: kind 3, length 3 */
static const unsigned char kOriginal[24] = {
    0x20, 0x12, 0x72, 0xFE, 0xB0, 0x81, 0x67, 0x0E, 0x20, 0x2A, 0x00, 0x6C,
    0x72, 0x0C, 0xE2, 0xA8, 0x72, 0x01, 0xC0, 0x81, 0x66, 0x1C, 0x76, 0x03
};
enum { kBranchAt = 6 };
const unsigned char kBranchOriginal[2] = { 0x67, 0x0E };   /* beq.s +14 */
const unsigned char kBranchPatched[2]  = { 0x60, 0x2A };   /* bra.s +42 */

static OSErr ReadAt(short ref, long offset, void* buf, long len)
{
    OSErr err = SetFPos(ref, fsFromStart, offset);
    if (err == noErr)
        err = FSRead(ref, &len, buf);
    return err;
}

static unsigned long Get32(const unsigned char* p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3];
}

static unsigned short Get16(const unsigned char* p)
{
    return (unsigned short)((p[0] << 8) | p[1]);
}

/* Looks through the code resources ('cd..' and 'cD..') of the library's
   resource fork for the instructions above. Returns kOriginalCode or
   kPatchedCode if they appear exactly once, and the file offset of the
   branch; kUnknown otherwise. Reads only. */
short FindBranch(const FSSpec* spec, long* branchOffset, OSErr* ioErr)
{
    short ref, status = kUnknown;
    int found = 0;
    unsigned char header[16];
    unsigned char* map = NULL;
    unsigned char* code = NULL;
    OSErr err;

    *ioErr = noErr;
    err = FSpOpenRF(spec, fsRdPerm, &ref);
    if (err != noErr) {
        *ioErr = err;
        return kUnknown;
    }
    err = ReadAt(ref, 0, header, sizeof(header));
    if (err == noErr) {
        unsigned long dataOff = Get32(header), mapOff = Get32(header + 4);
        unsigned long mapLen = Get32(header + 12);
        map = (unsigned char*)NewPtr((long)mapLen);
        if (!map || mapLen < 30 || ReadAt(ref, (long)mapOff, map, (long)mapLen) != noErr)
            goto done;
        {
            unsigned short typeList = Get16(map + 24);
            int numTypes = Get16(map + typeList) + 1, t;
            for (t = 0; t < numTypes; t++) {
                const unsigned char* te = map + typeList + 2 + t * 8;
                int count = Get16(te + 4) + 1, r;
                unsigned short refList = Get16(te + 6);
                if (te[0] != 'c' || (te[1] != 'd' && te[1] != 'D'))
                    continue;
                for (r = 0; r < count; r++) {
                    const unsigned char* re = map + typeList + refList + r * 12;
                    unsigned long at = dataOff + (Get32(re + 4) & 0x00FFFFFFUL);
                    unsigned char lenBytes[4];
                    unsigned long len, i;
                    if (ReadAt(ref, (long)at, lenBytes, 4) != noErr)
                        continue;
                    len = Get32(lenBytes);
                    if (len < sizeof(kOriginal) || len > 1024UL * 1024UL)
                        continue;
                    code = (unsigned char*)NewPtr((long)len);
                    if (!code)
                        continue;
                    if (ReadAt(ref, (long)at + 4, code, (long)len) == noErr) {
                        for (i = 0; i + sizeof(kOriginal) <= len; i += 2) {
                            if (memcmp(code + i, kOriginal, kBranchAt) != 0 ||
                                memcmp(code + i + kBranchAt + 2, kOriginal + kBranchAt + 2,
                                       sizeof(kOriginal) - kBranchAt - 2) != 0)
                                continue;
                            if (memcmp(code + i + kBranchAt, kBranchOriginal, 2) == 0)
                                status = kOriginalCode;
                            else if (memcmp(code + i + kBranchAt, kBranchPatched, 2) == 0)
                                status = kPatchedCode;
                            else
                                continue;
                            *branchOffset = (long)(at + 4 + i + kBranchAt);
                            found++;
                        }
                    }
                    DisposePtr((Ptr)code);
                    code = NULL;
                }
            }
        }
    } else {
        *ioErr = err;
    }
done:
    if (map)
        DisposePtr((Ptr)map);
    FSClose(ref);
    return found == 1 ? status : kUnknown;
}

/* Writes the 2 bytes of the branch, straight into the resource fork */
OSErr WriteBranch(const FSSpec* spec, long offset, const unsigned char* bytes)
{
    short ref;
    long len = 2;
    OSErr err = FSpOpenRF(spec, fsRdWrPerm, &ref);
    if (err != noErr)
        return err;
    err = SetFPos(ref, fsFromStart, offset);
    if (err == noErr)
        err = FSWrite(ref, &len, bytes);
    FSClose(ref);
    FlushVol(NULL, spec->vRefNum);
    return err;
}

