/* test_host.cpp — host-side unit tests for the 68k-independent core:
     · sse.cpp        — SSE line splitter (chunk boundaries, CRLF, overflow)
     · claude_client  — SSEHandleLine escape decode, \uXXXX, surrogate pairs
     · utf8_mac       — UTF-8 ↔ Mac Roman conversion, truncated-tail handling
     · ApiSend        — JSON body building via the stub HttpClient

   Build & run: tests/run_tests.sh (native c++, ASan/UBSan enabled).
   No Mac toolchain needed — these files contain no Toolbox calls.        */

#include "api_client.h"
#include "utf8_mac.h"
#include "HttpClient.h"   /* stub from tests/stubs/ */
#include "config.h"

#include <cstdio>
#include <cstring>
#include <string>

HttpClient gHttp;   /* claude_client.cpp links against this */

static int sPass = 0, sFail = 0;

#define CHECK(cond) do { \
    if (cond) { sPass++; } \
    else { sFail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void ResetSSE()
{
    gSSE.len = 0;
    gSSE.done = false;
    gSSE.text[0] = '\0';
    gSSE.error_text[0] = '\0';
}

/* Build a "data: {...content_block_delta..."text":"<payload>"...}" SSE line. */
static std::string DeltaLine(const char* payload)
{
    std::string s = "data: {\"type\":\"content_block_delta\",\"delta\":{\"type\":"
                    "\"text_delta\",\"text\":\"";
    s += payload;
    s += "\"}}";
    return s;
}

/* ── SSE splitter ────────────────────────────────────────────────── */

static void TestSplitterBasic()
{
    ResetSSE();
    std::string line = DeltaLine("hello") + "\n";
    SSEFeed(line.c_str(), (int)line.size());
    CHECK(gSSE.len == 5);
    CHECK(memcmp(gSSE.text, "hello", 5) == 0);
}

static void TestSplitterChunkBoundary()
{
    /* Line arrives split mid-token across three chunks. */
    ResetSSE();
    std::string line = DeltaLine("ab") + "\n";
    size_t cut1 = 10, cut2 = line.size() - 4;
    SSEFeed(line.c_str(), (int)cut1);
    CHECK(gSSE.len == 0);                          /* incomplete — no handler call */
    SSEFeed(line.c_str() + cut1, (int)(cut2 - cut1));
    SSEFeed(line.c_str() + cut2, (int)(line.size() - cut2));
    CHECK(gSSE.len == 2);
    CHECK(memcmp(gSSE.text, "ab", 2) == 0);
}

static void TestSplitterCRLFAndMultiLine()
{
    ResetSSE();
    std::string two = "event: x\r\n" + DeltaLine("A") + "\r\n\r\n" + DeltaLine("B") + "\n";
    SSEFeed(two.c_str(), (int)two.size());
    CHECK(gSSE.len == 2);                          /* blank lines produce no calls */
    CHECK(memcmp(gSSE.text, "AB", 2) == 0);
}

static void TestSplitterOverflow()
{
    /* A >4 KB line must be dropped whole; the next line must still parse. */
    ResetSSE();
    std::string big = "data: {\"type\":\"content_block_delta\",\"text\":\"";
    big.append(5000, 'x');
    big += "\"}\n";
    SSEFeed(big.c_str(), (int)big.size());
    CHECK(gSSE.len == 0);
    std::string after = DeltaLine("ok") + "\n";
    SSEFeed(after.c_str(), (int)after.size());
    CHECK(gSSE.len == 2);
    CHECK(memcmp(gSSE.text, "ok", 2) == 0);
}

/* ── SSEHandleLine escape decoding ───────────────────────────────── */

static void HandleDelta(const char* payload)
{
    std::string line = DeltaLine(payload);
    SSEHandleLine(line.c_str(), (int)line.size());
}

static void TestEscapesSimple()
{
    ResetSSE();
    HandleDelta("a\\nb\\\"c\\\\d\\re");           /* \n  \"  \\  \r(skipped) */
    CHECK(gSSE.len == 8);
    CHECK(memcmp(gSSE.text, "a\nb\"c\\de", 8) == 0);
}

static void TestEscapeUnicodeBMP()
{
    ResetSSE();
    HandleDelta("caf\\u00e9");                     /* é → C3 A9 */
    CHECK(gSSE.len == 5);
    CHECK((unsigned char)gSSE.text[3] == 0xC3 && (unsigned char)gSSE.text[4] == 0xA9);

    ResetSSE();
    HandleDelta("\\u2026");                        /* … → E2 80 A6 */
    CHECK(gSSE.len == 3);
    CHECK((unsigned char)gSSE.text[0] == 0xE2 &&
          (unsigned char)gSSE.text[1] == 0x80 &&
          (unsigned char)gSSE.text[2] == 0xA6);
}

static void TestEscapeSurrogatePair()
{
    ResetSSE();
    HandleDelta("\\ud83d\\ude00");                 /* 😀 U+1F600 → F0 9F 98 80 */
    CHECK(gSSE.len == 4);
    CHECK((unsigned char)gSSE.text[0] == 0xF0 &&
          (unsigned char)gSSE.text[1] == 0x9F &&
          (unsigned char)gSSE.text[2] == 0x98 &&
          (unsigned char)gSSE.text[3] == 0x80);
}

static void TestEscapeLoneHighSurrogate()
{
    /* High surrogate not followed by \uDCxx — encoded as-is (3-byte), no crash. */
    ResetSSE();
    HandleDelta("\\ud83dX");
    CHECK(gSSE.len == 4);                          /* 3 bytes for U+D83D + 'X' */
    CHECK(gSSE.text[3] == 'X');
}

static void TestEscapeMalformedUnicode()
{
    /* Short hex run — parser skips the 'u' and keeps going. */
    ResetSSE();
    HandleDelta("a\\u12\\\"b");
    CHECK(gSSE.text[0] == 'a');
    CHECK(gSSE.len >= 2);                          /* didn't abort the line */
    CHECK(gSSE.text[gSSE.len - 1] == 'b');
}

static void TestMessageStopAndIgnoredEvents()
{
    ResetSSE();
    const char* stop = "data: {\"type\":\"message_stop\"}";
    SSEHandleLine(stop, (int)strlen(stop));
    CHECK(gSSE.done == true);
    CHECK(gSSE.len == 0);

    ResetSSE();
    const char* other = "data: {\"type\":\"message_start\",\"text\":\"nope\"}";
    SSEHandleLine(other, (int)strlen(other));
    CHECK(gSSE.len == 0);                          /* non-delta events ignored */

    ResetSSE();
    const char* noprefix = "event: content_block_delta";
    SSEHandleLine(noprefix, (int)strlen(noprefix));
    CHECK(gSSE.len == 0);                          /* non-"data: " lines ignored */
}

/* ── utf8_mac ────────────────────────────────────────────────────── */

static void TestMacRomanToUTF8()
{
    char u[4];
    CHECK(MacRomanToUTF8('A', u) == 1 && u[0] == 'A');
    CHECK(MacRomanToUTF8(0xC9, u) == 3);           /* … U+2026 */
    CHECK((unsigned char)u[0] == 0xE2 && (unsigned char)u[1] == 0x80 &&
          (unsigned char)u[2] == 0xA6);
    CHECK(MacRomanToUTF8(0x8E, u) == 2);           /* é U+00E9 → C3 A9 */
    CHECK((unsigned char)u[0] == 0xC3 && (unsigned char)u[1] == 0xA9);
    CHECK(MacRomanToUTF8(0xF0, u) == 0);           /* Apple logo — unmappable */
}

static void TestToMacRoman()
{
    CHECK(ToMacRoman('z') == 'z');
    CHECK(ToMacRoman(0x00E9) == 0x8E);             /* é */
    CHECK(ToMacRoman(0x2026) == 0xC9);             /* … */
    CHECK(ToMacRoman(0x20AC) == 0xDB);             /* € */
    CHECK(ToMacRoman(0x4E00) == 0);                /* CJK — unmappable */
}

static void TestRoundTripAllMacRoman()
{
    /* Every encodable Mac Roman byte must survive MR → UTF-8 → MR. */
    int bad = 0;
    for (int b = 0x80; b <= 0xFF; b++) {
        char u[4];
        int n = MacRomanToUTF8((unsigned char)b, u);
        if (n == 0) continue;                      /* 0xF0 Apple logo */
        unsigned long cp = 0;
        int consumed = ReadUTF8(u, n, &cp);
        if (consumed != n || ToMacRoman(cp) != (unsigned char)b) bad++;
    }
    CHECK(bad == 0);
}

static void TestReadUTF8Tails()
{
    unsigned long cp = 99;
    /* Truncated 2-byte sequence: é first byte only. */
    const char trunc2[] = { (char)0xC3 };
    CHECK(ReadUTF8(trunc2, 1, &cp) == 1 && cp == 0);
    /* Truncated 3-byte sequence: only 2 of 3 bytes present. */
    const char trunc3[] = { (char)0xE2, (char)0x80 };
    CHECK(ReadUTF8(trunc3, 2, &cp) == 1 && cp == 0);
    /* 4-byte (emoji) consumed whole but yields cp=0 (dropped downstream). */
    const char emoji[] = { (char)0xF0, (char)0x9F, (char)0x98, (char)0x80 };
    CHECK(ReadUTF8(emoji, 4, &cp) == 4 && cp == 0);
    /* Bare continuation byte: consumed as 1, cp=0. */
    const char cont[] = { (char)0x80 };
    CHECK(ReadUTF8(cont, 1, &cp) == 1 && cp == 0);
    /* Empty input. */
    CHECK(ReadUTF8(cont, 0, &cp) == 0);
}

/* ── ApiSend body building (via stub HttpClient) ─────────────────── */

static void TestApiSendBody()
{
    ResetSSE();
    gHttp = HttpClient();
    ApiSend("say \"hi\"\rline2", "sk-test", nullptr);
    CHECK(gHttp.postCount == 1);
    CHECK(gHttp.lastEndpoint == API_ENDPOINT);
    CHECK(gHttp.lastAuth == "sk-test");
    CHECK(gHttp.lastBody.find("\"model\":\"" API_MODEL "\"") != std::string::npos);
    CHECK(gHttp.lastBody.find("say \\\"hi\\\"\\nline2") != std::string::npos); /* " escaped, CR→\n */
    CHECK(gHttp.lastBody.find("Conversation context") == std::string::npos);
}

static void TestApiSendWithContext()
{
    gHttp = HttpClient();
    ApiSend("next", "sk-test", "User: hello\rAI: hi");
    CHECK(gHttp.postCount == 1);
    CHECK(gHttp.lastBody.find("Conversation context") != std::string::npos);
    CHECK(gHttp.lastBody.find("User: hello\\nAI: hi") != std::string::npos);
}

static void TestApiSendEmptyAndMacRoman()
{
    gHttp = HttpClient();
    ApiSend("", "sk-test", nullptr);
    ApiSend(nullptr, "sk-test", nullptr);
    CHECK(gHttp.postCount == 0);                   /* empty input — no request */

    char mr[3] = { 'a', (char)0x8E, 0 };           /* Mac Roman é from TE field */
    ApiSend(mr, "sk-test", nullptr);
    CHECK(gHttp.lastBody.find("a\xC3\xA9") != std::string::npos); /* → UTF-8 in JSON */
}

/* ── main ────────────────────────────────────────────────────────── */

int main()
{
    TestSplitterBasic();
    TestSplitterChunkBoundary();
    TestSplitterCRLFAndMultiLine();
    TestSplitterOverflow();

    TestEscapesSimple();
    TestEscapeUnicodeBMP();
    TestEscapeSurrogatePair();
    TestEscapeLoneHighSurrogate();
    TestEscapeMalformedUnicode();
    TestMessageStopAndIgnoredEvents();

    TestMacRomanToUTF8();
    TestToMacRoman();
    TestRoundTripAllMacRoman();
    TestReadUTF8Tails();

    TestApiSendBody();
    TestApiSendWithContext();
    TestApiSendEmptyAndMacRoman();

    printf("%d passed, %d failed\n", sPass, sFail);
    return sFail ? 1 : 0;
}
