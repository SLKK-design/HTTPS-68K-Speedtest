/* Host-side verification of the loop/carry model used by
 * vli_square_comba_m68k.s (OPT #6).
 *
 * The model mirrors the asm exactly:
 *   - per column k = 0..14: pair count (k+1)>>1 rising / (15-k)>>1 falling,
 *     pairs (i, k-i) walking i up from min, summed once into a 96-bit
 *     accumulator s
 *   - s doubled once (asm: ADD/ADDX/ADDX; assert no overflow past bit 95)
 *   - diagonal left[k/2]^2 added when k is even
 *   - carry-in c1:c0 from the previous column added; result[k] = low word;
 *     c0/c1 = next 2 words (asm: rotate into a5/a6)
 *   - result[15] = final c0
 *
 * Reference: schoolbook 8x8 square with full carry propagation.
 *
 * Run: tests/run_tests.sh.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef unsigned __int128 u128;

static void square_model(uint32_t *result, const uint32_t *left) {
    uint32_t c0 = 0, c1 = 0;
    for (int k = 0; k < 15; k++) {
        int min   = (k < 8) ? 0 : k - 7;
        int pairs = (k < 8) ? (k + 1) >> 1 : (15 - k) >> 1;
        u128 s = 0;
        for (int p = 0; p < pairs; p++) {
            int i = min + p;
            s += (u128)((uint64_t)left[i] * left[k - i]);
        }
        s <<= 1;
        assert((s >> 96) == 0);          /* doubling must not leave d2 */
        if ((k & 1) == 0)
            s += (u128)((uint64_t)left[k / 2] * left[k / 2]);
        s += c0;
        s += (u128)c1 << 32;
        assert((s >> 96) == 0);          /* column total fits 3 words */
        result[k] = (uint32_t)s;
        c0 = (uint32_t)(s >> 32);
        c1 = (uint32_t)(s >> 64);
    }
    assert(c1 == 0);                     /* result[15] is a single word */
    result[15] = c0;
}

static void square_ref(uint32_t *r, const uint32_t *a) {
    memset(r, 0, 16 * sizeof(uint32_t));
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            uint64_t cur = (uint64_t)a[i] * a[j];
            int k = i + j;
            while (cur) {
                cur += r[k];
                r[k] = (uint32_t)cur;
                cur >>= 32;
                k++;
                assert(k <= 16);
            }
        }
    }
}

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint32_t rnd32(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 16);
}

int main(void) {
    uint32_t a[8], rm[16], rr[16];

    /* edge case: all-ones */
    for (int i = 0; i < 8; i++) a[i] = 0xFFFFFFFFu;
    square_model(rm, a);
    square_ref(rr, a);
    if (memcmp(rm, rr, sizeof rm) != 0) {
        fprintf(stderr, "FAIL all-ones\n");
        return 1;
    }

    /* edge case: zero */
    memset(a, 0, sizeof a);
    square_model(rm, a);
    square_ref(rr, a);
    if (memcmp(rm, rr, sizeof rm) != 0) {
        fprintf(stderr, "FAIL zero\n");
        return 1;
    }

    for (long t = 0; t < 200000; t++) {
        for (int i = 0; i < 8; i++) {
            a[i] = rnd32();
            if (t % 13 == 0) a[i] |= 0xFFFF0000u;  /* bias high words */
            if (t % 29 == 0) a[i] = (t % 58 == 0) ? 0 : 0xFFFFFFFFu;
        }
        square_model(rm, a);
        square_ref(rr, a);
        if (memcmp(rm, rr, sizeof rm) != 0) {
            fprintf(stderr, "FAIL at vector %ld\n", t);
            for (int i = 0; i < 8; i++) fprintf(stderr, "a[%d]=%08X\n", i, a[i]);
            return 1;
        }
    }
    printf("PASS: 200000 vectors + edge cases, model == schoolbook reference\n");
    return 0;
}
