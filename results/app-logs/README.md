# App logs

A few debug-console logs from Claude 68 and Notion Classic on the real machines, picked because the published numbers come from them. Both apps share the same engine: the two `june-first` logs come from Notion Classic (its page ID is blanked out), the rest from Claude 68. Times are in Toolbox ticks (1/60 s). The handshake is the `SslHandshak ok … (ticks=N)` line. Line endings are converted so the logs read on GitHub.

| File | Machine | What it shows | Handshake |
|---|---|---|---|
| `june-cold-se30.txt` | SE/30 | The cold handshake in the README table | 414 ticks, 6.9 s |
| `june-cold-classicII.txt` | Classic II | The same, on the Classic II | 661 ticks, 11.0 s |
| `june-first-se30.txt` | SE/30 | Notion Classic, June 11: first connection with nothing cached, both certificate signatures checked | 642 ticks, 10.7 s |
| `june-first-classicII.txt` | Classic II | The same, on the Classic II | 780 ticks, 13.0 s |
| `october-first-se30.txt` | SE/30 | First connection with nothing cached: both certificate signatures checked | 503 ticks, 8.4 s |
| `october-first-classicII.txt` | Classic II | The same. The request right after it fails, and the next connection resumes | 821 ticks, 13.7 s |
| `october-cold-resumed-se30.txt` | SE/30 | Two launches: cold with the certificate check cached (`p256-hit`), then resumed | 315 ticks (5.3 s), then 27 (0.45 s) |
| `october-cold-resumed-classicII.txt` | Classic II | The same, on the Classic II | 621 ticks (10.4 s), then 56 (0.93 s) |

The October 2026 [Speed Test runs](../) are one folder up: every method timed on both Macs, the stock baseline against a test server that never hangs up, and the runs before and after the Open Transport fix.

Claude 68's June build skipped one certificate check, which shows as `p256-bypass`. That shortcut is gone. In the October logs the check either runs (`p256-start … p256-ok`) or comes from the disk cache (`p256-hit`).

The October logs come from a temporary debug build. Holding Option at launch switched session resumption off (`TEST resume off`), so a cold handshake could be measured.

Claude's replies in these logs talk about the debug log. In debug mode the log lines share the chat window, and the app sends recent window text along as context.
