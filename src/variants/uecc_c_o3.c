/* micro-ecc in plain C at -O3: the flags since June 5, 2026. March to
   May 13 it was -Ofast, which is -O3 plus floating-point shortcuts this
   integer code doesn't use. Flags: CMakeLists.txt */
#define UECC_PFX(s) vCO3_##s
#include "uecc_variant.inc"
UECC_VARIANT(kUeccCO3, "C, -O3")

/* The 2-bit and 3-bit windows, tried in March and dropped for wNAF, on
   this same C -O3 build (rebuilt for the test: see the file) */
#include "uecc_windows.inc"
