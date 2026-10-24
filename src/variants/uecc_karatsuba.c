/* micro-ecc, -O3, multiply by one-level Karatsuba in C (karatsuba.c,
   tried and dropped); square and reduction still in C. Flags: CMakeLists.txt */
#define UECC_PFX(s) vKar_##s
#include "uecc_variant.inc"
UECC_VARIANT(kUeccKaratsuba, "Karatsuba multiply")
