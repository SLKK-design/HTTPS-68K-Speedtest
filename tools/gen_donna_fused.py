#!/usr/bin/env python3
"""gen_donna_fused.py — generate unused_donna_fused_m68k.s + host-test C model (OPT #8).

Emits fmul_fused_m68k / fsquare_fused_m68k: the 10x10 polynomial multiply
(or square) with the freduce_degree fold (x19) fused in.  For each output
limb k in 0..8 the high column k+10 is accumulated first, multiplied by 19
in registers (19v = 16v + 2v + v via add/addx pairs), then the low column k
is accumulated on top and the result stored.  Limb 9 has no high partner.

Output t[] is the caller's local temp (never aliases inputs), 10 limbs,
big-endian int64 (high word at lower address) — same conventions as the
existing unused_donna_m68k.s.  freduce_coefficients stays in C.

Coefficient rules (donna 26/25-bit packing):
  fproduct: ordered pairs (i,j), i+j=k, product a[i]*b[j]; coeff 2 iff
            both i and j are odd.
  fsquare:  unordered pairs i<=j, i+j=k; coeff = (1 if i==j else 2),
            doubled again if both i and j are odd.

Run: python3 tools/gen_donna_fused.py. It writes src/variants/unused_donna_fused_m68k.s.

Also emits tests/donna_fused_model.c with C functions computing the exact
same column/fold math for host verification against the original
fproduct + freduce_degree.
"""

import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def off(i):  # low 32-bit word of big-endian int64 limb i
    return i * 8 + 4


def product_pairs(k):
    """fproduct column k: ordered (i,j) with i+j=k, 0<=i,j<=9, plus coeff."""
    out = []
    for i in range(10):
        j = k - i
        if 0 <= j <= 9:
            coeff = 2 if (i & 1) and (j & 1) else 1
            out.append((i, j, coeff))
    return out


def square_pairs(k):
    """fsquare column k: unordered i<=j with i+j=k, plus coeff."""
    out = []
    for i in range(10):
        j = k - i
        if i <= j <= 9:
            coeff = 1 if i == j else 2
            if (i & 1) and (j & 1):
                coeff *= 2
            out.append((i, j, coeff))
    return out


def emit_double(lines, lo, hi, times):
    for _ in range(times):
        lines.append(f"    add.l   %{lo},%{lo}")
        lines.append(f"    addx.l  %{hi},%{hi}")


def emit_column(lines, pairs, regA, regB, fresh, what):
    """Accumulate one column into d1:d0.

    fresh=True: first product loads the accumulator; otherwise every
    product goes through d3:d2 and is added in.
    regA/regB: address registers holding the a/b operand bases.
    """
    first = fresh
    for (i, j, coeff) in pairs:
        times = {1: 0, 2: 1, 4: 2}[coeff]
        c = f"{what}[{i}]*{what.replace('a', 'b') if regA != regB else what}[{j}]"
        if regA != regB:
            c = f"a[{i}]*b[{j}]"
        else:
            c = f"in[{i}]*in[{j}]"
        if coeff > 1:
            c = f"{coeff}*" + c
        if first:
            lines.append(f"    move.l  {off(j)}(%{regB}),%d0")
            lines.append(f"    muls.l  {off(i)}(%{regA}),%d1:%d0      | {c}")
            emit_double(lines, "d0", "d1", times)
            first = False
        else:
            lines.append(f"    move.l  {off(j)}(%{regB}),%d2")
            lines.append(f"    muls.l  {off(i)}(%{regA}),%d3:%d2      | {c}")
            emit_double(lines, "d2", "d3", times)
            lines.append("    add.l   %d2,%d0")
            lines.append("    addx.l  %d3,%d1")


def emit_x19(lines):
    """d1:d0 = 19 * d1:d0  (clobbers d2/d3).  19v = 16v + 2v + v."""
    lines.append("    | --- x19 fold: 19v = 16v + 2v + v ---")
    lines.append("    move.l  %d0,%d2")
    lines.append("    move.l  %d1,%d3                  | save v")
    emit_double(lines, "d0", "d1", 4)        # 16v
    lines.append("    add.l   %d2,%d0")
    lines.append("    addx.l  %d3,%d1                  | 17v")
    lines.append("    add.l   %d2,%d2")
    lines.append("    addx.l  %d3,%d3                  | 2v")
    lines.append("    add.l   %d2,%d0")
    lines.append("    addx.l  %d3,%d1                  | 19v")


def emit_store(lines, k):
    lines.append(f"    move.l  %d1,(%a2)+               | t[{k}] hi")
    lines.append(f"    move.l  %d0,(%a2)+               | t[{k}] lo")
    lines.append("")


def gen_function(name, square):
    lines = []
    if square:
        lines.append(f"    .global {name}")
        lines.append(f"{name}:")
        lines.append("    movem.l %d2-%d3/%a2-%a3,-(%sp)")
        lines.append("    movea.l 20(%sp),%a2         | t (output, 10 limbs)")
        lines.append("    movea.l 24(%sp),%a3         | in")
        regA = regB = "a3"
        col = square_pairs
    else:
        lines.append(f"    .global {name}")
        lines.append(f"{name}:")
        lines.append("    movem.l %d2-%d3/%a2-%a4,-(%sp)")
        lines.append("    movea.l 24(%sp),%a2         | t (output, 10 limbs)")
        lines.append("    movea.l 28(%sp),%a3         | a")
        lines.append("    movea.l 32(%sp),%a4         | b")
        regA, regB = "a3", "a4"
        col = product_pairs
    lines.append("")
    for k in range(9):
        lines.append(f"    | === t[{k}] = 19*column[{k + 10}] + column[{k}] ===")
        emit_column(lines, col(k + 10), regA, regB, True, "a")
        emit_x19(lines)
        emit_column(lines, col(k), regA, regB, False, "a")
        emit_store(lines, k)
    lines.append("    | === t[9] = column[9] (no high partner) ===")
    emit_column(lines, col(9), regA, regB, True, "a")
    emit_store(lines, 9)
    if square:
        lines.append("    movem.l (%sp)+,%d2-%d3/%a2-%a3")
    else:
        lines.append("    movem.l (%sp)+,%d2-%d3/%a2-%a4")
    lines.append("    rts")
    return lines


HEADER = """| unused_donna_fused_m68k.s — GENERATED by tools/gen_donna_fused.py, do not edit.
| NOT BUILT — superseded by curve25519_sat.c (OPT #8b); kept as reference only.
|
| OPT #8: curve25519 fproduct/fsquare_inner with the freduce_degree x19
| fold fused in.  Output is the degree-reduced 10-limb t[0..9]; the caller
| runs freduce_coefficients + memcpy as before (freduce_degree is gone).
|
| C prototypes:
|   void fmul_fused_m68k(limb *t, const limb *a, const limb *b);
|   void fsquare_fused_m68k(limb *t, const limb *in);
|   limb = int64_t, big-endian (high word at lower address);
|   operand s32 low word of limb i is at offset i*8+4.
|
| Per output limb k (0..8): accumulate column k+10 in d1:d0, multiply by
| 19 in registers (16v+2v+v, add/addx pairs — X-flag chains are intact:
| each chain starts with add.l), then accumulate column k on top, store.
| t never aliases the inputs (caller local), so direct streaming via
| (%a2)+ is safe.

    .text
    .even
"""


def model_column(pairs, a, b):
    terms = [f"{c} * ((limb)((s32){a}[{i}])) * ((s32){b}[{j}])" for (i, j, c) in pairs]
    return " + ".join(terms)


def gen_model():
    out = []
    out.append("/* donna_fused_model.c — GENERATED by tools/gen_donna_fused.py.")
    out.append(" * Host-test C model of the fused asm: identical column/fold math,")
    out.append(" * x19 done as (v<<4) + (v<<1) + v to mirror the register sequence. */")
    out.append("")
    out.append("static limb x19(limb v) { return (v << 4) + (v << 1) + v; }")
    out.append("")
    out.append("static void fmul_fused_model(limb *t, const limb *a, const limb *b) {")
    for k in range(9):
        out.append(f"  t[{k}] = x19({model_column(product_pairs(k + 10), 'a', 'b')})")
        out.append(f"        + ({model_column(product_pairs(k), 'a', 'b')});")
    out.append(f"  t[9] = {model_column(product_pairs(9), 'a', 'b')};")
    out.append("}")
    out.append("")
    out.append("static void fsquare_fused_model(limb *t, const limb *a) {")
    for k in range(9):
        out.append(f"  t[{k}] = x19({model_column(square_pairs(k + 10), 'a', 'a')})")
        out.append(f"        + ({model_column(square_pairs(k), 'a', 'a')});")
    out.append(f"  t[9] = {model_column(square_pairs(9), 'a', 'a')};")
    out.append("}")
    out.append("")
    return out


def main():
    asm = [HEADER]
    asm += gen_function("fmul_fused_m68k", square=False)
    asm.append("")
    asm.append("| " + "-" * 70)
    asm.append("")
    asm += gen_function("fsquare_fused_m68k", square=True)
    asm.append("")
    with open(os.path.join(ROOT, "src/variants/unused_donna_fused_m68k.s"), "w") as f:
        f.write("\n".join(asm))
    with open(os.path.join(ROOT, "tests/donna_fused_model.c"), "w") as f:
        f.write("\n".join(gen_model()))
    # sanity: product counts must match the original (100 mul / 55 square)
    np = sum(len(product_pairs(k)) for k in range(19))
    ns = sum(len(square_pairs(k)) for k in range(19))
    assert np == 100 and ns == 55, (np, ns)
    print("unused_donna_fused_m68k.s + donna_fused_model.c written; %d/%d products" % (np, ns))


if __name__ == "__main__":
    main()
