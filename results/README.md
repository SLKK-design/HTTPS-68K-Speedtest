# Speed Test results

Full runs (File > Run Everything) on two Macs, before and after patching Open Transport with [Open Transport Bug Patcher](../OpenTransportBugPatcher/). The patcher was still called OT Window Fix when these ran, so that's the name in the logs and the file names. They're plain text, as the app wrote them, only converted to UTF-8 with Unix line endings.

| File | Mac | Network | Open Transport |
|---|---|---|---|
| 2026-10-18 SE30 before the fix.txt | Mac SE/30, 16 MHz 68030, 8 MB | BlueSCSI v2 Wi-Fi (DaynaPORT) | 1.3, as shipped |
| 2026-10-18 Classic II before the fix.txt | Mac Classic II, 16 MHz 68030 (16-bit bus), 10 MB | PiSCSI (DaynaPORT), shared over the Pi's Wi-Fi | 1.3, as shipped |
| 2026-10-19 SE30 with OT Window Fix.txt | same SE/30 | same | 1.3, patched |
| 2026-10-19 Classic II with OT Window Fix.txt | same Classic II | same | 1.3, patched |
| 2026-10-19 SE30 test server.txt | same SE/30 | same, to a test server on the local network | 1.3, patched |
| 2026-10-19 Classic II test server.txt | same Classic II | same, to a test server on the local network | 1.3, patched |
| 2026-10-20 SE30 crypto.txt | same SE/30 | none: Crypto Only | 1.3, patched |
| 2026-10-20 Classic II crypto.txt | same Classic II | none: Crypto Only | 1.3, patched |
| 2026-10-20 SE30 test server grouped.txt | same SE/30 | same, to the test server | 1.3, patched |
| 2026-10-20 Classic II test server grouped.txt | same Classic II | same, to the test server | 1.3, patched |
| 2026-10-20 SE30 test server less work.txt | same SE/30 | same, to the test server | 1.3, patched |
| 2026-10-20 Classic II test server less work.txt | same Classic II | same, to the test server | 1.3, patched |

Both Macs run System 7.5.5. "A page" is one request to api.notion.com without a token, which Notion answers with HTTP 401: the same TLS work as a real page, and nothing private is sent.

## Today's engine, before and after

| | SE/30 before | SE/30 after | Classic II before | Classic II after |
|---|---|---|---|---|
| First page, handshake | 9.3–9.5 s | 6.8–7.0 s | 11.7–12.2 s | 8.8–9.2 s |
| of which not crypto | 3.8–4.1 s | 0.9–1.2 s | 4.5 s | 1.6 s |
| Next page, handshake + request | 0.45 + 4.2 s | 0.47 + 0.32 s | 0.9 + 4.3 s | 0.92 + 0.43–0.74 s |
| Ticket refused, check remembered | 6.2–6.45 s | 3.35–3.39 s | 7.9–8.2 s | 5.0–5.8 s |

## The test-server runs

The two "test server" files run the 14 chart rows against `tools/lab_server.py` on a computer next to the Macs (192.168.1.248) instead of api.notion.com.

The server plays Notion's part: the same cipher, a certificate chain of the same shape and session tickets. But it has no 15-second limit, so every page finished, including the old methods that Notion hangs up on. Their full first-page handshakes:

| Row | SE/30 | Classic II |
|---|---|---|
| 1 Stock mbedTLS | 49.0 s | 73.0 s |
| 3 WE1 pinned | 30.1 s | 38.9 s |
| 7 donna | 15.9 s | 20.5 s |
| 9 -O3 | 11.8 s | 16.6 s |
| 11 tickets | 9.4 s | 15.3 s |
| 14 today | 6.8 s | 8.9 s |

The charts per Mac (`../tools/make_charts.py` draws `docs/performance-se30.svg` and `performance-classic-ii.svg`) take their steps from these runs. A page there is its measured crypto plus that Mac's median of everything else (connecting, the rest of the handshake, the request), so a network hiccup in one row doesn't count as a step. The ✕ marks come from the runs against the real api.notion.com.

The SVG charts named below aren't kept in the repo: `tools/make_charts.py` draws them into `docs/` from these files. The finished chart, the one in the [deep-dive](../docs/modern-HTTPS-on-68k.md), is `docs/performanceLesswork.png`.

## The grouped runs

The two "grouped" files (File > Live: Grouped Rows, Test Server) run the same 14 steps against the test server, in a different order. The steps are grouped by kind: faster P-256 math, faster X25519, less work on every page, and what the next page reuses. Each row still adds one step to everything above it, so these are new combinations, measured, not added up. `docs/performance-grouped.svg` is drawn from them.

That chart builds each row from the operations its pages did, each the median of every row that ran it the same way. One X25519 on the SE/30 took 1.45 s instead of 1.13 s in row 12, for example, and would otherwise have hidden what wNAF saves there.

## The less-work-first runs

The two "less work" files (File > Live: Less Work First, Test Server) run 12 of the steps in another order. First comes less work with stock math: WE1 pinned, the key pair made before connecting, checks remembered and session tickets. Then the math that's left gets faster: P-256, then X25519. `docs/performance-lesswork.svg` is drawn from them, the same way as the grouped chart.

wNAF tables and Straus aren't in this order: they need micro-ecc, and once tickets are in, the next page doesn't check a signature at all.

## The crypto-only runs

The two "crypto" files (October 20) come from a build that adds three things to the crypto part:
- the Comba multiply without the Solinas reduction
- the 2-bit and 3-bit windows tried before wNAF, rebuilt because their March code wasn't kept
- a 32 KB pool

The head-to-head chart (`docs/performance-methods.svg`) takes its crypto groups from these runs.

- **The windows** check about as fast as wNAF in C. On the SE/30 they're a little faster; on the Classic II, the 2-bit one is slower. What wNAF won was the table: the 3-bit window's 64 points take 3.2 s to build on the SE/30 and 3.8 s on the Classic II, against wNAF's 0.57 s and 0.75 s.
- **The 32 KB pool** fits one P-384 check or one X25519, at the same speed. It was dropped because a whole handshake ran past it, into a fragmented heap.
- **Single timings vary** by up to 10 % between runs. Donna's fused assembly was a little faster than donna in C on October 19 and a little slower here.

## Reading them

- **The 3-second wait.** Before the fix, every page waits about 3 s after the Mac's last handshake message, and a resumed page waits about 4 s for its answer. Open Transport 1.3 misreads Cloudflare's TCP window scale; the [Open Transport Bug Patcher README](../OpenTransportBugPatcher/README.md) has the details. The runs with the fix also compare Notion, Anthropic and Google directly.
- **The chart rows in the "with the fix" runs.** The old methods also run without the 3-second wait there, so these rows show what each step gives on a fixed Open Transport, not what the apps did at the time. The "before" runs are closer to the apps' history.
- **Failed pages.** api.notion.com closes a handshake that takes longer than about 15 s. A failed page shows the time up to that point, and what it had spent on crypto.
- **Page 2 of rows 6–11 in the "before" runs** includes an extra key pair after a failed page 1, which the apps don't do. The "with the fix" runs don't have this.
- **The micro-ecc C -O3 line in the SE/30 "before" run** (5.4 s) ran only once and is high; the "with the fix" run measures 4.5 s.
- **The Classic II's clock** reads 1956, because its PRAM battery is flat. Only the date is affected; all timing uses the Mac's microsecond timer.
- **The "Mac:" line** in the "before" runs just says "Macintosh", and those runs don't record the Open Transport version. The later build names the model and adds a "Network:" line.

The apps' own debug logs, from Claude 68 and Notion Classic, are in [`app-logs/`](app-logs/).
