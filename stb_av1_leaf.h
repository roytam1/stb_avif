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

/* Residual context is cul_level in bits 0..5 and dc-sign in bit 6. */
typedef struct stbv_av1_res_state {
    stbv_u8 above[32];
    stbv_u8 left[32];
} stbv_av1_res_state;

static void stbv_av1_res_state_init(stbv_av1_res_state *s)
{
    if (!s) return;
    memset(s->above, 0x40, sizeof(s->above));
    memset(s->left,  0x40, sizeof(s->left));
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
    unsigned la, ll;
    int i;

    if (!s) return 0;
    if (bw4 == txw4 && bh4 == txh4)
        return 0;

    la = 0;
    ll = 0;
    for (i = 0; i < txw4 && bx4 + i < 32; i++)
        la |= s->above[bx4 + i];
    for (i = 0; i < txh4 && by4 + i < 32; i++)
        ll |= s->left[by4 + i];

    if (txw4 >= 8) la |= la >> 16;
    if (txw4 >= 4) la |= la >> 8;
    if (txw4 >= 2) la |= la >> 4;
    if (txh4 >= 8) ll |= ll >> 16;
    if (txh4 >= 4) ll |= ll >> 8;
    if (txh4 >= 2) ll |= ll >> 4;

    la = la > 4 ? 4 : la;
    ll = ll > 4 ? 4 : ll;
    return stbv_av1_skip_ctx[la][ll];
}

static void stbv_av1_res_mark(stbv_av1_res_state *s,
                              int bx4, int by4, int txw4, int txh4,
                              stbv_u8 res_ctx)
{
    int i;
    if (!s) return;
    for (i = 0; i < txw4 && bx4 + i < 32; i++)
        s->above[bx4 + i] = res_ctx;
    for (i = 0; i < txh4 && by4 + i < 32; i++)
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

typedef struct stbv_av1_leaf_state {
    struct stb_av1_intra_state intra;
    stbv_av1_tx_state tx;
    stbv_av1_res_state res;
} stbv_av1_leaf_state;

static void stbv_av1_leaf_state_init(stbv_av1_leaf_state *s,
                                     stbv_u8 *above_mode, unsigned above_n,
                                     stbv_u8 *left_mode, unsigned left_n)
{
    if (!s) return;
    stb_av1_intra_state_init(&s->intra, above_mode, above_n,
                             left_mode, left_n);
    stbv_av1_tx_state_init(&s->tx);
    stbv_av1_res_state_init(&s->res);
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

    /* A skipped transform has the fixed residual context 0x40 in dav1d.
     * Non-skipped coefficient decoding will replace this when it is wired in. */
    stbv_av1_res_mark(&c->state->res, x4 & 31, y4 & 31,
                      txw4, txh4, skip ? (stbv_u8)0x40 : (stbv_u8)0);
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

    if (!msac || !cdf || !state || bs < 0 || bs >= STBV_AV1_N_BS_SIZES)
        return -1;
    bw4 = stbv_av1_block_dimensions[bs][0];
    bh4 = stbv_av1_block_dimensions[bs][1];
    if (!bw4 || !bh4)
        return -2;

    cfl_allowed = 1;
    if (stb_av1_intra_state_decode_leaf(msac, cdf, &state->intra,
                                        bx4, by4, bs, cfl_allowed, &intra))
        return -3;

    max_tx = stbv_av1_max_tx_for_bs(bs);
    if (frame && frame->txfm_mode == 0)
        tx0 = STBV_AV1_TX_4X4;
    else
        tx0 = max_tx;

    if (frame && frame->txfm_mode == 1 && max_tx > STBV_AV1_TX_4X4)
        tx0 = stbv_av1_decode_tx_size(msac, cdf, max_tx,
            stbv_av1_tx_is_smaller(state->tx.above_tx, bx4 & 31,
                                   stbv_av1_tx_dims[max_tx].lw) +
            stbv_av1_tx_is_smaller(state->tx.left_tx, by4 & 31,
                                   stbv_av1_tx_dims[max_tx].lh));

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
    c.out = out;

    return stbv_av1_decode_tx_tree(msac, cdf, &state->tx,
                                   tx0, bx4, by4,
                                   stbv_av1_leaf_tx_cb, &c);
}

#endif /* STB_AV1_LEAF_H */
