# Open Transport Bug Patcher

A small Mac app that fixes a TCP bug in Open Transport 1.3 on 68K Macs. Every connection to a server behind Cloudflare loses about 3 seconds, and everything the Mac sends to it crawls out 16 bytes at a time. Notion and Anthropic are both behind Cloudflare, so Notion Classic and Claude 68 are both hit.

## The bug

When the Mac opens a connection, Open Transport 1.3 offers TCP window scaling (RFC 1323, scale 0). The server can then pick its own scale. Cloudflare picks 13, so every window it advertises counts in units of 8 KB, and `16` means 128 KB. Open Transport ignores the server's scale and reads `16` as 16 bytes.

The first thing the Mac sends still goes out whole, because windows aren't scaled during the connection set-up. After that, whenever more than 16 bytes are waiting, Open Transport holds them back to avoid sending tiny pieces, until a timer gives up after about 3 seconds. From then on it sends 16 bytes per round trip.

Google and most other servers pick a small scale, so their windows still look large enough and nothing seems wrong.

## The fix

The app changes 2 bytes in **Open Tpt Inet 68K Library**, so Open Transport stops offering window scaling. Without the offer the server doesn't scale its windows, and they mean what Open Transport thinks they mean.

Measured on a Mac SE/30 (16 MHz 68030, System 7.5.5, Open Transport 1.3, BlueSCSI Wi-Fi), loading a page from Notion with Claude 68's TLS engine:

| | Before | After |
|---|---|---|
| Wait after the Mac's last handshake message | 3.13 s | 0.15 s |
| First page, TLS handshake | 9.1–10.1 s | 6.4–7.0 s |
| Next page (resumed), wait for the answer | 4.30 s | 0.22 s |
| Next page, in all | about 4.7 s | about 0.8 s |

## Using it

1. Download it from the [latest release](https://github.com/SLKK-design/HTTPS-68K-Speedtest/releases/latest), as [StuffIt](https://github.com/SLKK-design/HTTPS-68K-Speedtest/releases/latest/download/OpenTransportBugPatcher.sit) for the Mac itself or as a [zip](https://github.com/SLKK-design/HTTPS-68K-Speedtest/releases/latest/download/OpenTransportBugPatcher.zip) for a modern Mac, and copy it to the Mac. No need to build it yourself; if you want to, see Building below.
2. Open it. It finds Open Tpt Inet 68K Library in the Extensions folder, or asks you to choose it.
3. Click **Patch**, then **Restart**. Open Transport reads the library at startup.

Opening it again on a patched Mac offers **Restore**, which puts the 2 original bytes back.

It changes nothing it doesn't recognise. It looks for Open Transport 1.3's exact instructions around the option, and only patches if they appear exactly once. Any other version or file gets "isn't one this app knows". It's for 68K Macs only: a Power Mac runs Open Transport's TCP from a different library.

## How it was found

The [Speed Test](../README.md) app showed a steady 3-second wait on every page, on both an SE/30 (BlueSCSI) and a Classic II (PiSCSI). The wait only happened with servers behind Cloudflare. Google, Basilisk II, a relay on the local network, and a modern computer talking to Notion directly were all fast.

A packet recording on the PiSCSI's Raspberry Pi showed the cause:
- The Mac's connection request ends with `mss 1460, wscale 0`, and Cloudflare answers with `wscale 13`.
- After that the Mac only ever sends 16 bytes at a time, with one stall of about 3.8 s per connection.

## Technical details

The TCP code is in Open Tpt Inet 68K Library (type `libr`, creator `OMGR`), resource `cd70` 2 "Main", together with ip, udp and arp. The function that builds the connection request writes the MSS option, then this:

```
2012       move.l  (a2),d0
72FE       moveq   #-2,d1          ; SYN_SENT: the Mac opened the connection
B081       cmp.l   d1,d0
670E       beq.s   add_wscale      ; the patch makes this 602A: bra.s past it
202A 006C  move.l  108(a2),d0      ; otherwise only if the other side offered it
720C E2A8 7201 C081
661C       bne.s   skip
7603       moveq   #3,d3           ; add_wscale: kind 3, length 3, our scale, end
```

In Open Transport 1.3 that branch is at offset 0x222C8 in the resource, which is 0x3DB74 in the resource fork. The app finds it by the surrounding instructions, not by the offset, and writes those 2 bytes straight into the fork with the File Manager.

The app contains no Apple code; it only changes your own copy.

## Without the app

- **MacTCP** most likely doesn't offer window scaling, so it shouldn't have this problem. Not checked.
- **A Raspberry Pi or Linux router** between the Mac and the internet can remove the option from the Mac's connection requests. For example, on a PiSCSI that shares its Wi-Fi with the Mac:

      sudo iptables -t mangle -A FORWARD -s 10.10.20.0/24 -p tcp --tcp-flags SYN,RST SYN -j TCPOPTSTRIP --strip-options wscale

## Building

    cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<Retro68-build>/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
    make -C build

The app ends up in `build/` as `Open Transport Bug Patcher.APPL`, with its resource fork.

The patch code (`src/patch.c`) also builds on a modern computer. You can test it against a resource fork saved as a plain file:

    cc -std=c99 -I tools/hoststubs -I src tools/test_patch.c src/patch.c -o test_patch
    cp "Open Tpt Inet 68K Library/..namedfork/rsrc" inet.rsrc
    ./test_patch inet.rsrc original

Files:

```
src/
  main.c         the alerts: find the library, patch or restore, restart
  patch.c        finds and changes the 2 bytes
  patch.h
  resources.r    alerts, version, memory size, icon
tools/
  test_patch.c   runs patch.c on a modern computer
  hoststubs/     the File Manager calls it needs, on stdio
```

Lin van der Slikke, 2026, with Claude Code.
