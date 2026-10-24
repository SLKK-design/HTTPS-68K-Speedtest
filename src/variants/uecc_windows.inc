/* uecc_windows.inc: plain fixed-window Shamir verify, 2 or 3 bits at a time.

   In March 2026, before wNAF, Claude 68 tried a 2-bit Shamir table (16
   points) and a 3-bit window (64 points). That code wasn't kept, so this
   is rebuilt for the speed test from the same micro-ecc parts the wNAF
   verify uses (affine table, co-Z add, Jacobian doubling).

   T[a*W + b] = a*G + b*Q for a, b < W = 2^bits, affine X|Y, T[0] unused.
   The verify walks u1 and u2 `bits` at a time from the top: `bits`
   doublings, then one add from the table when either digit isn't 0.

   Include after uECC.c, with V_CURVE and v_pub_native defined
   (uecc_variant.inc does both). */

#define WIN_NW 8   /* P-256: 8 words per coordinate */

static uECC_word_t* win_pt(uint32_t* table, int i)
{
    return (uECC_word_t*)table + i * 2 * WIN_NW;
}

/* k*P for k = 1..W-1 into table entries at[k * step] */
static void win_multiples(uint32_t* table, int W, int step, const uECC_word_t* p, uECC_Curve curve)
{
    int k;
    for (k = 1; k < W; k++) {
        uECC_word_t* out = win_pt(table, k * step);
        if (k == 1) {
            uECC_vli_set(out, p, WIN_NW);
            uECC_vli_set(out + WIN_NW, p + WIN_NW, WIN_NW);
        } else if (k == 2) {
            uECC_word_t* one = win_pt(table, step);
            uECC_vli_set(out, one, WIN_NW);
            uECC_vli_set(out + WIN_NW, one + WIN_NW, WIN_NW);
            affine_double_p256(out, out + WIN_NW, curve);
        } else {
            uECC_word_t* prev = win_pt(table, (k - 1) * step);
            affine_add_p256(out, out + WIN_NW, prev, prev + WIN_NW, p, p + WIN_NW, curve);
        }
    }
}

void bench_win_table(int bits, const uint8_t* pub, uint32_t* table)
{
    uECC_Curve curve = V_CURVE;
    int W = 1 << bits, a, b;
    uECC_word_t q[2 * WIN_NW];

    v_pub_native(pub, q);
    win_multiples(table, W, W, curve->G, curve);   /* a*G at T[a*W] */
    win_multiples(table, W, 1, q, curve);          /* b*Q at T[b]   */
    for (a = 1; a < W; a++)
        for (b = 1; b < W; b++) {
            uECC_word_t* out = win_pt(table, a * W + b);
            uECC_word_t* g = win_pt(table, a * W);
            uECC_word_t* qb = win_pt(table, b);
            affine_add_p256(out, out + WIN_NW, g, g + WIN_NW, qb, qb + WIN_NW, curve);
        }
}

/* bits pos .. pos+bits-1 of a 256-bit native scalar */
static int win_digit(const uECC_word_t* u, int pos, int bits)
{
    int d = 0, k;
    for (k = bits - 1; k >= 0; k--) {
        int bit = pos + k;
        d = (d << 1) | (bit < 256 ? (int)((u[bit >> 5] >> (bit & 31)) & 1) : 0);
    }
    return d;
}

int bench_win_verify(int bits, const uint8_t* hash, unsigned hlen, const uint8_t* sig,
                     const uint32_t* table)
{
    uECC_Curve curve = V_CURVE;
    uECC_word_t u1[uECC_MAX_WORDS], u2[uECC_MAX_WORDS], z[uECC_MAX_WORDS];
    uECC_word_t rx[uECC_MAX_WORDS], ry[uECC_MAX_WORDS];
    uECC_word_t tx[uECC_MAX_WORDS], ty[uECC_MAX_WORDS], tz[uECC_MAX_WORDS];
    uECC_word_t r[uECC_MAX_WORDS], s[uECC_MAX_WORDS];
    wordcount_t nw = curve->num_words;
    wordcount_t nn = BITS_TO_WORDS(curve->num_n_bits);
    int W = 1 << bits, digits = (256 + bits - 1) / bits, acc = 0, i, k;

    r[nn - 1] = 0;
    s[nn - 1] = 0;
    uECC_vli_bytesToNative(r, sig, curve->num_bytes);
    uECC_vli_bytesToNative(s, sig + curve->num_bytes, curve->num_bytes);
    if (uECC_vli_isZero(r, nw) || uECC_vli_isZero(s, nw))
        return 0;
    if (uECC_vli_cmp_unsafe(curve->n, r, nn) != 1 || uECC_vli_cmp_unsafe(curve->n, s, nn) != 1)
        return 0;

    /* u1 = hash/s mod n, u2 = r/s mod n */
    uECC_vli_modInv(z, s, curve->n, nn);
    u1[nn - 1] = 0;
    bits2int(u1, hash, hlen, curve);
    uECC_vli_modMult(u1, u1, z, curve->n, nn);
    uECC_vli_modMult(u2, r, z, curve->n, nn);

    for (i = digits - 1; i >= 0; i--) {
        int d1 = win_digit(u1, i * bits, bits);
        int d2 = win_digit(u2, i * bits, bits);
        if (acc)
            for (k = 0; k < bits; k++)
                curve->double_jacobian(rx, ry, z, curve);
        if (d1 | d2) {
            const uECC_word_t* pt = (const uECC_word_t*)table + (d1 * W + d2) * 2 * nw;
            if (!acc) {
                uECC_vli_set(rx, pt, nw);
                uECC_vli_set(ry, pt + nw, nw);
                uECC_vli_clear(z, nw);
                z[0] = 1;
                acc = 1;
            } else {   /* as uecc_verify_precomp: co-Z add of an affine point */
                uECC_vli_set(tx, pt, nw);
                uECC_vli_set(ty, pt + nw, nw);
                apply_z(tx, ty, z, curve);
                uECC_vli_modSub(tz, rx, tx, curve->p, nw);
                XYcZ_add(tx, ty, rx, ry, tx, curve);
                uECC_vli_modMult_fast(z, z, tz, curve);
            }
        }
    }
    if (!acc)
        return 0;

    uECC_vli_modInv(z, z, curve->p, nw);
    apply_z(rx, ry, z, curve);
    if (uECC_vli_cmp_unsafe(curve->n, rx, nn) != 1)
        uECC_vli_sub(rx, rx, curve->n, nn);
    return (int)uECC_vli_equal(rx, r, nw);
}
