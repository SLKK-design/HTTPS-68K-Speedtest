/* Host verification for curve25519_sat.c (OPT #8b).
 *
 * 1. RFC 7748 section 5.2 test vectors (both).
 * 2. RFC 7748 iterated test: 1 and 1,000 iterations.
 * 3. Random cross-check vs the reference donna C implementation
 *    (unused_curve25519_donna.c compiled without asm_donna, entry renamed).
 *
 * Run: tests/run_tests.sh, which builds it against Claude 68's src/core/net.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Implementation under test (C fallback path — no sat_vli_asm). */
#include "curve25519_sat.c"

/* Reference implementation, entry renamed to avoid the collision. */
#define curve25519_donna ref_curve25519_donna
/* donna re-typedefs u8/s32/limb identically — C11 allows it */
#include "unused_curve25519_donna.c"
#undef curve25519_donna

static void hex2bin(uint8_t *out, const char *hex) {
  int i;
  for (i = 0; i < 32; i++) {
    unsigned v;
    sscanf(hex + 2 * i, "%02x", &v);
    out[i] = (uint8_t)v;
  }
}

static void dump(const char *tag, const uint8_t *b) {
  int i;
  printf("%s: ", tag);
  for (i = 0; i < 32; i++) printf("%02x", b[i]);
  printf("\n");
}

static int g_fail = 0;

static void expect(const char *name, const uint8_t *got, const uint8_t *want) {
  if (memcmp(got, want, 32) != 0) {
    printf("FAIL %s\n", name);
    dump("  got ", got);
    dump("  want", want);
    g_fail = 1;
  } else {
    printf("ok   %s\n", name);
  }
}

int main(void) {
  uint8_t k[32], u[32], out[32], want[32], ref[32];
  int i;

  /* RFC 7748 5.2 vector 1 */
  hex2bin(k, "a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4");
  hex2bin(u, "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c");
  hex2bin(want, "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552");
  curve25519_donna(out, k, u);
  expect("rfc7748 vector 1", out, want);

  /* RFC 7748 5.2 vector 2 */
  hex2bin(k, "4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d");
  hex2bin(u, "e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493");
  hex2bin(want, "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957");
  curve25519_donna(out, k, u);
  expect("rfc7748 vector 2", out, want);

  /* RFC 7748 iterated test: k = u = basepoint 9; each iteration
   * out = X25519(k, u); u = k; k = out. */
  memset(k, 0, 32); k[0] = 9;
  memcpy(u, k, 32);
  for (i = 0; i < 1000; i++) {
    curve25519_donna(out, k, u);
    memcpy(u, k, 32);
    memcpy(k, out, 32);
    if (i == 0) {
      hex2bin(want, "422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079");
      expect("rfc7748 iterate 1", k, want);
    }
  }
  hex2bin(want, "684cf59ba83309552800ef566f2f4d3c1c3887c49360e3875f2eb94d99532c51");
  expect("rfc7748 iterate 1000", k, want);

  /* Random cross-check vs reference donna. */
  srand(8675309);
  for (i = 0; i < 20000; i++) {
    int j;
    for (j = 0; j < 32; j++) { k[j] = (uint8_t)rand(); u[j] = (uint8_t)rand(); }
    curve25519_donna(out, k, u);
    ref_curve25519_donna(ref, k, u);
    if (memcmp(out, ref, 32) != 0) {
      printf("FAIL cross-check at iteration %d\n", i);
      dump("  k   ", k);
      dump("  u   ", u);
      dump("  sat ", out);
      dump("  ref ", ref);
      g_fail = 1;
      break;
    }
  }
  if (!g_fail) printf("ok   20000 random cross-checks vs reference donna\n");

  /* Edge inputs: u = 0, u = p-1, u = all 0xFF (top bit masked). */
  {
    static const char *edges[] = {
      "0000000000000000000000000000000000000000000000000000000000000000",
      "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
    };
    for (i = 0; i < 3; i++) {
      int j;
      hex2bin(u, edges[i]);
      for (j = 0; j < 32; j++) k[j] = (uint8_t)(rand());
      curve25519_donna(out, k, u);
      ref_curve25519_donna(ref, k, u);
      if (memcmp(out, ref, 32) != 0) {
        printf("FAIL edge %d\n", i);
        g_fail = 1;
      }
    }
    if (!g_fail) printf("ok   edge inputs\n");
  }

  printf(g_fail ? "FAILED\n" : "ALL PASS\n");
  return g_fail;
}
