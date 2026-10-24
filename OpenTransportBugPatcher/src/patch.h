/* patch.h: the window-scale branch in Open Transport 1.3's TCP code */

#ifndef PATCH_H
#define PATCH_H

enum { kUnknown, kOriginalCode, kPatchedCode };

extern const unsigned char kBranchOriginal[2];   /* beq.s: offer window scaling */
extern const unsigned char kBranchPatched[2];    /* bra.s: don't */

/* Looks through the library's code resources for the instructions around
   the branch. Returns kOriginalCode or kPatchedCode if they appear exactly
   once, with the branch's offset in the resource fork; kUnknown otherwise.
   *ioErr is set if the file couldn't be read. Reads only. */
short FindBranch(const FSSpec* spec, long* branchOffset, OSErr* ioErr);

/* Writes 2 bytes at that offset in the resource fork */
OSErr WriteBranch(const FSSpec* spec, long offset, const unsigned char* bytes);

#endif
