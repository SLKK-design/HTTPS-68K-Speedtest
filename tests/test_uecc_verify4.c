/* test_uecc_verify4.c — host-side correctness test for the 4-scalar
   cached-midpoint Straus verify (OPT #7) in uECC.c.

   Exercises, against random keys/signatures:
     · uECC_verify              (baseline)
     · uecc_verify_precomp      (existing 2-point wNAF Shamir path)
     · uecc_verify_precomp4     (new 4-point split path: G, Q, G2, Q2)
     · uecc_point_double_n_p256 (Q2 = 2^128·Q used by the new path)
     · tampered-signature / tampered-hash rejection for both fast paths

   Run: tests/run_tests.sh, which builds it against Claude 68's src/core/net.

   (asm_mult NOT defined → uECC.c emits its portable C uECC_vli_mult,
    standing in for the m68k Karatsuba asm used in the real build.)     */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uECC.h"

/* Non-static helpers from uECC.c (same externs as uecc_alt.c uses). */
extern void uecc_shamir2_precompute(const uint32_t *Q_native, uint32_t *table, uECC_Curve curve);
extern void uecc_shamir2_precompute_pair(const uint32_t *P_native, const uint32_t *Q_native,
                                          uint32_t *table, uECC_Curve curve);
extern int  uecc_verify_precomp(const uint8_t *pub, const uint8_t *hash, unsigned hlen,
                                 const uint8_t *sig, const uint32_t *table, uECC_Curve curve);
extern int  uecc_verify_precomp4(const uint8_t *hash, unsigned hlen, const uint8_t *sig,
                                  const uint32_t *tableA, const uint32_t *tableB, uECC_Curve curve);
extern void uecc_point_double_n_p256(uint32_t *px, uint32_t *py, int n, uECC_Curve curve);
extern void uecc_p256_bytes_to_native(uint32_t *native, const uint8_t *bytes32);

#define NWORDS 8
#define TABLE_WORDS (18 * NWORDS * 2)

/* G2 = 2^128 * G — must match kG2_native in uecc_alt.c */
static const uint32_t kG2_native[NWORDS * 2] = {
    0xD789BD85, 0x57C84FC9, 0xC297EAC3, 0xFC35FF7D,
    0x88C6766E, 0xFB982FD5, 0xEEDB5E67, 0x447D739B,
    0x72E25B32, 0x0C7E33C9, 0xA7FAE500, 0x3D349B95,
    0x3A4AAFF7, 0xE12E9D95, 0x834131EE, 0x2D4825AB
};

static int test_rng(uint8_t *dest, unsigned size)
{
    arc4random_buf(dest, size);
    return 1;
}

static int sPass = 0, sFail = 0;
#define CHECK(cond, what) do { \
    if (cond) { sPass++; } \
    else { sFail++; printf("FAIL iter=%d %s\n", iter, what); } \
} while (0)

int main(void)
{
    uECC_Curve curve = uECC_secp256r1();
    static uint32_t tableA[TABLE_WORDS], tableB[TABLE_WORDS];
    int iter;

    uECC_set_rng(test_rng);

    for (iter = 0; iter < 50; iter++) {
        uint8_t priv[32], pub[64], hash[32], sig[64];
        uint32_t Qn[NWORDS * 2], Q2n[NWORDS * 2];

        if (!uECC_make_key(pub, priv, curve)) { printf("make_key failed\n"); return 1; }
        arc4random_buf(hash, sizeof(hash));
        if (!uECC_sign(priv, hash, sizeof(hash), sig, curve)) { printf("sign failed\n"); return 1; }

        CHECK(uECC_verify(pub, hash, sizeof(hash), sig, curve) == 1, "baseline uECC_verify");

        /* Existing 2-point fast path */
        uecc_p256_bytes_to_native(Qn,          pub);
        uecc_p256_bytes_to_native(Qn + NWORDS, pub + 32);
        uecc_shamir2_precompute(Qn, tableA, curve);
        CHECK(uecc_verify_precomp(pub, hash, sizeof(hash), sig, tableA, curve) == 1,
              "verify_precomp valid sig");

        /* New 4-point path: Q2 = 2^128·Q, tableB over (G2, Q2) */
        memcpy(Q2n, Qn, sizeof(Q2n));
        uecc_point_double_n_p256(Q2n, Q2n + NWORDS, 128, curve);
        uecc_shamir2_precompute_pair(kG2_native, Q2n, tableB, curve);
        CHECK(uecc_verify_precomp4(hash, sizeof(hash), sig, tableA, tableB, curve) == 1,
              "verify_precomp4 valid sig");

        /* Tampered signature must fail on both fast paths */
        sig[iter % 64] ^= 0x01;
        CHECK(uecc_verify_precomp(pub, hash, sizeof(hash), sig, tableA, curve) == 0,
              "verify_precomp tampered sig");
        CHECK(uecc_verify_precomp4(hash, sizeof(hash), sig, tableA, tableB, curve) == 0,
              "verify_precomp4 tampered sig");
        sig[iter % 64] ^= 0x01;

        /* Tampered hash must fail */
        hash[iter % 32] ^= 0x80;
        CHECK(uecc_verify_precomp4(hash, sizeof(hash), sig, tableA, tableB, curve) == 0,
              "verify_precomp4 tampered hash");
    }

    printf("%d passed, %d failed\n", sPass, sFail);
    return sFail ? 1 : 0;
}
