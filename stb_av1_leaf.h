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

/* Luma skip context for square transforms.  This is the non-chroma branch of
 * dav1d's get_skip_ctx(), restricted to the initial square-transform path. */
static int stbv_av1_get_skip_ctx_square(const stbv_av1_res_state *s,
                                        int bx4, int by4,
                                        int bw4, int bh4,
                                        int txw4, int txh4)
{
    stbv_u64 la, ll;
    int i;

    if (!s) return 0;
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

/* dav1d_max_txfm_size_for_bs[bs][0], for the square luma block sizes used by
 * this first pass.  Rectangular BS values return the largest corresponding
 * square transform and are therefore still safe for the syntax walker. */
static int stbv_av1_max_tx_for_bs(int bs)
{
    switch (bs) {
    case STBV_AV1_BS_128x128:
    case STBV_AV1_BS_128x64:
    case STBV_AV1_BS_64x128:
    case STBV_AV1_BS_64x64:
        return STBV_AV1_TX_64X64;
    case STBV_AV1_BS_64x32:
    case STBV_AV1_BS_32x64:
    case STBV_AV1_BS_32x32:
        return STBV_AV1_TX_32X32;
    case STBV_AV1_BS_64x16:
    case STBV_AV1_BS_32x16:
    case STBV_AV1_BS_16x32:
    case STBV_AV1_BS_16x16:
        return STBV_AV1_TX_16X16;
    default:
        return STBV_AV1_TX_8X8;
    }
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
} stbv_av1_leaf_state_arrays;

typedef struct stbv_av1_leaf_state {
    struct stb_av1_intra_state intra;
    stbv_av1_tx_state tx;
    stbv_av1_res_state res;
    stbv_u8 *above_skip;
    stbv_u8 *left_skip;
    unsigned int above_skip_n;
    unsigned int left_skip_n;
} stbv_av1_leaf_state;

static void stbv_av1_leaf_state_init(stbv_av1_leaf_state *s,
                                     const stbv_av1_leaf_state_arrays *a)
{
    if (!s) return;
    if (!a) {
        memset(s, 0, sizeof(*s));
        return;
    }
    stb_av1_intra_state_init(&s->intra, a->above_mode, a->above_mode_n,
                             a->left_mode, a->left_mode_n);
    stbv_av1_tx_state_init(&s->tx, a->above_tx, a->above_tx_n,
                           a->left_tx, a->left_tx_n);
    stbv_av1_res_state_init(&s->res, a->above_res, a->above_res_n,
                            a->left_res, a->left_res_n);
    s->above_skip = a->above_skip;
    s->left_skip = a->left_skip;
    s->above_skip_n = a->above_skip_n;
    s->left_skip_n = a->left_skip_n;
    if (a->above_skip) memset(a->above_skip, 0, a->above_skip_n);
    if (a->left_skip) memset(a->left_skip, 0, a->left_skip_n);
}

/* dav1d reset_context() resets only the LEFT contexts at the start of each
 * superblock row; the above contexts persist across rows (they are reset
 * once per frame). */
static void stbv_av1_leaf_state_reset_row(stbv_av1_leaf_state *s)
{
    if (!s) return;
    stbv_av1_tx_state_reset_row(&s->tx);
    if (s->res.left) memset(s->res.left, 0x40, s->res.left_n);
    if (s->left_skip) memset(s->left_skip, 0, s->left_skip_n);
    if (s->intra.left_mode)
        memset(s->intra.left_mode, STBV_AV1_INTRA_DC,
               (size_t)s->intra.left_count);
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
    int max_tx;
    int reduced_txtp_set;
    int block_skip;
    stbv_av1_leaf_tx_result *out;
} stbv_av1_leaf_decode_ctx;

static int stbv_av1_leaf_tx_cb(int x4, int y4, int tx, void *opaque)
{
    stbv_av1_leaf_decode_ctx *c = (stbv_av1_leaf_decode_ctx *)opaque;
    int txw4 = stbv_av1_tx_dims[tx].w;
    int txh4 = stbv_av1_tx_dims[tx].h;
    int sctx;
    unsigned skip;
    int txtp;

    if (!c || !c->msac || !c->cdf || !c->state)
        return -1;

    if (c->block_skip) {
        /* A block-level skip codes no per-transform symbols.  The residual
         * context is the fixed "empty" value 0x40 (dav1d recon_b_intra). */
        stbv_av1_res_mark(&c->state->res, x4, y4,
                          txw4, txh4, (stbv_u8)0x40);
        if (c->out) {
            c->out->x4 = x4;
            c->out->y4 = y4;
            c->out->tx = tx;
            c->out->skipped = 1;
            c->out->txtp = STBV_AV1_TX_DCT_DCT;
            c->out->eob = 0;
            c->out->skip_ctx = 0;
        }
        return 0;
    }

    sctx = stbv_av1_get_skip_ctx_square(&c->state->res,
                                        x4 & 31, y4 & 31,
                                        c->bw4, c->bh4, txw4, txh4);
    skip = stb_av1_msac_bool_adapt(
        c->msac,
        c->cdf->coef + tx * 26 + sctx * 2);

    txtp = STBV_AV1_TX_DCT_DCT;
    if (!skip) {
        if (tx <= STBV_AV1_TX_16X16) {
            int ymode = c->intra ? c->intra->y_mode : STBV_AV1_INTRA_DC;
            txtp = stbv_av1_decode_intra_txtp(
                c->msac, c->cdf,
                stbv_av1_tx_dims[tx].min,
                ymode,
                c->reduced_txtp_set);
        }
    }

    if (c->out) {
        c->out->x4 = x4;
        c->out->y4 = y4;
        c->out->tx = tx;
        c->out->skipped = (int)skip;
        c->out->txtp = txtp;
        c->out->eob = 0;
        c->out->skip_ctx = sctx;
    }

    if (skip) {
        printf("TX  (x4=%d y4=%d tx=%d) skip=1 txtp=%d sctx=%d rng=%u rem=%ld\n",
               x4, y4, tx, txtp, sctx, c->msac->rng,
               (long)(c->msac->buf_end - c->msac->buf_pos));
        /* A skipped transform has the fixed residual context 0x40. */
        stbv_av1_res_mark(&c->state->res, x4, y4,
                          txw4, txh4, (stbv_u8)0x40);
    } else {
        stbv_i32 cf[64 * 64];
        int n = txw4 << 2;
        int txclass = stbv_av1_tx_class(txtp);
        int eob;
        stbv_u8 res_ctx;
        int dc_sign_ctx = 0;
        int s = 0;
        int i;

        /* dav1d get_dc_sign_ctx: sum res_ctx >> 6 over the transform width,
         * then subtract w4 and h4 and map to 0..2 via (s != 0) + (s > 0). */
        for (i = 0; i < txw4; i++) {
            if ((unsigned int)(x4 + i) < c->state->res.above_n)
                s += c->state->res.above[x4 + i] >> 6;
            if ((unsigned int)(y4 + i) < c->state->res.left_n)
                s += c->state->res.left[y4 + i] >> 6;
        }
        s -= txw4;
        s -= txh4;
        dc_sign_ctx = (s != 0) + (s > 0);

        /* This first integration pass validates coefficient syntax and MSAC
           consumption.  Quantization/reconstruction is still supplied by
           the caller in the block layer. */
        eob = stbv_av1_decode_coeffs_square(c->msac, c->cdf, tx, 0, txclass,
                                             n, 1, 1, 0, sctx, dc_sign_ctx, cf,
                                             &res_ctx);
        printf("TX  (x4=%d y4=%d tx=%d) skip=0 txtp=%d sctx=%d dcs=%d eob=%d rng=%u rem=%ld\n",
               x4, y4, tx, txtp, sctx, dc_sign_ctx, eob, c->msac->rng,
               (long)(c->msac->buf_end - c->msac->buf_pos));
        if (eob < 0)
            return -2;
        if (c->out)
            c->out->eob = eob;
        stbv_av1_res_mark(&c->state->res, x4, y4,
                          txw4, txh4, res_ctx);
    }
    return 0;
}

static int stbv_av1_decode_leaf_syntax(struct stb_av1_msac *msac,
                                       stbv_av1_cdf *cdf,
                                       stbv_av1_leaf_state *state,
                                       const struct stb_av1_framehdr *frame,
                                       int bs, int bx4, int by4,
                                       stbv_av1_leaf_tx_result *out)
{
    struct stb_av1_intra_block intra;
    stbv_av1_leaf_decode_ctx c;
    int bw4, bh4, max_tx, tx0;
    int cfl_allowed;
    unsigned block_skip;

    if (!msac || !cdf || !state || bs < 0 || bs >= STBV_AV1_N_BS_SIZES)
        return -1;
    bw4 = stbv_av1_block_dimensions[bs][0];
    bh4 = stbv_av1_block_dimensions[bs][1];
    if (!bw4 || !bh4)
        return -2;

    /* Block-level skip, decoded before intra modes (dav1d decode_b). */
    {
        int sctx = 0;
        int i;
        if (state->above_skip && (unsigned int)bx4 < state->above_skip_n &&
            state->above_skip[bx4])
            sctx += 1;
        if (state->left_skip && (unsigned int)by4 < state->left_skip_n &&
            state->left_skip[by4])
            sctx += 1;
        if (bs == STBV_AV1_BS_64x128 && bx4 == 288 && by4 == 0)
            printf("LEAFDBG pre-skip rng=%u f=%u f2=%u sctx=%d rem=%ld\n",
                   msac->rng, (unsigned)cdf->skip[sctx * 2],
                   (unsigned)cdf->skip[sctx * 2 + 1], sctx,
                   (long)(msac->buf_end - msac->buf_pos));
        block_skip = stb_av1_msac_bool_adapt(msac, cdf->skip + sctx * 2);
        if (bs == STBV_AV1_BS_64x128 && bx4 == 288 && by4 == 0)
            printf("LEAFDBG bs_skip rng=%u rem=%ld\n", msac->rng,
                   (long)(msac->buf_end - msac->buf_pos));
        for (i = 0; i < bw4 && (unsigned int)(bx4 + i) < state->above_skip_n; i++)
            state->above_skip[bx4 + i] = (stbv_u8)block_skip;
        for (i = 0; i < bh4 && (unsigned int)(by4 + i) < state->left_skip_n; i++)
            state->left_skip[by4 + i] = (stbv_u8)block_skip;
    }

    cfl_allowed = 1;
    if (stb_av1_intra_state_decode_leaf(msac, cdf, &state->intra,
                                        bx4, by4, bs, cfl_allowed, &intra))
        return -3;
    if (bs == STBV_AV1_BS_64x128 && bx4 == 288 && by4 == 0)
        printf("LEAFDBG intra rng=%u rem=%ld ymode=%d\n", msac->rng,
               (long)(msac->buf_end - msac->buf_pos), intra.y_mode);

    max_tx = stbv_av1_max_tx_for_bs(bs);
    if (frame && frame->txfm_mode == 0)
        tx0 = STBV_AV1_TX_4X4;
    else
        tx0 = max_tx;

    if (frame && frame->txfm_mode == 1 && max_tx > STBV_AV1_TX_4X4)
        tx0 = stbv_av1_decode_tx_size(msac, cdf, max_tx,
            stbv_av1_tx_is_large(state->tx.above_tx, bx4,
                                 stbv_av1_tx_dims[max_tx].lw,
                                 state->tx.above_n) +
            stbv_av1_tx_is_large(state->tx.left_tx, by4,
                                 stbv_av1_tx_dims[max_tx].lh,
                                 state->tx.left_n));
    if (bs == STBV_AV1_BS_64x128 && bx4 == 288 && by4 == 0)
        printf("LEAFDBG txsize rng=%u rem=%ld tx0=%d\n", msac->rng,
               (long)(msac->buf_end - msac->buf_pos), tx0);

    c.msac = msac;
    c.cdf = cdf;
    c.state = state;
    c.frame = frame;
    c.intra = &intra;
    c.bs = bs;
    c.bw4 = bw4;
    c.bh4 = bh4;
    c.max_tx = max_tx;
    c.reduced_txtp_set = frame ? (int)frame->reduced_txtp_set : 0;
    c.block_skip = (int)block_skip;
    c.out = out;

    /* Intra blocks use one transform size across the whole block; no var-tx
     * tree is coded (dav1d decode_b + recon_b_intra).  Coefficients are
     * decoded in raster order at tx0 size. */
    {
        int txw4 = stbv_av1_tx_dims[tx0].w;
        int txh4 = stbv_av1_tx_dims[tx0].h;
        int y4, x4, i, r;

        for (y4 = by4; y4 < by4 + bh4; y4 += txh4) {
            for (x4 = bx4; x4 < bx4 + bw4; x4 += txw4) {
                r = stbv_av1_leaf_tx_cb(x4, y4, tx0, &c);
                if (r) return -4;
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
