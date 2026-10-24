/* MacMemory.h for testing patch.c on a modern computer */
#ifndef HOST_MACMEMORY_H
#define HOST_MACMEMORY_H
#include <stdlib.h>
#define NewPtr(n) ((Ptr)malloc((size_t)(n)))
#define DisposePtr(p) free(p)
#endif
