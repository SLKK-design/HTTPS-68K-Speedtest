/* donna with the multiply and the x19 reduction fused in 68020 assembly
   (unused_donna_fused_m68k.s). Tried and dropped for the saturated code. */
#define asm_donna 1
#define curve25519_donna x25519_donna_fused
#include "unused_curve25519_donna.c"
