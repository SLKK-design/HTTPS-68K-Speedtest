/* tests.cpp: the three runs.

   Crypto: each handshake operation, with each method that was ever in the
   code, on fixed inputs (vectors.h) with the network off. Every result is
   checked, so a fast but wrong method reads WRONG, not as a good time.

   Today's engine: real connections to api.notion.com with the methods
   Claude 68 and Notion Classic run now. From a fresh start, page 1 is the
   very first connection and page 2 the next one. A "page" is one request
   without a token, GET /v1/users/me, which Notion answers with HTTP 401:
   the same TLS work as a real page, with nothing private sent or shown.

   Chart rows: the same two pages for every row of the performance charts,
   each row adding one method to the row above, oldest first. */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
/* The Code Fragment Manager's CloseConnection would clash with
   MacTCPHelper's (as in Claude 68's net_sockets_mac.c) */
#define CloseConnection __cfm_CloseConnection
#include <MacMemory.h>
#include <Gestalt.h>
#include <TextUtils.h>
#include <DateTimeUtils.h>
#include <OSUtils.h>
#include <Events.h>
#include <Windows.h>
#include <LowMem.h>
#include <Devices.h>
#include <Folders.h>
#undef CloseConnection

extern "C" {
#include <MacTCP.h>
#include <mactcp/CvtAddr.h>
#include <mactcp/TCPHi.h>
#include <mactcp/TCPRoutines.h>
#include "http_parser.h"
#include "patch.h"                 /* OpenTransportBugPatcher/: finds the window-scale branch */
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/x509_crt.h"
OSErr tcp_create_stream(unsigned long* stream, unsigned long recvLen,
                        GiveTimePtr giveTime, int* cancel);
int bench_verify_mbedtls_bytes(int nbytes, const uint8_t* pub, const uint8_t* hash,
                               size_t hlen, const uint8_t* sig, int pool);
}
#include "RootCert.h"
#include "methods.h"
#include "vectors.h"
#include "lab_certs.h"
#include "log.h"
#include "mac_entropy.h"
#include <math.h>

/* ------------------------------------------------------------------ */
/* The Mac                                                             */
/* ------------------------------------------------------------------ */

/* Open Transport's version, and whether the window-scale bug is patched: the
   1.3 TCP code offers window scaling and then misreads Cloudflare's
   windows (see OpenTransportBugPatcher/). Checked with that app's own code. */
static void LogNetworkStack(bool hasOT)
{
    long otv = 0;
    short vRefNum;
    long dirID;
    FSSpec spec;
    const char* fix = "Open Transport Bug Patcher: Open Tpt Inet 68K Library not found in Extensions";

    if (!hasOT) {
        LogText("Network: MacTCP\r");
        return;
    }
    if (FindFolder(kOnSystemDisk, kExtensionFolderType, kDontCreateFolder, &vRefNum, &dirID) == noErr &&
        FSMakeFSSpec(vRefNum, dirID, "\pOpen Tpt Inet 68K Library", &spec) == noErr) {
        long at;
        OSErr err;
        short state = FindBranch(&spec, &at, &err);
        fix = state == kPatchedCode  ? "Open Transport Bug Patcher: yes (no TCP window scaling offered)"
            : state == kOriginalCode ? "Open Transport Bug Patcher: no (offers TCP window scaling)"
                                     : "Open Transport Bug Patcher: unknown library";
    }
    Gestalt(gestaltOpenTptVersions, &otv);
    if ((otv >> 16) & 0xF)
        LogF("Network: Open Transport %ld.%ld.%ld, %s\r", (otv >> 24) & 0xFF, (otv >> 20) & 0xF,
             (otv >> 16) & 0xF, fix);
    else
        LogF("Network: Open Transport %ld.%ld, %s\r", (otv >> 24) & 0xFF, (otv >> 20) & 0xF, fix);
}

void LogMachine()
{
    long mach = 0, proc = 0, fpu = 0, sysv = 0, ram = 0, ot = 0;
    Gestalt(gestaltMachineType, &mach);
    Gestalt(gestaltProcessorType, &proc);
    Gestalt(gestaltFPUType, &fpu);
    Gestalt(gestaltSystemVersion, &sysv);
    Gestalt(gestaltPhysicalRAMSize, &ram);
    bool hasOT = Gestalt(gestaltOpenTpt, &ot) == noErr && (ot & 1);

    /* System 7.5.5's machine name list says "Macintosh" for these */
    Str255 name;
    name[0] = 0;
    GetIndString(name, kMachineNameStrID, (short)mach);
    char machine[64];
    if (mach == gestaltMacSE030) {
        strcpy(machine, "Macintosh SE/30");
    } else if (mach == gestaltClassicII) {
        strcpy(machine, "Macintosh Classic II");
    } else if (name[0]) {
        memcpy(machine, name + 1, name[0]);
        machine[name[0]] = '\0';
    } else {
        snprintf(machine, sizeof(machine), "machine type %ld", mach);
    }

    static const char* const cpus[] = { "?", "68000", "68010", "68020", "68030", "68040" };
    static const char* const fpus[] = { "no FPU", "68881", "68882", "68040 FPU" };

    unsigned long secs;
    DateTimeRec dt;
    GetDateTime(&secs);
    SecondsToDate(secs, &dt);

    LogF("\r==== Speed Test, %04d-%02d-%02d %02d:%02d (build %s)\r",
         dt.year, dt.month, dt.day, dt.hour, dt.minute, kBuildStamp);
    LogF("Mac: %s, %s, %s, System %lx.%lx.%lx, %ld MB, free memory %ld KB\r",
         machine, cpus[proc >= 1 && proc <= 5 ? proc : 0], fpus[fpu >= 0 && fpu <= 3 ? fpu : 0],
         (sysv >> 8) & 0xFF, (sysv >> 4) & 0xF, sysv & 0xF,
         ram / (1024L * 1024L), FreeMem() / 1024);
    LogNetworkStack(hasOT);
}

/* ------------------------------------------------------------------ */
/* Crypto, no network                                                  */
/* ------------------------------------------------------------------ */

typedef bool (*OpFn)();

static const UeccVariant* sV;
static int      sX, sPool;
static uint32_t sTabA[288], sTabB[288], sQ2[16];
static uint8_t  sBadHash[32];

static bool OpP256Mbed()  { return bench_verify_mbedtls_bytes(32, kP256Pub, kP256Hash, 32, kP256Sig, sPool) == 0; }
static bool OpP384Mbed()  { return bench_verify_mbedtls_bytes(48, kP384Pub, kP384Hash, 48, kP384Sig, sPool) == 0; }
static bool OpPlain()     { return sV->verify(kP256Pub, kP256Hash, 32, kP256Sig) == 1; }
static bool OpWNAF()      { return sV->verify_wnaf(kP256Hash, 32, kP256Sig, sTabA) == 1; }
static bool OpStraus()    { return sV->verify_straus(kP256Hash, 32, kP256Sig, sTabA, sTabB) == 1; }
static bool OpTableGQ()   { sV->table_GQ(kP256Pub, sTabA); return true; }
static bool OpMidpoint()  { sV->midpoint(kP256Pub, sQ2); return true; }

/* The 2-bit and 3-bit windows (dropped in March for wNAF) */
static int      sWinBits;
static uint32_t sWin[kWin3Words];
static bool OpWinTable()  { bench_win_table(sWinBits, kP256Pub, sWin); return true; }
static bool OpWin()       { return bench_win_verify(sWinBits, kP256Hash, 32, kP256Sig, sWin) == 1; }
static bool OpTableG2Q2() { sV->table_G2Q2(sQ2, sTabB); return true; }

/* The same checks on a hash with one bit flipped: must say no */
static bool BadPlain()  { return sV->verify(kP256Pub, sBadHash, 32, kP256Sig) == 0; }
static bool BadWNAF()   { return sV->verify_wnaf(sBadHash, 32, kP256Sig, sTabA) == 0; }
static bool BadStraus() { return sV->verify_straus(sBadHash, 32, kP256Sig, sTabA, sTabB) == 0; }
static bool BadWin()    { return bench_win_verify(sWinBits, sBadHash, 32, kP256Sig, sWin) == 0; }

static bool OpX25519()
{
    uint8_t out[32];
    int r = sX == kX25519MbedTLS ? x25519_mbedtls(out, kX25519Scalar, kX25519Point, sPool)
                                 : x25519_run(sX, out, kX25519Scalar, kX25519Point);
    return r == 0 && memcmp(out, kX25519Expect, 32) == 0;
}

/* A 32 KB pool, tried and dropped: the apps' 64 KB pool with its first
   half taken before the math starts, so fast_alloc.c stays as the apps
   build it. When the math needs more, the pool hands out heap blocks
   instead (NewPtr); the note says how often. */
extern "C" {
void fast_alloc_begin(void);
void fast_alloc_end(void);
extern unsigned int g_pool_overflow_count;
}
static char sHalfNote[64];

static bool InHalfPool(OpFn op)
{
    fast_alloc_begin();
    unsigned int before = g_pool_overflow_count;
    void* pad = malloc(32 * 1024 - 8);   /* 8: the pool's block header */
    bool ok = pad != NULL && g_pool_overflow_count == before && op();
    unsigned int over = g_pool_overflow_count - before;
    free(pad);
    fast_alloc_end();
    if (over)
        snprintf(sHalfNote, sizeof(sHalfNote), "(more than 32 KB: %u blocks from the heap)", over);
    else
        snprintf(sHalfNote, sizeof(sHalfNote), "(fits in 32 KB)");
    return ok;
}
static bool OpP384Half()   { return InHalfPool(OpP384Mbed); }
static bool OpX25519Half() { return InHalfPool(OpX25519); }

/* "  label ....... " padded to one width so the times line up */
static void LogLabel(const char* label)
{
    char line[64];
    int n = snprintf(line, sizeof(line), "  %s ", label);
    if (n > 60)
        n = 60;
    while (n < 44)
        line[n++] = '.';
    line[n++] = ' ';
    line[n] = '\0';
    LogText(line);
}

static uint32_t Median(uint32_t* t, int n)
{
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && t[j] < t[j - 1]; j--) {
            uint32_t x = t[j]; t[j] = t[j - 1]; t[j - 1] = x;
        }
    return t[n / 2];
}

/* Times op: once, or 3 times when it takes under 5 s (middle time shown).
   bad, when given, must reject a flipped hash (not timed). */
static bool Timed(const char* label, OpFn op, OpFn bad = NULL, const char* note = NULL)
{
    if (StopRequested())
        return false;
    LogLabel(label);

    uint32_t t[3];
    int n = 0;
    bool ok = true;
    do {
        uint32_t t0 = bench_us();
        ok = op() && ok;
        t[n++] = bench_us() - t0;
    } while (ok && n < 3 && t[0] < 5000000UL);
    if (ok && bad && !bad())
        ok = false;

    uint32_t all[3] = { t[0], t[1], t[2] };
    LogF("%9s  %s", FmtSec(Median(t, n)), ok ? "ok" : "WRONG");
    if (n == 3)
        LogF("  (%s, %s, %s)", FmtSec(all[0]), FmtSec(all[1]), FmtSec(all[2]));
    if (note)
        LogF("  %s", note);
    LogText("\r");
    return ok;
}

/* Randomness for the keys. mac_entropy.c hashes the gaps between
   back-to-back Microseconds() readings; this shows how much they vary
   here. A long gap means an interrupt ran in between: those are where
   the unpredictability comes from. The most-common-value estimate is an
   upper bound on the bits per gap. The raw gaps go to the gaps file
   for a proper look on a modern computer. */
static void LogJitter()
{
    enum { kN = 4096, kBins = 2048 };
    static unsigned short gaps[kN];
    static unsigned short count[kBins + 1];   /* last bin: kBins us or more */
    unsigned long lo = 0xFFFFUL, hi = 0, distinct = 0, best = 0, base = 0;
    unsigned long sum = 0, longGaps = 0;

    if (StopRequested())
        return;
    LogText("\rRandomness for the keys (Microseconds() jitter, see mac_entropy.c)\r");

    mac_entropy_raw_gaps(gaps, kN);
    memset(count, 0, sizeof(count));
    for (int i = 0; i < kN; i++) {
        unsigned long g = gaps[i];
        unsigned long b = g < kBins ? g : kBins;
        sum += g;
        if (g < lo) lo = g;
        if (g > hi) hi = g;
        if (count[b]++ == 0)
            distinct++;
        if (count[b] > best) {
            best = count[b];
            base = b;
        }
    }
    for (int i = 0; i < kN; i++)
        if (mac_entropy_is_interrupt(gaps[i], (unsigned short)base))
            longGaps++;

    double bits = log((double)kN / (double)best) / log(2.0);
    unsigned long perSec = sum ? (unsigned long)((double)longGaps * 1000000.0 / (double)sum) : 0UL;
    LogF("  %d gaps in %s: most are %lu us (%lu of them), %lu to %lu us, %lu different\r",
         kN, FmtSec(sum), base, best, lo, hi, distinct);
    LogF("  %lu long gaps (an interrupt in between), about %lu a second\r", longGaps, perSec);
    LogF("  at most %d.%02d bits per gap (most-common-value)\r",
         (int)bits, (int)((bits - (int)bits) * 100.0));
    LogF("  the source credits %d bit%s per interrupt: about %lu bits a second here\r",
         MAC_ENTROPY_BITS_PER_INTERRUPT, MAC_ENTROPY_BITS_PER_INTERRUPT == 1 ? "" : "s",
         perSec * MAC_ENTROPY_BITS_PER_INTERRUPT);

    /* One gather, as a reseed does; seeding the generator gathers twice */
    unsigned char out[32];
    size_t olen = 0, total = 0;
    uint32_t t0 = bench_us();
    while (total < MAC_ENTROPY_THRESHOLD) {
        mac_entropy_poll(NULL, out, sizeof(out), &olen);
        total += olen;
    }
    LogLabel("one gather (seeding takes two)");
    LogF("%9s\r", FmtSec(bench_us() - t0));

    long mach = 0;
    Gestalt(gestaltMachineType, &mach);
    char header[96];
    snprintf(header, sizeof(header),
             "==== gaps, machine type %ld, build %s, %d values in us",
             mach, kBuildStamp, kN);
    OSErr gerr = LogGaps(header, gaps, kN);
    if (gerr == noErr)
        LogF("  the raw gaps were added to \"%s\" next to this app\r", LogGapsFileName());
    else
        LogF("  couldn't write \"%s\" (error %d); the raw gaps went into this file\r",
             LogGapsFileName(), (int)gerr);
}

void RunCryptoTests()
{
    static const UeccVariant* const plain[] = {
        &kUeccCOs, &kUeccCO3, &kUeccUnrolled, &kUeccKaratsuba, &kUeccCombaC, &kUeccComba
    };
    static const UeccVariant* const tabled[] = { &kUeccCO3, &kUeccUnrolled, &kUeccComba };
    char label[64];

    if (bench_rng_init() != 0) {
        LogText("The random number generator didn't start.\r");
        return;
    }
    memcpy(sBadHash, kP256Hash, 32);
    sBadHash[5] ^= 0x10;

    LogText("\rCRYPTO, NO NETWORK\r"
            "One operation per line. Under 5 s it runs 3 times; the middle time is shown.\r");

    LogText("\rP-256 signature check (a first connection has 2: certificate, key exchange)\r");
    sPool = 0;
    Timed("mbedTLS", OpP256Mbed);
    sPool = 1;
    Timed("mbedTLS, 64 KB pool", OpP256Mbed);
    for (unsigned i = 0; i < sizeof(plain) / sizeof(plain[0]); i++) {
        sV = plain[i];
        snprintf(label, sizeof(label), "micro-ecc, %s", sV->name);
        Timed(label, OpPlain, BadPlain, sV == &kUeccKaratsuba ? "(dropped)" : NULL);
    }

    LogText("\rP-256 check with tables for the server's key (built after a connection)\r");
    for (unsigned i = 0; i < sizeof(tabled) / sizeof(tabled[0]); i++) {
        sV = tabled[i];
        OpTableGQ();
        snprintf(label, sizeof(label), "wNAF, %s", sV->name);
        Timed(label, OpWNAF, BadWNAF);
    }
    for (sWinBits = 2; sWinBits <= 3; sWinBits++) {
        OpWinTable();
        snprintf(label, sizeof(label), "%d-bit window, C, -O3", sWinBits);
        Timed(label, OpWin, BadWin, "(dropped)");
    }
    sV = &kUeccComba;
    OpTableGQ();
    OpMidpoint();
    OpTableG2Q2();
    Timed("Straus, Comba + Solinas asm", OpStraus, BadStraus, "(today)");

    LogText("\rBuilding those tables (once per server key, after the page)\r");
    for (unsigned i = 0; i < sizeof(tabled) / sizeof(tabled[0]); i++) {
        sV = tabled[i];
        snprintf(label, sizeof(label), "table (G,Q), %s", sV->name);
        Timed(label, OpTableGQ);
    }
    for (sWinBits = 2; sWinBits <= 3; sWinBits++) {
        snprintf(label, sizeof(label), "table %d-bit window (%d points), C, -O3",
                 sWinBits, sWinBits == 2 ? 16 : 64);
        Timed(label, OpWinTable);
    }
    sV = &kUeccComba;
    Timed("midpoint 2^128 Q, Comba", OpMidpoint, NULL, "(kept on disk by the app)");
    Timed("table (G2,Q2), Comba", OpTableG2Q2);

    LogText("\rP-384 signature check (GTS Root R4 on WE1, until WE1 was pinned)\r");
    sPool = 0;
    Timed("mbedTLS", OpP384Mbed);
    sPool = 1;
    Timed("mbedTLS, 64 KB pool", OpP384Mbed);
    Timed("mbedTLS, 32 KB pool", OpP384Half, NULL, sHalfNote);

    LogText("\rX25519 (a first connection has 1, and 1 more makes the key pair)\r");
    sX = kX25519MbedTLS;
    sPool = 0;
    Timed("mbedTLS", OpX25519);
    sPool = 1;
    Timed("mbedTLS, 64 KB pool", OpX25519);
    Timed("mbedTLS, 32 KB pool", OpX25519Half, NULL, sHalfNote);
    sX = kX25519DonnaC;
    Timed(kX25519Names[sX], OpX25519);
    sX = kX25519DonnaAsm;
    Timed(kX25519Names[sX], OpX25519, NULL, "(dropped)");
    sX = kX25519DonnaFused;
    Timed(kX25519Names[sX], OpX25519, NULL, "(dropped)");
    sX = kX25519Sat;
    Timed(kX25519Names[sX], OpX25519, NULL, "(today)");

    LogJitter();
}

/* ------------------------------------------------------------------ */
/* Live connections                                                    */
/* ------------------------------------------------------------------ */

static const char kHost[] = "api.notion.com";
static const char kRequest[] =
    "GET /v1/users/me HTTP/1.1\r\n"
    "Host: api.notion.com\r\n"
    "User-Agent: SpeedTest/1.0\r\n"
    "Connection: close\r\n\r\n";

/* As Claude 68's HttpClient: ChaCha20 first, no AES-GCM */
static const int kCipherSuites[] = {
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA,
    MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA256,
    MBEDTLS_TLS_RSA_WITH_AES_256_CBC_SHA,
    MBEDTLS_TLS_RSA_WITH_AES_256_CBC_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA,
    0
};

static const mbedtls_ecp_group_id kCurves[] = {
    MBEDTLS_ECP_DP_CURVE25519, MBEDTLS_ECP_DP_SECP256R1, MBEDTLS_ECP_DP_SECP384R1,
    MBEDTLS_ECP_DP_NONE
};

/* Where a connection goes. NULL: api.notion.com, as in every other part */
struct Target {
    const char*   label;
    const char*   host;      /* for the certificate and the Host header */
    unsigned long ip;
    short         port;
    const char*   request;
    bool          verify;    /* Notion's pinned chain; the others are only timed */
    bool          lab;       /* tools/lab_server.py: the test chain instead of Notion's */
};
static const Target* sTarget = NULL;

static bool                sNetReady = false;
static unsigned long       sIP = 0;
static mbedtls_x509_crt    sCAPinned;   /* GTS Root R4 + WE1 */
static mbedtls_x509_crt    sCARoot;     /* GTS Root R4 only  */
static mbedtls_x509_crt    sLabPinned;  /* the test server's anchor + its WE1 */
static mbedtls_x509_crt    sLabRoot;    /* the test server's anchor only */
static mbedtls_ssl_config  sConf;
static mbedtls_ssl_session sTicket;
static bool                sHaveTicket = false;
static bool                sCancel = false;
static int                 sCancelInt = 0;

static void GiveTime()
{
    SystemTask();
}

static void DropTicket()
{
    mbedtls_ssl_session_free(&sTicket);
    mbedtls_ssl_session_init(&sTicket);
    sHaveTicket = false;
}

static bool NetSetup()
{
    if (sNetReady)
        return true;
    if (bench_rng_init() != 0) {
        LogText("The random number generator didn't start.\r");
        return false;
    }

    OSErr err = InitNetwork();
    if (err != noErr) {
        LogF("The network didn't open (InitNetwork %d). Is MacTCP or Open Transport set up?\r", err);
        return false;
    }

    /* MacTCPHelper writes the canonical name back over the buffer (see
       ResolveHost in Claude 68's HttpClient.cpp): give it room */
    char name[1 + 256];
    memset(name, 0, sizeof(name));
    strcpy(name + 1, kHost);
    uint32_t t0 = bench_us();
    err = ConvertStringToAddr(name + 1, &sIP, GiveTime);
    if (err != noErr || sIP == 0) {
        LogF("Couldn't look up %s (error %d).\r", kHost, err);
        return false;
    }
    LogF("%s is %lu.%lu.%lu.%lu (DNS %s)\r", kHost,
         (sIP >> 24) & 0xFF, (sIP >> 16) & 0xFF, (sIP >> 8) & 0xFF, sIP & 0xFF,
         FmtSec(bench_us() - t0));

    mbedtls_x509_crt_init(&sCAPinned);
    mbedtls_x509_crt_init(&sCARoot);
    if (mbedtls_x509_crt_parse_der(&sCAPinned, kGlobalSignRootCA_DER, kGlobalSignRootCA_DER_len) != 0 ||
        mbedtls_x509_crt_parse_der(&sCAPinned, kGoogleWE1_DER, kGoogleWE1_DER_len) != 0 ||
        mbedtls_x509_crt_parse_der(&sCARoot, kGlobalSignRootCA_DER, kGlobalSignRootCA_DER_len) != 0) {
        LogText("The built-in certificates didn't parse.\r");
        return false;
    }
    mbedtls_x509_crt_init(&sLabPinned);
    mbedtls_x509_crt_init(&sLabRoot);
    if (mbedtls_x509_crt_parse_der(&sLabPinned, kLabAnchor_DER, sizeof(kLabAnchor_DER)) != 0 ||
        mbedtls_x509_crt_parse_der(&sLabPinned, kLabWE1_DER, sizeof(kLabWE1_DER)) != 0 ||
        mbedtls_x509_crt_parse_der(&sLabRoot, kLabAnchor_DER, sizeof(kLabAnchor_DER)) != 0) {
        LogText("The built-in certificates didn't parse.\r");
        return false;
    }

    mbedtls_ssl_config_init(&sConf);
    if (mbedtls_ssl_config_defaults(&sConf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        LogText("mbedtls_ssl_config_defaults failed.\r");
        return false;
    }
    mbedtls_ssl_conf_authmode(&sConf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_rng(&sConf, bench_rng, NULL);
    mbedtls_ssl_conf_extended_master_secret(&sConf, MBEDTLS_SSL_EXTENDED_MS_DISABLED);
    mbedtls_ssl_conf_max_tls_version(&sConf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_min_tls_version(&sConf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_curves(&sConf, kCurves);
    mbedtls_ssl_conf_ciphersuites(&sConf, kCipherSuites);
    mbedtls_ssl_session_init(&sTicket);

    sNetReady = true;
    return true;
}

struct Conn {
    bool ok;
    const char* fail;
    int err;
    int status;
    bool offeredTicket;
    bool resumed;
    uint32_t tcp_us, hs_us, http_us;
    HandshakeStats hs;
    const char* suite;
};

struct HttpState {
    int status;
    bool done;
};

static int OnStatus(http_parser* p, const char*, size_t)
{
    ((HttpState*)p->data)->status = p->status_code;
    return 0;
}

static int OnMessageComplete(http_parser* p)
{
    ((HttpState*)p->data)->done = true;
    return 0;
}

/* ------------------------------------------------------------------ */
/* What goes over the network, and when                                */
/* ------------------------------------------------------------------ */

/* mbedTLS hands the network one TLS record at a time, so the last flight
   of a handshake leaves as 2 or 3 small TCP sends, as in the apps. With
   sCombine, the records wait here and go out as one send when mbedTLS
   next reads (the answer can't come before that).

   The apps send through MacTCPHelper's SendData, which always clears the
   TCP push flag, so the Mac's TCP may hold the data back waiting for more.
   With sPush, the same TCPSend goes out with push set. */
static bool          sPush = false;
static bool          sCombine = false;
static bool          sYieldFirst = false;   /* give the system time just before each send */
static unsigned char sOut[2048];
static size_t        sOutLen = 0;

struct NetEvent {
    char     kind;          /* 'S' sent, 'R' read, 'H' handshake done, 'T' TCP counters */
    int      bytes;         /* or the error; for 'T' the bytes not yet acknowledged */
    uint32_t start, end;    /* µs from the start of the handshake */
    bool     stats;         /* 'T': the counters below are there */
    unsigned long sent, resent, rcvd;   /* 'T': data packets so far */
};
static NetEvent sTrace[64];
static int      sTraceN = 0;
static uint32_t sTraceT0 = 0;

static void Trace(char kind, int bytes, uint32_t start, uint32_t end)
{
    if (sTraceN < (int)(sizeof(sTrace) / sizeof(sTrace[0]))) {
        NetEvent& e = sTrace[sTraceN++];
        e.kind = kind;
        e.bytes = bytes;
        e.start = start - sTraceT0;
        e.end = end - sTraceT0;
    }
}

/* What the Mac's TCP says about the connection: TCPStatus, as recv_sys7
   in Claude 68 polls it, plus the per-connection counters it points to */
extern "C" short refNum;   /* MacTCPHelper's handle on the TCP driver */

struct TcpSnap {
    bool ok, stats;
    short state;
    unsigned short unacked, unread;
    unsigned long srtt, lastRTT;
    unsigned long sent, resent, rcvd;
};
static TcpSnap sLastSnap;   /* last one in the trace */
static TcpSnap sEndSnap;    /* at the end of the last connection */

/* A pointer from the driver is checked before it's read: an odd or
   out-of-RAM address would crash a 68030 */
static bool PlausiblePtr(const void* p)
{
    unsigned long a = (unsigned long)StripAddress((Ptr)p);
    return a != 0 && (a & 1) == 0 && a < (unsigned long)StripAddress(LMGetMemTop());
}

static void TcpSnapshot(unsigned long stream, TcpSnap& s)
{
    static TCPiopb pb;
    memset(&pb, 0, sizeof(pb));
    memset(&s, 0, sizeof(s));
    pb.ioCRefNum = refNum;
    pb.ioResult = 1;
    pb.tcpStream = (StreamPtr)stream;
    pb.csCode = TCPStatus;
    PBControl((ParmBlkPtr)&pb, false);
    if (pb.ioResult != noErr)
        return;
    s.ok = true;
    s.state = pb.csParam.status.connectionState;
    s.unacked = pb.csParam.status.amtUnackedData;
    s.unread = pb.csParam.status.amtUnreadData;
    s.srtt = pb.csParam.status.srtt;
    s.lastRTT = pb.csParam.status.lastRTT;
    const TCPConnectionStats* cs = pb.csParam.status.connStatPtr;
    if (PlausiblePtr(cs)) {
        s.stats = true;
        s.sent = cs->dataPktsSent;
        s.resent = cs->dataPktsResent;
        s.rcvd = cs->dataPktsRcvd;
    }
}

/* Adds a 'T' event when anything we watch has changed; *now gets the
   fresh status either way */
static void TraceTcp(unsigned long stream, TcpSnap* now = NULL)
{
    TcpSnap s;
    TcpSnapshot(stream, s);
    if (now)
        *now = s;
    if (!s.ok)
        return;
    if (s.unacked == sLastSnap.unacked && s.sent == sLastSnap.sent &&
        s.resent == sLastSnap.resent && s.rcvd == sLastSnap.rcvd)
        return;
    sLastSnap = s;
    if (sTraceN < (int)(sizeof(sTrace) / sizeof(sTrace[0]))) {
        NetEvent& e = sTrace[sTraceN++];
        e.kind = 'T';
        e.bytes = s.unacked;
        e.start = e.end = bench_us() - sTraceT0;
        e.stats = s.stats;
        e.sent = s.sent;
        e.resent = s.resent;
        e.rcvd = s.rcvd;
    }
}

static void YieldOnce()
{
    EventRecord e;
    SystemTask();
    WaitNextEvent(everyEvent & ~(updateMask | activMask), &e, 1, NULL);
}

/* Waits for data the way recv_sys7 does (TCPStatus, then a yield), noting
   every change in what's unacknowledged, sent, resent and received. The
   read that follows finds the data waiting. */
static void WatchTcp(unsigned long stream)
{
    unsigned long start = TickCount();
    for (;;) {
        TcpSnap s;
        TraceTcp(stream, &s);
        if (!s.ok || s.unread > 0 || s.state != 8 /* established */ ||
            TickCount() - start > 60UL * 60UL)
            return;
        YieldOnce();
    }
}

/* SendData with the push flag set: the same TCPSend, timeout 20 s */
static int PushSend(void* ctx, const unsigned char* buf, size_t len)
{
    mbedtls_net_context* net = (mbedtls_net_context*)ctx;
    wdsEntry wds[2];
    wds[0].length = (unsigned short)len;
    wds[0].ptr = (Ptr)buf;
    wds[1].length = 0;
    wds[1].ptr = NULL;
    OSErr err = LowTCPSendData((StreamPtr)net->fd, 20, true, false, (Ptr)wds, false, NULL,
                               (GiveTimePtr)GiveTime, &sCancel);
    return err == noErr ? (int)len : MBEDTLS_ERR_NET_SEND_FAILED;
}

static int RawSend(void* ctx, const unsigned char* buf, size_t len)
{
    if (sYieldFirst)
        YieldOnce();
    uint32_t t = bench_us();
    int ret = sPush ? PushSend(ctx, buf, len) : mbedtls_net_send(ctx, buf, len);
    Trace('S', ret, t, bench_us());
    TraceTcp((unsigned long)((mbedtls_net_context*)ctx)->fd);
    return ret;
}

static int FlushOut(void* ctx)
{
    size_t off = 0;
    while (off < sOutLen) {
        int ret = RawSend(ctx, sOut + off, sOutLen - off);
        if (ret <= 0) {
            sOutLen = 0;
            return ret < 0 ? ret : MBEDTLS_ERR_NET_SEND_FAILED;
        }
        off += (size_t)ret;
    }
    sOutLen = 0;
    return 0;
}

static int BioSend(void* ctx, const unsigned char* buf, size_t len)
{
    if (!sCombine)
        return RawSend(ctx, buf, len);
    if (sOutLen + len > sizeof(sOut)) {
        int ret = FlushOut(ctx);
        if (ret < 0)
            return ret;
        if (len > sizeof(sOut))
            return RawSend(ctx, buf, len);
    }
    memcpy(sOut + sOutLen, buf, len);
    sOutLen += len;
    return (int)len;
}

static int BioRecv(void* ctx, unsigned char* buf, size_t len)
{
    if (sOutLen > 0) {
        int ret = FlushOut(ctx);
        if (ret < 0)
            return ret;
    }
    uint32_t t = bench_us();
    WatchTcp((unsigned long)((mbedtls_net_context*)ctx)->fd);
    int ret = mbedtls_net_recv(ctx, buf, len);
    Trace('R', ret, t, bench_us());
    return ret;
}

/* The last connection's sends and reads. Reads in a row are one line:
   mbedTLS reads each record's header and body separately, and the crypto
   runs between them, so only the time spent waiting inside a read counts. */
static void LogTimeline()
{
    LogText("          timeline, from the start of the handshake:\r");
    for (int i = 0; i < sTraceN; ) {
        const NetEvent& e = sTrace[i];
        if (e.kind == 'S') {
            uint32_t took = e.end - e.start;
            if (e.bytes < 0)
                LogF("          %9s  send failed, -0x%04X\r", FmtSec(e.start), (unsigned)-e.bytes);
            else if (took >= 50000)
                LogF("          %9s  sent %d bytes (the send took %s)\r", FmtSec(e.start), e.bytes, FmtSec(took));
            else
                LogF("          %9s  sent %d bytes\r", FmtSec(e.start), e.bytes);
            i++;
        } else if (e.kind == 'H') {
            LogF("          %9s  handshake done\r", FmtSec(e.start));
            i++;
        } else if (e.kind == 'T') {
            if (e.stats)
                LogF("          %9s    TCP: %d bytes unacknowledged; data packets sent %lu, resent %lu, received %lu\r",
                     FmtSec(e.start), e.bytes, e.sent, e.resent, e.rcvd);
            else
                LogF("          %9s    TCP: %d bytes unacknowledged\r", FmtSec(e.start), e.bytes);
            i++;
        } else {
            uint32_t first = e.start, firstWait = e.end - e.start, waited = 0;
            int bytes = 0, reads = 0, end = 1;
            for (; i < sTraceN && sTrace[i].kind == 'R'; i++) {
                waited += sTrace[i].end - sTrace[i].start;
                reads++;
                if (sTrace[i].bytes > 0) {
                    bytes += sTrace[i].bytes;
                } else {
                    end = sTrace[i].bytes;
                    i++;
                    break;
                }
            }
            LogF("          %9s  got %d bytes in %d read%s, first after %s, %s waiting in all",
                 FmtSec(first), bytes, reads, reads > 1 ? "s" : "", FmtSec(firstWait), FmtSec(waited));
            if (end == 0)
                LogText(", then the connection closed\r");
            else if (end < 0)
                LogF(", then -0x%04X\r", (unsigned)-end);
            else
                LogText("\r");
        }
    }
}

/* One connection: TCP, handshake with gMethods, one request, close. */
static void OneConnection(Conn& c)
{
    memset(&c, 0, sizeof(c));
    c.fail = "";
    sCancel = false;
    sCancelInt = 0;
    sOutLen = 0;
    sTraceN = 0;
    memset(&sLastSnap, 0, sizeof(sLastSnap));
    memset(&sEndSnap, 0, sizeof(sEndSnap));

    unsigned long stream = 0;
    uint32_t t0 = bench_us();
    OSErr err = tcp_create_stream(&stream, 8192, (GiveTimePtr)GiveTime, &sCancelInt);
    if (err != noErr) {
        c.fail = "TCPCreate";
        c.err = err;
        return;
    }
    err = OpenConnection(stream, (long)(sTarget ? sTarget->ip : sIP), sTarget ? sTarget->port : 443,
                         30, (GiveTimePtr)GiveTime, &sCancel);
    if (err != noErr) {
        ReleaseStream(stream, (GiveTimePtr)GiveTime, &sCancel);
        c.fail = "TCP connect";
        c.err = err;
        return;
    }
    c.tcp_us = bench_us() - t0;

    mbedtls_net_context net;
    mbedtls_net_init(&net, (void*)GiveTime);
    net.fd = (int)stream;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_init(&ssl);

    if (sTarget && sTarget->lab)
        mbedtls_ssl_conf_ca_chain(&sConf, gMethods.pinWE1 ? &sLabPinned : &sLabRoot, NULL);
    else
        mbedtls_ssl_conf_ca_chain(&sConf, gMethods.pinWE1 ? &sCAPinned : &sCARoot, NULL);
    /* The others skip the chain (a P-384 issuer would cost a 68030 15 s);
       the key exchange's signature is still checked */
    mbedtls_ssl_conf_authmode(&sConf, !sTarget || sTarget->verify ? MBEDTLS_SSL_VERIFY_REQUIRED
                                                                  : MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_session_tickets(&sConf, gMethods.tickets ? MBEDTLS_SSL_SESSION_TICKETS_ENABLED
                                                              : MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
    int ret = mbedtls_ssl_setup(&ssl, &sConf);
    if (ret == 0)
        ret = mbedtls_ssl_set_hostname(&ssl, sTarget ? sTarget->host : kHost);
    if (ret != 0) {
        c.fail = "TLS setup";
        c.err = ret;
        goto done;
    }
    mbedtls_ssl_set_bio(&ssl, &net, BioSend, BioRecv, NULL);
    if (gMethods.tickets && sHaveTicket)
        c.offeredTicket = mbedtls_ssl_set_session(&ssl, &sTicket) == 0;

    bench_stats_reset();
    t0 = bench_us();
    sTraceT0 = t0;
    ret = mbedtls_ssl_handshake(&ssl);
    c.hs_us = bench_us() - t0;
    c.hs = gHS;
    if (ret != 0) {
        c.fail = "handshake";
        c.err = ret;
        goto done;
    }
    Trace('H', 0, t0 + c.hs_us, t0 + c.hs_us);
    c.resumed = gHS.verify256_n + gHS.verify256_hits + gHS.verify384_n + gHS.x25519_n == 0;
    c.suite = mbedtls_ssl_get_ciphersuite(&ssl);

    t0 = bench_us();
    {
        const char* request = sTarget ? sTarget->request : kRequest;
        const unsigned char* p = (const unsigned char*)request;
        size_t left = strlen(request);
        while (left > 0) {
            ret = mbedtls_ssl_write(&ssl, p, left);
            if (ret > 0) {
                p += ret;
                left -= (size_t)ret;
            } else if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                c.fail = "sending the request";
                c.err = ret;
                goto done;
            }
        }

        static unsigned char buf[2048];
        http_parser parser;
        http_parser_settings settings;
        HttpState hs = { 0, false };
        memset(&settings, 0, sizeof(settings));
        settings.on_status = OnStatus;
        settings.on_message_complete = OnMessageComplete;
        http_parser_init(&parser, HTTP_RESPONSE);
        parser.data = &hs;
        while (!hs.done) {
            ret = mbedtls_ssl_read(&ssl, buf, sizeof(buf));
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
                continue;
            if (ret <= 0)
                break;
            http_parser_execute(&parser, &settings, (const char*)buf, (size_t)ret);
        }
        c.http_us = bench_us() - t0;
        c.status = hs.status;
        if (hs.status == 0) {
            c.fail = "the response";
            c.err = ret;
            goto done;
        }
    }
    c.ok = true;

    if (gMethods.tickets) {
        DropTicket();
        sHaveTicket = mbedtls_ssl_get_session(&ssl, &sTicket) == 0;
    }

done:
    TcpSnapshot((unsigned long)net.fd, sEndSnap);
    if (!c.ok && c.offeredTicket)
        DropTicket();   /* refused or broken: the app drops it too */
    mbedtls_ssl_free(&ssl);
    net.terminated = 1;   /* no graceful close to wait for: TCPRelease ends it */
    mbedtls_net_free(&net);
}

static const char* ErrName(int err)
{
    switch (err) {
        case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:   return "the server closed the connection";
        case MBEDTLS_ERR_SSL_CONN_EOF:            return "the connection closed";
        case MBEDTLS_ERR_NET_RECV_FAILED:         return "receive failed";
        case MBEDTLS_ERR_NET_SEND_FAILED:         return "send failed";
        case MBEDTLS_ERR_NET_CONN_RESET:          return "connection reset";
        case MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE: return "the server sent an alert";
        case MBEDTLS_ERR_X509_CERT_VERIFY_FAILED: return "certificate check failed";
        default:                                  return "";
    }
}

/* What the handshake spent on crypto, and the rest (network, parsing,
   hashing, the record layer) */
static void LogCrypto(const Conn& c)
{
    const HandshakeStats& h = c.hs;
    uint32_t crypto = h.verify256_us + h.verify384_us + h.x25519_us + h.keygen_us;
    LogText("          crypto:");
    if (h.verify256_n)
        LogF(" P-256 x%d (%s) %s,", h.verify256_n, h.verify_path, FmtSec(h.verify256_us));
    if (h.verify256_hits)
        LogF(" %d check%s remembered,", h.verify256_hits, h.verify256_hits > 1 ? "s" : "");
    if (h.verify384_n)
        LogF(" P-384 %s,", FmtSec(h.verify384_us));
    if (h.keygen_n)
        LogF(" key pair %s,", FmtSec(h.keygen_us));
    if (h.x25519_n)
        LogF(" X25519 %s,", FmtSec(h.x25519_us));
    LogF(" rest %s\r", FmtSec(c.hs_us > crypto ? c.hs_us - crypto : 0));
}

static void LogConn(const char* name, const Conn& c)
{
    if (c.ok) {
        LogF("  %-7s TCP %s, handshake %s%s, HTTP %d in %s\r", name,
             FmtSec(c.tcp_us), FmtSec(c.hs_us), c.resumed ? " (resumed)" : "",
             c.status, FmtSec(c.http_us));
        if (!c.resumed)
            LogCrypto(c);
    } else if (c.hs_us) {
        LogF("  %-7s FAILED: %s after %s, error -0x%04X %s\r", name, c.fail,
             FmtSec(c.hs_us), (unsigned)-c.err, ErrName(c.err));
        LogCrypto(c);
    } else {
        LogF("  %-7s FAILED: %s, error %d\r", name, c.fail, c.err);
    }
    if (c.hs.verify_fail)
        LogF("          %d signature check%s said no\r", c.hs.verify_fail, c.hs.verify_fail > 1 ? "s" : "");
}

/* One page, with the app's retry: a refused ticket gets one full handshake */
static bool Page(const char* name, Conn& c)
{
    OneConnection(c);
    if (!c.ok && c.offeredTicket && !StopRequested()) {
        LogConn(name, c);
        LogText("          ticket refused: full handshake, as the app does\r");
        OneConnection(c);
    }
    LogConn(name, c);
    return c.ok;
}

/* What the app does once a page is done, or has failed (SslClose in
   Claude 68): tables for the server's key, and the next key pair */
static void AfterPage()
{
    uint32_t tables = bench_build_tables();
    uint32_t key = bench_pregen_key(bench_rng, NULL);
    if (tables && key)
        LogF("          after the page: tables %s, next key pair %s\r", FmtSec(tables), FmtSec(key));
    else if (tables)
        LogF("          after the page: tables %s\r", FmtSec(tables));
    else if (key)
        LogF("          after the page: next key pair %s\r", FmtSec(key));
}

static void FreshStart()
{
    bench_clear_state();
    DropTicket();
    uint32_t key = bench_pregen_key(bench_rng, NULL);
    if (key)
        LogF("  key pair made before connecting: %s\r", FmtSec(key));
}

static void LogLiveHeader(const char* title)
{
    LogF("\r%s\r", title);
    LogF("One page = connect to %s, handshake, GET /v1/users/me without a token\r"
         "(answered with HTTP 401), close. Times: TCP connect, TLS handshake, request.\r", kHost);
}

void RunLiveToday()
{
    Conn c;
    LogLiveHeader("LIVE, TODAY'S ENGINE (Claude 68 and Notion Classic, October 2026)");
    if (!NetSetup())
        return;
    bench_methods_today(&gMethods);

    for (int rep = 1; rep <= 3 && !StopRequested(); rep++) {
        LogF("\rFresh start %d of 3: nothing stored\r", rep);
        FreshStart();
        Page("page 1", c);
        AfterPage();
        if (StopRequested())
            break;
        Page("page 2", c);
        AfterPage();
    }

    if (!StopRequested())
        LogText("\rTicket refused or expired: certificate check remembered, tables built\r");
    for (int rep = 1; rep <= 3 && !StopRequested(); rep++) {
        gMethods.tickets = 0;   /* don't offer it, and don't take a new one */
        Page("cold", c);
        gMethods.tickets = 1;
        AfterPage();
    }

    if (!StopRequested())
        LogText("\rResumed with a session ticket\r");
    for (int rep = 1; rep <= 3 && !StopRequested(); rep++) {
        Page("resumed", c);
        AfterPage();
    }
}

/* ------------------------------------------------------------------ */
/* The chart rows                                                      */
/* ------------------------------------------------------------------ */

struct RowResult {
    const char* label;
    bool ran1, ok1, ran2, ok2;
    uint32_t hs1, hs2;
};

static void LogSummaryCell(bool ran, bool ok, uint32_t us)
{
    char cell[24];
    if (!ran)
        snprintf(cell, sizeof(cell), "-");
    else
        snprintf(cell, sizeof(cell), "%s%s", ok ? "" : "FAILED ", FmtSec(us));
    LogF("  %-18s", cell);
}

/* The steps of the charts. Each row adds one to everything above it. */
enum {
    kStepStock, kStepMicroEcc, kStepWE1, kStepPregen, kStepPool, kStepSigCache, kStepDonna,
    kStepWNAF, kStepO3, kStepUnrolled, kStepTickets, kStepComba, kStepStraus, kStepX25519Comba,
    kStepCount
};

/* In the order they were added, March to June 2026 */
static const int kOrderHistory[kStepCount] = {
    kStepStock, kStepMicroEcc, kStepWE1, kStepPregen, kStepPool, kStepSigCache, kStepDonna,
    kStepWNAF, kStepO3, kStepUnrolled, kStepTickets, kStepComba, kStepStraus, kStepX25519Comba
};

/* Grouped by kind, so each group ends with the method in the code today:
   faster P-256 math, faster X25519, less work on every page, and what the
   next page can reuse */
static const int kOrderGrouped[kStepCount] = {
    kStepStock,
    kStepMicroEcc, kStepO3, kStepUnrolled, kStepComba,
    kStepPool, kStepDonna, kStepX25519Comba,
    kStepWE1, kStepPregen,
    kStepSigCache, kStepWNAF, kStepStraus, kStepTickets
};

/* Less work first: skip work on every page and reuse the first page, with
   stock math; then make the math that's left faster. wNAF and Straus aren't
   in it: they need micro-ecc, and once tickets are in, the next page doesn't
   check a signature at all. */
static const int kOrderLessWork[] = {
    kStepStock,
    kStepWE1, kStepPregen,
    kStepSigCache, kStepTickets,
    kStepMicroEcc, kStepO3, kStepUnrolled, kStepComba,
    kStepPool, kStepDonna, kStepX25519Comba
};

/* Adds one step to the methods; returns its label */
static const char* ApplyStep(int step, BenchMethods& m)
{
    switch (step) {
        case kStepStock:       return "Stock mbedTLS";
        case kStepMicroEcc:    m.verify = kVerifyMicroEcc; m.uecc = &kUeccCOs; return "+ micro-ecc for P-256 (C, -Os)";
        case kStepWE1:         m.pinWE1 = 1;               return "+ WE1 pinned: no P-384 check";
        case kStepPregen:      m.pregen = 1;               return "+ key pair made before connecting";
        case kStepPool:        m.pool = 1;                 return "+ 64 KB memory pool";
        case kStepSigCache:    m.sigcache = 1;             return "+ signature checks remembered";
        case kStepDonna:       m.x25519 = kX25519DonnaC;   return "+ X25519 in C (donna)";
        case kStepWNAF:        m.tables = kTablesWNAF;     return "+ wNAF tables";
        case kStepO3:          m.uecc = &kUeccCO3;         return "+ -O3 for the curve code";
        case kStepUnrolled:    m.uecc = &kUeccUnrolled;    return "+ unrolled assembly multiply";
        case kStepTickets:     m.tickets = 1;              return "+ session tickets";
        case kStepComba:       m.uecc = &kUeccComba;       return "+ Comba and Solinas assembly";
        case kStepStraus:      m.tables = kTablesStraus;   return "+ Straus verify";
        case kStepX25519Comba: m.x25519 = kX25519Sat;      return "+ X25519 on Comba (today)";
    }
    return "";
}

/* The rows against api.notion.com (target NULL), or against the test server,
   in one of the orders above */
static void ChartRows(const Target* target, const int* order, int count, const char* name)
{
    static RowResult results[16];
    int nrows = 0;
    BenchMethods m;
    Conn c;

    if (!target) {
        LogLiveHeader("LIVE, EACH ROW OF THE CHARTS (oldest first; each adds one method)");
        LogText("A failed page still shows how long the handshake took and what it spent on crypto.\r");
    } else {
        LogF("\rLIVE, EACH ROW OF THE CHARTS, AGAINST THE TEST SERVER (%s)\r"
             "The test server (tools/lab_server.py, %s port %d) plays api.notion.com: the\r"
             "same cipher, a certificate chain of the same shape, session tickets, an HTTP 401\r"
             "of the same size, but no 15 s limit, so every page can finish.\r",
             name, "192.168.1.248", target->port);
    }
    LogText("Page 2 is skipped when nothing from page 1 could make it different.\r");
    if (!NetSetup())
        return;
    sTarget = target;

    memset(&m, 0, sizeof(m));
    m.verify = kVerifyMbedTLS;
    m.x25519 = kX25519MbedTLS;

    for (int row = 0; row < count && !StopRequested(); row++) {
        const char* label = ApplyStep(order[row], m);
        gMethods = m;

        RowResult& r = results[nrows++];
        memset(&r, 0, sizeof(r));
        r.label = label;
        LogF("\r%d. %s\r", row + 1, label);

        FreshStart();
        r.ran1 = true;
        r.ok1 = Page("page 1", c);
        r.hs1 = c.hs_us;
        AfterPage();
        if (StopRequested())
            break;

        if (!m.sigcache && !m.tickets && m.tables == kTablesNone) {
            LogText("  page 2  skipped: nothing stored, so it would repeat page 1\r");
            continue;
        }
        r.ran2 = true;
        r.ok2 = Page("page 2", c);
        r.hs2 = c.hs_us;
        AfterPage();
    }

    LogText("\rSUMMARY: handshake time per row\r");
    LogF("  %-36s  %-18s  %-18s\r", "", "page 1", "page 2");
    for (int i = 0; i < nrows; i++) {
        LogF("  %2d %-33s", i + 1, results[i].label);
        LogSummaryCell(results[i].ran1, results[i].ok1, results[i].hs1);
        LogSummaryCell(results[i].ran2, results[i].ok2, results[i].hs2);
        LogText("\r");
    }
    sTarget = NULL;
    bench_methods_today(&gMethods);
}

void RunLiveRows()
{
    ChartRows(NULL, kOrderHistory, kStepCount, "oldest first");
}

/* tools/lab_server.py on the computer next to the Macs */
static const unsigned long kLabIP = (192UL << 24) | (168UL << 16) | (1UL << 8) | 248UL;

void RunLiveRowsLab()
{
    static Target lab = { "test server", "speedtest.lab", kLabIP, 4433, kRequest, true, true };
    ChartRows(&lab, kOrderHistory, kStepCount, "oldest first");
}

void RunLiveRowsLabGrouped()
{
    static Target lab = { "test server", "speedtest.lab", kLabIP, 4433, kRequest, true, true };
    ChartRows(&lab, kOrderGrouped, kStepCount, "grouped by kind");
}

void RunLiveRowsLabLessWork()
{
    static Target lab = { "test server", "speedtest.lab", kLabIP, 4433, kRequest, true, true };
    ChartRows(&lab, kOrderLessWork, sizeof(kOrderLessWork) / sizeof(kOrderLessWork[0]), "less work first");
}

/* ------------------------------------------------------------------ */
/* Sends: one per TLS record, or one per flight                        */
/* ------------------------------------------------------------------ */

struct SendResult {
    int n1, n2;                      /* pages that worked */
    uint32_t hs1, req1, hs2, req2;   /* their sums */
};

static void LogSendCells(int n, uint32_t hs, uint32_t req)
{
    if (n)
        LogF("  %-9s  %-9s", FmtSec(hs / n), FmtSec(req / n));
    else
        LogF("  %-9s  %-9s", "failed", "-");
}

/* ------------------------------------------------------------------ */
/* Compare servers: is the 3 s wait Notion's, or the network's?         */
/* ------------------------------------------------------------------ */

static const char kRequestAnthropic[] =
    "GET /v1/models HTTP/1.1\r\n"
    "Host: api.anthropic.com\r\n"
    "User-Agent: SpeedTest/1.0\r\n"
    "Connection: close\r\n\r\n";
static const char kRequestGoogle[] =
    "GET /generate_204 HTTP/1.1\r\n"
    "Host: www.google.com\r\n"
    "User-Agent: SpeedTest/1.0\r\n"
    "Connection: close\r\n\r\n";

static bool LookUp(const char* host, unsigned long* ip)
{
    char name[1 + 256];   /* MacTCPHelper writes the canonical name back */
    memset(name, 0, sizeof(name));
    strcpy(name + 1, host);
    *ip = 0;
    return ConvertStringToAddr(name + 1, ip, GiveTime) == noErr && *ip != 0;
}

/* From the last connection's trace: how long the first read waited after
   the handshake's last send (the full handshake's final flight), and after
   the request */
static void TraceWaits(uint32_t* hsWait, uint32_t* reqWait)
{
    int h = -1;
    *hsWait = *reqWait = 0;
    for (int i = 0; i < sTraceN; i++)
        if (sTrace[i].kind == 'H')
            h = i;
    if (h < 0)
        return;
    int lastSend = -1;
    for (int i = 0; i < h; i++)
        if (sTrace[i].kind == 'S')
            lastSend = i;
    for (int i = lastSend + 1; lastSend >= 0 && i < h; i++)
        if (sTrace[i].kind == 'R') {
            *hsWait = sTrace[i].end - sTrace[i].start;
            break;
        }
    bool sent = false;
    for (int i = h + 1; i < sTraceN; i++) {
        if (sTrace[i].kind == 'S')
            sent = true;
        else if (sent && sTrace[i].kind == 'R') {
            *reqWait = sTrace[i].end - sTrace[i].start;
            break;
        }
    }
}

void RunSendTest()
{
    enum { kNumTargets = 3, kReps = 3 };
    static Target targets[kNumTargets] = {
        { "api.notion.com (Cloudflare)", "api.notion.com", 0, 443, kRequest, true, false },
        { "api.anthropic.com (Cloudflare)", "api.anthropic.com", 0, 443, kRequestAnthropic, false, false },
        { "www.google.com", "www.google.com", 0, 443, kRequestGoogle, false, false },
    };
    static uint32_t hsWait[kNumTargets], req1[kNumTargets], req2[kNumTargets];
    static int n1[kNumTargets], n2[kNumTargets];
    memset(hsWait, 0, sizeof(hsWait));
    memset(req1, 0, sizeof(req1));
    memset(req2, 0, sizeof(req2));
    memset(n1, 0, sizeof(n1));
    memset(n2, 0, sizeof(n2));
    Conn c;

    LogText("\rLIVE, COMPARE SERVERS (today's engine)\r");
    LogText("The same two pages to Notion, to Anthropic (Claude 68's server) and to Google.\r"
            "Anthropic and Google are only timed, without the certificate check. Notion and\r"
            "Anthropic are behind Cloudflare, whose TCP window scale Open Transport 1.3\r"
            "misreads: without Open Transport Bug Patcher they wait about 3 s after the\r"
            "Mac's last handshake message, and about 4 s for a resumed page's answer. Each\r"
            "takes turns, 3 times; the first shows the timeline.\r");
    if (!NetSetup())
        return;
    bench_methods_today(&gMethods);
    for (int t = 0; t < kNumTargets; t++) {
        if (strcmp(targets[t].host, kHost) == 0)
            targets[t].ip = sIP;
        else if (!LookUp(targets[t].host, &targets[t].ip))
            LogF("Couldn't look up %s.\r", targets[t].host);
    }

    for (int rep = 1; rep <= kReps && !StopRequested(); rep++) {
        for (int t = 0; t < kNumTargets && !StopRequested(); t++) {
            uint32_t hw, rw;
            if (!targets[t].ip)
                continue;
            sTarget = &targets[t];
            LogF("\r%s (%d of %d)\r", targets[t].label, rep, kReps);
            FreshStart();
            if (Page("page 1", c)) {
                TraceWaits(&hw, &rw);
                n1[t]++;
                hsWait[t] += hw;
                req1[t] += rw;
            }
            if (rep == 1)
                LogTimeline();
            AfterPage();
            if (StopRequested())
                break;
            if (Page("page 2", c)) {
                TraceWaits(&hw, &rw);
                n2[t]++;
                req2[t] += rw;
            }
            if (rep == 1)
                LogTimeline();
            AfterPage();
        }
    }
    sTarget = NULL;
    mbedtls_ssl_conf_authmode(&sConf, MBEDTLS_SSL_VERIFY_REQUIRED);

    LogText("\rSUMMARY: average wait for the server, pages that worked\r");
    LogText("Page 1: after the Mac's last handshake message, and after the request.\r"
            "Page 2 (resumed): after the request.\r");
    LogF("  %-42s  %-9s  %-9s  %-9s\r", "", "p1 hs", "p1 req", "p2 req");
    for (int t = 0; t < kNumTargets; t++) {
        LogF("  %-42s", targets[t].label);
        if (n1[t])
            LogF("  %-9s  %-9s", FmtSec(hsWait[t] / n1[t]), FmtSec(req1[t] / n1[t]));
        else
            LogF("  %-9s  %-9s", "failed", "-");
        if (n2[t])
            LogF("  %-9s\r", FmtSec(req2[t] / n2[t]));
        else
            LogText("  failed\r");
    }
}
