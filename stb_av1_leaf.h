/*
 * stb_av1_leaf.h - first scalar intra leaf syntax integration
 *
 * The ordering follows dav1d 1.5.4 read_b()/decode_coefs(): after intra
 * syntax, select the maximum transform, optionally decode tx-size, then at
 * each transform leaf decode coefficient skip and (when required) transform
 * type.  Reconstruction is intentionally left to the next layer.
 */
#ifndef STB_AV1_LEAF_H
#define STB_AV1_LEAF_H

#include <string.h>

#ifndef STB_AV1_TXSTATE_H
#error "include stb_av1_txstate.h first"
#endif
#ifndef STB_AV1_STATE_H
#error "include stb_av1_state.h first"
#endif
#ifndef STB_AV1_COEF_H
#error "include stb_av1_coef.h first"
#endif

static const stbv_u8 stbv_av1_skip_ctx[5][5] = {
    { 1, 2, 2, 2, 3 },
    { 2, 4, 4, 4, 5 },
    { 2, 4, 4, 4, 5 },
    { 2, 4, 4, 4, 5 },
    { 3, 5, 5, 5, 6 }
};

/* Residual context is cul_level in bits 0..5 and dc-sign in bit 6.  Arrays
 * are frame-wide, indexed in 4x4 units. */
typedef struct stbv_av1_res_state {
    stbv_u8 *above;
    stbv_u8 *left;
    unsigned int above_n;
    unsigned int left_n;
} stbv_av1_res_state;

static void stbv_av1_res_state_init(stbv_av1_res_state *s,
                                    stbv_u8 *above, unsigned int above_n,
                                    stbv_u8 *left, unsigned int left_n)
{
    if (!s) return;
    s->above = above;
    s->left = left;
    s->above_n = above_n;
    s->left_n = left_n;
    if (above) memset(above, 0x40, above_n);
    if (left) memset(left, 0x40, left_n);
}

static unsigned stbv_av1_res_merge(const stbv_u8 *p, int n)
{
    unsigned v = 0;
    int i;
    for (i = 0; i < n; i++)
        v |= p[i];
    return v;
}

/* Skip context, following dav1d's get_skip_ctx().  chroma == 0: luma branch
 * (equal block/transform dims give ctx 0, otherwise merge the residual
 * contexts over the transform width/height and look up the table).  chroma
 * != 0: the caller passes the chroma block/transform dims in chroma 4x4
 * units and gets dav1d's 7 + not_one_blk*3 + ca + cl.  Coordinates are
 * absolute (frame-wide arrays). */
static int stbv_av1_get_skip_ctx(const stbv_av1_res_state *s,
                                 int bx4, int by4,
                                 int bw4, int bh4,
                                 int txw4, int txh4,
                                 int chroma)
{
    stbv_u64 la, ll;
    int i;

    if (!s) return 0;
    if (chroma) {
        int not_one_blk, ca, cl;
        la = 0;
        ll = 0;
        for (i = 0; i < txw4 && (unsigned int)(bx4 + i) < s->above_n; i++)
            la |= s->above[bx4 + i];
        for (i = 0; i < txh4 && (unsigned int)(by4 + i) < s->left_n; i++)
            ll |= s->left[by4 + i];
        not_one_blk = (bw4 != txw4) || (bh4 != txh4);
        ca = (int)(la != 0x40);
        cl = (int)(ll != 0x40);
        return 7 + not_one_blk * 3 + ca + cl;
    }
    if (bw4 == txw4 && bh4 == txh4)
        return 0;

    la = 0;
    ll = 0;
    for (i = 0; i < txw4 && (unsigned int)(bx4 + i) < s->above_n; i++)
        la |= s->above[bx4 + i];
    for (i = 0; i < txh4 && (unsigned int)(by4 + i) < s->left_n; i++)
        ll |= s->left[by4 + i];

    /* Collapse every context byte into the low byte, exactly like dav1d's
       MERGE_CTX (read N bytes, then OR-fold with >>16/>>8). */
    la |= la >> 32;
    la |= la >> 16;
    la |= la >> 8;
    ll |= ll >> 32;
    ll |= ll >> 16;
    ll |= ll >> 8;

    /* bit 6 is the DC-sign flag; skip context uses magnitude only. */
    la &= 0x3fU;
    ll &= 0x3fU;
    la = la > 4 ? 4 : la;
    ll = ll > 4 ? 4 : ll;
    return stbv_av1_skip_ctx[(int)la][(int)ll];
}

static void stbv_av1_res_mark(stbv_av1_res_state *s,
                              int bx4, int by4, int txw4, int txh4,
                              stbv_u8 res_ctx)
{
    int i;
    if (!s) return;
    for (i = 0; i < txw4 && (unsigned int)(bx4 + i) < s->above_n; i++)
        s->above[bx4 + i] = res_ctx;
    for (i = 0; i < txh4 && (unsigned int)(by4 + i) < s->left_n; i++)
        s->left[by4 + i] = res_ctx;
}

typedef struct stbv_av1_leaf_state_arrays {
    stbv_u8 *above_mode;
    unsigned int above_mode_n;
    stbv_u8 *left_mode;
    unsigned int left_mode_n;
    stbv_u8 *above_tx;
    unsigned int above_tx_n;
    stbv_u8 *left_tx;
    unsigned int left_tx_n;
    stbv_u8 *above_res;
    unsigned int above_res_n;
    stbv_u8 *left_res;
    unsigned int left_res_n;
    stbv_u8 *above_skip;
    unsigned int above_skip_n;
    stbv_u8 *left_skip;
    unsigned int left_skip_n;
    stbv_u8 *above_cre[2];
    unsigned int above_cre_n[2];
    stbv_u8 *left_cre[2];
    unsigned int left_cre_n[2];
    stbv_u8 *above_pal_sz;
    unsigned int above_pal_sz_n;
    stbv_u8 *left_pal_sz;
    unsigned int left_pal_sz_n;
    stbv_u8 *above_pal_uv;
    unsigned int above_pal_uv_n;
    stbv_u8 *left_pal_uv;
    unsigned int left_pal_uv_n;
    stbv_u16 *above_pal[2];
    unsigned int above_pal_n;
    stbv_u16 *left_pal[2];
    unsigned int left_pal_n;
} stbv_av1_leaf_state_arrays;

typedef struct stbv_av1_leaf_state {
    struct stb_av1_intra_state intra;
    stbv_av1_tx_state tx;
    stbv_av1_res_state res;
    stbv_av1_res_state cres[2];
    stbv_u8 *above_skip;
    stbv_u8 *left_skip;
    unsigned int above_skip_n;
    unsigned int left_skip_n;
    int cdef_sb_x;
    int cdef_sb_y;
    int cdef_idx[4];
    stbv_u8 *above_pal_sz;
    stbv_u8 *left_pal_sz;
    unsigned int above_pal_sz_n;
    unsigned int left_pal_sz_n;
    stbv_u8 *above_pal_uv;
    stbv_u8 *left_pal_uv;
    unsigned int above_pal_uv_n;
    unsigned int left_pal_uv_n;
    stbv_u16 *above_pal[2];
    stbv_u16 *left_pal[2];
    unsigned int above_pal_n;
    unsigned int left_pal_n;
    stbv_u16 pal_y[8];
    stbv_u16 pal_u[8];
    stbv_u16 pal_v[8];
    stbv_u16 cache[8];
    stbv_u16 used_cache[8];
    stbv_u8 pal_tmp[64 * 64];
    stbv_u8 pal_order[64][8];
    stbv_u8 pal_ctxs[64];
    int pal_sz_y;
    int pal_sz_uv;
} stbv_av1_leaf_state;

static void stbv_av1_leaf_state_init(stbv_av1_leaf_state *s,
                                     const stbv_av1_leaf_state_arrays *a)
{
    int pl;
    if (!s) return;
    if (!a) {
        memset(s, 0, sizeof(*s));
        s->cdef_sb_x = s->cdef_sb_y = -1;
        s->cdef_idx[0] = s->cdef_idx[1] = -1;
        s->cdef_idx[2] = s->cdef_idx[3] = -1;
        return;
    }
    stb_av1_intra_state_init(&s->intra, a->above_mode, a->above_mode_n,
                             a->left_mode, a->left_mode_n);
    stbv_av1_tx_state_init(&s->tx, a->above_tx, a->above_tx_n,
                           a->left_tx, a->left_tx_n);
    stbv_av1_res_state_init(&s->res, a->above_res, a->above_res_n,
                            a->left_res, a->left_res_n);
    for (pl = 0; pl < 2; pl++)
        stbv_av1_res_state_init(&s->cres[pl], a->above_cre[pl],
                                a->above_cre_n[pl], a->left_cre[pl],
                                a->left_cre_n[pl]);
    s->above_skip = a->above_skip;
    s->left_skip = a->left_skip;
    s->above_skip_n = a->above_skip_n;
    s->left_skip_n = a->left_skip_n;
    s->above_pal_sz = a->above_pal_sz;
    s->left_pal_sz = a->left_pal_sz;
    s->above_pal_sz_n = a->above_pal_sz_n;
    s->left_pal_sz_n = a->left_pal_sz_n;
    s->above_pal_uv = a->above_pal_uv;
    s->left_pal_uv = a->left_pal_uv;
    s->above_pal_uv_n = a->above_pal_uv_n;
    s->left_pal_uv_n = a->left_pal_uv_n;
    s->above_pal[0] = a->above_pal[0];
    s->above_pal[1] = a->above_pal[1];
    s->left_pal[0] = a->left_pal[0];
    s->left_pal[1] = a->left_pal[1];
    s->above_pal_n = a->above_pal_n;
    s->left_pal_n = a->left_pal_n;
    if (a->above_pal_sz) memset(a->above_pal_sz, 0, a->above_pal_sz_n);
    if (a->left_pal_sz) memset(a->left_pal_sz, 0, a->left_pal_sz_n);
    if (a->above_pal_uv) memset(a->above_pal_uv, 0, a->above_pal_uv_n);
    if (a->left_pal_uv) memset(a->left_pal_uv, 0, a->left_pal_uv_n);
    if (a->above_pal[0])
        memset(a->above_pal[0], 0, a->above_pal_n * 8U * sizeof(stbv_u16));
    if (a->above_pal[1])
        memset(a->above_pal[1], 0, a->above_pal_n * 8U * sizeof(stbv_u16));
    if (a->left_pal[0])
        memset(a->left_pal[0], 0, a->left_pal_n * 8U * sizeof(stbv_u16));
    if (a->left_pal[1])
        memset(a->left_pal[1], 0, a->left_pal_n * 8U * sizeof(stbv_u16));
    s->cdef_sb_x = s->cdef_sb_y = -1;
    s->cdef_idx[0] = s->cdef_idx[1] = -1;
    s->cdef_idx[2] = s->cdef_idx[3] = -1;
    if (a->above_skip) memset(a->above_skip, 0, a->above_skip_n);
    if (a->left_skip) memset(a->left_skip, 0, a->left_skip_n);
}

/* dav1d reset_context() resets only the LEFT contexts at the start of each
 * superblock row; the above contexts persist across rows (they are reset
 * once per frame). */
static void stbv_av1_leaf_state_reset_row(stbv_av1_leaf_state *s)
{
    int pl;
    if (!s) return;
    stbv_av1_tx_state_reset_row(&s->tx);
    if (s->res.left) memset(s->res.left, 0x40, s->res.left_n);
    for (pl = 0; pl < 2; pl++)
        if (s->cres[pl].left) memset(s->cres[pl].left, 0x40, s->cres[pl].left_n);
    if (s->left_skip) memset(s->left_skip, 0, s->left_skip_n);
    if (s->intra.left_mode)
        memset(s->intra.left_mode, STBV_AV1_INTRA_DC,
               (size_t)s->intra.left_count);
    if (s->left_pal_sz) memset(s->left_pal_sz, 0, s->left_pal_sz_n);
    if (s->left_pal_uv) memset(s->left_pal_uv, 0, s->left_pal_uv_n);
    if (s->left_pal[0])
        memset(s->left_pal[0], 0, s->left_pal_n * 8U * sizeof(stbv_u16));
    if (s->left_pal[1])
        memset(s->left_pal[1], 0, s->left_pal_n * 8U * sizeof(stbv_u16));
}

typedef struct stbv_av1_leaf_tx_result {
    int x4, y4, tx;
    int skipped;
    int txtp;
    int eob;
    int skip_ctx;
} stbv_av1_leaf_tx_result;

typedef struct stbv_av1_leaf_decode_ctx {
    struct stb_av1_msac *msac;
    stbv_av1_cdf *cdf;
    stbv_av1_leaf_state *state;
    const struct stb_av1_framehdr *frame;
    const struct stb_av1_intra_block *intra;
    int bs;
    int bw4, bh4;
    int cbw4, cbh4;
    int lossless;
    int qidx;
    int y_mode_nofilt;
    int block_skip;
    int reduced_txtp_set;
} stbv_av1_leaf_decode_ctx;

/* Per-transform coefficient syntax for one plane (dav1d decode_coefs +
 * read_coef_blocks): per-transform skip, then transform type (gated), then
 * coefficients.  x4/y4 are plane-local 4x4 coordinates and bw4/bh4 the
 * plane-local block dims in 4x4 units.  When out is non-NULL the first
 * (luma) transform's results are recorded there. */
static int stbv_av1_leaf_tx_plane(struct stb_av1_msac *msac,
                                  stbv_av1_cdf *cdf,
                                  const stbv_av1_leaf_decode_ctx *c,
                                  int x4, int y4, int tx, int chroma,
                                  stbv_av1_res_state *rs,
                                  int bw4, int bh4,
                                  stbv_av1_leaf_tx_result *out)
{
    int txw4 = stbv_av1_tx_dims[tx].w;
    int txh4 = stbv_av1_tx_dims[tx].h;
    int sctx, txtp, max;
    unsigned skip;
    int is_chroma = chroma != 0; /* dav1d: chroma = !!plane */

    sctx = stbv_av1_get_skip_ctx(rs, x4, y4, bw4, bh4, txw4, txh4,
                                 is_chroma);
    fprintf(stderr, "SKP ch=%d ctx=%d sctx=%d off=%d pre_cdf=%u pre_cnt=%u r=%u dif=%llu cnt=%d\n",
            chroma, stbv_av1_tx_dims[tx].ctx, sctx,
            stbv_av1_tx_dims[tx].ctx * 26 + sctx * 2,
            cdf->coef[stbv_av1_tx_dims[tx].ctx * 26 + sctx * 2],
            cdf->coef[stbv_av1_tx_dims[tx].ctx * 26 + sctx * 2 + 1],
            msac->rng, (unsigned long long)msac->dif, msac->cnt);
    skip = stb_av1_msac_bool_adapt(
        msac, cdf->coef + stbv_av1_tx_dims[tx].ctx * 26 + sctx * 2);
    if (!skip) {
        max = stbv_av1_tx_dims[tx].max;
        if (c->lossless)
            txtp = STBV_AV1_TX_WHT_WHT;
        else if (max + 1 >= STBV_AV1_TX_64X64) /* max + intra >= TX_64X64 */
            txtp = STBV_AV1_TX_DCT_DCT;
        else if (is_chroma)
            /* inferred from the intra UV mode, never coded */
            txtp = stbv_av1_txtp_from_uvmode[c->intra ? c->intra->uv_mode : 0];
        else if (!c->qidx)
            txtp = STBV_AV1_TX_DCT_DCT;
        else
            txtp = stbv_av1_decode_intra_txtp(msac, cdf,
                stbv_av1_tx_dims[tx].min, c->y_mode_nofilt,
                c->reduced_txtp_set);
    } else {
        /* dav1d: *txtp = lossless * WHT_WHT */
        if (c->lossless)
            txtp = STBV_AV1_TX_WHT_WHT;
    }

    if (out) {
        out->x4 = x4;
        out->y4 = y4;
        out->tx = tx;
        out->skipped = (int)skip;
        out->txtp = txtp;
        out->eob = 0;
        out->skip_ctx = sctx;
    }
    fprintf(stderr, "L8 ch=%d x=%d y=%d tx=%d skip=%d sctx=%d r=%u dif=%llu cdf=%u cnt=%u\n",
            chroma, x4, y4, tx, (int)skip, sctx, msac->rng,
            (unsigned long long)msac->dif,
            cdf->coef[stbv_av1_tx_dims[tx].ctx * 26 + sctx * 2],
            cdf->coef[stbv_av1_tx_dims[tx].ctx * 26 + sctx * 2 + 1]);

    if (skip) {
        /* A skipped transform has the fixed residual context 0x40. */
        stbv_av1_res_mark(rs, x4, y4, txw4, txh4, (stbv_u8)0x40);
        return 0;
    }

    {
        stbv_i32 cf[64 * 64];
        int txclass = stbv_av1_tx_class(txtp);
        int eob;
        stbv_u8 res_ctx;
        int dc_sign_ctx = 0;
        int s = 0;
        int i;

        /* dav1d get_dc_sign_ctx: sum res_ctx >> 6 over the transform width
         * for the above row and the transform HEIGHT for the left column,
         * then subtract w4 and h4 and map to 0..2 via (s != 0) + (s > 0). */
        for (i = 0; i < txw4; i++) {
            if ((unsigned int)(x4 + i) < rs->above_n)
                s += rs->above[x4 + i] >> 6;
        }
        for (i = 0; i < txh4; i++) {
            if ((unsigned int)(y4 + i) < rs->left_n)
                s += rs->left[y4 + i] >> 6;
        }
        s -= txw4;
        s -= txh4;
        dc_sign_ctx = (s != 0) + (s > 0);

        /* This first integration pass validates coefficient syntax and MSAC
           consumption.  Quantization/reconstruction is still supplied by
           the caller in the block layer. */
        eob = stbv_av1_decode_coeffs_square(msac, cdf, tx, is_chroma, txclass,
                                             1, 1, 0, sctx, dc_sign_ctx, cf,
                                             &res_ctx);
        if (eob < 0)
            return -2;
        if (out)
            out->eob = eob;
        stbv_av1_res_mark(rs, x4, y4, txw4, txh4, res_ctx);
    }
    return 0;
}

static int stbv_av1_ulog2(unsigned int v)
{
    int n = 0;
    while (v >>= 1) n++;
    return n;
}

/* Palette size + colors for one plane (dav1d dav1d_read_pal_plane).
 * The cache is the merge of the above/left neighbor palettes; entries are
 * reused via equi-probability flags, the rest is delta coded. */
static int stbv_av1_palette_read_plane(struct stb_av1_msac *msac,
                                       stbv_av1_cdf *cdf,
                                       stbv_av1_leaf_state *state,
                                       int pl, int sz_ctx, int bx4, int by4,
                                       int bpc, stbv_u16 *pal_out,
                                       int *pal_sz_out)
{
    int pal_sz, i, l_cache, a_cache, n_cache = 0, n_used = 0, prev;
    int bits, max;
    stbv_u16 *l, *a;
    stbv_u16 *cache = state->cache;
    stbv_u16 *used = state->used_cache;

    pal_sz = (int)stb_av1_msac_symbol(msac,
                                      cdf->pal_sz + (pl * 7 + sz_ctx) * 8,
                                      6) + 2;
    fprintf(stderr, "PAL sz=%d pl=%d szctx=%d pre_cdf=%u/%u/%u/%u/%u/%u r=%u\n",
            pal_sz, pl, sz_ctx,
            cdf->pal_sz[(pl * 7 + sz_ctx) * 8],
            cdf->pal_sz[(pl * 7 + sz_ctx) * 8 + 1],
            cdf->pal_sz[(pl * 7 + sz_ctx) * 8 + 2],
            cdf->pal_sz[(pl * 7 + sz_ctx) * 8 + 3],
            cdf->pal_sz[(pl * 7 + sz_ctx) * 8 + 4],
            cdf->pal_sz[(pl * 7 + sz_ctx) * 8 + 5], msac->rng);
    if (pal_sz > 8) return -1;
    fprintf(stderr, "PAL sz=%d pl=%d szctx=%d post r=%u\n",
            pal_sz, pl, sz_ctx, msac->rng);
    l_cache = pl ? (state->left_pal_uv &&
                    (unsigned)by4 < state->left_pal_uv_n ?
                    state->left_pal_uv[by4] : 0)
                 : (state->left_pal_sz &&
                    (unsigned)by4 < state->left_pal_sz_n ?
                    state->left_pal_sz[by4] : 0);
    a_cache = (by4 & 15) ? (pl ? (state->above_pal_uv &&
                                  (unsigned)bx4 < state->above_pal_uv_n ?
                                  state->above_pal_uv[bx4] : 0)
                               : (state->above_pal_sz &&
                                  (unsigned)bx4 < state->above_pal_sz_n ?
                                  state->above_pal_sz[bx4] : 0))
                         : 0;
    l = (state->left_pal[pl] && (unsigned)by4 < state->left_pal_n) ?
        state->left_pal[pl] + by4 * 8 : NULL;
    a = (state->above_pal[pl] && (unsigned)bx4 < state->above_pal_n) ?
        state->above_pal[pl] + bx4 * 8 : NULL;

    while (l_cache && a_cache) {
        if (*l < *a) {
            if (!n_cache || cache[n_cache - 1] != *l)
                cache[n_cache++] = *l;
            l++;
            l_cache--;
        } else {
            if (*a == *l) {
                l++;
                l_cache--;
            }
            if (!n_cache || cache[n_cache - 1] != *a)
                cache[n_cache++] = *a;
            a++;
            a_cache--;
        }
    }
    if (l_cache) {
        do {
            if (!n_cache || cache[n_cache - 1] != *l)
                cache[n_cache++] = *l;
            l++;
        } while (--l_cache > 0);
    } else if (a_cache) {
        do {
            if (!n_cache || cache[n_cache - 1] != *a)
                cache[n_cache++] = *a;
            a++;
        } while (--a_cache > 0);
    }

    i = 0;
    {
        int n;
        for (n = 0; n < n_cache && i < pal_sz; n++)
            if (stb_av1_msac_bool_equi(msac))
                used[i++] = cache[n];
    }
    n_used = i;

    if (i < pal_sz) {
        int n, m;
        prev = (int)stb_av1_msac_bools(msac, (unsigned)bpc);
        pal_out[i++] = (stbv_u16)prev;
        fprintf(stderr, "PALC first=%d r=%u\n", prev, msac->rng);
        if (i < pal_sz) {
            bits = bpc - 3 + (int)stb_av1_msac_bools(msac, 2);
            max = (1 << bpc) - 1;
            fprintf(stderr, "PALC bits=%d r=%u\n", bits, msac->rng);
            do {
                int delta = (int)stb_av1_msac_bools(msac, (unsigned)bits);
                prev += delta + (pl ? 0 : 1);
                if (prev > max) prev = max;
                pal_out[i++] = (stbv_u16)prev;
                fprintf(stderr, "PALC delta=%d prev=%d r=%u\n", delta, prev,
                        msac->rng);
                if (prev + (pl ? 0 : 1) >= max) {
                    for (; i < pal_sz; i++)
                        pal_out[i] = (stbv_u16)max;
                    break;
                }
                bits = bits < 1 + stbv_av1_ulog2((unsigned)(max - prev -
                                                  (pl ? 0 : 1))) ?
                       bits : 1 + stbv_av1_ulog2((unsigned)(max - prev -
                                                  (pl ? 0 : 1)));
            } while (i < pal_sz);
        }
        n = 0;
        m = n_used;
        for (i = 0; i < pal_sz; i++) {
            if (n < n_used && (m >= pal_sz || used[n] <= pal_out[m]))
                pal_out[i] = used[n++];
            else
                pal_out[i] = pal_out[m++];
        }
    } else {
        for (i = 0; i < n_used; i++)
            pal_out[i] = used[i];
    }

    if (pal_sz_out) *pal_sz_out = pal_sz;
    return 0;
}

/* V plane of the UV palette (dav1d read_pal_uv's V pal coding). */
static void stbv_av1_palette_read_uv_v(struct stb_av1_msac *msac, int bpc,
                                       int pal_sz, stbv_u16 *pal_v)
{
    int i, bits, prev, delta, max;
    if (stb_av1_msac_bool_equi(msac)) {
        bits = bpc - 4 + (int)stb_av1_msac_bools(msac, 2);
        prev = (int)stb_av1_msac_bools(msac, (unsigned)bpc);
        pal_v[0] = (stbv_u16)prev;
        max = (1 << bpc) - 1;
        for (i = 1; i < pal_sz; i++) {
            delta = (int)stb_av1_msac_bools(msac, (unsigned)bits);
            if (delta && stb_av1_msac_bool_equi(msac))
                delta = -delta;
            prev = (prev + delta) & max;
            pal_v[i] = (stbv_u16)prev;
        }
    } else {
        for (i = 0; i < pal_sz; i++)
            pal_v[i] = stb_av1_msac_bools(msac, (unsigned)bpc);
    }
}

/* Per-cell palette order/context for one wave-front diagonal (dav1d
 * order_palette). */
static void stbv_av1_palette_order(const stbv_u8 *pal_idx, int stride,
                                   int i, int first, int last,
                                   stbv_u8 (*order)[8], stbv_u8 *ctx)
{
    int have_top = i > first;
    int j, n;

    pal_idx += first + (i - first) * stride;
    for (j = first, n = 0; j >= last; have_top = 1, j--, n++,
         pal_idx += stride - 1) {
        int have_left = j > 0;
        unsigned mask = 0;
        int o_idx = 0;
#define STBV_PAL_ADD(v_in) do { \
            int v = (v_in); \
            order[n][o_idx++] = (stbv_u8)v; \
            mask |= 1U << v; \
        } while (0)

        if (!have_left) {
            ctx[n] = 0;
            STBV_PAL_ADD(pal_idx[-stride]);
        } else if (!have_top) {
            ctx[n] = 0;
            STBV_PAL_ADD(pal_idx[-1]);
        } else {
            int l = pal_idx[-1], t = pal_idx[-stride];
            int tl = pal_idx[-(stride + 1)];
            int same_t_l = t == l;
            int same_t_tl = t == tl;
            int same_l_tl = l == tl;
            int same_all = same_t_l & same_t_tl & same_l_tl;

            if (same_all) {
                ctx[n] = 4;
                STBV_PAL_ADD(t);
            } else if (same_t_l) {
                ctx[n] = 3;
                STBV_PAL_ADD(t);
                STBV_PAL_ADD(tl);
            } else if (same_t_tl | same_l_tl) {
                ctx[n] = 2;
                STBV_PAL_ADD(tl);
                STBV_PAL_ADD(same_t_tl ? l : t);
            } else {
                ctx[n] = 1;
                STBV_PAL_ADD(t < l ? t : l);
                STBV_PAL_ADD(t < l ? l : t);
                STBV_PAL_ADD(tl);
            }
        }
        {
            unsigned m;
            int bit;
            for (m = 1, bit = 0; m < 0x100; m <<= 1, bit++)
                if (!(mask & m))
                    order[n][o_idx++] = (stbv_u8)bit;
        }
#undef STBV_PAL_ADD
    }
}

/* Palette index map (dav1d read_pal_indices). */
static int stbv_av1_palette_indices(struct stb_av1_msac *msac,
                                    stbv_av1_cdf *cdf,
                                    int pl, int pal_sz,
                                    int bw4, int bh4,
                                    stbv_u8 *pal_tmp,
                                    stbv_u8 (*order)[8], stbv_u8 *ctx)
{
    int stride = bw4 * 4;
    int wpx = bw4 * 4, hpx = bh4 * 4;
    int i, j, m, first, last;
    stbv_u16 *color_map_cdf;

    pal_tmp[0] = stb_av1_msac_uniform(msac, (unsigned)pal_sz);
    fprintf(stderr, "PALI uniform=%d r=%u\n", pal_tmp[0], msac->rng);
    color_map_cdf = cdf->color_map + (pl * 7 + (pal_sz - 2)) * 40;
    for (i = 1; i < wpx + hpx - 1; i++) {
        first = i < wpx - 1 ? i : wpx - 1;
        last = i - hpx + 1 > 0 ? i - hpx + 1 : 0;
        stbv_av1_palette_order(pal_tmp, stride, i, first, last, order, ctx);
        for (j = first, m = 0; j >= last; j--, m++) {
            int color_idx = (int)stb_av1_msac_symbol(
                msac, color_map_cdf + ctx[m] * 8, (size_t)(pal_sz - 1));
            if (i < 3)
                fprintf(stderr, "PALI i=%d j=%d ctx=%d cdf=%u ci=%d r=%u\n",
                        i, j, ctx[m], color_map_cdf[ctx[m] * 8], color_idx,
                        msac->rng);
            pal_tmp[(i - j) * stride + j] = order[m][color_idx];
        }
    }
    return 0;
}

static int stbv_av1_decode_leaf_syntax(struct stb_av1_msac *msac,
                                       stbv_av1_cdf *cdf,
                                       stbv_av1_leaf_state *state,
                                       const struct stb_av1_seqhdr *seq,
                                       const struct stb_av1_framehdr *frame,
                                       int bs, int bx4, int by4,
                                       stbv_av1_leaf_tx_result *out)
{
    struct stb_av1_intra_block intra;
    stbv_av1_leaf_decode_ctx c;
    int bw4, bh4, max_tx, uv_tx, tx0;
    int layout, ss_hor, ss_ver, sb_step;
    int cfl_allowed, cbw4, cbh4, has_chroma;
    int lossless, qidx;
    int y_mode_nofilt, i;
    unsigned block_skip;

    if (!msac || !cdf || !state || bs < 0 || bs >= STBV_AV1_N_BS_SIZES)
        return -1;
    bw4 = stbv_av1_block_dimensions[bs][0];
    bh4 = stbv_av1_block_dimensions[bs][1];
    if (!bw4 || !bh4)
        return -2;

    layout = seq ? (int)seq->layout : STB_AV1_LAYOUT_I444;
    fprintf(stderr, "L1 bs=%d bx=%d by=%d r=%u\n", bs, bx4, by4,
            msac->rng);
    ss_hor = layout == STB_AV1_LAYOUT_I420 || layout == STB_AV1_LAYOUT_I422;
    ss_ver = layout == STB_AV1_LAYOUT_I420;
    sb_step = (seq && seq->sb128) ? 32 : 16;
    lossless = frame ? (int)frame->segmentation.lossless[0] : 0;
    qidx = frame ? (int)frame->segmentation.qidx[0] : 0;
    cbw4 = (bw4 + ss_hor) >> ss_hor;
    cbh4 = (bh4 + ss_ver) >> ss_ver;
    has_chroma = layout != STB_AV1_LAYOUT_I400 &&
                 (bw4 > ss_hor || (bx4 & 1)) && (bh4 > ss_ver || (by4 & 1));

    /* Segment ids are not implemented; no coded bitstream in the sample set
     * uses them (steam: segmentation disabled). */
    if (frame && frame->segmentation.enabled && frame->segmentation.update_map)
        return -5;

    /* Block-level skip, decoded before intra modes (dav1d decode_b). */
    {
        int sctx = 0;
        if (state->above_skip && (unsigned int)bx4 < state->above_skip_n &&
            state->above_skip[bx4])
            sctx += 1;
        if (state->left_skip && (unsigned int)by4 < state->left_skip_n &&
            state->left_skip[by4])
            sctx += 1;
        block_skip = stb_av1_msac_bool_adapt(msac, cdf->skip + sctx * 2);
        fprintf(stderr, "L2 skip r=%u\n", msac->rng);
        for (i = 0; i < bw4 && (unsigned int)(bx4 + i) < state->above_skip_n; i++)
            state->above_skip[bx4 + i] = (stbv_u8)block_skip;
        for (i = 0; i < bh4 && (unsigned int)(by4 + i) < state->left_skip_n; i++)
            state->left_skip[by4 + i] = (stbv_u8)block_skip;
    }

    /* cdef index, once per superblock, only when the block is not skipped.
     * The slot state resets at each superblock start (dav1d decode_sb). */
    if (frame) {
        int sbx = bx4 & ~(sb_step - 1);
        int sby = by4 & ~(sb_step - 1);
        int idx;
        if (state->cdef_sb_x != sbx || state->cdef_sb_y != sby) {
            state->cdef_sb_x = sbx;
            state->cdef_sb_y = sby;
            state->cdef_idx[0] = state->cdef_idx[1] = -1;
            state->cdef_idx[2] = state->cdef_idx[3] = -1;
        }
        idx = seq && seq->sb128 ? ((bx4 & 16) >> 4) + ((by4 & 16) >> 3) : 0;
        if (!block_skip && state->cdef_idx[idx] == -1) {
            int v;
            fprintf(stderr, "L3 cdef pre r=%u d=%016llx cnt=%d\n", msac->rng,
                    (unsigned long long)msac->dif, msac->cnt);
            v = stb_av1_msac_bools(msac, frame->cdef.n_bits);
            fprintf(stderr, "L3 cdef post r=%u d=%016llx cnt=%d\n", msac->rng,
                    (unsigned long long)msac->dif, msac->cnt);
            state->cdef_idx[idx] = v;
            if (bw4 > 16) state->cdef_idx[idx + 1] = v;
            if (bh4 > 16) state->cdef_idx[idx + 2] = v;
            if (bw4 == 32 && bh4 == 32) state->cdef_idx[idx + 3] = v;
        }
    }

    /* delta-q/lf at superblock origin; needs the delta_q CDFs which are not
     * part of this integration pass. */
    if (frame && frame->delta_q_present &&
        !((bx4 | by4) & (sb_step - 1)))
        return -6;

    /* Intra flag: key frames without intrabc need no symbol. */
    if (frame && frame->allow_intrabc)
        return -6;

    cfl_allowed = lossless ? (cbw4 == 1 && cbh4 == 1) :
        !!(STBV_AV1_CFL_ALLOWED_MASK & (1U << bs));
    if (stb_av1_intra_state_decode_leaf(msac, cdf, &state->intra,
                                        bx4, by4, bs, cfl_allowed, &intra))
        return -3;
    fprintf(stderr, "L4 intra r=%u dif=%llu\n", msac->rng,
            (unsigned long long)msac->dif);

    /* Palette: presence bools, size, colors and index map (dav1d decode.c
     * 1126-1193 + recon_tmpl read_pal_plane/read_pal_uv/read_pal_indices). */
    state->pal_sz_y = 0;
    state->pal_sz_uv = 0;
    if (frame && frame->allow_screen_content_tools &&
        (bw4 > bh4 ? bw4 : bh4) <= 16 && bw4 + bh4 >= 4) {
        int sz_ctx = stbv_av1_block_dimensions[bs][2] +
                     stbv_av1_block_dimensions[bs][3] - 2;
        int bpc = (seq && seq->hbd) ? 10 : 8;
        if (intra.y_mode == STBV_AV1_INTRA_DC) {
            int pal_ctx = 0;
            if (state->above_pal_sz && (unsigned)bx4 < state->above_pal_sz_n &&
                state->above_pal_sz[bx4] > 0)
                pal_ctx++;
            if (state->left_pal_sz && (unsigned)by4 < state->left_pal_sz_n &&
                state->left_pal_sz[by4] > 0)
                pal_ctx++;
            if (stb_av1_msac_bool_adapt(msac,
                                        cdf->pal_y + sz_ctx * 6 + pal_ctx * 2)) {
                if (stbv_av1_palette_read_plane(msac, cdf, state, 0, sz_ctx,
                                                bx4, by4, bpc, state->pal_y,
                                                &state->pal_sz_y))
                    return -7;
                if (stbv_av1_palette_indices(msac, cdf, 0, state->pal_sz_y,
                                             bw4, bh4, state->pal_tmp,
                                             state->pal_order,
                                             state->pal_ctxs))
                    return -7;
            }
            fprintf(stderr, "L5 ypal r=%u\n", msac->rng);
        }
        if (has_chroma && intra.uv_mode == STBV_AV1_INTRA_DC) {
            int pal_ctx = state->pal_sz_y > 0;
            if (stb_av1_msac_bool_adapt(msac, cdf->pal_uv + pal_ctx * 2)) {
                if (stbv_av1_palette_read_plane(msac, cdf, state, 1, sz_ctx,
                                                bx4, by4, bpc, state->pal_u,
                                                &state->pal_sz_uv))
                    return -8;
                stbv_av1_palette_read_uv_v(msac, bpc, state->pal_sz_uv,
                                           state->pal_v);
                if (stbv_av1_palette_indices(msac, cdf, 1, state->pal_sz_uv,
                                             cbw4, cbh4, state->pal_tmp,
                                             state->pal_order,
                                             state->pal_ctxs))
                    return -8;
            }
            fprintf(stderr, "L5b uvpal r=%u\n", msac->rng);
        }
    }

    /* Filter-intra bool (dav1d decode.c, after the palette bools). */
    if (seq && seq->filter_intra && intra.y_mode == STBV_AV1_INTRA_DC &&
        !state->pal_sz_y &&
        stbv_av1_block_dimensions[bs][2] <= 3 &&
        stbv_av1_block_dimensions[bs][3] <= 3) {
        if (stb_av1_msac_bool_adapt(msac, cdf->use_filter_intra + bs * 2)) {
            intra.y_mode = STBV_AV1_INTRA_FILTER;
            intra.y_angle = (int)stb_av1_msac_symbol(msac, cdf->filter_intra, 4);
        }
    }
    fprintf(stderr, "L6 filt r=%u dif=%llu\n", msac->rng,
            (unsigned long long)msac->dif);

    /* dav1d stores y_mode_nofilt in the neighbour mode maps (set_ctx):
     * FILTER_PRED maps to DC_PRED, NOT to the filter angle's mode. */
    y_mode_nofilt = intra.y_mode == STBV_AV1_INTRA_FILTER ?
        STBV_AV1_INTRA_DC : intra.y_mode;
    if (state->intra.above_mode) {
        for (i = 0; i < bw4 && (unsigned)(bx4 + i) < state->intra.above_count; i++)
            state->intra.above_mode[bx4 + i] = (stbv_u8)y_mode_nofilt;
    }
    if (state->intra.left_mode) {
        for (i = 0; i < bh4 && (unsigned)(by4 + i) < state->intra.left_count; i++)
            state->intra.left_mode[by4 + i] = (stbv_u8)y_mode_nofilt;
        fprintf(stderr, "MODE-W bx4=%d by4=%d bh4=%d nofilt=%d lm8=%d lm9=%d\n",
                bx4, by4, bh4, y_mode_nofilt, state->intra.left_mode[8],
                state->intra.left_mode[9]);
    }

    /* Palette neighbour state (dav1d set_ctx: pal_sz maps + al_pal copies;
     * the UV palette sizes use luma coordinates). */
    if (state->above_pal_sz) {
        for (i = 0; i < bw4 && (unsigned)(bx4 + i) < state->above_pal_sz_n; i++)
            state->above_pal_sz[bx4 + i] = (stbv_u8)state->pal_sz_y;
    }
    if (state->left_pal_sz) {
        for (i = 0; i < bh4 && (unsigned)(by4 + i) < state->left_pal_sz_n; i++)
            state->left_pal_sz[by4 + i] = (stbv_u8)state->pal_sz_y;
    }
    if (state->above_pal_uv) {
        for (i = 0; i < bw4 && (unsigned)(bx4 + i) < state->above_pal_uv_n; i++)
            state->above_pal_uv[bx4 + i] =
                (stbv_u8)(has_chroma ? state->pal_sz_uv : 0);
    }
    if (state->left_pal_uv) {
        for (i = 0; i < bh4 && (unsigned)(by4 + i) < state->left_pal_uv_n; i++)
            state->left_pal_uv[by4 + i] =
                (stbv_u8)(has_chroma ? state->pal_sz_uv : 0);
    }
    if (state->pal_sz_y && state->above_pal[0]) {
        for (i = 0; i < bw4 && (unsigned)(bx4 + i) < state->above_pal_n; i++)
            memcpy(state->above_pal[0] + (bx4 + i) * 8, state->pal_y,
                   8 * sizeof(stbv_u16));
    }
    if (state->pal_sz_y && state->left_pal[0]) {
        for (i = 0; i < bh4 && (unsigned)(by4 + i) < state->left_pal_n; i++)
            memcpy(state->left_pal[0] + (by4 + i) * 8, state->pal_y,
                   8 * sizeof(stbv_u16));
    }
    if (has_chroma && state->pal_sz_uv && state->above_pal[1]) {
        for (i = 0; i < bw4 && (unsigned)(bx4 + i) < state->above_pal_n; i++)
            memcpy(state->above_pal[1] + (bx4 + i) * 8, state->pal_u,
                   8 * sizeof(stbv_u16));
    }
    if (has_chroma && state->pal_sz_uv && state->left_pal[1]) {
        for (i = 0; i < bh4 && (unsigned)(by4 + i) < state->left_pal_n; i++)
            memcpy(state->left_pal[1] + (by4 + i) * 8, state->pal_u,
                   8 * sizeof(stbv_u16));
    }

    /* Transform size.  dav1d: lossless blocks are forced to TX_4X4; the
     * maximum otherwise comes from max_txfm_size_for_bs[bs][plane]; with
     * TX_SWITCHABLE a tx-size symbol is coded when max > TX_4X4. */
    if (lossless) {
        tx0 = STBV_AV1_TX_4X4;
        uv_tx = STBV_AV1_TX_4X4;
        max_tx = STBV_AV1_TX_4X4;
    } else {
        tx0 = stbv_av1_max_tx_for_bs[bs][0];
        uv_tx = stbv_av1_max_tx_for_bs[bs][layout];
        max_tx = tx0;
        if (frame && frame->txfm_mode == 1 &&
            stbv_av1_tx_dims[max_tx].max > STBV_AV1_TX_4X4)
            tx0 = stbv_av1_decode_tx_size(msac, cdf, max_tx,
                stbv_av1_tx_is_large(state->tx.above_tx, bx4,
                                     stbv_av1_tx_dims[max_tx].lw,
                                     state->tx.above_n) +
                stbv_av1_tx_is_large(state->tx.left_tx, by4,
                                     stbv_av1_tx_dims[max_tx].lh,
                                     state->tx.left_n));
    }
    fprintf(stderr, "L7 tx0=%d max=%d uv=%d layout=%d r=%u dif=%llu\n", tx0,
            max_tx, uv_tx, layout, msac->rng, (unsigned long long)msac->dif);

    c.msac = msac;
    c.cdf = cdf;
    c.state = state;
    c.frame = frame;
    c.intra = &intra;
    c.bs = bs;
    c.bw4 = bw4;
    c.bh4 = bh4;
    c.cbw4 = cbw4;
    c.cbh4 = cbh4;
    c.lossless = lossless;
    c.qidx = qidx;
    c.y_mode_nofilt = y_mode_nofilt;
    c.reduced_txtp_set = frame ? (int)frame->reduced_txtp_set : 0;
    c.block_skip = (int)block_skip;

    /* Coefficients: intra blocks use one transform size across the whole
     * block, decoded in raster order (dav1d read_coef_blocks).  The luma
     * plane first, then the two chroma planes at uv_tx. */
    {
        int txw4 = stbv_av1_tx_dims[tx0].w;
        int txh4 = stbv_av1_tx_dims[tx0].h;
        int y4, x4, cx4, cy4, pl, r;
        int uv_txw4 = stbv_av1_tx_dims[uv_tx].w;
        int uv_txh4 = stbv_av1_tx_dims[uv_tx].h;
        int cbx4 = bx4 >> ss_hor;
        int cby4 = by4 >> ss_ver;
        int first = 1;

        if (!block_skip) {
            for (y4 = by4; y4 < by4 + bh4; y4 += txh4) {
                for (x4 = bx4; x4 < bx4 + bw4; x4 += txw4) {
                    r = stbv_av1_leaf_tx_plane(msac, cdf, &c, x4, y4,
                                               tx0, 0, &state->res,
                                               bw4, bh4, first ? out : NULL);
                    first = 0;
                    if (r) return -4;
                }
            }
            if (has_chroma) {
                for (pl = 0; pl < 2; pl++) {
                    for (cy4 = cby4; cy4 < cby4 + cbh4; cy4 += uv_txh4) {
                        for (cx4 = cbx4; cx4 < cbx4 + cbw4; cx4 += uv_txw4) {
                            r = stbv_av1_leaf_tx_plane(msac, cdf, &c, cx4, cy4,
                                                       uv_tx, pl + 1,
                                                       &state->cres[pl],
                                                       cbw4, cbh4, NULL);
                            if (r) return -4;
                        }
                    }
                }
            }
        } else {
            /* dav1d read_coef_blocks marks the full block edges 0x40. */
            for (i = 0; i < bw4 && (unsigned int)(bx4 + i) < state->res.above_n; i++)
                state->res.above[bx4 + i] = 0x40;
            for (i = 0; i < bh4 && (unsigned int)(by4 + i) < state->res.left_n; i++)
                state->res.left[by4 + i] = 0x40;
            if (has_chroma) {
                for (pl = 0; pl < 2; pl++) {
                    for (i = 0; i < cbw4 && (unsigned int)(cbx4 + i) < state->cres[pl].above_n; i++)
                        state->cres[pl].above[cbx4 + i] = 0x40;
                    for (i = 0; i < cbh4 && (unsigned int)(cby4 + i) < state->cres[pl].left_n; i++)
                        state->cres[pl].left[cby4 + i] = 0x40;
                }
            }
            if (out) {
                out->x4 = bx4;
                out->y4 = by4;
                out->tx = tx0;
                out->skipped = 1;
                out->txtp = lossless ? STBV_AV1_TX_WHT_WHT : STBV_AV1_TX_DCT_DCT;
                out->eob = 0;
                out->skip_ctx = 0;
            }
        }

        /* Tx neighbour map: dav1d sets it to the block tx dimensions over the
         * whole block edge after reconstruction; skipped blocks use the
         * block's default tx size (dav1d b_dim[2+i], set_ctx skip path). */
        {
            int txm = (int)block_skip ? max_tx : tx0;
            for (i = 0; i < bw4 && (unsigned int)(bx4 + i) < state->tx.above_n; i++)
                state->tx.above_tx[bx4 + i] =
                    (stbv_u8)stbv_av1_tx_dims[txm].lw;
            for (i = 0; i < bh4 && (unsigned int)(by4 + i) < state->tx.left_n; i++)
                state->tx.left_tx[by4 + i] =
                    (stbv_u8)stbv_av1_tx_dims[txm].lh;
        }
    }
    return 0;
}

#endif /* STB_AV1_LEAF_H */
