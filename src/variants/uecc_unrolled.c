/* micro-ecc, -O3, multiply in the unrolled 68020 assembly
   (unused_vli_mult_m68k.s, renamed vUnr_uECC_vli_mult); square and
   reduction still in C. Flags: CMakeLists.txt */
#define UECC_PFX(s) vUnr_##s
#include "uecc_variant.inc"
UECC_VARIANT(kUeccUnrolled, "unrolled asm multiply")
