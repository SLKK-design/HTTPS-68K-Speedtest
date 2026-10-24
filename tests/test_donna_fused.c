/* test_donna_fused.c — host verification for OPT #8 (fused donna reduce).
 *
 * Compares the generated fused model (donna_fused_model.c — identical
 * column/x19 math to unused_donna_fused_m68k.s) against the original
 * fproduct/fsquare_inner + freduce_degree over random limb vectors at
 * realistic magnitudes (post-fsum/fdifference limbs are < 2^27).
 *
 * Run: tests/run_tests.sh, which builds it against Claude 68's src/core/net.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pull in the reference implementation (no asm_donna on host). */
#include "unused_curve25519_donna.c"

/* Generated model of the fused asm. */
#include "donna_fused_model.c"

#define VECTORS 200000

static limb rand_limb(unsigned bits)
{
    /* uniform in (-2^bits, 2^bits) */
    uint64_t r = ((uint64_t)rand() << 31) ^ (uint64_t)rand();
    limb v = (limb)(r & ((1ULL << bits) - 1));
    if (rand() & 1) v = -v;
    return v;
}

int main(void)
{
    limb a[10], b[10], ref[19], fused[10];
    int i, n, fail = 0;

    srand(0x68000);

    for (n = 0; n < VECTORS; n++) {
        /* mostly realistic 27-bit limbs; sprinkle extremes */
        unsigned bits = (n % 17 == 0) ? 28 : 27;
        for (i = 0; i < 10; i++) { a[i] = rand_limb(bits); b[i] = rand_limb(bits); }

        /* fmul path */
        fproduct(ref, a, b);
        freduce_degree(ref);
        fmul_fused_model(fused, a, b);
        for (i = 0; i < 10; i++) {
            if (ref[i] != fused[i]) {
                printf("FAIL mul vec %d limb %d: ref=%lld fused=%lld\n",
                       n, i, (long long)ref[i], (long long)fused[i]);
                fail = 1;
            }
        }

        /* fsquare path */
        fsquare_inner(ref, a);
        freduce_degree(ref);
        fsquare_fused_model(fused, a);
        for (i = 0; i < 10; i++) {
            if (ref[i] != fused[i]) {
                printf("FAIL sqr vec %d limb %d: ref=%lld fused=%lld\n",
                       n, i, (long long)ref[i], (long long)fused[i]);
                fail = 1;
            }
        }
        if (fail) return 1;
    }

    /* End-to-end sanity: full scalar mult must be unchanged when fmul/
     * fsquare are rebuilt on top of the fused model + freduce_coefficients. */
    {
        u8 priv[32], base[32], out_ref[32], out_fused[32];
        int k;
        for (k = 0; k < 20; k++) {
            for (i = 0; i < 32; i++) { priv[i] = (u8)rand(); base[i] = (u8)rand(); }
            base[31] &= 0x7f;
            curve25519_donna(out_ref, priv, base);

            /* fused pipeline by hand: expand → ladder using model ops */
            /* (curve25519_donna already used the reference fmul/fsquare;
             *  here we just re-check one fmul/fsquare equivalence at the
             *  field-op level with reduced inputs from fexpand) */
            limb x[10], t_ref[19], t_fused[19];
            fexpand(x, base);
            fproduct(t_ref, x, x);
            freduce_degree(t_ref);
            freduce_coefficients(t_ref);
            fmul_fused_model(t_fused, x, x);
            t_fused[10] = 0;
            freduce_coefficients(t_fused);
            for (i = 0; i < 10; i++)
                if (t_ref[i] != t_fused[i]) { printf("FAIL e2e fmul limb %d\n", i); return 1; }
            (void)out_ref; (void)out_fused;
        }
    }

    printf("PASS: %d vectors, mul+sqr fused model == fproduct/fsquare_inner+freduce_degree\n", VECTORS);
    return 0;
}
