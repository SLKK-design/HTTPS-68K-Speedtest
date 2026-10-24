/* donna with its multiply and square in 68020 assembly, reduction still in
   C (unused_donna_m68k.s): the step before the fused version. donna's C no
   longer has a switch for it, so the fused hooks are pointed at glue that
   runs the assembly product and then the C reduction. Every call site
   passes a 19-limb buffer, which the assembly fills. */
#define asm_donna 1
#define fmul_fused_m68k    donna_asm_fmul
#define fsquare_fused_m68k donna_asm_fsquare
#define curve25519_donna   x25519_donna_asm
#include "unused_curve25519_donna.c"

extern void fproduct_m68k(limb* output, const limb* in2, const limb* in);
extern void fsquare_inner_m68k(limb* output, const limb* in);

void donna_asm_fmul(limb* t, const limb* a, const limb* b)
{
    fproduct_m68k(t, a, b);
    freduce_degree(t);
}

void donna_asm_fsquare(limb* t, const limb* in)
{
    fsquare_inner_m68k(t, in);
    freduce_degree(t);
}
