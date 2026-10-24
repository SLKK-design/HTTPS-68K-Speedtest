/* micro-ecc as Claude 68 builds it today: -O3, looped Comba multiply and
   square and the Solinas reduction in 68020 assembly (renamed vCmb_*).
   Flags: CMakeLists.txt */
#define UECC_PFX(s) vCmb_##s
#include "uecc_variant.inc"
UECC_VARIANT(kUeccComba, "Comba + Solinas asm")
