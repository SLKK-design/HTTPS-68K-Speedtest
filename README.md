# HTTPS 68K Speedtest

An HTTPS connection test app. It times every TLS handshake method Notion Classic and Claude 68 have used, on the Mac it runs on, so the performance charts can show measured bars instead of calculated ones.

This repo also holds the write-up those numbers feed: **[HTTPS on a 37-Year-Old Mac](docs/modern-HTTPS-on-68k.md)**, how the shared engine gets a 16 MHz 68030 through a modern TLS handshake. The measured runs are in [`results/`](results/).

## Open Transport Bug Patcher

If your 68K Mac runs Open Transport 1.3, every connection it makes to a server behind Cloudflare loses about 3 seconds, Notion and Anthropic included. Open Transport misreads Cloudflare's TCP window scale, so the Mac stalls, then sends everything 16 bytes at a time.

**[Open Transport Bug Patcher](OpenTransportBugPatcher/)** fixes it. The small app changes 2 bytes in Open Tpt Inet 68K Library, and puts them back if you ask. It only touches the exact Open Transport 1.3 code it knows. On an SE/30, the next page from Notion went from about 4.7 s to 0.8 s.

**Download:** [OpenTransportBugPatcher.sit](https://github.com/SLKK-design/HTTPS-68K-Speedtest/releases/latest/download/OpenTransportBugPatcher.sit) (StuffIt, for the Mac itself) or [OpenTransportBugPatcher.zip](https://github.com/SLKK-design/HTTPS-68K-Speedtest/releases/latest/download/OpenTransportBugPatcher.zip) (for a modern Mac, which keeps the resource fork when it unzips). Copy it to the Mac, open it, click **Patch**, then **Restart**.

Speed Test is how the bug was found. The [patcher's README](OpenTransportBugPatcher/README.md) tells the story.

## HTTPS in your own 68k app

The engine Speed Test times is [Claude 68](https://github.com/SLKK-design/Claude68)'s `src/core`, and Claude 68 is built to be the starting point: a small System 7 app with TLS 1.2, a checked certificate chain and the fast curve code, ready to point at another API. That takes three edits:
- `src/config.h`: the host, the endpoint and the headers
- `src/api/`: the request and the response, with a commented `api_template.cpp` to start from
- the project name in `CMakeLists.txt`

If the server isn't behind Google Trust Services, swap both pinned certificates in `RootCert.cpp` too. Speed Test itself compiles the engine straight from a Claude 68 checkout. Claude 68 is 0BSD, like this repo: use it for anything, no credit needed.

## Running it

1. Download [SpeedTest.sit](https://github.com/SLKK-design/HTTPS-68K-Speedtest/releases/latest/download/SpeedTest.sit) or [SpeedTest.zip](https://github.com/SLKK-design/HTTPS-68K-Speedtest/releases/latest/download/SpeedTest.zip) from the [latest release](https://github.com/SLKK-design/HTTPS-68K-Speedtest/releases/latest) and copy the app to the Mac, or build it (see Building below). The test-server runs (parts 5 to 7) need your own build, with your own test keys.
2. The network setup is the same as for Notion Classic: MacTCP or Open Transport, with DNS. On Open Transport 1.3, run [Open Transport Bug Patcher](#open-transport-bug-patcher) first, or the runs to Notion and Anthropic include its 3 s wait.
3. Choose File > Run Everything (Cmd-R) and leave it alone. The oldest methods take minutes per step. Cmd-period stops the run after the current step. File > Live: Compare Servers (Cmd-T) runs only that part, which takes a few minutes.
4. The results show in the window and are added to a SimpleText file next to the app, one per build and kind of Mac: **SP - build - bus**, for example *SP - 10241725 - 16-bit* (built October 24 at 17:25, on a Mac with a 16-bit data bus). The Crypto Only run also writes the raw timer gaps behind the randomness check to *SP - 10241725 - 16-bit gaps*. Each run starts with the date, the Mac, the System version, the memory, the Open Transport version and whether Open Transport Bug Patcher has patched it. The app can't see which network adapter is in use, so note BlueSCSI or PiSCSI yourself.

The File menu can also run each part on its own.

## What it measures

**1. Crypto, no network.** Each handshake operation, done with every method that was ever in the code, on fixed inputs:
- P-256 signature check
- P-256 check with tables
- building those tables
- P-384 check
- X25519

Every result is checked against known answers (`tools/gen_vectors.py`), so a fast but wrong method shows WRONG instead of a good time. Anything under 5 s runs 3 times, and the middle time is shown.

It also times three things that are no longer in the code:
- **the 2-bit and 3-bit windows** tried before wNAF. That code wasn't kept, so `src/variants/uecc_windows.inc` rebuilds them from micro-ecc's own parts; `tests/test_windows.c` checks them on a modern computer first.
- **a 32 KB pool**: the 64 KB pool with its first half taken, and a note when the math needed more
- **the Comba multiply without the Solinas reduction**, to show the two steps apart

**2. Live, today's engine.** Real connections to `api.notion.com`:
- 3 fresh starts: page 1 is the very first connection, page 2 the next one
- 3 cold handshakes with the certificate check remembered, as when a ticket is refused
- 3 resumed handshakes

**3. Live, compare servers.** The same two pages go to Notion, to Anthropic (Claude 68's server) and to Google, taking turns 3 times. Anthropic and Google are only timed, without the certificate check.

Notion and Anthropic are behind Cloudflare, so without [Open Transport Bug Patcher](#open-transport-bug-patcher) the Mac waits about 3 s after its last handshake message, and about 4 s for a resumed page's answer. Google isn't affected. This part was how that bug was tracked down, together with a packet recording on a PiSCSI.

**4. Live, each chart row.** The 14 rows of the SE/30 and Classic II charts, oldest first, each adding one method to the row above. Each row gets a fresh start, then page 1 and page 2. After every page, worked or not, the tables and the next key pair are made, as Claude 68's `SslClose` does. A table at the end lists the handshake times.

**5. Live, each chart row, test server** (File menu, Cmd-L, not in Run Everything). The same 14 rows, against `tools/lab_server.py` on a computer on the Mac's network (the address is `kLabIP` in `tests.cpp`) instead of api.notion.com.

The repo has no keys for the test server. Run `tools/lab/make_certs.sh` once: it makes a new test chain with its own keys and rewrites `src/lab_certs.h`, so build the app after it. Then start the server with `python3 tools/lab_server.py`.

The test server plays Notion's part:
- the same cipher, ECDHE-ECDSA with ChaCha20-Poly1305 over X25519
- a certificate chain of the same shape, from `tools/lab/make_certs.sh`: a self-signed P-384 "R4" that the Mac trusts (as the apps trust GTS Root R4), a P-256 "WE1" and a P-256 leaf. The server also sends an RSA-signed copy of R4, as Notion's does, which the Mac never needs to check.
- session tickets, and an HTTP 401 of the same size

But it has no 15-second limit. So the old rows, which Notion hangs up on, can finish their pages, and their next page can use what the first one stored.

**6. and 7. The same steps in other orders, test server** (Cmd-G and Cmd-F, not in Run Everything):
- **Grouped Rows**: the 14 steps grouped by kind: faster P-256 math, faster X25519, less work on every page, and what the next page reuses
- **Less Work First**: 12 steps, first the ones that skip work, with stock math, then the faster math. wNAF and Straus are left out: they need micro-ecc, and with session tickets the next page checks no signature at all.

A "page" is one request, `GET /v1/users/me` with no token, which Notion answers with HTTP 401. It costs the same TLS work as a real page, and nothing private is sent or shown. Every connection reports the TCP connect, the handshake and the request separately. The handshake is split into its crypto (signature checks, key pair, X25519) and the rest (network round trips, parsing, hashing).

## How it switches methods

- The engine is Claude 68's, compiled straight from a [Claude 68](https://github.com/SLKK-design/Claude68) checkout (`CLAUDE68_DIR` in CMake), so it's the apps' own code.
- `src/bench_alt.c` takes the place of Claude 68's `uecc_alt.c`. It has the same three mbedTLS hooks, but each one checks the current method settings (`gMethods`) first, and times itself.
- `src/variants/` builds Claude 68's `uECC.c` six times, once per set of flags:
  - C at -Os
  - C at -O3
  - the unrolled assembly multiply
  - Karatsuba
  - Comba without Solinas
  - Comba with Solinas, as built today

  `uecc_rename.h` gives each build its own symbol names, and CMake copies the assembly with the same prefixes.
- X25519 comes in five versions: mbedTLS's own, donna in C, donna with the assembly multiply, donna fused, and today's 8 x 32-bit code on the Comba assembly.
- The methods the apps no longer use (`unused_*` in `src/variants/`) live here, not in Claude 68.

## Where it differs from back then

- **mbedTLS** is today's 3.2.1 build, not the 2.x port from March. The "stock mbedTLS" numbers are today's stock math.
- **The server** is Notion's today: WE1 under GTS Root R4, the same chain as in the October measurements.
- **micro-ecc's compiler flags** changed over time:
  - March to May 13: -Ofast, the project-wide setting
  - May 15 to June 5: -Os
  - since June 5: -O3

  The chart rows follow the chart and run -Os until the "-O3 for the curve code" row. The crypto part shows -Os and -O3 side by side.
- **The P-384 check** runs on mbedTLS, as it did in March.
- **The caches** are kept in memory, not on disk, and "fresh start" clears them.
- **Open Transport's window-scale bug** cost every connection to Notion about 3 s until October 2026. Runs with the bug patched don't have that wait, so their rows are faster than the apps were at the time.
- **BlueSCSI vs PiSCSI** is a hardware change, so it isn't a row.
- **Not tested**, because none of these are in the code any more:
  - the 68882 FPU
  - the shared-secret cache
  - the first session-ticket attempt
  - the alignment probe

## Building

You need [Retro68](https://github.com/autc04/Retro68) and a checkout of [Claude 68](https://github.com/SLKK-design/Claude68), whose engine this builds. CMake looks for it next to this repo, in `../Claude68`; point `CLAUDE68_DIR` elsewhere if yours is somewhere else.

    cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<Retro68-build>/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake \
          -DCLAUDE68_DIR=<path to Claude68>
    make -C build

To regenerate the test vectors (needs the `openssl` command): `python3 tools/gen_vectors.py`.

## Tests

`tests/run_tests.sh` builds and runs host tests for Claude 68's engine on a modern computer, under ASan and UBSan: models of the assembly against micro-ecc's C, the X25519 code against RFC 7748, the 4-point signature check, the TLS random seed's accounting and the app's SSE, JSON and UTF-8 parsing. Like the build, it looks for Claude 68 in `../Claude68`, or set `CLAUDE68_DIR`.

`tools/gen_solinas_m68k.py` and `tools/gen_donna_fused.py` generate assembly and the C models the tests check it against. The Solinas reduction goes into Claude 68's `src/core/net/`.

```
docs/
  modern-HTTPS-on-68k.md  the HTTPS deep-dive
OpenTransportBugPatcher/  the Open Transport 1.3 patcher, a separate app with its own README
results/          the runs, with a README
  app-logs/       Claude 68's and Notion Classic's own debug logs
src/
  main.cpp        window, menu, runs
  log.cpp         window text and the results file
  tests.cpp       the runs
  bench_alt.c     the switchable mbedTLS hooks
  methods.h       method settings and timing counters
  vectors.h       test keys and signatures (generated)
  lab_certs.h     the test server's anchor and WE1 (generated)
  variants/       the micro-ecc and X25519 builds, and the methods the apps dropped
tests/            host tests for Claude 68's engine (run_tests.sh)
tools/
  gen_vectors.py  writes src/vectors.h
  lab_server.py   the test server for part 5
  lab/            make_certs.sh: its certificate chain (makes new test keys; they aren't in the repo)
  make_charts.py  draws the SVG charts into docs/ from results/
  gen_*.py        generate the Solinas and fused-donna assembly and their C models
  set_finder_flags.py  turns on the Finder flags after each build, so the app icons show
```

## License

My code is released under [0BSD](LICENSE). `src/variants/unused_curve25519_donna.c` is [curve25519-donna](https://github.com/agl/curve25519-donna) by Adam Langley (BSD 3-clause, notice in the file). The app icons are Notion's "stopwatch" (Speed Test) and "swap-horizontally" (Open Transport Bug Patcher), redrawn as 1-bit pixels. They belong to Notion Labs, and 0BSD doesn't cover them.
