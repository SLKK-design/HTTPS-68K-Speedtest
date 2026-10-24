/* Host-side verification of the column/carry model used by
 * solinas_p256_m68k.s (OPT #6b).
 *
 * Reference: faithful reimplementation of uECC's uint32
 * vli_mmod_fast_secp256r1 table-building half (curve-specific.inc):
 * sequential 8-word vli_add/vli_sub passes for t, 2*s1, 2*s2, s3, s4,
 * -d1..-d4 with the int carry accumulator.  The model must produce the
 * identical (result[0..7], carry) pair for any 16-word product.
 *
 * Run: tests/run_tests.sh.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "solinas_model.c"

static uint32_t vli_add(uint32_t *r, const uint32_t *a, const uint32_t *b) {
    uint32_t carry = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t s = (uint64_t)a[i] + b[i] + carry;
        r[i] = (uint32_t)s;
        carry = (uint32_t)(s >> 32);
    }
    return carry;
}

static uint32_t vli_sub(uint32_t *r, const uint32_t *a, const uint32_t *b) {
    uint32_t borrow = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t d = (uint64_t)a[i] - b[i] - borrow;
        r[i] = (uint32_t)d;
        borrow = (uint32_t)((d >> 32) & 1);
    }
    return borrow;
}

/* table-building half of uECC vli_mmod_fast_secp256r1, verbatim logic */
static int solinas_ref(uint32_t *result, const uint32_t *product) {
    uint32_t tmp[8];
    int carry;

    memcpy(result, product, 8 * sizeof(uint32_t));            /* t */

    tmp[0] = tmp[1] = tmp[2] = 0;                              /* s1 */
    tmp[3] = product[11];
    tmp[4] = product[12];
    tmp[5] = product[13];
    tmp[6] = product[14];
    tmp[7] = product[15];
    carry = (int)vli_add(tmp, tmp, tmp);
    carry += (int)vli_add(result, result, tmp);

    tmp[3] = product[12];                                      /* s2 */
    tmp[4] = product[13];
    tmp[5] = product[14];
    tmp[6] = product[15];
    tmp[7] = 0;
    carry += (int)vli_add(tmp, tmp, tmp);
    carry += (int)vli_add(result, result, tmp);

    tmp[0] = product[8];                                       /* s3 */
    tmp[1] = product[9];
    tmp[2] = product[10];
    tmp[3] = tmp[4] = tmp[5] = 0;
    tmp[6] = product[14];
    tmp[7] = product[15];
    carry += (int)vli_add(result, result, tmp);

    tmp[0] = product[9];                                       /* s4 */
    tmp[1] = product[10];
    tmp[2] = product[11];
    tmp[3] = product[13];
    tmp[4] = product[14];
    tmp[5] = product[15];
    tmp[6] = product[13];
    tmp[7] = product[8];
    carry += (int)vli_add(result, result, tmp);

    tmp[0] = product[11];                                      /* d1 */
    tmp[1] = product[12];
    tmp[2] = product[13];
    tmp[3] = tmp[4] = tmp[5] = 0;
    tmp[6] = product[8];
    tmp[7] = product[10];
    carry -= (int)vli_sub(result, result, tmp);

    tmp[0] = product[12];                                      /* d2 */
    tmp[1] = product[13];
    tmp[2] = product[14];
    tmp[3] = product[15];
    tmp[4] = tmp[5] = 0;
    tmp[6] = product[9];
    tmp[7] = product[11];
    carry -= (int)vli_sub(result, result, tmp);

    tmp[0] = product[13];                                      /* d3 */
    tmp[1] = product[14];
    tmp[2] = product[15];
    tmp[3] = product[8];
    tmp[4] = product[9];
    tmp[5] = product[10];
    tmp[6] = 0;
    tmp[7] = product[12];
    carry -= (int)vli_sub(result, result, tmp);

    tmp[0] = product[14];                                      /* d4 */
    tmp[1] = product[15];
    tmp[2] = 0;
    tmp[3] = product[9];
    tmp[4] = product[10];
    tmp[5] = product[11];
    tmp[6] = 0;
    tmp[7] = product[13];
    carry -= (int)vli_sub(result, result, tmp);

    return carry;
}

static uint64_t rng_state = 0x243F6A8885A308D3ull;
static uint32_t rnd32(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 16);
}

int main(void) {
    uint32_t p[16], rm[8], rr[8];

    /* edges: all-ones, zero, single bits in the high half */
    for (int e = 0; e < 18; e++) {
        memset(p, 0, sizeof p);
        if (e == 0)
            for (int i = 0; i < 16; i++) p[i] = 0xFFFFFFFFu;
        else if (e >= 2)
            p[8 + (e - 2) / 2] = (e % 2) ? 1u : 0xFFFFFFFFu;
        int cm = solinas_p256_columns_model(rm, p);
        int cr = solinas_ref(rr, p);
        if (cm != cr || memcmp(rm, rr, sizeof rm) != 0) {
            fprintf(stderr, "FAIL edge %d (carry %d vs %d)\n", e, cm, cr);
            return 1;
        }
    }

    for (long t = 0; t < 500000; t++) {
        for (int i = 0; i < 16; i++) {
            p[i] = rnd32();
            if (t % 11 == 0) p[i] |= 0xFFFF0000u;
            if (t % 23 == 0) p[i] = (t % 46 == 0) ? 0 : 0xFFFFFFFFu;
        }
        int cm = solinas_p256_columns_model(rm, p);
        int cr = solinas_ref(rr, p);
        if (cm != cr || memcmp(rm, rr, sizeof rm) != 0) {
            fprintf(stderr, "FAIL at vector %ld (carry %d vs %d)\n", t, cm, cr);
            for (int i = 0; i < 16; i++)
                fprintf(stderr, "p[%d]=%08X\n", i, p[i]);
            return 1;
        }
    }
    printf("PASS: 500000 vectors + edges, model == uECC sequential reference\n");
    return 0;
}
