/* bench_alt.c: Claude 68's three mbedTLS hooks, with every method switchable.

   Claude 68 replaces mbedtls_ecdsa_verify, mbedtls_ecdh_gen_public and
   mbedtls_ecdh_compute_shared in uecc_alt.c (the app's definitions win at
   link time, -Wl,--allow-multiple-definition). This file replaces them in
   the same way, but each call reads gMethods first, so one app can run a
   connection the way the March build did and the next the way today's
   does. The hooks also time themselves into gHS, which splits a handshake
   into its crypto and everything else.

   The pieces are Claude 68's own: the micro-ecc builds in src/variants are
   its uECC.c, the X25519 code its curve25519_sat.c and the unused_* files
   it keeps for reference, and the caches below follow uecc_alt.c. Only the
   on-disk persistence is left out: a test run is one launch, and clearing
   the caches in memory is what "a fresh start" means here. */

#include <string.h>
#include <Timer.h>
#include "mbedtls/ecdh.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"
#include "mbedtls/error.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mac_entropy.h"
#include "methods.h"

/* fast_alloc.c: calloc/free come from a static 64 KB pool in between */
extern void fast_alloc_begin(void);
extern void fast_alloc_end(void);

/* src/variants: X25519, all with donna's contract (clamps the scalar) */
extern int x25519_donna_c(uint8_t* out, const uint8_t* scalar, const uint8_t* point);
extern int x25519_donna_asm(uint8_t* out, const uint8_t* scalar, const uint8_t* point);
extern int x25519_donna_fused(uint8_t* out, const uint8_t* scalar, const uint8_t* point);
extern int x25519_sat(uint8_t* out, const uint8_t* scalar, const uint8_t* point);

const char* const kX25519Names[kX25519Count] = {
    "mbedTLS",
    "donna in C",
    "donna, assembly multiply",
    "donna, fused assembly",
    "8 x 32-bit on Comba assembly",
};

/* 2^128 G on P-256, uECC native words, X then Y (from uecc_alt.c) */
const uint32_t kBenchG2[16] = {
    0xD789BD85, 0x57C84FC9, 0xC297EAC3, 0xFC35FF7D,
    0x88C6766E, 0xFB982FD5, 0xEEDB5E67, 0x447D739B,
    0x72E25B32, 0x0C7E33C9, 0xA7FAE500, 0x3D349B95,
    0x3A4AAFF7, 0xE12E9D95, 0x834131EE, 0x2D4825AB
};

BenchMethods   gMethods;
HandshakeStats gHS;

void bench_methods_today(BenchMethods* m)
{
    m->verify   = kVerifyMicroEcc;
    m->uecc     = &kUeccComba;
    m->tables   = kTablesStraus;
    m->x25519   = kX25519Sat;
    m->pool     = 1;
    m->pregen   = 1;
    m->sigcache = 1;
    m->tickets  = 1;
    m->pinWE1   = 1;
}

void bench_stats_reset(void)
{
    memset(&gHS, 0, sizeof(gHS));
    gHS.verify_path = "";
}

uint32_t bench_us(void)
{
    UnsignedWide w;
    Microseconds(&w);
    return w.lo;
}

/* ------------------------------------------------------------------ */
/* RNG                                                                 */
/* ------------------------------------------------------------------ */
static mbedtls_entropy_context s_entropy;
static mbedtls_ctr_drbg_context s_drbg;
static int s_rng_ready = 0;

static int uecc_rng(uint8_t* dest, unsigned size)
{
    return mbedtls_ctr_drbg_random(&s_drbg, dest, size) == 0;
}

int bench_rng_init(void)
{
    static const UeccVariant* const all[] = {
        &kUeccCOs, &kUeccCO3, &kUeccUnrolled, &kUeccKaratsuba, &kUeccComba
    };
    unsigned i;
    int ret;

    if (s_rng_ready)
        return 0;
    mbedtls_entropy_init(&s_entropy);
    /* as in the apps: Time Manager jitter on top of the build's TickCount() */
    mbedtls_entropy_add_source(&s_entropy, mac_entropy_poll, NULL,
                               MAC_ENTROPY_THRESHOLD, MBEDTLS_ENTROPY_SOURCE_STRONG);
    mbedtls_ctr_drbg_init(&s_drbg);
    ret = mbedtls_ctr_drbg_seed(&s_drbg, mbedtls_entropy_func, &s_entropy,
                                (const unsigned char*)"SpeedTest", 9);
    if (ret != 0)
        return ret;
    for (i = 0; i < sizeof(all) / sizeof(all[0]); i++)
        all[i]->set_rng(uecc_rng);
    s_rng_ready = 1;
    return 0;
}

int bench_rng(void* p_rng, unsigned char* buf, size_t len)
{
    (void)p_rng;
    return mbedtls_ctr_drbg_random(&s_drbg, buf, len);
}

/* ------------------------------------------------------------------ */
/* State one connection leaves for the next                            */
/* ------------------------------------------------------------------ */

/* Remembered signature checks, 2 slots as in uecc_alt.c: slot 0 ends up
   with the certificate check (same key and hash every time), slot 1 with
   the server's signature over this connection's key exchange. */
#define SIG_SLOTS 2
static uint8_t s_sig_pub[SIG_SLOTS][64];
static uint8_t s_sig_hash[SIG_SLOTS][64];
static size_t  s_sig_len[SIG_SLOTS];
static int     s_sig_valid[SIG_SLOTS];
static int     s_sig_next = 0;

/* Verify tables, for the key in s_tab_pub */
static uint32_t s_tabA[288];    /* (G, Q)   */
static uint32_t s_tabB[288];    /* (G2, Q2) */
static uint8_t  s_tab_pub[64];
static int      s_tab_kind = kTablesNone;
static const UeccVariant* s_tab_uecc = NULL;

/* The last P-256 key checked in a handshake: the server certificate's own
   key, which signs the key exchange. The tables are built for it. */
static uint8_t s_server_pub[64];
static int     s_server_pub_valid = 0;

/* X25519 key pair made before connecting (little-endian, as RFC 7748) */
static uint8_t s_pre_priv[32];
static uint8_t s_pre_pub[32];
static int     s_pre_ready = 0;

void bench_clear_state(void)
{
    memset(s_sig_valid, 0, sizeof(s_sig_valid));
    s_sig_next = 0;
    s_tab_kind = kTablesNone;
    s_tab_uecc = NULL;
    s_server_pub_valid = 0;
    memset(s_pre_priv, 0, sizeof(s_pre_priv));
    s_pre_ready = 0;
}

static int sig_cache_hit(const uint8_t* pub, const unsigned char* hash, size_t len)
{
    int i;
    if (len > sizeof(s_sig_hash[0]))
        return 0;
    for (i = 0; i < SIG_SLOTS; i++)
        if (s_sig_valid[i] && s_sig_len[i] == len &&
            memcmp(s_sig_pub[i], pub, 64) == 0 &&
            memcmp(s_sig_hash[i], hash, len) == 0)
            return 1;
    return 0;
}

static void sig_cache_store(const uint8_t* pub, const unsigned char* hash, size_t len)
{
    int i, slot = -1;
    if (len > sizeof(s_sig_hash[0]))
        return;
    for (i = 0; i < SIG_SLOTS; i++) {
        if (!s_sig_valid[i] || memcmp(s_sig_pub[i], pub, 64) == 0) {
            slot = i;   /* empty, or the same key: update in place */
            break;
        }
    }
    if (slot < 0) {
        slot = s_sig_next;
        s_sig_next = (s_sig_next + 1) % SIG_SLOTS;
    }
    memcpy(s_sig_pub[slot], pub, 64);
    memcpy(s_sig_hash[slot], hash, len);
    s_sig_len[slot] = len;
    s_sig_valid[slot] = 1;
}

/* ------------------------------------------------------------------ */
/* mbedTLS's own math                                                  */
/* ------------------------------------------------------------------ */

/* ECDSA verify the way stock mbedtls_ecdsa_verify does it (ecp_muladd).
   The group is loaded fresh: the caller's belongs to a certificate that
   outlives this call, and mbedTLS caches a table of multiples of G in it.
   Made from the pool, that table would be overwritten by the next pool
   user (fast_alloc.c). Everything is freed before the pool closes. */
static int verify_mbedtls(mbedtls_ecp_group_id id, const unsigned char* buf, size_t blen,
                          const mbedtls_ecp_point* Q, const mbedtls_mpi* r,
                          const mbedtls_mpi* s, int pool)
{
    mbedtls_ecp_group grp;
    mbedtls_ecp_point R;
    mbedtls_mpi e, s_inv, u1, u2;
    size_t n_size, use_size;
    int ret;

    if (pool)
        fast_alloc_begin();
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&R);
    mbedtls_mpi_init(&e);
    mbedtls_mpi_init(&s_inv);
    mbedtls_mpi_init(&u1);
    mbedtls_mpi_init(&u2);

    MBEDTLS_MPI_CHK(mbedtls_ecp_group_load(&grp, id));
    if (mbedtls_mpi_cmp_int(r, 1) < 0 || mbedtls_mpi_cmp_mpi(r, &grp.N) >= 0 ||
        mbedtls_mpi_cmp_int(s, 1) < 0 || mbedtls_mpi_cmp_mpi(s, &grp.N) >= 0) {
        ret = MBEDTLS_ERR_ECP_VERIFY_FAILED;
        goto cleanup;
    }

    n_size   = (grp.nbits + 7) / 8;
    use_size = blen > n_size ? n_size : blen;
    MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&e, buf, use_size));
    if (use_size * 8 > grp.nbits)
        MBEDTLS_MPI_CHK(mbedtls_mpi_shift_r(&e, use_size * 8 - grp.nbits));
    if (mbedtls_mpi_cmp_mpi(&e, &grp.N) >= 0)
        MBEDTLS_MPI_CHK(mbedtls_mpi_sub_mpi(&e, &e, &grp.N));

    MBEDTLS_MPI_CHK(mbedtls_mpi_inv_mod(&s_inv, s, &grp.N));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(&u1, &e, &s_inv));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&u1, &u1, &grp.N));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(&u2, r, &s_inv));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&u2, &u2, &grp.N));
    MBEDTLS_MPI_CHK(mbedtls_ecp_muladd(&grp, &R, &u1, &grp.G, &u2, Q));

    if (mbedtls_ecp_is_zero(&R)) {
        ret = MBEDTLS_ERR_ECP_VERIFY_FAILED;
        goto cleanup;
    }
    MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&R.MBEDTLS_PRIVATE(X), &R.MBEDTLS_PRIVATE(X), &grp.N));
    ret = mbedtls_mpi_cmp_mpi(&R.MBEDTLS_PRIVATE(X), r) == 0 ? 0 : MBEDTLS_ERR_ECP_VERIFY_FAILED;

cleanup:
    mbedtls_ecp_point_free(&R);
    mbedtls_mpi_free(&e);
    mbedtls_mpi_free(&s_inv);
    mbedtls_mpi_free(&u1);
    mbedtls_mpi_free(&u2);
    mbedtls_ecp_group_free(&grp);
    if (pool)
        fast_alloc_end();
    return ret;
}

/* For the crypto tests: verify_mbedtls on byte inputs. nbytes 32 or 48. */
int bench_verify_mbedtls_bytes(int nbytes, const uint8_t* pub, const uint8_t* hash,
                               size_t hlen, const uint8_t* sig, int pool)
{
    mbedtls_ecp_point Q;
    mbedtls_mpi r, s;
    int ret;

    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&Q.MBEDTLS_PRIVATE(X), pub, nbytes));
    MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&Q.MBEDTLS_PRIVATE(Y), pub + nbytes, nbytes));
    MBEDTLS_MPI_CHK(mbedtls_mpi_lset(&Q.MBEDTLS_PRIVATE(Z), 1));
    MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&r, sig, nbytes));
    MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&s, sig + nbytes, nbytes));
    ret = verify_mbedtls(nbytes == 48 ? MBEDTLS_ECP_DP_SECP384R1 : MBEDTLS_ECP_DP_SECP256R1,
                         hash, hlen, &Q, &r, &s, pool);
cleanup:
    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    return ret;
}

/* X25519 with mbedTLS's Montgomery ladder (ecp_mul). The result is copied
   out and every MPI freed before the pool closes, for the reason above. */
int x25519_mbedtls(uint8_t* out, const uint8_t* scalar, const uint8_t* point, int pool)
{
    mbedtls_ecp_group grp;
    mbedtls_ecp_point P, R;
    mbedtls_mpi m;
    uint8_t k[32], u[32];
    int ret;

    memcpy(k, scalar, 32);
    k[0]  &= 248;
    k[31] &= 127;
    k[31] |= 64;
    memcpy(u, point, 32);
    u[31] &= 127;

    if (pool)
        fast_alloc_begin();
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&P);
    mbedtls_ecp_point_init(&R);
    mbedtls_mpi_init(&m);

    MBEDTLS_MPI_CHK(mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519));
    MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary_le(&m, k, 32));
    MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary_le(&P.MBEDTLS_PRIVATE(X), u, 32));
    MBEDTLS_MPI_CHK(mbedtls_mpi_lset(&P.MBEDTLS_PRIVATE(Z), 1));
    MBEDTLS_MPI_CHK(mbedtls_ecp_mul(&grp, &R, &m, &P, bench_rng, NULL));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary_le(&R.MBEDTLS_PRIVATE(X), out, 32));

cleanup:
    mbedtls_mpi_free(&m);
    mbedtls_ecp_point_free(&R);
    mbedtls_ecp_point_free(&P);
    mbedtls_ecp_group_free(&grp);
    if (pool)
        fast_alloc_end();
    memset(k, 0, sizeof(k));
    return ret;
}

int x25519_run(int which, uint8_t* out, const uint8_t* scalar, const uint8_t* point)
{
    switch (which) {
        case kX25519DonnaC:     return x25519_donna_c(out, scalar, point);
        case kX25519DonnaAsm:   return x25519_donna_asm(out, scalar, point);
        case kX25519DonnaFused: return x25519_donna_fused(out, scalar, point);
        case kX25519Sat:        return x25519_sat(out, scalar, point);
        default:                return x25519_mbedtls(out, scalar, point, gMethods.pool);
    }
}

/* ------------------------------------------------------------------ */
/* Before and after a connection                                       */
/* ------------------------------------------------------------------ */

static int make_x25519_key(uint8_t* priv, uint8_t* pub)
{
    static const uint8_t base[32] = { 9 };
    if (bench_rng(NULL, priv, 32) != 0)
        return MBEDTLS_ERR_ECP_RANDOM_FAILED;
    priv[0]  &= 248;
    priv[31] &= 127;
    priv[31] |= 64;
    return x25519_run(gMethods.x25519, pub, priv, base);
}

uint32_t bench_pregen_key(int (*f_rng)(void*, unsigned char*, size_t), void* p_rng)
{
    uint32_t t0;
    (void)f_rng;
    (void)p_rng;
    if (!gMethods.pregen || s_pre_ready)
        return 0;
    t0 = bench_us();
    if (make_x25519_key(s_pre_priv, s_pre_pub) == 0)
        s_pre_ready = 1;
    return bench_us() - t0;
}

uint32_t bench_build_tables(void)
{
    const UeccVariant* u = gMethods.uecc;
    uint32_t t0, q2[16];

    if (gMethods.verify != kVerifyMicroEcc || gMethods.tables == kTablesNone ||
        !u || !s_server_pub_valid)
        return 0;
    if (s_tab_kind == gMethods.tables && s_tab_uecc == u &&
        memcmp(s_tab_pub, s_server_pub, 64) == 0)
        return 0;   /* already built for this key */

    t0 = bench_us();
    u->table_GQ(s_server_pub, s_tabA);
    if (gMethods.tables == kTablesStraus) {
        u->midpoint(s_server_pub, q2);
        u->table_G2Q2(q2, s_tabB);
    }
    memcpy(s_tab_pub, s_server_pub, 64);
    s_tab_kind = gMethods.tables;
    s_tab_uecc = u;
    return bench_us() - t0;
}

/* ------------------------------------------------------------------ */
/* The hooks                                                           */
/* ------------------------------------------------------------------ */

int mbedtls_ecdsa_verify(mbedtls_ecp_group* grp, const unsigned char* buf, size_t blen,
                         const mbedtls_ecp_point* Q, const mbedtls_mpi* r, const mbedtls_mpi* s)
{
    uint8_t pub[64], sig[64];
    uint32_t t0;
    int ok, ret;

    if (grp->id != MBEDTLS_ECP_DP_SECP256R1) {
        /* P-384: GTS Root R4's signature on WE1, while WE1 isn't pinned */
        t0 = bench_us();
        ret = verify_mbedtls(grp->id, buf, blen, Q, r, s, gMethods.pool);
        gHS.verify384_us += bench_us() - t0;
        gHS.verify384_n++;
        if (ret != 0)
            gHS.verify_fail++;
        return ret;
    }

    if ((ret = mbedtls_mpi_write_binary(&Q->MBEDTLS_PRIVATE(X), pub, 32)) != 0 ||
        (ret = mbedtls_mpi_write_binary(&Q->MBEDTLS_PRIVATE(Y), pub + 32, 32)) != 0 ||
        (ret = mbedtls_mpi_write_binary(r, sig, 32)) != 0 ||
        (ret = mbedtls_mpi_write_binary(s, sig + 32, 32)) != 0)
        return ret;

    memcpy(s_server_pub, pub, 64);
    s_server_pub_valid = 1;

    if (gMethods.sigcache && sig_cache_hit(pub, buf, blen)) {
        gHS.verify256_hits++;
        gHS.verify_path = "remembered";
        return 0;
    }

    t0 = bench_us();
    if (gMethods.verify == kVerifyMbedTLS) {
        ok = verify_mbedtls(grp->id, buf, blen, Q, r, s, gMethods.pool) == 0;
        gHS.verify_path = "mbedTLS";
    } else {
        const UeccVariant* u = gMethods.uecc;
        int have = s_tab_uecc == u && memcmp(s_tab_pub, pub, 64) == 0;
        if (gMethods.tables == kTablesStraus && have && s_tab_kind == kTablesStraus) {
            ok = u->verify_straus(buf, (unsigned)blen, sig, s_tabA, s_tabB);
            gHS.verify_path = "Straus";
        } else if (gMethods.tables != kTablesNone && have) {
            ok = u->verify_wnaf(buf, (unsigned)blen, sig, s_tabA);
            gHS.verify_path = "wNAF";
        } else {
            ok = u->verify(pub, buf, (unsigned)blen, sig);
            gHS.verify_path = "plain";
        }
    }
    gHS.verify256_us += bench_us() - t0;
    gHS.verify256_n++;

    if (!ok) {
        gHS.verify_fail++;
        return MBEDTLS_ERR_ECP_VERIFY_FAILED;
    }
    if (gMethods.sigcache)
        sig_cache_store(pub, buf, blen);
    return 0;
}

int mbedtls_ecdh_gen_public(mbedtls_ecp_group* grp, mbedtls_mpi* d, mbedtls_ecp_point* Q,
                            int (*f_rng)(void*, unsigned char*, size_t), void* p_rng)
{
    uint8_t priv[32], pub[64];
    uint32_t t0;
    int ret;
    (void)f_rng;
    (void)p_rng;

    if (grp->id == MBEDTLS_ECP_DP_CURVE25519) {
        if (gMethods.pregen && s_pre_ready) {
            memcpy(priv, s_pre_priv, 32);
            memcpy(pub, s_pre_pub, 32);
            memset(s_pre_priv, 0, sizeof(s_pre_priv));
            s_pre_ready = 0;
        } else {
            /* on the server's clock */
            t0 = bench_us();
            ret = make_x25519_key(priv, pub);
            gHS.keygen_us += bench_us() - t0;
            gHS.keygen_n++;
            if (ret != 0)
                return ret;
        }
        ret = mbedtls_mpi_read_binary_le(d, priv, 32);
        if (ret == 0) ret = mbedtls_mpi_read_binary_le(&Q->MBEDTLS_PRIVATE(X), pub, 32);
        if (ret == 0) ret = mbedtls_mpi_lset(&Q->MBEDTLS_PRIVATE(Z), 1);
        memset(priv, 0, sizeof(priv));
        return ret;
    }

    /* P-256, if a server ever picks it over X25519 */
    {
        const UeccVariant* u = gMethods.uecc ? gMethods.uecc : &kUeccComba;
        t0 = bench_us();
        ret = u->make_key(pub, priv);
        gHS.keygen_us += bench_us() - t0;
        gHS.keygen_n++;
        if (!ret)
            return MBEDTLS_ERR_ECP_RANDOM_FAILED;
        ret = mbedtls_mpi_read_binary(d, priv, 32);
        if (ret == 0) ret = mbedtls_mpi_read_binary(&Q->MBEDTLS_PRIVATE(X), pub, 32);
        if (ret == 0) ret = mbedtls_mpi_read_binary(&Q->MBEDTLS_PRIVATE(Y), pub + 32, 32);
        if (ret == 0) ret = mbedtls_mpi_lset(&Q->MBEDTLS_PRIVATE(Z), 1);
        memset(priv, 0, sizeof(priv));
        return ret;
    }
}

int mbedtls_ecdh_compute_shared(mbedtls_ecp_group* grp, mbedtls_mpi* z,
                                const mbedtls_ecp_point* Q, const mbedtls_mpi* d,
                                int (*f_rng)(void*, unsigned char*, size_t), void* p_rng)
{
    uint8_t k[32], u[64], out[32];
    uint32_t t0;
    int ret, i, zero;
    (void)f_rng;
    (void)p_rng;

    if (grp->id == MBEDTLS_ECP_DP_CURVE25519) {
        if ((ret = mbedtls_mpi_write_binary_le(d, k, 32)) != 0 ||
            (ret = mbedtls_mpi_write_binary_le(&Q->MBEDTLS_PRIVATE(X), u, 32)) != 0)
            return ret;
        t0 = bench_us();
        ret = x25519_run(gMethods.x25519, out, k, u);
        gHS.x25519_us += bench_us() - t0;
        gHS.x25519_n++;
        memset(k, 0, sizeof(k));
        if (ret != 0)
            return ret;
        for (zero = 1, i = 0; i < 32; i++)
            if (out[i]) zero = 0;
        if (zero)   /* low-order point */
            return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
        return mbedtls_mpi_read_binary_le(z, out, 32);
    }

    {
        const UeccVariant* uv = gMethods.uecc ? gMethods.uecc : &kUeccComba;
        if ((ret = mbedtls_mpi_write_binary(&Q->MBEDTLS_PRIVATE(X), u, 32)) != 0 ||
            (ret = mbedtls_mpi_write_binary(&Q->MBEDTLS_PRIVATE(Y), u + 32, 32)) != 0 ||
            (ret = mbedtls_mpi_write_binary(d, k, 32)) != 0)
            return ret;
        t0 = bench_us();
        ret = uv->shared_secret(u, k, out);
        gHS.x25519_us += bench_us() - t0;
        gHS.x25519_n++;
        memset(k, 0, sizeof(k));
        if (!ret)
            return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
        return mbedtls_mpi_read_binary(z, out, 32);
    }
}
