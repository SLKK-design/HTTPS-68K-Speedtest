/* test_mac_entropy.c — host-side checks for mac_entropy.c's control flow.
 *
 * The Mac's timer, tick count and clock are stubs, and SHA-256 is a
 * stand-in mixer: what's tested is the flow and the accounting, not the
 * hash or the jitter (Speed Test measures the jitter on a real Mac).
 * The fake timer gives ordinary gaps of 40..42 us, an "interrupt" of
 * +200 us every g_irqEvery readings, and a 0 us reading every 97th, as
 * the SE/30's timer once did. Checks:
 *   1. a poll that sees its interrupts credits 32 bits (4 bytes),
 *   2. it needed that many interrupts' worth of readings, so 0 us gaps
 *      weren't mistaken for interrupts and ordinary gaps weren't counted,
 *   3. with no interrupts at all, a poll ends at the one-second limit
 *      and credits nothing,
 *   4. with no interrupts and a frozen tick count, it ends at 65,536 gaps,
 *   5. consecutive polls give different output (the state advances),
 *   6. a note changes the notes pool the next poll hashes,
 *   7. mac_entropy_raw_gaps() hands back the timer's gaps unchanged,
 *   8. the interrupt test: 1.5 x usual + 4 us,
 *   9. mac_entropy_take_notes() changes after a note.
 *
 * Run: tests/run_tests.sh, which builds it against Claude 68's src/core/net.
 */

#include <stdio.h>
#include <string.h>
#include "Timer.h"
#include "Events.h"
#include "DateTimeUtils.h"
#include "mbedtls/sha256.h"
#include "mbedtls/platform_util.h"
#include "mac_entropy.h"

/* ---- stubs --------------------------------------------------------- */
static unsigned long g_us;            /* fake microsecond clock            */
static unsigned long g_calls;         /* Microseconds() calls so far       */
static unsigned long g_tick;
static unsigned long g_tickEvery;     /* tick moves every N calls; 0 = never */
static unsigned long g_irqEvery;      /* an interrupt every N calls; 0 = none */

void Microseconds(UnsignedWide *w)
{
    g_calls++;
    if (g_calls % 97 != 0)            /* every 97th reading: a 0 us gap */
        g_us += 40 + g_calls % 3;     /* ordinary gaps: 40..42 us       */
    if (g_irqEvery && g_calls % g_irqEvery == 0)
        g_us += 200;                  /* an interrupt ran in between    */
    if (g_tickEvery && g_calls % g_tickEvery == 0)
        g_tick++;
    w->hi = 0;
    w->lo = g_us;
}

unsigned long TickCount(void) { return g_tick; }
void GetDateTime(unsigned long *secs) { *secs = 3900000000UL; }

/* The notes pool is the only 64-byte update; keep the last one seen. */
static unsigned char g_lastNotes[64];

void mbedtls_sha256_init(mbedtls_sha256_context *c) { c->h = 0; }
int mbedtls_sha256_starts(mbedtls_sha256_context *c, int is224)
{
    (void)is224;
    c->h = 1469598103934665603ULL;
    return 0;
}
int mbedtls_sha256_update(mbedtls_sha256_context *c, const unsigned char *in, size_t len)
{
    if (len == sizeof(g_lastNotes))
        memcpy(g_lastNotes, in, len);
    while (len--) { c->h ^= *in++; c->h *= 1099511628211ULL; }
    return 0;
}
int mbedtls_sha256_finish(mbedtls_sha256_context *c, unsigned char out[32])
{
    for (int i = 0; i < 32; i++) {
        c->h ^= (unsigned long long)i;
        c->h *= 1099511628211ULL;
        out[i] = (unsigned char)(c->h >> 56);
    }
    return 0;
}
void mbedtls_sha256_free(mbedtls_sha256_context *c) { c->h = 0; }
void mbedtls_platform_zeroize(void *buf, size_t len) { memset(buf, 0, len); }

/* ---- tests --------------------------------------------------------- */
static int failures;
#define CHECK(cond, msg) \
    do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
         else printf("ok:   %s\n", msg); } while (0)

int main(void)
{
    unsigned char a[128], b[128];
    size_t olen = 0;
    unsigned long before, used;
    const unsigned long full = MAC_ENTROPY_INTERRUPTS_PER_POLL * MAC_ENTROPY_BITS_PER_INTERRUPT / 8;

    /* 1 + 2: interrupts every 50 readings, ticks every 400 (16.6 ms at 41 us) */
    g_irqEvery = 50;
    g_tickEvery = 400;
    before = g_calls;
    memset(a, 0xEE, sizeof(a));
    CHECK(mac_entropy_poll(NULL, a, sizeof(a), &olen) == 0, "poll returns 0");
    CHECK(olen == full, "a poll's interrupts credit 32 bits");
    CHECK(a[full] == 0xEE, "poll writes only the credited bytes");
    used = g_calls - before;
    CHECK(used >= (MAC_ENTROPY_INTERRUPTS_PER_POLL - 1) * 50 &&
          used <= (MAC_ENTROPY_INTERRUPTS_PER_POLL + 1) * 50 + 32,
          "it took that many interrupts' worth of readings (0 us gaps not counted)");

    /* 3: no interrupts: the half-second limit, no credit */
    g_irqEvery = 0;
    before = g_calls;
    CHECK(mac_entropy_poll(NULL, b, sizeof(b), &olen) == 0 && olen == 0,
          "no interrupts: no credit");
    used = g_calls - before;
    CHECK(used >= 59 * 400 && used <= 61 * 400 + 32, "no interrupts: stops at 60 ticks");

    /* 4: no interrupts and a frozen tick count */
    g_tickEvery = 0;
    before = g_calls;
    CHECK(mac_entropy_poll(NULL, b, sizeof(b), &olen) == 0 && olen == 0,
          "frozen tick count: the poll still ends");
    used = g_calls - before;
    CHECK(used >= 65536 && used <= 65536 + 32 + 1, "frozen tick count: stops at 65,536 gaps");

    /* 5: the state advances */
    g_irqEvery = 50;
    g_tickEvery = 400;
    mac_entropy_poll(NULL, a, sizeof(a), &olen);
    mac_entropy_poll(NULL, b, sizeof(b), &olen);
    CHECK(olen == full && memcmp(a, b, full) != 0, "consecutive polls differ");

    /* 6: a note changes the notes pool the next poll hashes */
    {
        unsigned char before6[64];
        mac_entropy_poll(NULL, a, sizeof(a), &olen);
        memcpy(before6, g_lastNotes, sizeof(before6));
        mac_entropy_note(0x12345678UL);
        mac_entropy_poll(NULL, a, sizeof(a), &olen);
        CHECK(memcmp(before6, g_lastNotes, sizeof(before6)) != 0,
              "a note reaches the next poll's hash");
    }

    /* 7: raw gaps are the timer's own gaps */
    {
        unsigned short gaps[100];
        unsigned long c0;
        int okGaps = 1;
        g_irqEvery = 0;
        c0 = g_calls;
        mac_entropy_raw_gaps(gaps, 100);
        for (int i = 0; i < 100; i++)
            if (gaps[i] != 0 && (gaps[i] < 40 || gaps[i] > 42)) okGaps = 0;
        CHECK(okGaps, "raw gaps match the timer (40..42 us, or a 0 us glitch)");
        CHECK(g_calls - c0 == 101, "raw gaps: one reading per gap, plus the first");
    }

    /* 8: the interrupt test */
    CHECK(!mac_entropy_is_interrupt(42, 41) && !mac_entropy_is_interrupt(0, 41) &&
          !mac_entropy_is_interrupt(65, 41) && mac_entropy_is_interrupt(66, 41) &&
          mac_entropy_is_interrupt(241, 41),
          "interrupt test: longer than 1.5 x usual + 4 us");

    /* 9: taking notes */
    {
        unsigned char n1[32], n2[32];
        mac_entropy_take_notes(n1);
        mac_entropy_note(0xCAFEUL);
        mac_entropy_take_notes(n2);
        CHECK(memcmp(n1, n2, 32) != 0, "take_notes changes after a note");
    }

    printf(failures ? "\n%d FAILED\n" : "\nPASS\n", failures);
    return failures != 0;
}
