"""make_charts.py: draws docs/performance-se30.svg and
performance-classic-ii.svg from the Speed Test results (SpeedTest/results).

One chart per Mac. Each row adds one method to everything above it, and
shows how long a page took: a dot for the first page (nothing stored yet)
and one for the next page (which uses what the first one stored).

The 14 steps come from the runs against the test server (tools/lab_server.py),
which plays api.notion.com without its 15 s limit, so every page finished.
A page is its measured crypto plus that Mac's median of everything else
(TCP connect, the rest of the handshake, the request), so a network hiccup in
one row doesn't show as a step. The red marks come from the runs against the
real api.notion.com: there its server gave up on that page. The last two
rows are the real api.notion.com today, before and after Open Transport Bug Patcher.

Also draws docs/performance-pages.svg (both Macs, a page and the next one per
step) and docs/performance-methods.svg (every method tried for each job,
slowest to fastest, ending with the one in the code today).

    python3 tools/make_charts.py                  all charts
    python3 tools/make_charts.py methods pages    only these
"""
import os
import re
import statistics

HERE = os.path.dirname(os.path.abspath(__file__))
RESULTS = os.path.join(HERE, "..", "results")
OUTDIR = os.path.join(HERE, "..", "docs")

MACS = [
    dict(file="SE30", out="performance-se30.svg", name="Mac SE/30",
         spec="16 MHz 68030, 32-bit bus, BlueSCSI Wi-Fi network"),
    dict(file="Classic II", out="performance-classic-ii.svg", name="Mac Classic II",
         spec="16 MHz 68030, 16-bit bus, PiSCSI network"),
]

# (label, note, in the code today?)
STEPS = [
    ("Stock mbedTLS", "generic big-number math", False),
    ("+ micro-ecc for the P-256 curve math", "", True),
    ("+ WE1 pinned as trust anchor", "no P-384 check", True),
    ("+ Key pair made before connecting", "off the server’s clock", True),
    ("+ 64 KB memory pool for the math", "X25519 stops fighting the Memory Manager", True),
    ("+ Signature checks remembered", "the next page skips the certificate check", True),
    ("+ X25519 in C (curve25519-donna)", "replaced further down", False),
    ("+ Shamir’s trick with wNAF tables", "tables are built after the first page", True),
    ("+ -O3 for the curve code", "it had been -Os since May", True),
    ("+ Unrolled assembly multiply", "replaced further down", False),
    ("+ Session tickets, kept on disk", "the next page resumes", True),
    ("+ Comba multiply and square, Solinas reduction", "custom 68020 assembly", True),
    ("+ Straus verify on half-length scalars", "used when a ticket is refused", True),
    ("+ X25519 on the Comba assembly", "", True),
]
NOTION = [
    ("Before the Open Transport fix", "every page waits 3–4 s for nothing"),
    ("With Open Transport Bug Patcher", "today"),
]
TRIED = ("Tried and dropped: the 68882 FPU (no faster) · a shared-secret cache (could reuse a stale secret) · a 32 KB pool (out of "
         "memory) · session tickets, first try (Cloudflare refused) · 2-bit and 3-bit Shamir tables · Karatsuba · X25519 in assembly "
         "(donna, plain and fused) · an alignment probe (no gain).")


def read(name):
    with open(os.path.join(RESULTS, name), encoding="utf-8") as f:
        return f.read()


def pages_of(block):
    """page 1 and 2 of one row or fresh start: tcp, handshake, resumed,
    request, crypto (seconds), rest of the handshake"""
    pages = {}
    for pg in (1, 2):
        m = re.search(rf"page {pg}  TCP ([\d.]+) s, handshake ([\d.]+) s( \(resumed\))?, HTTP \d+ in ([\d.]+) s"
                      rf"(?:\n\s+crypto: .*?rest ([\d.]+) s)?", block)
        if not m:
            continue
        hs, rest = float(m.group(2)), float(m.group(5)) if m.group(5) else None
        pages[pg] = dict(tcp=float(m.group(1)), hs=hs, resumed=bool(m.group(3)), req=float(m.group(4)),
                         crypto=hs - rest if rest is not None else 0.0, rest=rest)
    return pages


def test_server_rows(text):
    """Page times per step against the test server: (first, next)"""
    blocks = re.split(r"\n(?=\d+\. )", text.split("LIVE, EACH ROW", 1)[1])[1:len(STEPS) + 1]
    rows = []
    for b in blocks:
        p = pages_of(b)
        p.setdefault(2, p[1])          # skipped: nothing stored, the same as page 1
        rows.append(p)
    other = statistics.median(p[pg]["tcp"] + p[pg]["rest"] + p[pg]["req"]
                              for p in rows for pg in (1, 2) if not p[pg]["resumed"])
    resumed = statistics.median(p[2]["tcp"] + p[2]["hs"] + p[2]["req"] for p in rows if p[2]["resumed"])
    page = lambda q: resumed if q["resumed"] else q["crypto"] + other
    return [(page(p[1]), page(p[2])) for p in rows], other, resumed


def notion_refused(text):
    """Per step against the real api.notion.com: did its server give up on
    the first page, the next page?"""
    part = text.split("SUMMARY: handshake time per row", 1)[1]
    out = []
    for m in re.finditer(r"^\s+\d+ .+?\s{2,}(FAILED )?[\d.]+ s\s+(?:(FAILED )?[\d.]+ s|-)", part, re.M):
        first = bool(m.group(1))
        nxt = bool(m.group(2)) if m.group(2) is not None or "-" not in m.group(0)[-20:] else first
        out.append((first, nxt))
    return out[:len(STEPS)]


def today(text):
    """Median whole pages of today's engine against api.notion.com"""
    part = text.split("LIVE, TODAY'S ENGINE", 1)[1].split("Ticket refused", 1)[0]
    starts = [pages_of(b) for b in re.split(r"\nFresh start", part)[1:]]
    return tuple(statistics.median(s[pg]["tcp"] + s[pg]["hs"] + s[pg]["req"] for s in starts) for pg in (1, 2))


def secs(t):
    return f"{t:.0f} s" if t >= 9.95 else (f"{t:.1f} s" if t >= 0.995 else f"{t:.2f} s")


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


# ---------------------------------------------------------------------------

W = 1000
LBL_X = 46
PLOT_X0, PLOT_X1 = 372, 796
MAXS = 75.0
COL1, COL2 = 900, 976           # right edges of the value columns
ROW_H = 36
BLUE, GREEN, RED, GREY = "#4c72b0", "#55a868", "#c44e52", "#c9c9c9"
STYLE = """<style>
text { font-family: Helvetica, Arial, sans-serif; }
.title { font-weight: 700; font-size: 20px; fill: #222; }
.sub   { font-size: 13px; fill: #555; }
.m     { font-size: 13px; fill: #222; }
.mo    { font-size: 11.5px; fill: #8a8a8a; font-style: italic; }
.old   { fill: #8a8a8a; }
.col   { font-size: 11.5px; fill: #555; font-weight: 700; }
.v     { font-size: 12.5px; fill: #222; }
.vx    { font-size: 12.5px; fill: #c44e52; }
.axis  { font-size: 11px; fill: #999; }
.leg   { font-size: 12px; fill: #444; }
.wall  { font-size: 11px; fill: #c44e52; }
.foot  { font-size: 11px; fill: #777; }
</style>"""


def sx(s):
    return PLOT_X0 + s / MAXS * (PLOT_X1 - PLOT_X0)


def wrap(text, width):
    words, lines, line = text.split(), [], ""
    for w_ in words:
        if len(line) + len(w_) + 1 > width:
            lines.append(line)
            line = w_
        else:
            line = (line + " " + w_).strip()
    return lines + [line]


def chart(mac):
    rows, other, resumed = test_server_rows(read(f"2026-10-19 {mac['file']} test server.txt"))
    refused = notion_refused(read(f"2026-10-19 {mac['file']} with OT Window Fix.txt"))
    before = today(read(f"2026-10-18 {mac['file']} before the fix.txt"))
    after = today(read(f"2026-10-19 {mac['file']} with OT Window Fix.txt"))

    b = []
    add = b.append

    def text(x, y, s, cls, anchor=None):
        a = f' text-anchor="{anchor}"' if anchor else ""
        add(f'<text class="{cls}" x="{x}" y="{y}"{a}>{s}</text>')

    add(f'<rect width="{W}" height="HEIGHT" fill="#fff"/>')
    text(24, 34, f"{mac['name']}: loading a Notion page, one method at a time", "title")
    text(24, 54, f"{mac['spec']}. Each row adds one method to everything above it.", "sub")

    # Legend
    ly = 84
    add(f'<circle cx="31" cy="{ly - 4}" r="6" fill="{BLUE}"/>')
    text(44, ly, "first page: nothing stored yet", "leg")
    add(f'<circle cx="246" cy="{ly - 4}" r="6" fill="{GREEN}"/>')
    text(259, ly, "next page: uses what the first one stored", "leg")
    text(537, ly, "&#x2715;", "vx")
    text(552, ly, "Notion’s server gave up after 15 s", "leg")
    ly2 = ly + 22
    add(f'<circle cx="31" cy="{ly2 - 4}" r="4.2" fill="#222"/>')
    text(44, ly2, "in the code today", "leg")
    add(f'<circle cx="166" cy="{ly2 - 4}" r="3.8" fill="#fff" stroke="#999" stroke-width="1.3"/>')
    text(179, ly2, "replaced later", "leg")

    # Column heads and axis
    top = ly2 + 46
    text(COL1, top - 10, "first page", "col", "end")
    text(COL2, top - 10, "next page", "col", "end")
    n_rows = len(STEPS) + len(NOTION)
    plot_bottom = top + n_rows * ROW_H + 26
    for t in (0, 15, 30, 45, 60, 75):
        add(f'<line x1="{sx(t):.1f}" y1="{top}" x2="{sx(t):.1f}" y2="{plot_bottom}" stroke="#ececec"/>')
        text(f"{sx(t):.1f}", top - 10, f"{t} s", "axis", "middle")

    def row(i, y, label, note, today_mark, t1, t2, x1, x2, shade):
        if shade:
            add(f'<rect x="16" y="{y}" width="968" height="{ROW_H}" fill="{shade}"/>')
        if today_mark is not None:
            if today_mark:
                add(f'<circle cx="31" cy="{y + 13}" r="4.2" fill="#222"/>')
            else:
                add(f'<circle cx="31" cy="{y + 13}" r="3.8" fill="#fff" stroke="#999" stroke-width="1.3"/>')
        cls = "m" if today_mark is not False else "m old"
        text(LBL_X, y + 17, esc(label), cls)
        if note:
            text(LBL_X + 14, y + 31, esc(note), "mo")
        cy = y + 18
        lo, hi = sorted((t1, t2))
        if hi - lo > 0.05:
            add(f'<line x1="{sx(lo):.1f}" y1="{cy}" x2="{sx(hi):.1f}" y2="{cy}" stroke="{GREY}" stroke-width="3"/>')
        add(f'<circle cx="{sx(t2):.1f}" cy="{cy}" r="6.2" fill="{GREEN}"/>')
        add(f'<circle cx="{sx(t1):.1f}" cy="{cy}" r="6.2" fill="{BLUE}"/>')
        if abs(t1 - t2) <= 0.05:   # the same page twice: show both colours
            add(f'<path d="M{sx(t1):.1f} {cy - 6.2} A6.2 6.2 0 0 1 {sx(t1):.1f} {cy + 6.2} Z" fill="{GREEN}"/>')
        for col, t, x in ((COL1, t1, x1), (COL2, t2, x2)):
            text(col, y + 22, ("&#x2715; " if x else "") + secs(t), "vx" if x else "v", "end")

    for i, (label, note, in_code) in enumerate(STEPS):
        y = top + i * ROW_H
        t1, t2 = rows[i]
        x1, x2 = refused[i]
        row(i, y, label, note, in_code, t1, t2, x1, x2, "#f6f7f9" if i % 2 == 0 else None)

    ny = top + len(STEPS) * ROW_H + 26
    text(24, ny - 8, "ON THE REAL API.NOTION.COM", "col")
    for j, (label, note) in enumerate(NOTION):
        y = ny + j * ROW_H
        t1, t2 = (before, after)[j]
        row(None, y, label, note, None, t1, t2, False, False, "#fbecec" if j == 0 else "#eaf4ec")

    # The 15 s wall, over the rows
    add(f'<line x1="{sx(15):.1f}" y1="{top}" x2="{sx(15):.1f}" y2="{plot_bottom + 4}" stroke="{RED}" '
        'stroke-width="1.4" stroke-dasharray="4 3"/>')
    text(f"{sx(15) + 5:.1f}", plot_bottom + 16, "Notion’s server gives up on a handshake after 15 s", "wall")

    fy = plot_bottom + 46
    notes = [
        "Measured in October 2026 with the Speed Test app; the data is in SpeedTest/results. Each row starts fresh, loads a first page, then the next",
        "one. The 14 steps ran against a test server that plays api.notion.com (same cipher, same kind of certificate chain) without its 15 s limit,",
        f"so every page finished; ✕ marks what Notion’s own server did. A page there is its measured crypto plus the median of everything else,",
        f"{secs(other)} (connecting, the rest of the handshake, the request), so a network hiccup in one row doesn’t count; a resumed page: {secs(resumed)}.",
        "The last two rows are whole pages from the real api.notion.com, before and after Open Transport Bug Patcher.",
    ]
    # Where a dot and its mark seem to disagree: the line is for the
    # handshake, a dot is the whole page (TCP connect and the request too)
    edge = [i + 1 for i, ((t1, _), (x1, _)) in enumerate(zip(rows, refused)) if (t1 >= 15) != x1]
    if edge:
        which = " and ".join(str(n) for n in edge)
        notes.append(f"Row{'s' if len(edge) > 1 else ''} {which} sit{'' if len(edge) > 1 else 's'} right at the limit: the 15 s line is for "
                     "the handshake, which just made it there, while a dot is the whole page, with connecting and the request.")
    for n, s in enumerate(notes):
        text(24, fy + n * 15, esc(s), "foot")
    for n, s in enumerate(wrap(TRIED, 132)):
        text(24, fy + (len(notes) + 1) * 15 + n * 15, esc(s), "foot")
    height = fy + (len(notes) + 1) * 15 + len(wrap(TRIED, 132)) * 15 + 20

    head = (f'<?xml version="1.0" encoding="UTF-8"?>\n'
            f'<svg xmlns="http://www.w3.org/2000/svg" version="1.1" viewBox="0 0 {W} {height}" width="{W}" height="{height}">\n'
            f"<title>{mac['name']}: loading a Notion page, one method at a time</title>\n"
            f"<desc>How long the first and the next page took on the {mac['name']} as each method was added, measured with "
            f"the Speed Test app in October 2026.</desc>\n<defs>{STYLE}</defs>\n")
    body = "\n".join(b).replace('height="HEIGHT"', f'height="{height}"')
    with open(os.path.join(OUTDIR, mac["out"]), "w", encoding="utf-8") as f:
        f.write(head + body + "\n</svg>\n")
    print(f"wrote {mac['out']} ({height} px high)")
    for (label, _, _), (t1, t2), (x1, x2) in zip(STEPS, rows, refused):
        print(f"  {label:48s} {('x ' if x1 else '  ') + secs(t1):>9s} {('x ' if x2 else '  ') + secs(t2):>9s}")
    print(f"  {'Notion before / after the fix':48s} {secs(before[0]):>9s} {secs(before[1]):>9s} / {secs(after[0])} {secs(after[1])}")



# ---------------------------------------------------------------------------
# Both Macs in one chart: per step, a line for the Classic II and one for the
# SE/30, each the first page followed by the next page, end to end. The whole
# line is how long it takes to open a page and then the next one.
#
# A page is built from two measured parts: the crypto of that step (from the
# test-server run, which has no 15 s limit), and everything else (connecting,
# the rest of the handshake, the request) as the real api.notion.com took it.
# Until October 2026 every step ran with Open Transport's window-scale bug, so
# the steps get everything else as measured before the patch; the last
# row gets it as measured with the fix.

PAGES_OUT = "performance-pages.svg"
PAIR = [  # top line first: the Classic II has the biggest win
    dict(file="Classic II", name="Mac Classic II", first="#f28e2b", next="#a85a17"),
    dict(file="SE30", name="Mac SE/30", first="#f6c142", next="#a8812a"),
]
FIX_ROW = ("With Open Transport Bug Patcher", "today: no TCP window scaling offered")
SERVER_LIMIT = 15.0   # api.notion.com gives up on a handshake after about 15 s
EDGE = 0.5            # a handshake this close to 15 s is "right at the limit"


def step_crypto(text):
    """Per step against the test server: crypto of the first page, of the
    next page, and whether the next page resumed (no crypto)"""
    blocks = re.split(r"\n(?=\d+\. )", text.split("LIVE, EACH ROW", 1)[1])[1:len(STEPS) + 1]
    out = []
    for blk in blocks:
        p = pages_of(blk)
        p.setdefault(2, p[1])
        out.append((p[1]["crypto"], p[2]["crypto"], p[2]["resumed"]))
    return out


def everything_else(text):
    """From today's engine against api.notion.com, medians of the fresh
    starts: a full page's non-crypto time, its handshake's non-crypto part,
    and a resumed page's whole time"""
    part = text.split("LIVE, TODAY'S ENGINE", 1)[1].split("Ticket refused", 1)[0]
    starts = [pages_of(b) for b in re.split(r"\nFresh start", part)[1:]]
    med = lambda f: statistics.median(f(st) for st in starts)
    return dict(page=med(lambda st: st[1]["tcp"] + st[1]["rest"] + st[1]["req"]),
                hs=med(lambda st: st[1]["rest"]),
                resumed=med(lambda st: st[2]["tcp"] + st[2]["hs"] + st[2]["req"]))


def pages_chart():
    macs = []
    for m in PAIR:
        crypto = step_crypto(read(f"2026-10-19 {m['file']} test server.txt"))
        bug = everything_else(read(f"2026-10-18 {m['file']} before the fix.txt"))
        fix = everything_else(read(f"2026-10-19 {m['file']} with OT Window Fix.txt"))
        rows = []
        for c1, c2, resumed in crypto + [crypto[-1]]:
            rows.append(dict(c1=c1, c2=c2, resumed=resumed))
        lines = []
        for i, r in enumerate(rows):
            e = fix if i == len(STEPS) else bug
            t1 = r["c1"] + e["page"]
            t2 = e["resumed"] if r["resumed"] else r["c2"] + e["page"]
            hs1 = r["c1"] + e["hs"]
            hs2 = None if r["resumed"] else r["c2"] + e["hs"]
            state = lambda hs: "" if hs is None or hs < SERVER_LIMIT - EDGE else ("edge" if hs <= SERVER_LIMIT + EDGE else "x")
            lines.append((t1, t2, state(hs1), state(hs2)))
        macs.append(dict(m, lines=lines, bug=bug, fix=fix))

    X0, X1, MAX = 360, 890, 160.0
    px = lambda s: s / MAX * (X1 - X0)
    RH, BAR = 44, 13
    b = []
    add = b.append
    labels = []   # the times go on top of everything, the 15 s line too

    def text(x, y, s, cls, anchor=None, extra=""):
        a = f' text-anchor="{anchor}"' if anchor else ""
        add(f'<text class="{cls}" x="{x}" y="{y}"{a}{extra}>{s}</text>')

    text(24, 34, "Opening a Notion page, then the next one, on a 16 MHz 68030", "title")
    text(24, 54, "Each row adds one method to everything above it. Each line is a first page followed by the next page; "
                 "the whole line is the time for both.", "sub")
    ly = 84
    lx = 24
    for m in macs:
        add(f'<rect x="{lx}" y="{ly - 10}" width="22" height="11" fill="{m["first"]}"/>')
        add(f'<rect x="{lx + 23}" y="{ly - 10}" width="14" height="11" fill="{m["next"]}"/>')
        text(lx + 44, ly, f'{m["name"]}: first page, then the next', "leg")
        lx += 300
    text(lx, ly, "&#x2715;", "vx")
    text(lx + 15, ly, "Notion’s server gave up (after 15 s)", "leg")
    ly2 = ly + 22
    add(f'<circle cx="31" cy="{ly2 - 4}" r="4.2" fill="#222"/>')
    text(44, ly2, "in the code today", "leg")
    add(f'<circle cx="166" cy="{ly2 - 4}" r="3.8" fill="#fff" stroke="#999" stroke-width="1.3"/>')
    text(179, ly2, "replaced later", "leg")
    any_edge = any(st == "edge" for m in macs for ln in m["lines"] for st in ln[2:])
    if any_edge:
        text(lx, ly2, "&#x25D0;", "vx")
        text(lx + 15, ly2, "right at the 15 s limit", "leg")

    top = ly2 + 40
    n = len(STEPS) + 1
    bottom = top + n * RH + 12
    for t in (0, 30, 60, 90, 120, 150):
        add(f'<line x1="{X0 + px(t):.1f}" y1="{top}" x2="{X0 + px(t):.1f}" y2="{bottom}" stroke="#ececec"/>')
        text(f"{X0 + px(t):.1f}", top - 8, f"{t} s", "axis", "middle")

    def mark(state, white=False):
        if not state:
            return ""
        sym = "&#x2715;" if state == "x" else "&#x25D0;"
        return f"{sym} " if white else f'<tspan class="vx">{sym}</tspan> '

    def line(y, m, t1, t2, s1, s2):
        w1, w2 = px(t1), px(t2)
        add(f'<rect x="{X0}" y="{y}" width="{w1:.1f}" height="{BAR}" fill="{m["first"]}"/>')
        add(f'<rect x="{X0 + w1 + 1.5:.1f}" y="{y}" width="{max(w2, 1.5):.1f}" height="{BAR}" fill="{m["next"]}"/>')
        end = X0 + w1 + 1.5 + max(w2, 1.5)
        if end + 120 <= W - 8:
            labels.append(f'<text class="vh" x="{end + 7:.1f}" y="{y + 11}">{mark(s1)}{secs(t1)} + {mark(s2)}{secs(t2)}</text>')
        else:   # long lines: each value inside its own part, the mark in white
            labels.append(f'<text class="vin" x="{X0 + 7}" y="{y + 11}">{mark(s1, True)}{secs(t1)}</text>')
            labels.append(f'<text class="vin" x="{X0 + w1 + 8.5:.1f}" y="{y + 11}">{mark(s2, True)}{secs(t2)}</text>')

    def step(i, y, label, note, in_code, shade, bold=False):
        if shade:
            add(f'<rect x="16" y="{y}" width="968" height="{RH}" fill="{shade}"/>')
        if in_code is not None:
            if in_code:
                add(f'<circle cx="31" cy="{y + 15}" r="4.2" fill="#222"/>')
            else:
                add(f'<circle cx="31" cy="{y + 15}" r="3.8" fill="#fff" stroke="#999" stroke-width="1.3"/>')
        text(LBL_X, y + 19, esc(label), "m" if in_code is not False else "m old",
             extra=' font-weight="700"' if bold else "")
        if note:
            text(LBL_X + 14, y + 34, esc(note), "mo")
        for j, m in enumerate(macs):
            t1, t2, s1, s2 = m["lines"][i]
            line(y + 6 + j * (BAR + 3), m, t1, t2, s1, s2)

    for i, (label, note, in_code) in enumerate(STEPS):
        step(i, top + i * RH, label, note, in_code, "#f6f7f9" if i % 2 == 0 else None)
    step(len(STEPS), top + len(STEPS) * RH + 6, FIX_ROW[0], FIX_ROW[1], True, "#eaf4ec", bold=True)

    add(f'<line x1="{X0 + px(15):.1f}" y1="{top}" x2="{X0 + px(15):.1f}" y2="{bottom + 4}" stroke="{RED}" '
        'stroke-width="1.4" stroke-dasharray="4 3"/>')
    text(f"{X0 + px(15) + 5:.1f}", bottom + 16, "15 s: Notion’s server gives up on a page that takes longer", "wall")

    fy = bottom + 46
    cii, se = macs
    wait1 = [m["bug"]["page"] - m["fix"]["page"] for m in macs]
    wait2 = [m["bug"]["resumed"] - m["fix"]["resumed"] for m in macs]
    notes = [
        "Measured in October 2026 with the Speed Test app; the data is in SpeedTest/results. Each row starts fresh, opens a first page, then the",
        "next one. Each page is the crypto of that step, measured against a test server that plays api.notion.com without its 15 s limit, plus",
        "everything else (connecting, the rest of the handshake, the request) as the real api.notion.com took it. Until patched, Open Transport",
        f"misread Cloudflare’s TCP window and every page waited for nothing: {wait1[0]:.1f} s and {wait1[1]:.1f} s on a first page, "
        f"{wait2[0]:.1f} s and {wait2[1]:.1f} s on a resumed one (Classic II, SE/30).",
        "Every step includes that wait, as the apps had it; the last row doesn’t. ✕: the handshake took longer than 15 s, so Notion’s server gave up.",
    ]
    if any_edge:
        notes.append("◐: the handshake took within half a second of 15 s, so Notion’s server sometimes let it through.")
    for k, s_ in enumerate(notes):
        text(24, fy + k * 15, esc(s_), "foot")
    tried = wrap(TRIED, 132)
    for k, s_ in enumerate(tried):
        text(24, fy + (len(notes) + 1) * 15 + k * 15, esc(s_), "foot")
    height = fy + (len(notes) + 1 + len(tried)) * 15 + 20

    style = STYLE.replace("</style>", ".vin { font-size: 11.5px; fill: #fff; font-weight: 700; }\n"
                          ".vh { font-size: 12.5px; fill: #222; paint-order: stroke; stroke: #fff; stroke-width: 3.5px; "
                          "stroke-linejoin: round; }\n</style>")
    head = (f'<?xml version="1.0" encoding="UTF-8"?>\n'
            f'<svg xmlns="http://www.w3.org/2000/svg" version="1.1" viewBox="0 0 {W} {height}" width="{W}" height="{height}">\n'
            "<title>Opening a Notion page, then the next one, on a 16 MHz 68030</title>\n"
            "<desc>How long the first and the next page took on a Mac Classic II and a Mac SE/30 as each method was added, "
            "measured with the Speed Test app in October 2026.</desc>\n"
            f'<defs>{style}</defs>\n<rect width="{W}" height="{height}" fill="#fff"/>\n')
    with open(os.path.join(OUTDIR, PAGES_OUT), "w", encoding="utf-8") as f:
        f.write(head + "\n".join(b + labels) + "\n</svg>\n")
    print(f"wrote {PAGES_OUT} ({height} px high)")
    for m in macs:
        print(f"  {m['name']}: bug adds {m['bug']['page'] - m['fix']['page']:.2f} s first, "
              f"{m['bug']['resumed'] - m['fix']['resumed']:.2f} s resumed")
        for (label, _, _), ln in zip(STEPS + [FIX_ROW + (True,)], m["lines"]):
            print(f"    {label:48s} {ln[2] or '':4s}{secs(ln[0]):>7s} + {ln[3] or '':4s}{secs(ln[1])}")



# ---------------------------------------------------------------------------
# Every method tried for each job, in a System 7 window. Per group, the main
# line only gets faster and ends with the winner, the one in the code today.
# A branch (└) was tried after the step above it and wasn't faster; a branch
# without bars is no longer in the code, so the Speed Test can't time it.

METHODS_OUT = "performance-methods.svg"

# (kind, label, where the time comes from, note)
#   kind: main | win | branch | gone (a branch without a time)
#   time: ("crypto", section, method) | ("live", key) | None
METHOD_GROUPS = [
    ("P-256 signature check", "A first connection does two: the certificate's and the key exchange's.", [
        ("main", "Stock mbedTLS, generic big-number math", ("crypto", "P-256 signature check", "mbedTLS"), ""),
        ("main", "micro-ecc in C, -Os", ("crypto", "P-256 signature check", "micro-ecc, C, -Os"), ""),
        ("main", "micro-ecc in C, -O3", ("crypto", "P-256 signature check", "micro-ecc, C, -O3"), ""),
        ("branch", "Karatsuba multiply", ("crypto", "P-256 signature check", "micro-ecc, Karatsuba multiply"),
         "slower than the plain multiply"),
        ("main", "Unrolled assembly multiply", ("crypto", "P-256 signature check", "micro-ecc, unrolled asm multiply"), ""),
        ("main", "Comba multiply, C reduction", ("crypto", "P-256 signature check", "micro-ecc, Comba asm, C reduction"), ""),
        ("win", "Comba + Solinas reduction, asm", ("crypto", "P-256 signature check", "micro-ecc, Comba + Solinas asm"), ""),
    ]),
    ("P-256 check, server key seen before", "Tables for the server's key are built after the first page.", [
        ("main", "Stock mbedTLS, no tables", ("crypto", "P-256 signature check", "mbedTLS"), ""),
        ("main", "wNAF tables, C -O3", ("crypto", "P-256 check with tables", "wNAF, C, -O3"), ""),
        ("branch", "2-bit window, C -O3", ("crypto", "P-256 check with tables", "2-bit window, C, -O3"),
         "beaten by the 3-bit window"),
        ("branch", "3-bit window, C -O3", ("crypto", "P-256 check with tables", "3-bit window, C, -O3"),
         "its table takes 5x longer to build"),
        ("main", "wNAF, unrolled assembly", ("crypto", "P-256 check with tables", "wNAF, unrolled asm multiply"), ""),
        ("main", "wNAF, Comba assembly", ("crypto", "P-256 check with tables", "wNAF, Comba + Solinas asm"), ""),
        ("win", "Straus, half-length scalars", ("crypto", "P-256 check with tables", "Straus, Comba + Solinas asm"), ""),
    ]),
    ("X25519 key exchange", "A first connection does one; one more makes the next key pair.", [
        ("main", "Stock mbedTLS", ("crypto", "X25519", "mbedTLS"), ""),
        ("main", "mbedTLS + 64 KB memory pool", ("crypto", "X25519", "mbedTLS, 64 KB pool"), ""),
        ("branch", "32 KB memory pool", ("crypto", "X25519", "mbedTLS, 32 KB pool"),
         "fits one operation; a whole handshake ran out"),
        ("main", "curve25519-donna in C", ("crypto", "X25519", "donna in C"), ""),
        ("branch", "donna, assembly multiply", ("crypto", "X25519", "donna, assembly multiply"), "no gain"),
        ("branch", "donna, fused assembly", ("crypto", "X25519", "donna, fused assembly"), "no gain"),
        ("gone", "Shared-secret cache", None, "could reuse a stale secret"),
        ("win", "8 x 32-bit on the Comba asm", ("crypto", "X25519", "8 x 32-bit on Comba assembly"), ""),
    ]),
    ("P-384 signature check", "GTS Root R4's signature on WE1, on every first connection.", [
        ("main", "Stock mbedTLS", ("crypto", "P-384 signature check", "mbedTLS"), ""),
        ("main", "mbedTLS + 64 KB memory pool", ("crypto", "P-384 signature check", "mbedTLS, 64 KB pool"), ""),
        ("win", "WE1 pinned as trust anchor", ("zero",), "not checked at all"),
    ]),
    ("A page, then the next one", "Whole pages on the real api.notion.com. The next page can use what the first one stored.", [
        ("main", "Stock mbedTLS", ("pair", "stock1", "stock2"), ""),
        ("main", "Faster crypto, key pair made during it", ("pair", "during", "during"), ""),
        ("main", "Key pair made before connecting", ("pair", "first", "first"), ""),
        ("main", "Certificate check remembered, Straus", ("pair", "first", "remembered"), ""),
        ("gone", "Session tickets, first try", None, "Cloudflare refused them"),
        ("main", "Session ticket, kept on disk", ("pair", "first", "resumed"), ""),
        ("win", "Open Transport Bug Patcher", ("pair", "patched1", "patched2"), ""),
    ]),
]
METHODS_ALSO = "Also tried, no gain: the 68882 FPU, an alignment probe."


def crypto_ops(text):
    """CRYPTO, NO NETWORK: {(section heading, method): seconds}"""
    part = text.split("CRYPTO, NO NETWORK", 1)[1].split("\nLIVE", 1)[0]
    ops, section = {}, ""
    for ln in part.splitlines():
        if ln and not ln.startswith(" "):
            section = ln
            continue
        m = re.match(r"\s+(.+?) \.{2,}\s+([\d.]+) s\s+ok", ln)
        if m:
            ops[(section, m.group(1))] = float(m.group(2))
    return ops


def live_pages(text):
    """LIVE, TODAY'S ENGINE: medians of whole pages (TCP connect, handshake
    and request): the first page, the key pair made before it, and a page
    with the certificate check remembered but no ticket"""
    part = text.split("LIVE, TODAY'S ENGINE", 1)[1].split("\nLIVE", 1)[0]
    fresh, cold = part.split("Ticket refused", 1)
    cold = cold.split("Resumed with", 1)[0]
    med = statistics.median
    whole = lambda pat, t: med(float(a) + float(b) + float(c) for a, b, c in re.findall(
        pat + r" +TCP ([\d.]+) s, handshake ([\d.]+) s(?: \(resumed\))?, HTTP \d+ in ([\d.]+) s", t))
    return dict(first=whole("page 1", fresh), remembered=whole("cold", cold),
                keypair=med(float(x) for x in re.findall(r"key pair made before connecting: ([\d.]+) s", fresh)))


def methods_chart():
    macs = []
    for m in PAIR:
        fixed = read(f"2026-10-19 {m['file']} with OT Window Fix.txt")
        before = read(f"2026-10-18 {m['file']} before the fix.txt")
        # Stock pages: the test server's crypto plus the network time of the
        # real api.notion.com, as the pages chart builds them. Every page
        # before the patch has Open Transport's 3-4 s wait in it.
        crypto = step_crypto(read(f"2026-10-19 {m['file']} test server.txt"))
        bug, fix = everything_else(before), everything_else(fixed)
        lb, lf = live_pages(before), live_pages(fixed)
        pages = dict(stock1=crypto[0][0] + bug["page"], stock2=crypto[0][1] + bug["page"],
                     during=lb["first"] + lb["keypair"], first=lb["first"], patched1=lf["first"],
                     remembered=lb["remembered"], resumed=bug["resumed"], patched2=fix["resumed"])
        macs.append(dict(m, ops=crypto_ops(read(f"2026-10-20 {m['file']} crypto.txt")), pages=pages))

    def value(mac, src):
        """seconds, or (first page, next page) for a pair"""
        if src is None:
            return None
        if src[0] == "zero":
            return 0.0
        if src[0] == "pair":
            return (mac["pages"][src[1]], mac["pages"][src[2]])
        hits = [v for (sec, meth), v in mac["ops"].items() if sec.startswith(src[1]) and meth == src[2]]
        assert len(hits) == 1, src
        return hits[0]

    total = lambda v: sum(v) if isinstance(v, tuple) else v

    # Check the rule the chart promises: the main line only gets faster
    # (a pair by both pages together)
    for title, _, rows in METHOD_GROUPS:
        for mac in macs:
            last = None
            for kind, label, src, _ in rows:
                v = value(mac, src)
                if v is None or kind not in ("main", "win"):
                    continue
                assert last is None or total(v) < last, (mac["name"], title, label)
                last = total(v)

    FONT = "Monaco, Menlo, Consolas, 'DejaVu Sans Mono', 'Courier New', monospace"
    WX0, WX1 = 16, 976                 # the window
    GX0, GX1 = 36, 956                 # group boxes
    BX, MAXV = 448, 30.0               # bars: x of 0 s, the scale's end
    BW = 380                           # bar length at MAXV
    ROW = 42
    BH = (20, 10)                      # bar height: Classic II, SE/30
    BY = (4, 27)                       # bar top within a row
    bx = lambda v: v / MAXV * BW
    fmt = lambda v: "0 s" if v == 0 else (f"{v:.1f} s" if v >= 0.995 else f"{v:.2f} s")
    SOLID = 'fill="#000"'
    DOTTED = 'fill="url(#dots)" stroke="#000" stroke-width="1"'
    DASHED = 'fill="#fff" stroke="#000" stroke-width="1" stroke-dasharray="2 2"'

    b = []
    add = b.append

    def text(x, y, s, cls, anchor=None, extra=""):
        a = f' text-anchor="{anchor}"' if anchor else ""
        add(f'<text class="{cls}" x="{x}" y="{y}"{a}{extra}>{s}</text>')

    def brk(x, y, h):   # a break in a bar that's longer than the scale
        add(f'<path d="M{x:.1f} {y + h + 2} L{x + 6:.1f} {y - 2} M{x + 5:.1f} {y + h + 2} L{x + 11:.1f} {y - 2}" '
            'stroke="#fff" stroke-width="2.4"/>')

    # Heading and legend, under the title bar
    y = 74
    text(GX0, y, "Every method tried for each job, slowest to fastest", "h")
    text(GX0, y + 20, "Each group ends with the winner (&#x2605;), the one in the code today.", "s")
    text(GX0, y + 36, "A &#x2514; branch was tried after the step above it and dropped; its note says why.", "s")
    ly = y + 68
    add(f'<rect x="{GX0}" y="{ly - 15}" width="30" height="{BH[0]}" {SOLID}/>')
    text(GX0 + 38, ly, "Mac Classic II", "s")
    add(f'<rect x="{GX0 + 180}" y="{ly - 10}" width="30" height="{BH[1]}" {SOLID}/>')
    text(GX0 + 218, ly, "Mac SE/30", "s")
    add(f'<rect x="{GX0 + 340}" y="{ly - 10}" width="30" height="{BH[1]}" {DOTTED}/>')
    text(GX0 + 378, ly, "the next page", "s")
    add(f'<rect x="{GX0 + 510}" y="{ly - 10}" width="30" height="{BH[1]}" {DASHED}/>')
    text(GX0 + 548, ly, "tried, dropped", "s")
    add(f'<rect x="{GX0 + 718}" y="{ly - 14}" width="84" height="18" fill="#000"/>')
    text(GX0 + 724, ly, "&#x2605; winner", "win")
    y = ly + 34

    limits = []
    for title, note, rows in METHOD_GROUPS:
        top = y
        text(GX0 + 16, top + 30, esc(note), "n")
        y += 50
        # scale, once per group
        for t in range(0, int(MAXV) + 1, 5):
            gx = BX + bx(t)
            add(f'<line x1="{gx:.1f}" y1="{y}" x2="{gx:.1f}" y2="{y + len(rows) * ROW}" stroke="#000" '
                'stroke-width="0.6" stroke-dasharray="1 3"/>')
            text(f"{gx:.1f}", y - 6, f"{t} s" if t == MAXV else str(t), "ax", "middle")
        page_group = any(r[2] and r[2][0] == "pair" for r in rows)
        if page_group:
            lim = BX + bx(15)
            add(f'<line x1="{lim:.1f}" y1="{y}" x2="{lim:.1f}" y2="{y + len(rows) * ROW + 4}" stroke="#000" '
                'stroke-width="1.4" stroke-dasharray="5 3"/>')
            limits.append((lim, y + len(rows) * ROW + 16))
        for kind, label, src, rnote in rows:
            vals = [value(mac, src) for mac in macs]
            lx = GX0 + 20
            if kind in ("branch", "gone"):
                text(lx + 10, y + (20 if kind == "branch" and rnote else 26), "&#x2514;", "g")
                lx += 30
            if kind == "win":
                add(f'<rect x="{lx - 6}" y="{y + 11}" width="{len(label) * 7.6 + 34:.0f}" height="20" fill="#000"/>')
                text(lx, y + 26, "&#x2605; " + esc(label), "win")
            elif kind == "branch" and rnote:     # with why it was dropped below
                text(lx, y + 20, esc(label), "g")
                text(lx + 2, y + 34, esc(rnote), "gi")
            else:
                text(lx, y + 26, esc(label), "m" if kind == "main" else "g")
            if vals[0] is None:
                text(BX, y + 26, esc(rnote), "gi")
            elif kind == "win" and vals[0] == 0:
                text(BX, y + 26, "0 s: " + esc(rnote), "v")
            else:
                for j, v in enumerate(vals):
                    by, h = y + BY[j], BH[j]
                    vy = by + h / 2 + 4
                    vcls = "v" if kind != "branch" else "vg"
                    if isinstance(v, tuple):          # a first page, then the next one
                        first, nxt = v
                        if first + nxt > MAXV:        # too long: both halves broken
                            w1 = w2 = (BW + 26) / 2 - 1
                        else:
                            w1, w2 = max(bx(first), 1.5), max(bx(nxt), 1.5)
                        add(f'<rect x="{BX}" y="{by}" width="{w1:.1f}" height="{h}" {SOLID}/>')
                        add(f'<rect x="{BX + w1 + 2:.1f}" y="{by}" width="{w2:.1f}" height="{h}" {DOTTED}/>')
                        if first + nxt > MAXV:
                            brk(BX + w1 - 18, by, h)
                            brk(BX + w1 + 2 + w2 - 18, by, h)
                        if first + nxt > MAXV:        # both pages failed: a short label
                            label_ = f"&#x2715; {first:.0f} s + {nxt:.0f} s"
                        else:
                            mark = lambda t: ("&#x2715; " if t > 15 else "") + fmt(t)
                            label_ = f"{mark(first)} + {mark(nxt)}"
                        text(f"{BX + w1 + 2 + w2 + 6:.1f}", vy, label_, vcls)
                    else:
                        w = max(bx(v), 1.5)
                        if v > MAXV:
                            w = BW + 26
                        add(f'<rect x="{BX}" y="{by}" width="{w:.1f}" height="{h}" {DASHED if kind == "branch" else SOLID}/>')
                        if v > MAXV:
                            brk(BX + BW + 6, by, h)
                        text(f"{BX + w + 6:.1f}", vy, fmt(v), vcls)
            y += ROW
        if page_group:
            text(f"{limits[-1][0] - 6:.1f}", limits[-1][1], "Notion's server gives up on a handshake after 15 s", "lim", "end")
            y += 14
        y += 10
        # the group box, its title and its result on the border
        add(f'<rect x="{GX0}" y="{top + 8}" width="{GX1 - GX0}" height="{y - top - 8}" fill="none" stroke="#000" stroke-width="1"/>')
        add(f'<rect x="{GX0 + 10}" y="{top}" width="{len(title) * 8.2 + 14:.0f}" height="16" fill="#fff"/>')
        text(GX0 + 16, top + 13, esc(title), "gt")
        firsts = [total(value(mac, rows[0][2])) for mac in macs]
        wins = [total(value(mac, next(r[2] for r in rows if r[0] == "win"))) for mac in macs]
        res = " &#xB7; ".join(f"{mac['name'].replace('Mac ', '')} {fmt(a)} &#x2192; {fmt(w_)}" for mac, a, w_ in zip(macs, firsts, wins))
        if page_group:
            res = "both pages: " + res
        rw = len(res.replace("&#x2192;", ">").replace("&#xB7;", ".")) * 6.9 + 14
        add(f'<rect x="{GX1 - 10 - rw:.0f}" y="{top}" width="{rw:.0f}" height="16" fill="#fff"/>')
        text(GX1 - 17, top + 12, res, "r", "end")
        y += 22

    notes = [
        "Measured on the real Macs in October 2026 with the Speed Test app; the data is in SpeedTest/results. The crypto groups",
        "time one operation with no network (October 20); under 5 s, the middle of 3 runs. The 2-bit and 3-bit windows are",
        "rebuilt for the test: their March code wasn't kept. The last group is whole pages on the real api.notion.com, medians",
        "of 3. Until patched, Open Transport added 3-4 s to every page, so every row but the last has that wait in it. Stock",
        "rows add the test server's crypto to the same network time; \u201cduring\u201d adds the measured key pair. \u2715: longer than",
        "Notion's 15 s limit, so its server gave up. Rows without bars can't be timed any more.",
        METHODS_ALSO,
    ]
    for k, s_ in enumerate(notes):
        text(GX0, y + k * 15, esc(s_), "f")
    height = y + len(notes) * 15 + 22

    # The window: 1-pixel shadow, title bar with stripes, close and zoom box
    TB = 26
    title = "Head to head"
    tw = len(title) * 9 + 24
    chrome = [
        f'<rect x="{WX0 + 2}" y="{14}" width="{WX1 - WX0}" height="{height - 18}" fill="#000"/>',
        f'<rect x="{WX0}" y="12" width="{WX1 - WX0}" height="{height - 18}" fill="#fff" stroke="#000" stroke-width="1.2"/>',
    ]
    for k in range(6):
        sy = 12 + 6 + k * 3
        chrome.append(f'<line x1="{WX0 + 3}" y1="{sy}" x2="{WX1 - 3}" y2="{sy}" stroke="#000" stroke-width="1"/>')
    chrome += [
        f'<rect x="{WX0 + 14}" y="17" width="23" height="17" fill="#fff"/>',
        f'<rect x="{WX0 + 18}" y="19" width="14" height="13" fill="#fff" stroke="#000" stroke-width="1"/>',
        f'<rect x="{WX1 - 37}" y="17" width="23" height="17" fill="#fff"/>',
        f'<rect x="{WX1 - 32}" y="19" width="14" height="13" fill="#fff" stroke="#000" stroke-width="1"/>',
        f'<rect x="{WX1 - 32}" y="19" width="8" height="7" fill="none" stroke="#000" stroke-width="1"/>',
        f'<rect x="{(WX0 + WX1 - tw) / 2:.0f}" y="15" width="{tw}" height="21" fill="#fff"/>',
        f'<text class="tt" x="{(WX0 + WX1) / 2:.0f}" y="30" text-anchor="middle">{title}</text>',
        f'<line x1="{WX0}" y1="{12 + TB + 0.5}" x2="{WX1}" y2="{12 + TB + 0.5}" stroke="#000" stroke-width="1.2"/>',
    ]

    style = f"""<style>
text {{ font-family: {FONT}; fill: #000; }}
.tt  {{ font-size: 15px; font-weight: 700; }}
.h   {{ font-size: 15px; font-weight: 700; }}
.s   {{ font-size: 12.5px; }}
.gt  {{ font-size: 13.5px; font-weight: 700; }}
.r   {{ font-size: 11.5px; }}
.n   {{ font-size: 11.5px; fill: #555; }}
.m   {{ font-size: 12.5px; }}
.g   {{ font-size: 12.5px; fill: #777; }}
.gi  {{ font-size: 11.5px; fill: #777; paint-order: stroke; stroke: #fff; stroke-width: 3px; stroke-linejoin: round; }}
.win {{ font-size: 12.5px; font-weight: 700; fill: #fff; }}
.v   {{ font-size: 11px; paint-order: stroke; stroke: #fff; stroke-width: 3px; stroke-linejoin: round; }}
.vg  {{ font-size: 11px; fill: #777; paint-order: stroke; stroke: #fff; stroke-width: 3px; stroke-linejoin: round; }}
.ax  {{ font-size: 10px; fill: #777; }}
.lim {{ font-size: 10.5px; }}
.f   {{ font-size: 11px; fill: #444; }}
</style>
<pattern id="dots" width="4" height="4" patternUnits="userSpaceOnUse">
<rect width="4" height="4" fill="#fff"/><rect width="2" height="2" fill="#000"/><rect x="2" y="2" width="2" height="2" fill="#000"/>
</pattern>"""
    head = (f'<?xml version="1.0" encoding="UTF-8"?>\n'
            f'<svg xmlns="http://www.w3.org/2000/svg" version="1.1" viewBox="0 0 {W} {height}" width="{W}" height="{height}">\n'
            "<title>Head to head: every method tried for each job in the handshake</title>\n"
            "<desc>For the P-256 check, the repeat check with tables, X25519, the P-384 check, and a page then the next one: "
            "every method tried, slowest to fastest, on a Mac Classic II and a Mac SE/30, measured with the Speed Test app "
            "in October 2026.</desc>\n"
            f'<defs>{style}</defs>\n<rect width="{W}" height="{height}" fill="#fff"/>\n')
    with open(os.path.join(OUTDIR, METHODS_OUT), "w", encoding="utf-8") as f:
        f.write(head + "\n".join(chrome + b) + "\n</svg>\n")
    print(f"wrote {METHODS_OUT} ({height} px high)")
    for title, _, rows in METHOD_GROUPS:
        print(f"  {title}")
        for kind, label, src, _ in rows:
            vs = [value(mac, src) for mac in macs]
            show = lambda v: " + ".join(fmt(t) for t in v) if isinstance(v, tuple) else fmt(v)
            print(f"    {kind:6s} {label:40s} " + ("   ".join(show(v) for v in vs) if vs[0] is not None else "-"))


# ---------------------------------------------------------------------------
# The pages chart again, with the steps grouped by kind: faster P-256 math,
# faster X25519, less work on every page, and what the next page reuses.
# The Speed Test ran the rows in this order against the test server
# (File > Live: Grouped Rows, Test Server).
#
# A row's crypto is built from the operations its pages did, each the median
# of every row that ran it the same way (the same code, the same pool). So a
# hiccup in one row doesn't show as a step, and each step only changes the
# operations it replaces. Everything else is as the pages chart has it.

GROUPED_OUT = "performance-grouped.svg"

# (group title, note, [(row label, row note, in the code today?, step)])
GROUPS = [
    (None, None, [("Stock mbedTLS", "generic big-number math", False, "stock")]),
    ("Faster P-256 math", "the two signature checks", [
        ("+ micro-ecc for the P-256 curve math", "", True, "micro"),
        ("+ -O3 for the curve code", "it had been -Os since May", True, "o3"),
        ("+ Unrolled assembly multiply", "replaced below", False, "unrolled"),
        ("+ Comba multiply and square, Solinas reduction", "custom 68020 assembly", True, "comba"),
    ]),
    ("Faster X25519", "the key exchange, and making the next key pair", [
        ("+ 64 KB memory pool for the math", "mbedTLS stops fighting the Memory Manager", True, "pool"),
        ("+ X25519 in C (curve25519-donna)", "replaced below", False, "donna"),
        ("+ X25519 on the Comba assembly", "", True, "sat"),
    ]),
    ("Less work on every page", None, [
        ("+ WE1 pinned as trust anchor", "no P-384 check", True, "we1"),
        ("+ Key pair made before connecting", "off the server’s clock", True, "pregen"),
    ]),
    ("The next page reuses the first", None, [
        ("+ Signature checks remembered", "the next page skips the certificate check", True, "sigcache"),
        ("+ Shamir’s trick with wNAF tables", "tables are built after the first page", True, "wnaf"),
        ("+ Straus verify on half-length scalars", "a faster check for a key it has seen", True, "straus"),
        ("+ Session tickets, kept on disk", "the next page resumes", True, "tickets"),
    ]),
    ("The network", None, [
        ("With Open Transport Bug Patcher", "today: no TCP window scaling offered", True, "otfix"),
    ]),
]

CRYPTO_ITEM = re.compile(r"(P-256 x(\d) \((\w+)\)|P-384|key pair|X25519) ([\d.]+) s")


def grouped_rows(text, groups=None):
    """Per row of a grouped run: for page 1 and page 2, the operations it
    did as (key, seconds), or 'resumed'. Keys say which code ran."""
    steps = [st for _, _, rows in (groups or GROUPS) for (_, _, _, st) in rows if st != "otfix"]
    blocks = re.split(r"\n(?=\d+\. )", text.split("LIVE, EACH ROW", 1)[1])[1:len(steps) + 1]
    cfg = dict(uecc="mbedTLS", pool=0, x="mbedTLS")
    effect = dict(micro=("uecc", "-Os"), o3=("uecc", "-O3"), unrolled=("uecc", "unrolled"),
                  comba=("uecc", "Comba"), pool=("pool", 1), donna=("x", "donna"), sat=("x", "sat"))
    out = []
    for st, blk in zip(steps, blocks):
        if st in effect:
            cfg[effect[st][0]] = effect[st][1]
        pages = []
        for pg in (1, 2):
            m = re.search(rf"page {pg}  TCP [\d.]+ s, handshake [\d.]+ s( \(resumed\))?.*?(?:\n\s+crypto: (.*))?$",
                          blk, re.M)
            if not m:                       # skipped: the same as page 1
                pages.append(pages[0])
                continue
            if m.group(1):
                pages.append("resumed")
                continue
            ops = []
            for item in CRYPTO_ITEM.finditer(m.group(2) or ""):
                name, n, path, t = item.group(1), item.group(2), item.group(3), float(item.group(4))
                if n:        # P-256 checks: how many, which path, which code
                    code = ("mbedTLS", cfg["pool"]) if path == "mbedTLS" else cfg["uecc"]
                    key = ("p256", n, path, code)
                elif name == "P-384":
                    key = ("p384", cfg["pool"])
                else:        # the key pair is an X25519 too
                    key = ("x25519", cfg["x"], cfg["pool"] if cfg["x"] == "mbedTLS" else 0)
                ops.append((key, t))
            pages.append(ops)
        out.append(pages)
    return out


def op_medians(rows):
    """Each operation's median over every row that ran it the same way"""
    samples = {}
    for pages in rows:
        for page in pages:
            if page != "resumed":
                for key, t in page:
                    samples.setdefault(key, []).append(t)
    return {k: statistics.median(v) for k, v in samples.items()}


def grouped_lines(rows, med, bug, fix):
    """(first page, next page, marks) per row, the patched row last"""
    crypto = lambda page: sum(med[k] for k, _ in page)
    state = lambda h: "" if h is None or h < SERVER_LIMIT - EDGE else ("edge" if h <= SERVER_LIMIT + EDGE else "x")
    lines = []
    for i, pages in enumerate(rows + [rows[-1]]):
        e = fix if i == len(rows) else bug
        t = [e["resumed"] if page == "resumed" else crypto(page) + e["page"] for page in pages]
        hs = [None if page == "resumed" else crypto(page) + e["hs"] for page in pages]
        lines.append((t[0], t[1], state(hs[0]), state(hs[1])))
    return lines


def grouped_chart():
    macs = []
    for m in PAIR:
        rows = grouped_rows(read(f"2026-10-20 {m['file']} test server grouped.txt"))
        bug = everything_else(read(f"2026-10-18 {m['file']} before the fix.txt"))
        fix = everything_else(read(f"2026-10-19 {m['file']} with OT Window Fix.txt"))
        lines = grouped_lines(rows, op_medians(rows), bug, fix)
        macs.append(dict(m, lines=lines, bug=bug, fix=fix))
    draw_grouped(GROUPS, macs, GROUPED_OUT, "grouped by kind")



LESSWORK_OUT = "performance-lesswork.svg"
LESSWORK_ESTIMATE_OUT = "performance-lesswork-estimate.svg"
LESSWORK_DARK_OUT = "performance-lesswork-dark.svg"

# Less work first, with stock math: then the math that's left, made faster
LESSWORK_GROUPS = [
    ("Starting point", None, [("Stock mbedTLS", "", False, "stock")]),
    ("Less work on every page", None, [
        ("+ Skip the slowest check", "", True, "we1"),          # WE1 pinned: no P-384 check
        ("+ Make the key early", "", True, "pregen"),               # key pair before connecting
    ]),
    ("The next page reuses the first", None, [
        ("+ Remember the certificate", "", True, "sigcache"),
        ("+ Store the session", "", True, "tickets"),    # session tickets, on disk
    ]),
    ("Faster signature checks", "P-256", [
        ("+ Faster library (micro-ecc)", "", True, "micro"),
        ("+ Optimize more (-O3)", "", True, "o3"),
        ("+ Assembly multiply (replaced)", "", False, "unrolled"),
        ("+ Custom assembly (Comba, Solinas)", "", True, "comba"),
    ]),
    ("Faster key exchange", "X25519", [
        ("+ 64 KB memory pool", "", True, "pool"),
        ("+ Key exchange in C (replaced)", "", False, "donna"),      # curve25519-donna
        ("+ Key exchange in assembly", "", True, "sat"),
    ]),
    ("The network", None, [
        ("+ Fix Apple’s TCP bug", "", True, "otfix"),
    ]),
]


def lesswork_estimate_rows(med, groups=None):
    """The less-work-first rows as page operation lists, built from the
    grouped run's medians (keys as grouped_rows makes them). Stock math with
    one check remembered isn't in that run: it counts half of stock's two."""
    cfg = dict(uecc="mbedTLS", pool=0, x="mbedTLS", we1=0, pregen=0, sigcache=0, tickets=0)
    effect = dict(micro=("uecc", "-Os"), o3=("uecc", "-O3"), unrolled=("uecc", "unrolled"), comba=("uecc", "Comba"),
                  pool=("pool", 1), donna=("x", "donna"), sat=("x", "sat"), we1=("we1", 1), pregen=("pregen", 1),
                  sigcache=("sigcache", 1), tickets=("tickets", 1))
    rows = []
    for _, _, grows in (groups or LESSWORK_GROUPS):
        for _, _, _, st in grows:
            if st == "otfix":
                continue
            if st in effect:
                cfg[effect[st][0]] = effect[st][1]
            xkey = ("x25519", cfg["x"], cfg["pool"] if cfg["x"] == "mbedTLS" else 0)
            if cfg["uecc"] == "mbedTLS":
                p2 = ("p256", "2", "mbedTLS", ("mbedTLS", cfg["pool"]))
            else:
                p2 = ("p256", "2", "plain", cfg["uecc"])

            def page(checks):
                ops = [(p2, 0)] if checks == 2 else [(("half",) + p2, 0)]
                if not cfg["we1"]:
                    ops.append((("p384", cfg["pool"]), 0))
                if not cfg["pregen"]:
                    ops.append((xkey, 0))          # the key pair
                ops.append((xkey, 0))              # the key exchange
                return ops
            first = page(2)
            nxt = "resumed" if cfg["tickets"] else (page(1) if cfg["sigcache"] else first)
            rows.append([first, nxt])
    for k in [k for k in med if k[0] == "p256" and k[1] == "2"]:
        med[("half",) + k] = med[k] / 2
    return rows


def lesswork_chart():
    """Measured when the less-work-first run is in results/, else the estimate"""
    macs, estimate = [], False
    for m in PAIR:
        bug = everything_else(read(f"2026-10-18 {m['file']} before the fix.txt"))
        fix = everything_else(read(f"2026-10-19 {m['file']} with OT Window Fix.txt"))
        measured = os.path.join(RESULTS, f"2026-10-20 {m['file']} test server less work.txt")
        if os.path.exists(measured):
            rows = grouped_rows(read(os.path.basename(measured)), LESSWORK_GROUPS)
            med = op_medians(rows)
        else:
            estimate = True
            med = op_medians(grouped_rows(read(f"2026-10-20 {m['file']} test server grouped.txt")))
            rows = lesswork_estimate_rows(med)
        macs.append(dict(m, lines=grouped_lines(rows, med, bug, fix), bug=bug, fix=fix))
    if not estimate:   # black, for Hackaday
        draw_grouped(LESSWORK_GROUPS, macs, LESSWORK_DARK_OUT, "less work first", dark=True,
                     extra="wNAF tables and Straus aren\u2019t rows here: they speed up the signature check when a ticket is "
                           "refused, and with a ticket the next page doesn\u2019t check one at all.")
    draw_grouped(LESSWORK_GROUPS, macs, LESSWORK_ESTIMATE_OUT if estimate else LESSWORK_OUT,
                 "less work first", estimate=estimate,
                 extra="wNAF tables and Straus aren’t rows here: they speed up the signature check when a ticket is "
                       "refused, and with a ticket the next page doesn’t check one at all.")


# Order B: the next page reuses the first, then faster P-256 math, then less
# work on every page, then faster X25519. Only estimated so far.
REUSE_FIRST_ESTIMATE_OUT = "performance-reuse-first-estimate.svg"
_LW = {g[0]: g for g in LESSWORK_GROUPS}
REUSE_FIRST_GROUPS = [_LW["Starting point"], _LW["The next page reuses the first"], _LW["Faster signature checks"],
                      _LW["Less work on every page"], _LW["Faster key exchange"], _LW["The network"]]


def reuse_first_chart():
    macs = []
    for m in PAIR:
        bug = everything_else(read(f"2026-10-18 {m['file']} before the fix.txt"))
        fix = everything_else(read(f"2026-10-19 {m['file']} with OT Window Fix.txt"))
        med = op_medians(grouped_rows(read(f"2026-10-20 {m['file']} test server grouped.txt")))
        rows = lesswork_estimate_rows(med, REUSE_FIRST_GROUPS)
        macs.append(dict(m, lines=grouped_lines(rows, med, bug, fix), bug=bug, fix=fix))
    draw_grouped(REUSE_FIRST_GROUPS, macs, REUSE_FIRST_ESTIMATE_OUT, "reuse first", estimate=True,
                 extra="wNAF tables and Straus aren’t rows here: they speed up the signature check when a ticket is "
                       "refused, and with a ticket the next page doesn’t check one at all.")


def draw_grouped(groups, macs, out_name, order_name, estimate=False, extra="", dark=False):
    """The chart alone. Its title, what the lines mean and how it was
    measured go in the text around it (extra: that text's note on rows that
    aren't in this order).

    Per row: its label, then two bars, the first page on top and the next
    page below. In each bar the 16-bit bus is thick and dark, the 32-bit bus
    thin and bright on top of it; orange for the first page, yellow for the
    next. The times stand left of the bars as a range, 32-bit to 16-bit,
    only where they change. Each group's name stands large on the right."""
    # The promise of this chart: every row is shorter than the one above
    for m in macs:
        for a, b_ in zip(m["lines"], m["lines"][1:]):
            assert b_[0] + b_[1] < a[0] + a[1], (m["name"], a, b_)

    # Colours: black for Hackaday, or a white page
    P = (dict(bg="#000", band="#121212", net="#0e2014", grid="#262626", red="#ff6b6b", dash="#777") if dark else
         dict(bg="#fff", band="#f6f7f9", net="#eaf4ec", grid="#ececec", red=RED, dash="#999"))
    X0, X1, MAX = 150, 930, 80.0
    DASH_X = X0 - 72                       # the times line up on their dash
    px = lambda s_: s_ / MAX * (X1 - X0)
    THICK, THIN, LINE = 16, 9, 19          # bar heights, and from one bar to the next
    RH = 80                                # a row: space, label, two bars
    LAB_Y, BAR_Y = 26, 36                  # label baseline and first bar, from the row's top
    DARK, BRIGHT = ("#a85a17", "#a8812a"), ("#f28e2b", "#f6c142")
    DARK_TEXT = ("#c87a35", "#c49d45") if dark else DARK   # the 16-bit numbers, lighter on black
    # A range is whole seconds when it starts at 10 s or more; below that it
    # keeps a decimal (two under 1 s, for the 0.85 s), the same for both ends
    def fmt_range(lo, hi):
        if lo >= 9.95:
            return f"{int(lo + 0.5)}", f"{int(hi + 0.5)}"
        one = lambda t: f"{t:.1f}" if t >= 0.995 else f"{t:.2f}"
        return one(lo), one(hi)
    b, labels = [], []
    add = b.append

    # Every text's font, size and colour as attributes, not CSS classes:
    # Affinity and other editors read attributes, but not all of a stylesheet
    LOOK = dict(  # name: (size, light fill, dark fill, weight, italic)
        lab=(20, "#222", "#e0e0e0", "300", False), old=(20, "#9a9a9a", "#7a7a7a", "300", False),
        last=(22, "#111", "#ffffff", "500", False), plus=(20, "#888", "#8a8a8a", "300", False),
        num=(16, None, None, None, False), numb=(18, None, None, "700", False),
        gt=(22, "#8a8a8a", "#858585", "300", False), gn=(14, "#8a8a8a", "#858585", "300", True),
        axis=(12, "#999", "#7a7a7a", None, False), leg=(14, "#444", "#c8c8c8", None, False),
        ctx=(12, "#888", "#7a7a7a", None, True), hdr=(13, None, None, "700", False))

    def attrs(look, fill=None, weight=None):
        sz, light, dark_, w, italic = LOOK[look]
        out = (f' font-family="Helvetica Neue, Helvetica, Arial, sans-serif" font-size="{sz}"'
               f' fill="{fill or (dark_ if dark else light)}"')
        if weight or w:
            out += f' font-weight="{weight or w}"'
        if italic:
            out += ' font-style="italic"'
        return out

    def text(x, y, s_, look, anchor=None, weight=None, to=None, fill=None):
        a = f' text-anchor="{anchor}"' if anchor else ""
        (to if to is not None else b).append(f'<text x="{x}" y="{y}"{a}{attrs(look, fill=fill, weight=weight)}>{s_}</text>')

    # Legend, one line
    ly = 30
    for x, page, name in ((24, 0, "first page"), (160, 1, "next page")):
        add(f'<rect x="{x}" y="{ly - 12}" width="30" height="{THICK}" fill="{DARK[page]}"/>')
        add(f'<rect x="{x}" y="{ly - 12}" width="20" height="{THIN}" fill="{BRIGHT[page]}"/>')
        text(x + 38, ly, name, "leg")
    add(f'<rect x="292" y="{ly - 12}" width="30" height="{THIN}" fill="{BRIGHT[0]}"/>')
    text(330, ly, "thin: 32-bit bus", "leg")
    add(f'<rect x="470" y="{ly - 12}" width="30" height="{THICK}" fill="{DARK[0]}"/>')
    text(508, ly, "thick: 16-bit bus", "leg")
    add(f'<line x1="662" y1="{ly - 14}" x2="662" y2="{ly + 5}" stroke="{P["red"]}" stroke-width="1.6" stroke-dasharray="2 3"/>')
    text(672, ly, "Cloudflare gives up after 15 s", "leg")
    if estimate:
        ly += 22
        text(24, ly, "ESTIMATE: not measured in this order yet", "leg", weight="700")

    top = ly + 34
    PAD_T, PAD_B = 2, 6
    bottom = top + sum(PAD_T + len(rows) * RH + PAD_B for _, _, rows in groups)
    for t in range(0, int(MAX) + 1, 10):
        text(f"{X0 + px(t):.1f}", top - 8, f"{t} s", "axis", "middle")
        text(f"{X0 + px(t):.1f}", bottom + 16, f"{t} s", "axis", "middle")
    # The time column's header, lined up on the same dash as the times
    text(DASH_X - 5, top - 8, "32-bit", "hdr", "end", fill=BRIGHT[0])
    text(DASH_X, top - 8, "&#x2013;", "hdr", "middle", fill=BRIGHT[0])
    text(DASH_X + 5, top - 8, "16-bit", "hdr", fill=DARK_TEXT[0])
    text(24, bottom + 40, "Seconds per page, only where they change", "ctx")

    def shown(i, page):
        """Times only where they change from the same page in the row above"""
        vals = lambda r: fmt_range(*(m["lines"][r][page] for m in reversed(macs)))
        return i == 0 or vals(i) != vals(i - 1)

    def bars(y, i, last, grid_end):
        y0, y1 = y - 3, y + LINE + THICK + 3        # the bars' height, for the lines
        for t in range(0, int(MAX) + 1, 10):        # grid, behind the bars, not under a group name
            if X0 + px(t) > grid_end:
                break
            add(f'<line x1="{X0 + px(t):.1f}" y1="{y0}" x2="{X0 + px(t):.1f}" y2="{y1}" stroke="{P["grid"]}"/>')
        # the 15 s line, on top of the bars, thin and dotted
        labels.insert(0, f'<line x1="{X0 + px(15):.1f}" y1="{y0}" x2="{X0 + px(15):.1f}" y2="{y1}" '
                         f'stroke="{P["red"]}" stroke-width="1.6" stroke-dasharray="2 3"/>')
        for page in (0, 1):
            yl = y + page * LINE
            t16, t32 = (m["lines"][i][page] for m in macs)
            add(f'<rect x="{X0}" y="{yl}" width="{max(px(t16), 1.5):.1f}" height="{THICK}" fill="{DARK[page]}"/>')
            add(f'<rect x="{X0}" y="{yl}" width="{max(px(t32), 1.5):.1f}" height="{THIN}" fill="{BRIGHT[page]}"/>')
            if shown(i, page):   # "52.2–77.2 s", lined up on the dash: 32-bit bright, 16-bit dark
                look = "numb" if last else "num"
                yt = yl + 13
                s32, s16 = fmt_range(t32, t16)
                labels.append(f'<text x="{DASH_X - 5}" y="{yt}" text-anchor="end"{attrs(look, fill=BRIGHT[page])}>{s32}</text>')
                labels.append(f'<text x="{DASH_X}" y="{yt}" text-anchor="middle"{attrs(look, fill=BRIGHT[page])}>&#x2013;</text>')
                labels.append(f'<text x="{DASH_X + 5}" y="{yt}"{attrs(look, fill=DARK_TEXT[page])}>{s16} s</text>')

    # Group names on the right, in lines that fit beside the bars
    BREAKS = {"Less work on every page": ["Less work on", "every page"],
              "The next page reuses the first": ["The next page", "reuses the first"],
              "Faster signature checks": ["Faster", "signature checks"],
              "Faster key exchange": ["Faster", "key exchange"],
              "Faster P-256 math": ["Faster", "P-256 math"],
              "Faster X25519": ["Faster", "X25519"]}

    y = top
    i = 0
    for g, (title, gnote, rows) in enumerate(groups):
        height = PAD_T + len(rows) * RH + PAD_B
        shade = P["net"] if title == "The network" else (P["band"] if g % 2 == 0 else None)
        if shade:
            add(f'<rect x="16" y="{y}" width="968" height="{height}" fill="{shade}"/>')
        grid_end = X1 + 1
        if title and len(rows) == 1 and title != "The network":   # one long row: name on the label line
            text(976, f"{y + PAD_T + LAB_Y:.0f}", esc(title.upper()), "gt", "end")
        elif title:
            lines = [l.upper() for l in BREAKS.get(title, [title])]
            block = len(lines) * 26 + (20 if gnote else 0)
            ty = y + (height - block) / 2 + 20
            longest = max(m["lines"][k][0] for m in macs for k in range(i, i + len(rows)))
            grid_end = 976 - max(len(l) for l in lines) * 14 - 12
            assert X0 + px(longest) + 12 < grid_end, title   # the name fits beside the bars
            for k, l in enumerate(lines):
                text(976, f"{ty + k * 26:.0f}", esc(l), "gt", "end")
            if gnote:
                text(976, f"{ty + len(lines) * 26 - 4:.0f}", esc(gnote), "gn", "end")
        y += PAD_T
        for label, note, in_code, st in rows:
            last = st == "otfix"
            replaced = not in_code and st != "stock"
            look = "last" if last else ("old" if replaced else "lab")
            if label.startswith("+ "):
                text(X0, y + LAB_Y, "+", "plus")
                text(X0 + 22, y + LAB_Y, esc(label[2:]), look)
            else:
                text(X0, y + LAB_Y, esc(label), look)
            bars(y + BAR_Y, i, last, grid_end)
            y += RH
            i += 1
        y += PAD_B

    height = bottom + 54

    head = (f'<?xml version="1.0" encoding="UTF-8"?>\n'
            f'<svg xmlns="http://www.w3.org/2000/svg" version="1.1" viewBox="0 0 {W} {height}" width="{W}" height="{height}">\n'
            "<title>Opening a page from Cloudflare, then the next one, on a 16 MHz 68030</title>\n"
            "<desc>How long the first and the next page took on a 68030 with a 16-bit bus (Mac Classic II) and one with a "
            f"32-bit bus (Mac SE/30) as each method was added, {order_name}, measured with the Speed Test app in "
            "October 2026.</desc>\n"
            f'<rect width="{W}" height="{height}" fill="{P["bg"]}"/>\n')
    with open(os.path.join(OUTDIR, out_name), "w", encoding="utf-8") as f:
        f.write(head + "\n".join(b + labels) + "\n</svg>\n")
    print(f"wrote {out_name} ({height} px high)")
    flat = [r for _, _, rows in groups for r in rows]
    for m in macs:
        print(f"  {m['name']}")
        for (label, _, _, _), ln in zip(flat, m["lines"]):
            print(f"    {label:48s} {secs(ln[0]):>7s} + {secs(ln[1]):>7s}  = {secs(ln[0] + ln[1])}")

if __name__ == "__main__":
    import sys
    which = sys.argv[1:] or ["macs", "pages", "methods", "grouped", "lesswork"]
    if "macs" in which:
        for mac in MACS:
            chart(mac)
    if "pages" in which:
        pages_chart()
    if "methods" in which:
        methods_chart()
    if "grouped" in which:
        grouped_chart()
    if "lesswork" in which:
        lesswork_chart()
    if "reusefirst" in which:
        reuse_first_chart()
