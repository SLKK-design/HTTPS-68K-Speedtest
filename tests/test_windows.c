/* test_windows.c: checks the rebuilt 2-bit and 3-bit window verify
   (src/variants/uecc_windows.inc) on this computer before it goes on a Mac.
   It compiles Claude 68's uECC.c in plain C, with the speed test's vectors.
   Run: tests/run_tests.sh.
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "uECC.c"
#include "vectors.h"

#define V_CURVE uECC_secp256r1()
static void v_pub_native(const uint8_t* pub, uint32_t* q)
{
    uecc_p256_bytes_to_native(q, pub);
    uecc_p256_bytes_to_native(q + 8, pub + 32);
}
#include "uecc_windows.inc"

static uint32_t table[64 * 16];

int main(void)
{
    uint8_t bad[32];
    int bits, fails = 0;

    memcpy(bad, kP256Hash, 32);
    bad[5] ^= 0x10;
    printf("micro-ecc plain verify: good %d, flipped %d\n",
           uECC_verify(kP256Pub, kP256Hash, 32, kP256Sig, V_CURVE),
           uECC_verify(kP256Pub, bad, 32, kP256Sig, V_CURVE));
    for (bits = 2; bits <= 3; bits++) {
        int good, wrong;
        bench_win_table(bits, kP256Pub, table);
        good = bench_win_verify(bits, kP256Hash, 32, kP256Sig, table);
        wrong = bench_win_verify(bits, bad, 32, kP256Sig, table);
        printf("%d-bit window: good %d, flipped %d  %s\n", bits, good, wrong,
               good == 1 && wrong == 0 ? "ok" : "WRONG");
        fails += !(good == 1 && wrong == 0);
    }
    return fails;
}
