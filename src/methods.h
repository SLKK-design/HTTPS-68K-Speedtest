/* methods.h: every handshake method the speed test can switch between.

   The TLS engine is Claude 68's (Notion Classic runs the same one). Its
   three mbedTLS hooks, ECDSA verify and the two ECDH steps, live in
   bench_alt.c here instead of uecc_alt.c, so each one can be pointed at a
   different implementation between connections. */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* micro-ecc builds                                                    */
/* ------------------------------------------------------------------ */

/* One build of Claude 68's uECC.c (src/variants/uecc_*.c). Each is the
   same source compiled with its own flags and symbol prefix, so all of
   them are in the app at once. Keys and signatures are big-endian bytes
   (X|Y and r|s), tables are 288 words (uECC native format). */
typedef struct {
    const char* name;
    /* Plain micro-ecc verify: Shamir's trick, 1 bit at a time */
    int  (*verify)(const uint8_t* pub, const uint8_t* hash, unsigned hlen, const uint8_t* sig);
    /* wNAF table for (G, Q), then a verify that uses it */
    void (*table_GQ)(const uint8_t* pub, uint32_t* table);
    int  (*verify_wnaf)(const uint8_t* hash, unsigned hlen, const uint8_t* sig, const uint32_t* table);
    /* Straus: Q2 = 2^128 Q (the cached midpoint), the (G2, Q2) table,
       then the verify over both tables */
    void (*midpoint)(const uint8_t* pub, uint32_t* q2);
    void (*table_G2Q2)(const uint32_t* q2, uint32_t* table);
    int  (*verify_straus)(const uint8_t* hash, unsigned hlen, const uint8_t* sig,
                          const uint32_t* tableA, const uint32_t* tableB);
    /* P-256 ECDH, for a server that doesn't offer X25519 */
    int  (*make_key)(uint8_t* pub, uint8_t* priv);
    int  (*shared_secret)(const uint8_t* pub, const uint8_t* priv, uint8_t* secret);
    void (*set_rng)(int (*rng)(uint8_t* dest, unsigned size));
} UeccVariant;

extern const UeccVariant kUeccCOs;       /* C, -Os: micro-ecc as first added      */
extern const UeccVariant kUeccCO3;       /* C, -O3                                 */
extern const UeccVariant kUeccUnrolled;  /* unrolled assembly multiply             */
extern const UeccVariant kUeccKaratsuba; /* Karatsuba multiply (tried, dropped)    */
extern const UeccVariant kUeccCombaC;    /* Comba multiply and square, C reduction */
extern const UeccVariant kUeccComba;     /* Comba multiply and square + Solinas    */

/* Plain fixed windows over (G, Q), tried in March before wNAF: 2 bits
   (16 points) or 3 bits (64 points), on the C -O3 build. Rebuilt for the
   test from micro-ecc's own parts (src/variants/uecc_windows.inc). */
enum { kWin3Words = 64 * 16 };
void bench_win_table(int bits, const uint8_t* pub, uint32_t* table);
int  bench_win_verify(int bits, const uint8_t* hash, unsigned hlen, const uint8_t* sig,
                      const uint32_t* table);

/* G2 = 2^128 G on P-256, uECC native words (bench_alt.c) */
extern const uint32_t kBenchG2[16];

/* ------------------------------------------------------------------ */
/* X25519 implementations                                              */
/* ------------------------------------------------------------------ */

/* out = X25519(scalar, point), all 32-byte little-endian (RFC 7748) */
typedef int (*X25519Fn)(uint8_t* out, const uint8_t* scalar, const uint8_t* point);

enum {
    kX25519MbedTLS = 0,  /* mbedTLS's own ecp_mul                         */
    kX25519DonnaC,       /* curve25519-donna in C                         */
    kX25519DonnaAsm,     /* donna with a 68020 assembly multiply (dropped) */
    kX25519DonnaFused,   /* donna, multiply and reduce fused (dropped)    */
    kX25519Sat,          /* 8 x 32-bit words on the Comba assembly: today */
    kX25519Count
};
extern const char* const kX25519Names[kX25519Count];

/* mbedTLS's X25519, with or without the 64 KB pool */
int x25519_mbedtls(uint8_t* out, const uint8_t* scalar, const uint8_t* point, int pool);
/* Runs implementation `which` (the mbedTLS one uses gMethods.pool) */
int x25519_run(int which, uint8_t* out, const uint8_t* scalar, const uint8_t* point);

/* ------------------------------------------------------------------ */
/* Method settings for the live connections                            */
/* ------------------------------------------------------------------ */

enum { kVerifyMbedTLS = 0, kVerifyMicroEcc = 1 };
enum { kTablesNone = 0, kTablesWNAF = 1, kTablesStraus = 2 };

typedef struct {
    int verify;               /* kVerifyMbedTLS or kVerifyMicroEcc            */
    const UeccVariant* uecc;  /* the micro-ecc build, when verify is micro-ecc */
    int tables;               /* built for the server's key after a connection */
    int x25519;               /* kX25519...                                   */
    int pool;                 /* mbedTLS math runs from the 64 KB pool        */
    int pregen;               /* key pair made before connecting              */
    int sigcache;             /* signature checks remembered                  */
    int tickets;              /* session tickets: the next connection resumes */
    int pinWE1;               /* WE1 is a trust anchor: no P-384 check        */
} BenchMethods;

extern BenchMethods gMethods;
void bench_methods_today(BenchMethods* m);   /* what Claude 68 runs now */

/* Time spent inside the hooks during one handshake, in microseconds */
typedef struct {
    uint32_t verify256_us, verify384_us, x25519_us, keygen_us;
    int      verify256_n, verify256_hits, verify384_n, x25519_n, keygen_n;
    int      verify_fail;
    const char* verify_path;  /* how the last P-256 check ran */
} HandshakeStats;

extern HandshakeStats gHS;
void bench_stats_reset(void);

/* Forget everything a previous connection left behind: remembered
   signature checks, tables, the pregenerated key. A fresh launch. */
void bench_clear_state(void);

/* Make the key pair for the next connection now, off the server's clock
   (when gMethods.pregen). Returns microseconds spent, 0 if nothing to do. */
uint32_t bench_pregen_key(int (*f_rng)(void*, unsigned char*, size_t), void* p_rng);

/* After a connection: build the verify tables for the server's key, as
   the app does once a page has loaded. Returns microseconds spent. */
uint32_t bench_build_tables(void);

/* RNG for the hooks and the crypto tests: a CTR-DRBG seeded once */
int  bench_rng_init(void);
int  bench_rng(void* p_rng, unsigned char* buf, size_t len);

/* Microseconds since boot, from the Time Manager (wraps after 71 min) */
uint32_t bench_us(void);

#ifdef __cplusplus
}
#endif
