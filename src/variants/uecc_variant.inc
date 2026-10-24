/* uecc_variant.inc: one build of Claude 68's uECC.c, wrapped as a UeccVariant.

   Include this from a uecc_*.c file after defining UECC_PFX, then emit the
   table with UECC_VARIANT(symbol, "name"). The flags that make the builds
   differ (optimization, which assembly) are set per file in CMakeLists.txt.
   The adapters are static, so every build has its own. */
#include <stdint.h>
#include <string.h>
#include "uecc_rename.h"
#include "uECC.c"
#include "methods.h"

#define V_CURVE uECC_secp256r1()

static int v_verify(const uint8_t* pub, const uint8_t* hash, unsigned hlen, const uint8_t* sig)
{
    return uECC_verify(pub, hash, hlen, sig, V_CURVE);
}

static void v_pub_native(const uint8_t* pub, uint32_t* q)
{
    uecc_p256_bytes_to_native(q, pub);
    uecc_p256_bytes_to_native(q + 8, pub + 32);
}

static void v_table_GQ(const uint8_t* pub, uint32_t* table)
{
    uint32_t q[16];
    v_pub_native(pub, q);
    uecc_shamir2_precompute(q, table, V_CURVE);
}

static int v_verify_wnaf(const uint8_t* hash, unsigned hlen, const uint8_t* sig, const uint32_t* table)
{
    return uecc_verify_precomp(NULL, hash, hlen, sig, table, V_CURVE);  /* Q is in the table */
}

static void v_midpoint(const uint8_t* pub, uint32_t* q2)
{
    v_pub_native(pub, q2);
    uecc_point_double_n_p256(q2, q2 + 8, 128, V_CURVE);
}

static void v_table_G2Q2(const uint32_t* q2, uint32_t* table)
{
    uecc_shamir2_precompute_pair(kBenchG2, q2, table, V_CURVE);
}

static int v_verify_straus(const uint8_t* hash, unsigned hlen, const uint8_t* sig,
                           const uint32_t* tableA, const uint32_t* tableB)
{
    return uecc_verify_precomp4(hash, hlen, sig, tableA, tableB, V_CURVE);
}

static int v_make_key(uint8_t* pub, uint8_t* priv)
{
    return uECC_make_key(pub, priv, V_CURVE);
}

static int v_shared_secret(const uint8_t* pub, const uint8_t* priv, uint8_t* secret)
{
    return uECC_shared_secret(pub, priv, secret, V_CURVE);
}

static void v_set_rng(int (*rng)(uint8_t* dest, unsigned size))
{
    uECC_set_rng(rng);
}

#define UECC_VARIANT(sym, label)                                          \
    const UeccVariant sym = {                                             \
        label, v_verify, v_table_GQ, v_verify_wnaf, v_midpoint,           \
        v_table_G2Q2, v_verify_straus, v_make_key, v_shared_secret,       \
        v_set_rng                                                         \
    };
