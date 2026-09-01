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

/* neg_deinterleave: decode segment ID diff (dav1d decode.c) */
static int stb_neg_deinterleave(int diff, int ref, int max)
{
    if (!ref) return diff;
    if (ref >= (max - 1)) return max - diff - 1;
    if (2 * ref < max) {
        if (diff <= 2 * ref) {
            if (diff & 1) return ref + ((diff + 1) >> 1);
            else return ref - (diff >> 1);
        }
        return diff;
    } else {
        if (diff <= 2 * (max - ref - 1)) {
            if (diff & 1) return ref + ((diff + 1) >> 1);
            else return ref - (diff >> 1);
        }
        return max - (diff + 1);
    }
}
#ifndef STB_AV1_COEF_H
#error "include stb_av1_coef.h first"
#endif
#ifndef STB_AV1_QUANT_H
#include "stb_av1_quant.h"
#endif

/* Reconstruction callback interface (NULL-safe, for stb_avif integration). */
typedef struct stbv_av1_leaf_recon {
    void *ud;
    stbv_i32 *cf;
    void (*block_info)(void *ud, int intra, int bs, int bx4, int by4, int has_chroma, int cbw4, int cbh4, int uv_tx, int tx0, int pal_sz_y, int pal_sz_uv, int skip, int y_mode, int y_angle, int uv_mode, int uv_angle, int cfl_alpha_u, int cfl_alpha_v, int ibc_mv_y, int ibc_mv_x);
    void (*luma_txb)(void *ud, int x4, int y4, int tx, int txtp, int eob, stbv_i32 *cf);
    void (*chroma_txb)(void *ud, int pl, int x4, int y4, int tx, int txtp, int eob, stbv_i32 *cf);
    void (*luma_pal)(void *ud, const stbv_u8 *idx, int sz, int bw4, int bh4, const stbv_u16 *pal);
    void (*chroma_pal)(void *ud, int pl, const stbv_u8 *idx, int sz, int cbw4, int cbh4, const stbv_u16 *pal);
} stbv_av1_leaf_recon;

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
    /* dav1d clips residual-context WRITES to the true plane extent
     * (imin(txw, f->bw - bx)); reads fall through into 0x40 padding.
     * 0 means "same as above_n/left_n". */
    unsigned int above_mark_n;
    unsigned int left_mark_n;
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
    s->above_mark_n = above_n;
    s->left_mark_n = left_n;
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
    unsigned int n;
    if (!s) return;
    n = s->above_mark_n ? s->above_mark_n : s->above_n;
    for (i = 0; i < txw4 && (unsigned int)(bx4 + i) < n; i++)
        s->above[bx4 + i] = res_ctx;
    n = s->left_mark_n ? s->left_mark_n : s->left_n;
    for (i = 0; i < txh4 && (unsigned int)(by4 + i) < n; i++)
        s->left[by4 + i] = res_ctx;
}

/* dav1d's SKIP-block marking (memset_pow2) is NOT clipped to the
 * frame extent, unlike its coded-coefficient marking. */
static void stbv_av1_res_mark_unc(stbv_av1_res_state *s,
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
    stbv_u8 *above_tx_intra;
    stbv_u8 *left_tx_intra;
    stbv_u8 *above_res;
    unsigned int above_res_n;
    stbv_u8 *left_res;
    unsigned int left_res_n;
    unsigned int above_res_mark_n;
    unsigned int left_res_mark_n;
    unsigned int above_cre_mark_n[2];
    unsigned int left_cre_mark_n[2];
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
    /* segment id context */
    stbv_u8 *above_seg_id;
    unsigned int above_seg_id_n;
    stbv_u8 *left_seg_id;
    unsigned int left_seg_id_n;
    /* IBC MV neighbour arrays for MV prediction (dav1d refmvs_find). */
    int *above_ibc_mv_y;
    int *above_ibc_mv_x;
    stbv_u8 *above_ibc_valid;
    unsigned int above_ibc_mv_n;
    int *left_ibc_mv_y;
    int *left_ibc_mv_x;
    stbv_u8 *left_ibc_valid;
    unsigned int left_ibc_mv_n;
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
    stbv_u16 cache[16];
    stbv_u16 used_cache[8];
    stbv_u8 pal_tmp[64 * 64];
    stbv_u8 pal_tmp_y[64 * 64];
    stbv_u8 pal_order[64][8];
    stbv_u8 pal_ctxs[64];
    int pal_sz_y;
    int pal_sz_uv;
    /* segment id context */
    stbv_u8 *above_seg_id;
    stbv_u8 *left_seg_id;
    unsigned int above_seg_id_n;
    unsigned int left_seg_id_n;
    /* CDEF index output grid (per-64x64 block) */
    int *cdef_idx_grid;
    int cdef_grid_stride;
    /* Per-SB quantizer/lf state (persists across leaf callbacks within a tile) */
    int last_qidx;
    int last_delta_lf[4];
    /* IBC MV neighbour arrays for MV prediction. */
    int *above_ibc_mv_y;
    int *above_ibc_mv_x;
    stbv_u8 *above_ibc_valid;
    unsigned int above_ibc_mv_n;
    int *left_ibc_mv_y;
    int *left_ibc_mv_x;
    stbv_u8 *left_ibc_valid;
    unsigned int left_ibc_mv_n;
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
                           a->left_tx, a->left_tx_n,
                           a->above_tx_intra, a->left_tx_intra);
    stbv_av1_res_state_init(&s->res, a->above_res, a->above_res_n,
                            a->left_res, a->left_res_n);
    for (pl = 0; pl < 2; pl++)
        stbv_av1_res_state_init(&s->cres[pl], a->above_cre[pl],
                                a->above_cre_n[pl], a->left_cre[pl],
                                a->left_cre_n[pl]);
    s->res.above_mark_n = a->above_res_mark_n;
    s->res.left_mark_n = a->left_res_mark_n;
    for (pl = 0; pl < 2; pl++) {
        s->cres[pl].above_mark_n = a->above_cre_mark_n[pl];
        s->cres[pl].left_mark_n = a->left_cre_mark_n[pl];
    }
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
    s->above_seg_id = a->above_seg_id;
    s->left_seg_id = a->left_seg_id;
    s->above_seg_id_n = a->above_seg_id_n;
    s->left_seg_id_n = a->left_seg_id_n;
    s->above_ibc_mv_y = a->above_ibc_mv_y;
    s->above_ibc_mv_x = a->above_ibc_mv_x;
    s->above_ibc_valid = a->above_ibc_valid;
    s->above_ibc_mv_n = a->above_ibc_mv_n;
    s->left_ibc_mv_y = a->left_ibc_mv_y;
    s->left_ibc_mv_x = a->left_ibc_mv_x;
    s->left_ibc_valid = a->left_ibc_valid;
    s->left_ibc_mv_n = a->left_ibc_mv_n;
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
    if (a->above_seg_id) memset(a->above_seg_id, 0, a->above_seg_id_n);
    if (a->above_ibc_valid) memset(a->above_ibc_valid, 0, a->above_ibc_mv_n);
    if (a->left_ibc_valid) memset(a->left_ibc_valid, 0, a->left_ibc_mv_n);
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
    if (s->left_seg_id) memset(s->left_seg_id, 0, s->left_seg_id_n);
    if (s->left_ibc_valid) memset(s->left_ibc_valid, 0, s->left_ibc_mv_n);
    if (s->intra.left_mode)
        memset(s->intra.left_mode, STBV_AV1_INTRA_DC,
               (size_t)s->intra.left_count);
    /* dav1d reset_context() also clears uvmode to DC_PRED each superblock
     * row; leaving stale SMOOTH modes here made sm_uv_flag fire on rows
     * where dav1d saw a clean left edge (diffuse chroma fog). */
    if (s->intra.left_uvmode)
        memset(s->intra.left_uvmode, STBV_AV1_INTRA_DC,
               (size_t)s->intra.left_uv_count);
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
    /* Unclipped luma block dims (dav1d uses full b_dim for skip ctx). */
    int bw4_unc, bh4_unc;
    int cbw4, cbh4;
    /* Unclipped chroma block dims (dav1d uses full b_dim for skip ctx). */
    int cbw4_unc, cbh4_unc;
    int lossless;
    int qidx;
    int y_mode_nofilt;
    int y_mode_txtp;
    int block_skip;
    int reduced_txtp_set;
    int hbd;
    int is_intra;  /* 1 = intra block, 0 = IBC block */
    int luma_txtp; /* stored luma txtp for inter/IBC chroma derivation */
    int ibc_mv_y;  /* decoded IBC MV, 1/8-pel luma units */
    int ibc_mv_x;
    const stbv_av1_leaf_recon *recon;
} stbv_av1_leaf_decode_ctx;

/* Per-transform coefficient syntax for one plane (dav1d decode_coefs +
 * read_coef_blocks): per-transform skip, then transform type (gated), then
 * coefficients.  x4/y4 are plane-local 4x4 coordinates and bw4/bh4 the
 * plane-local block dims in 4x4 units.  When out is non-NULL the first
 * (luma) transform's results are recorded there. */
static int stbv_av1_leaf_tx_plane(struct stb_av1_msac *msac,
                                  stbv_av1_cdf *cdf,
                                  stbv_av1_leaf_decode_ctx *c,
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

    sctx = stbv_av1_get_skip_ctx(rs, x4, y4,
                                 is_chroma ? c->cbw4_unc : c->bw4_unc,
                                 is_chroma ? c->cbh4_unc : c->bh4_unc,
                                 txw4, txh4, is_chroma);
skip = stb_av1_msac_bool_adapt(
    msac, cdf->coef + stbv_av1_tx_dims[tx].ctx * 26 + sctx * 2);
    if (!skip) {
        max = stbv_av1_tx_dims[tx].max;
        if (c->lossless)
            txtp = STBV_AV1_TX_WHT_WHT;
        else if (max + 1 >= STBV_AV1_TX_64X64) /* max + intra >= TX_64X64 */
            txtp = STBV_AV1_TX_DCT_DCT;
        else if (is_chroma) {
            if (c->is_intra)
                txtp = stbv_av1_txtp_from_uvmode[c->intra ? c->intra->uv_mode : 0];
            else
                txtp = stbv_av1_get_uv_inter_txtp(
                    stbv_av1_tx_dims[tx].min, stbv_av1_tx_dims[tx].max,
                    c->luma_txtp);
        } else if (!c->qidx)
            txtp = STBV_AV1_TX_DCT_DCT;
        else if (c->is_intra)
            txtp = stbv_av1_decode_intra_txtp(msac, cdf,
                stbv_av1_tx_dims[tx].min, c->y_mode_txtp,
                c->reduced_txtp_set);
        else
            txtp = stbv_av1_decode_inter_txtp(msac, cdf,
                stbv_av1_tx_dims[tx].min, stbv_av1_tx_dims[tx].max,
                c->reduced_txtp_set);
    } else {
        /* dav1d: *txtp = lossless * WHT_WHT */
        txtp = c->lossless ? STBV_AV1_TX_WHT_WHT : STBV_AV1_TX_DCT_DCT;
    }

    /* Store luma txtp for inter/IBC chroma derivation */
    if (!is_chroma)
        c->luma_txtp = txtp;

    if (out) {
        out->x4 = x4;
        out->y4 = y4;
        out->tx = tx;
        out->skipped = (int)skip;
        out->txtp = txtp;
        out->eob = 0;
        out->skip_ctx = sctx;
    }

    if (skip) {
        /* A skipped transform has the fixed residual context 0x40. */
        stbv_av1_res_mark_unc(rs, x4, y4, txw4, txh4, (stbv_u8)0x40);
        if (c->recon && c->recon->cf) {
            /* coefficient count is (4w)*(4h): dims are in 4x4 units */
            int n = stbv_av1_tx_dims[tx].w * stbv_av1_tx_dims[tx].h * 16;
            int i;
            for (i = 0; i < n; i++) c->recon->cf[i] = 0;
            if (is_chroma) {
                if (c->recon->chroma_txb)
                    c->recon->chroma_txb(c->recon->ud, chroma - 1, x4, y4,
                                         tx, txtp, 0, c->recon->cf);
            } else {
                if (c->recon->luma_txb)
                    c->recon->luma_txb(c->recon->ud, x4, y4,
                                       tx, txtp, 0, c->recon->cf);
            }
        }
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
        {
            /* Real dequantization: dav1d_dq_tbl[hbd][qidx][{dc,ac}] with the
             * per-plane qidx deltas; dq_shift = imax(0, t_dim->ctx - 2). */
            const int hbd_i = c->hbd;
            int base_q = c->qidx ? c->qidx : (c->frame ? (int)c->frame->quant.yac : 0);
            int qdc, qac;
            int dq_dc, dq_ac, dq_shift;
            if (base_q < 0) base_q = 0;
            if (base_q > 255) base_q = 255;
            if (is_chroma) {
                int udc = base_q + (c->frame ? c->frame->quant.udc_delta : 0);
                int uac = base_q + (c->frame ? c->frame->quant.uac_delta : 0);
                if (chroma == 2) { /* V plane */
                    udc += c->frame ? c->frame->quant.vdc_delta - c->frame->quant.udc_delta : 0;
                    uac += c->frame ? c->frame->quant.vac_delta - c->frame->quant.uac_delta : 0;
                }
                udc = udc < 0 ? 0 : udc > 255 ? 255 : udc;
                uac = uac < 0 ? 0 : uac > 255 ? 255 : uac;
                qdc = udc; qac = uac;
            } else {
                int ydc = base_q + (c->frame ? c->frame->quant.ydc_delta : 0);
                ydc = ydc < 0 ? 0 : ydc > 255 ? 255 : ydc;
                qdc = ydc; qac = base_q;
            }
            dq_dc = stbv_av1_dq_tbl[hbd_i][qdc][0];
            dq_ac = stbv_av1_dq_tbl[hbd_i][qac][1];
            dq_shift = stbv_av1_tx_dims[tx].ctx - 2;
            if (dq_shift < 0) dq_shift = 0;

            eob = stbv_av1_decode_coeffs_square(msac, cdf, tx, is_chroma,
                                    txclass,
                                    dq_dc, dq_ac, dq_shift,
                                    sctx, dc_sign_ctx, cf,
                                    &res_ctx);
        }
        if (eob < 0)
            return -2;
        if (out)
            out->eob = eob;
        if (c->recon && c->recon->cf) {
            /* coefficient count is (4w)*(4h): dims are in 4x4 units */
            int n = stbv_av1_tx_dims[tx].w * stbv_av1_tx_dims[tx].h * 16;
            int i;
            for (i = 0; i < n; i++) c->recon->cf[i] = cf[i];
            if (is_chroma) {
                if (c->recon->chroma_txb) c->recon->chroma_txb(c->recon->ud, chroma-1, x4, y4, tx, txtp, eob, c->recon->cf);
            } else {
                if (c->recon->luma_txb) c->recon->luma_txb(c->recon->ud, x4, y4, tx, txtp, eob, c->recon->cf);
            }
        }
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
    if (pal_sz > 8) return -1;
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
            if (stb_av1_msac_bool_equi(msac)) {
                used[i++] = cache[n];
            }
    }
    n_used = i;

    if (i < pal_sz) {
        int n, m;
        prev = (int)stb_av1_msac_bools(msac, (unsigned)bpc);
        pal_out[i++] = (stbv_u16)prev;
        if (i < pal_sz) {
            bits = bpc - 3 + (int)stb_av1_msac_bools(msac, 2);
            max = (1 << bpc) - 1;
            do {
                int delta = (int)stb_av1_msac_bools(msac, (unsigned)bits);
                prev += delta + (pl ? 0 : 1);
                if (prev > max) prev = max;
                pal_out[i++] = (stbv_u16)prev;
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
    color_map_cdf = cdf->color_map + (pl * 7 + (pal_sz - 2)) * 40;
    for (i = 1; i < wpx + hpx - 1; i++) {
        first = i < wpx - 1 ? i : wpx - 1;
        last = i - hpx + 1 > 0 ? i - hpx + 1 : 0;
        stbv_av1_palette_order(pal_tmp, stride, i, first, last, order, ctx);
        for (j = first, m = 0; j >= last; j--, m++) {
            int color_idx = (int)stb_av1_msac_symbol(
                msac, color_map_cdf + ctx[m] * 8, (size_t)(pal_sz - 1));
            pal_tmp[(i - j) * stride + j] = order[m][color_idx];
        }
    }
    return 0;
}

/* ---- MV residual decode for IBC (dav1d decode.c:76-117) ---- */

/* Decode the diff for one MV component. For IBC, mv_prec=-1 so fp and hp
 * are never decoded (only sign + class + class bits). */
static int stbv_av1_read_mv_component_diff(struct stb_av1_msac *msac,
                                            stbv_u16 *mv_comp_sign,
                                            stbv_u16 *mv_comp_classes,
                                            stbv_u16 *mv_comp_class0,
                                            stbv_u16 mv_comp_classN[10][2],
                                            int mv_prec)
{
    int sign, cl, up, fp = 3, hp = 1;
    sign = (int)stb_av1_msac_bool_adapt(msac, mv_comp_sign);
    cl = (int)stb_av1_msac_symbol(msac, mv_comp_classes, 10);
    if (!cl) {
        up = (int)stb_av1_msac_bool_adapt(msac, mv_comp_class0);
        if (mv_prec >= 0) {
            fp = (int)stb_av1_msac_symbol(msac,
                mv_comp_class0 + up * 4, 3);
            if (mv_prec > 0)
                hp = (int)stb_av1_msac_bool_adapt(msac,
                    mv_comp_class0 + up * 4 + 2);
        }
    } else {
        up = 1 << cl;
        { int n; for (n = 0; n < cl; n++)
            up |= (int)stb_av1_msac_bool_adapt(msac, mv_comp_classN[n]) << n;
        }
        if (mv_prec >= 0) {
            fp = (int)stb_av1_msac_symbol(msac, mv_comp_classN[0] + 10, 3);
            if (mv_prec > 0)
                hp = (int)stb_av1_msac_bool_adapt(msac, mv_comp_classN[0] + 12);
        }
    }
    { int diff = ((up << 3) | (fp << 1) | hp) + 1;
      return sign ? -diff : diff;
    }
}

/* Decode MV joint + component residuals. mv_prec=-1 for IBC. */
static void stbv_av1_read_mv_residual(struct stb_av1_msac *msac,
                                       stbv_av1_cdf *cdf,
                                       int *mv_y, int *mv_x,
                                       int mv_prec,
                                       int bx4, int by4)
{
    int joint;
    joint = (int)stb_av1_msac_symbol(msac, cdf->mv_joint, 3);
    if (joint & 2) /* MV_JOINT_V */
        *mv_y += stbv_av1_read_mv_component_diff(msac,
            cdf->mv_sign, cdf->mv_classes, cdf->mv_class0,
            cdf->mv_classN, mv_prec);
    if (joint & 1) /* MV_JOINT_H */
        *mv_x += stbv_av1_read_mv_component_diff(msac,
            cdf->mv_sign_x, cdf->mv_classes_x, cdf->mv_class0_x,
            cdf->mv_classN_x, mv_prec);
}

/* Find IBC MV prediction from spatial neighbours (dav1d refmvs_find with
 * ref={0,-1}).  Searches above-right, above, above-left, left, below-left
 * in that order; returns the first valid candidate.  If none found, returns
 * a default MV: (-(512<<sb128)-2048, 0) if near top of frame, else
 * (0, -(512<<sb128)).  These match dav1d decode.c:1279-1287. */
static void stbv_av1_find_ibc_mv_pred(const stbv_av1_leaf_state *s,
                                       int bx4, int by4, int bw4, int bh4,
                                       int frame_top4, int sb128,
                                       int *pred_y, int *pred_x)
{
    int i;
    /* Search above row (by4-1): right-to-left across block width. */
    if (s->above_ibc_mv_y && s->above_ibc_valid && by4 > 0) {
        for (i = bw4 - 1; i >= 0; i--) {
            int col = bx4 + i;
            if (col >= 0 && (unsigned)col < s->above_ibc_mv_n &&
                s->above_ibc_valid[col]) {
                *pred_y = s->above_ibc_mv_y[col];
                *pred_x = s->above_ibc_mv_x[col];
                return;
            }
        }
    }
    /* Search above-left. */
    if (s->above_ibc_mv_y && s->above_ibc_valid && by4 > 0 && bx4 > 0) {
        int col = bx4 - 1;
        if (col >= 0 && (unsigned)col < s->above_ibc_mv_n &&
            s->above_ibc_valid[col]) {
            *pred_y = s->above_ibc_mv_y[col];
            *pred_x = s->above_ibc_mv_x[col];
            return;
        }
    }
    /* Search left column (bx4-1): top-to-bottom across block height. */
    if (s->left_ibc_mv_y && s->left_ibc_valid && bx4 > 0) {
        for (i = 0; i < bh4; i++) {
            int row = by4 + i;
            if (row >= 0 && (unsigned)row < s->left_ibc_mv_n &&
                s->left_ibc_valid[row]) {
                *pred_y = s->left_ibc_mv_y[row];
                *pred_x = s->left_ibc_mv_x[row];
                return;
            }
        }
    }
    /* Search below-left. */
    if (s->left_ibc_mv_y && s->left_ibc_valid && bx4 > 0) {
        int row = by4 + bh4;
        if (row >= 0 && (unsigned)row < s->left_ibc_mv_n &&
            s->left_ibc_valid[row]) {
            *pred_y = s->left_ibc_mv_y[row];
            *pred_x = s->left_ibc_mv_x[row];
            return;
        }
    }
    /* No spatial candidate found: use dav1d default MV. */
    {
        int sb_step = 16 << sb128;
        if (by4 - sb_step < frame_top4) {
            *pred_y = 0;
            *pred_x = -(512 << sb128) - 2048;
        } else {
            *pred_y = -(512 << sb128);
            *pred_x = 0;
        }
    }
}

/* ---- IBC luma TX tree leaf callback ---- */
/* Called by stbv_av1_decode_tx_tree for each luma TX leaf in an IBC block.
 * Decodes coefficients at the leaf's TX size. */
static int stbv_av1_ibc_luma_leaf(int x4, int y4, int tx, void *opaque)
{
    stbv_av1_leaf_decode_ctx *c = (stbv_av1_leaf_decode_ctx *)opaque;
    int r;
    if (!c) return -1;
    r = stbv_av1_leaf_tx_plane(c->msac, c->cdf, c, x4, y4, tx, 0,
                               &c->state->res, c->bw4, c->bh4, NULL);
    return r ? -1 : 0;
}

static int stbv_av1_decode_leaf_syntax(struct stb_av1_msac *msac,
                                       stbv_av1_cdf *cdf,
                                       stbv_av1_leaf_state *state,
                                       const struct stb_av1_seqhdr *seq,
                                       const struct stb_av1_framehdr *frame,
                                       int bs, int bx4, int by4,
                                       stbv_av1_leaf_tx_result *out,
                                       const stbv_av1_leaf_recon *recon)
{
    struct stb_av1_intra_block intra;
    stbv_av1_leaf_decode_ctx c;
    int bw4, bh4, bw4_unc, bh4_unc, max_tx, uv_tx, tx0;
    int layout, ss_hor, ss_ver, sb_step;
    int cfl_allowed, cbw4, cbh4, has_chroma;
    int cbw4_unc, cbh4_unc;
    int lossless, qidx;
    int y_mode_nofilt, i;
    int seg_id = 0, seg_pred = 0;
    unsigned block_skip = 0;
    unsigned int n;
    int intra_flag = 1; /* 1 = intra, 0 = IBC */
c.recon = recon;
    if (!msac || !cdf || !state || bs < 0 || bs >= STBV_AV1_N_BS_SIZES)
        return -1;
    bw4 = stbv_av1_block_dimensions[bs][0];
    bh4 = stbv_av1_block_dimensions[bs][1];
    if (!bw4 || !bh4)
        return -2;
#ifdef STB_AV1_IBC_DEBUG
    if (by4 == 0 && bx4 >= 140) {
        static FILE *fp = NULL;
        if (!fp) fp = fopen("ibc_debug.txt", "a");
        if (fp) {
            fprintf(fp, "ENTER bx4=%d by4=%d bw4=%d bh4=%d bs=%d\n",
                    bx4, by4, bw4, bh4, bs);
            fflush(fp);
        }
    }
#endif

    layout = seq ? (int)seq->layout : STB_AV1_LAYOUT_I444;
    ss_hor = layout == STB_AV1_LAYOUT_I420 || layout == STB_AV1_LAYOUT_I422;
    ss_ver = layout == STB_AV1_LAYOUT_I420;
    sb_step = (seq && seq->sb128) ? 32 : 16;
    lossless = frame ? (int)frame->segmentation.lossless[0] : 0;
    qidx = state->last_qidx + (frame ? (int)frame->segmentation.d[seg_id].delta_q : 0);
    if (qidx < 0) qidx = 0;
    if (qidx > 255) qidx = 255;
    /* dav1d gates chroma presence on the UNCLIPPED block dims, then clips
     * the coefficient grids to the padded frame area ((w+7)&~7)>>2. */
    has_chroma = layout != STB_AV1_LAYOUT_I400 &&
                 (bw4 > ss_hor || (bx4 & 1)) && (bh4 > ss_ver || (by4 & 1));
    cbw4_unc = (bw4 + ss_hor) >> ss_hor;
    cbh4_unc = (bh4 + ss_ver) >> ss_ver;
    if (frame && frame->width[0] > 0 && frame->height > 0) {
        int fw4 = (((int)frame->width[0] + 7) & ~7) >> 2;
        int fh4 = (((int)frame->height + 7) & ~7) >> 2;
        /* Syntax decisions (skip ctx, tx size, palette/filter-intra gates)
         * must use the block's own dimensions; only coefficient-grid
         * extents may be clipped to the padded frame (dav1d keeps b_dim
         * unclipped and clips during recon). */
        /* Clip coefficient-loop dims to the 8-aligned frame extent
         * (dav1d read_coef_blocks: w4 = imin(bw4, f->bw - t->bx)).
         * bw4_unc/bh4_unc stay unclipped for syntax decisions. */
        bw4_unc = bw4;
        bh4_unc = bh4;
        if (fw4 - bx4 < bw4) bw4 = fw4 - bx4;
        if (fh4 - by4 < bh4) bh4 = fh4 - by4;
        if (bw4 <= 0 || bh4 <= 0)
            return 0;
    } else {
        bw4_unc = bw4;
        bh4_unc = bh4;
    }
    cbw4 = (bw4 + ss_hor) >> ss_hor;
    cbh4 = (bh4 + ss_ver) >> ss_ver;

    /* Segment ID decoding (dav1d decode_b segment_id section). */
    seg_id = 0;
    seg_pred = 0;
    if (frame && frame->segmentation.enabled && frame->segmentation.update_map) {
        int have_top = (state->above_seg_id && (unsigned)bx4 < state->above_seg_id_n);
        int have_left = (state->left_seg_id && (unsigned)by4 < state->left_seg_id_n);
        if (frame->segmentation.preskip) {
            /* preskip: decode segment_id before skip */
            if (!frame->segmentation.temporal) {
                /* Spatial prediction: get predicted seg_id from neighbours */
                int seg_ctx = 0;
                unsigned pred_seg_id = 0;
                if (have_left && have_top) {
                    int l = state->left_seg_id[by4];
                    int a = state->above_seg_id[bx4];
                    int al = (bx4 > 0 && by4 > 0) ? state->above_seg_id[bx4 - 1] : a;
                    if (l == a && al == l) seg_ctx = 2;
                    else if (l == a || al == l || a == al) seg_ctx = 1;
                    else seg_ctx = 0;
                    pred_seg_id = (unsigned)(a == al ? a : l);
                } else {
                    pred_seg_id = have_left ? (unsigned)state->left_seg_id[by4] :
                                  have_top ? (unsigned)state->above_seg_id[bx4] : 0;
                }
                if (block_skip) {
                    seg_id = (int)pred_seg_id;
                } else {
                    unsigned diff = (unsigned)stb_av1_msac_symbol(msac,
                        cdf->seg_id + seg_ctx * 8, 7);
                    int last_active = frame->segmentation.last_active_segid;
                    seg_id = stb_neg_deinterleave((int)diff, (int)pred_seg_id,
                                              last_active + 1);
                    if (seg_id > last_active) seg_id = 0;
                }
                if (seg_id < 0 || seg_id >= 8) seg_id = 0;
            }
        }
        /* Apply per-segment features: skip */
        if (frame->segmentation.d[seg_id].skip)
            block_skip = 1;
        /* Store segment_id in context arrays */
        if (state->above_seg_id && (unsigned)bx4 < state->above_seg_id_n) {
            for (i = 0; i < bw4_unc && (unsigned)(bx4 + i) < state->above_seg_id_n; i++)
                state->above_seg_id[bx4 + i] = (stbv_u8)seg_id;
        }
        if (state->left_seg_id && (unsigned)by4 < state->left_seg_id_n) {
            for (i = 0; i < bh4_unc && (unsigned)(by4 + i) < state->left_seg_id_n; i++)
                state->left_seg_id[by4 + i] = (stbv_u8)seg_id;
        }
    }

    /* Block-level skip, decoded before intra modes (dav1d decode_b).
     * When skip_mode or segment skip is already set, skip=1 without
     * reading from MSAC (dav1d decode.c:888-895). */
    {
        int sctx = 0;
        if (state->above_skip && (unsigned int)bx4 < state->above_skip_n &&
            state->above_skip[bx4])
            sctx += 1;
        if (state->left_skip && (unsigned int)by4 < state->left_skip_n &&
            state->left_skip[by4])
            sctx += 1;
        if (!block_skip) {
            block_skip = stb_av1_msac_bool_adapt(msac, cdf->skip + sctx * 2);
        }
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
            v = stb_av1_msac_bools(msac, frame->cdef.n_bits);
            state->cdef_idx[idx] = v;
            if (bw4 > 16) state->cdef_idx[idx + 1] = v;
            if (bh4 > 16) state->cdef_idx[idx + 2] = v;
            if (bw4 == 32 && bh4 == 32) state->cdef_idx[idx + 3] = v;
            /* Write to cdef_idx output grid for post-decode CDEF filtering. */
            if (state->cdef_idx_grid && state->cdef_grid_stride > 0) {
                int gx = sbx / 16 + (idx & 1);
                int gy = sby / 16 + (idx >> 1);
                if (gx >= 0 && gx < state->cdef_grid_stride &&
                    gy >= 0)
                    state->cdef_idx_grid[gy * state->cdef_grid_stride + gx] = v;
            }
        }
        /* Also write cdef_idx for skipped blocks - use 0 as default. */
        if (block_skip && state->cdef_idx[idx] == -1) {
            state->cdef_idx[idx] = 0;
            if (bw4 > 16) state->cdef_idx[idx + 1] = 0;
            if (bh4 > 16) state->cdef_idx[idx + 2] = 0;
            if (bw4 == 32 && bh4 == 32) state->cdef_idx[idx + 3] = 0;
            if (state->cdef_idx_grid && state->cdef_grid_stride > 0) {
                int gx = sbx / 16 + (idx & 1);
                int gy = sby / 16 + (idx >> 1);
                if (gx >= 0 && gx < state->cdef_grid_stride &&
                    gy >= 0)
                    state->cdef_idx_grid[gy * state->cdef_grid_stride + gx] = 0;
            }
        }
    }

    /* delta-q/lf at superblock origin (dav1d decode.c:962-1028). */
    if (frame && frame->delta_q_present &&
        !((bx4 | by4) & (sb_step - 1)))
    {
        int have_delta_q = (bs != (int)(seq && seq->sb128 ? STBV_AV1_BS_128x128 : STBV_AV1_BS_64x64) || !block_skip);
        if (have_delta_q) {
            int dq = (int)stb_av1_msac_symbol(msac, cdf->delta_q, 3);
            if (dq == 3) {
                int nb = 1 + (int)stb_av1_msac_bools(msac, 3);
                dq = (int)stb_av1_msac_bools(msac, (unsigned)nb) + 1 + (1 << nb);
            }
            if (dq) {
                if (stb_av1_msac_bool_equi(msac)) dq = -dq;
                dq *= 1 << frame->delta_q_res_log2;
            }
            state->last_qidx = dq + state->last_qidx;
            if (state->last_qidx < 0) state->last_qidx = 0;
            if (state->last_qidx > 255) state->last_qidx = 255;
            if (frame->delta_lf_present) {
                int nlfs = frame->delta_lf_multi ?
                    (seq && seq->layout != STB_AV1_LAYOUT_I400 ? 4 : 2) : 1;
                int i;
                for (i = 0; i < nlfs; i++) {
                    int dl = (int)stb_av1_msac_symbol(msac,
                        cdf->delta_lf + (i + (int)frame->delta_lf_multi) * 4, 3);
                    if (dl == 3) {
                        int nb = 1 + (int)stb_av1_msac_bools(msac, 3);
                        dl = (int)stb_av1_msac_bools(msac, (unsigned)nb) + 1 + (1 << nb);
                    }
                    if (dl) {
                        if (stb_av1_msac_bool_equi(msac)) dl = -dl;
                        dl *= 1 << frame->delta_lf_res_log2;
                    }
                    state->last_delta_lf[i] += dl;
                    if (state->last_delta_lf[i] < -63) state->last_delta_lf[i] = -63;
                    if (state->last_delta_lf[i] > 63) state->last_delta_lf[i] = 63;
                }
            }
        }
        qidx = state->last_qidx;
    }

    /* Intra flag: for key frames with allow_intrabc, decode the intrabc
     * flag (dav1d decode.c:1043-1044).  For key frames without intrabc,
     * all blocks are implicitly intra. */
    if (frame && frame->allow_intrabc) {
        intra_flag = !stb_av1_msac_bool_adapt(msac, cdf->intrabc);
    }

    /* Recompute qidx using the SB-level last_qidx (may have been updated
     * by delta_q above). */
    qidx = state->last_qidx + (frame ? (int)frame->segmentation.d[seg_id].delta_q : 0);
    if (qidx < 0) qidx = 0;
    if (qidx > 255) qidx = 255;

    /* IBC MV residual decode (dav1d decode.c:1267-1340).
     * Find spatial MV prediction from above/left IBC neighbours, then
     * decode residual relative to that prediction. */
    if (!intra_flag) {
        int pred_mv_y = 0, pred_mv_x = 0;
        int mv_y, mv_x;
        int sb128 = (seq && seq->sb128) ? 1 : 0;
        int frame_top4 = 0;
        stbv_av1_find_ibc_mv_pred(state, bx4, by4, bw4, bh4,
                                   frame_top4, sb128,
                                   &pred_mv_y, &pred_mv_x);
        mv_y = pred_mv_y;
        mv_x = pred_mv_x;
        stbv_av1_read_mv_residual(msac, cdf, &mv_y, &mv_x, -1, bx4, by4);

        /* Clip IBC MV to decoded parts of the current tile/SB
         * (dav1d decode.c:1292-1346).  All values in pixel units. */
        {
            int fw = frame ? (int)frame->width[0] : 0;
            int border_left  = 0;
            int border_top   = 0;
            int border_right, src_left, src_top, src_right, src_bottom;
            int sbx, sby, sb_size;

            if (has_chroma) {
                if (bw4 < 2 && ss_hor) border_left += 4;
                if (bh4 < 2 && ss_ver) border_top  += 4;
            }

            src_left   = bx4 * 4 + (mv_x >> 3);
            src_top    = by4 * 4 + (mv_y >> 3);
            src_right  = src_left + bw4 * 4;
            src_bottom = src_top  + bh4 * 4;

            /* Single-tile: border_right = frame width rounded up to bw4 */
            border_right = ((fw + 3 + (bw4 * 4 - 1)) & ~(bw4 * 4 - 1));

            /* Clip to left/right tile boundary */
            if (src_left < border_left) {
                src_right += border_left - src_left;
                src_left  += border_left - src_left;
            } else if (src_right > border_right) {
                src_left  -= src_right - border_right;
                src_right -= src_right - border_right;
            }
            /* Clip to top tile boundary */
            if (src_top < border_top) {
                src_bottom += border_top - src_top;
                src_top    += border_top - src_top;
            }

            /* SB position and size in pixel units */
            sb_size = 1 << (6 + sb128);
            sbx = (bx4 >> (4 + sb128)) << (6 + sb128);
            sby = (by4 >> (4 + sb128)) << (6 + sb128);

            /* Avoid overlap with current superblock */
            if (src_bottom > sby && src_right > sbx) {
                if (src_top - border_top >= src_bottom - sby) {
                    src_top    -= src_bottom - sby;
                    src_bottom -= src_bottom - sby;
                } else if (src_left - border_left >= src_right - sbx) {
                    src_left  -= src_right - sbx;
                    src_right -= src_right - sbx;
                }
            }
            /* Move src up if below current SB row */
            if (src_bottom > sby + sb_size) {
                src_top    -= src_bottom - (sby + sb_size);
                src_bottom -= src_bottom - (sby + sb_size);
            }

            /* Write back clipped MV in 1/8-pel luma units */
            mv_x = (src_left - bx4 * 4) * 8;
            mv_y = (src_top  - by4 * 4) * 8;
        }

        c.ibc_mv_y = mv_y;
        c.ibc_mv_x = mv_x;
    } else {
        c.ibc_mv_y = 0;
        c.ibc_mv_x = 0;
    }

    cfl_allowed = lossless ? (cbw4 == 1 && cbh4 == 1) :
        !!(STBV_AV1_CFL_ALLOWED_MASK & (1U << bs));
    if (intra_flag) {
        if (stb_av1_intra_state_decode_leaf(msac, cdf, &state->intra,
                                             bx4, by4, bs, cfl_allowed,
                                             has_chroma, &intra))
            return -3;
    } else {
        /* IBC: no intra mode decode; set defaults for ctx. */
        memset(&intra, 0, sizeof(intra));
        intra.y_mode = STBV_AV1_INTRA_DC;
        intra.uv_mode = STBV_AV1_INTRA_DC;
    }
    /* Palette, filter-intra, and palette indices: intra-only.
     * IBC blocks skip all of these (dav1d decode.c:1267). */
    state->pal_sz_y = 0;
    state->pal_sz_uv = 0;
    if (intra_flag) {
        if (frame && frame->allow_screen_content_tools &&
            (bw4 > bh4 ? bw4 : bh4) <= 16 && bw4 + bh4 >= 4) {
            int sz_ctx = stbv_av1_block_dimensions[bs][2] +
                         stbv_av1_block_dimensions[bs][3] - 2;
            int bpc = 8 + (seq ? seq->hbd : 0) * 2;
            if (intra.y_mode == STBV_AV1_INTRA_DC) {
                int pal_ctx = 0;
                int above_palsz = 0, left_palsz = 0;
                if (state->above_pal_sz && (unsigned)bx4 < state->above_pal_sz_n &&
                    state->above_pal_sz[bx4] > 0) {
                    pal_ctx++;
                    above_palsz = state->above_pal_sz[bx4];
                }
                if (state->left_pal_sz && (unsigned)by4 < state->left_pal_sz_n &&
                    state->left_pal_sz[by4] > 0) {
                    pal_ctx++;
                    left_palsz = state->left_pal_sz[by4];
                }
                if (by4 == 0 && bx4 >= 156) {
                    static FILE *fp = NULL;
                    if (!fp) fp = fopen("ibc_debug.txt", "a");
                    if (fp) {
                        fprintf(fp, "PAL-BOOL bx4=%d by4=%d bw4=%d bh4=%d sz_ctx=%d pal_ctx=%d above=%d left=%d y_mode=%d uv_mode=%d has_chroma=%d\n",
                                bx4, by4, bw4, bh4, sz_ctx, pal_ctx, above_palsz, left_palsz,
                                intra.y_mode, intra.uv_mode, has_chroma);
                        fflush(fp);
                    }
                }
                {
                    unsigned rng_before = msac ? (unsigned)msac->rng : 0;
                    int pal_result = stb_av1_msac_bool_adapt(msac,
                                                cdf->pal_y + sz_ctx * 6 + pal_ctx * 2);
                    if (pal_result) {
                        if (stbv_av1_palette_read_plane(msac, cdf, state, 0, sz_ctx,
                                                        bx4, by4, bpc, state->pal_y,
                                                        &state->pal_sz_y))
                            return -7;
                    }
                }
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
                }
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

        /* Palette index maps come after filter-intra. */
        if (state->pal_sz_y) {
            if (stbv_av1_palette_indices(msac, cdf, 0, state->pal_sz_y,
                                         bw4, bh4, state->pal_tmp_y,
                                         state->pal_order, state->pal_ctxs))
                return -7;
        }
        if (state->pal_sz_uv) {
            if (stbv_av1_palette_indices(msac, cdf, 1, state->pal_sz_uv,
                                         cbw4, cbh4, state->pal_tmp,
                                         state->pal_order, state->pal_ctxs))
                return -8;
        }
    } /* end intra-only palette/filter-intra */

    /* block_info hook: fires AFTER all mode decisions are final (including
     * filter_intra override), so reconstruction uses the correct mode.
     * For palette blocks this still fires but luma_pal/chroma_pal will
     * overwrite the prediction afterwards. */
    if (c.recon && c.recon->block_info) {
        if (by4 == 0 && bx4 >= 140) {
            static FILE *fp = NULL;
            if (!fp) fp = fopen("ibc_debug.txt", "a");
            if (fp) {
                fprintf(fp, "LEAF-CALL bx4=%d by4=%d bw4=%d bh4=%d bs=%d intra=%d pal_sz_y=%d pal_sz_uv=%d\n",
                        bx4, by4, bw4, bh4, bs, intra_flag, state->pal_sz_y, state->pal_sz_uv);
                fflush(fp);
            }
        }
        c.recon->block_info(c.recon->ud, intra_flag, bs, bx4, by4,
                            has_chroma, cbw4, cbh4, 0, 0,
                            state->pal_sz_y, state->pal_sz_uv,
                            (int)block_skip,
                            intra.y_mode, intra.y_angle, intra.uv_mode,
                            intra.uv_angle,
                            intra.cfl_alpha_u, intra.cfl_alpha_v,
                             c.ibc_mv_y, c.ibc_mv_x);
    }


    /* Palette pixel application must run AFTER block_info (the callbacks
     * read the recon context's current block position) and before the
     * coefficient loop; txb prediction is suppressed for palette blocks
     * so nothing overwrites these pixels. */
    if (state->pal_sz_y && c.recon && c.recon->luma_pal)
        c.recon->luma_pal(c.recon->ud, state->pal_tmp_y, state->pal_sz_y,
                          bw4, bh4, state->pal_y);
    if (state->pal_sz_uv && c.recon && c.recon->chroma_pal) {
        c.recon->chroma_pal(c.recon->ud, 0, state->pal_tmp, state->pal_sz_uv, cbw4, cbh4, state->pal_u);
        c.recon->chroma_pal(c.recon->ud, 1, state->pal_tmp, state->pal_sz_uv, cbw4, cbh4, state->pal_v);
    }

    /* NOTE: neighbour-mode / palette context writes happen AFTER the
     * reconstruction loop below (dav1d calls set_ctx after recon), so
     * prediction reads the PRE-BLOCK neighbour state. */

    /* Transform size.  dav1d: lossless blocks are forced to TX_4X4; the
     * maximum otherwise comes from max_txfm_size_for_bs[bs][plane]; with
     * TX_SWITCHABLE a tx-size symbol is coded when max > TX_4X4.
     *
     * For IBC blocks, the TX tree bools are decoded separately via
     * read_vartx_tree/read_tx_tree (dav1d decode.c:1352).  No single
     * tx-size symbol is decoded for IBC. */
    if (lossless) {
        tx0 = STBV_AV1_TX_4X4;
        uv_tx = STBV_AV1_TX_4X4;
        max_tx = STBV_AV1_TX_4X4;
    } else {
        tx0 = stbv_av1_max_tx_for_bs[bs][0];
        uv_tx = stbv_av1_max_tx_for_bs[bs][layout];
        max_tx = tx0;
        if (intra_flag &&
            frame && frame->txfm_mode == 1 &&
            stbv_av1_tx_dims[max_tx].max > STBV_AV1_TX_4X4) {
            tx0 = stbv_av1_decode_tx_size(msac, cdf, max_tx,
                                          stbv_av1_tx_is_large(state->tx.above_tx_intra, bx4,
                                                               stbv_av1_tx_dims[max_tx].lw,
                                                               state->tx.above_n) +
                                          stbv_av1_tx_is_large(state->tx.left_tx_intra, by4,
                                                               stbv_av1_tx_dims[max_tx].lh,
                                                               state->tx.left_n));
        }
    }

    c.msac = msac;
    c.cdf = cdf;
    c.state = state;
    c.frame = frame;
    c.intra = &intra;
    c.bs = bs;
    c.bw4 = bw4;
    c.bw4_unc = bw4_unc;
    c.bh4_unc = bh4_unc;
    c.bh4 = bh4;
    c.cbw4 = cbw4;
    c.cbh4 = cbh4;
    c.cbw4_unc = cbw4_unc;
    c.cbh4_unc = cbh4_unc;
    c.lossless = lossless;
    c.qidx = qidx;
    /* dav1d stores y_mode_nofilt in the neighbour mode maps (set_ctx):
     * FILTER_PRED maps to DC_PRED, NOT to the filter angle's mode. */
    y_mode_nofilt = intra.y_mode == STBV_AV1_INTRA_FILTER ?
        STBV_AV1_INTRA_DC : intra.y_mode;
    c.y_mode_nofilt = y_mode_nofilt;
    c.reduced_txtp_set = frame ? (int)frame->reduced_txtp_set : 0;
    c.hbd = seq ? (int)seq->hbd : 0;
    c.block_skip = (int)block_skip;
    c.is_intra = intra_flag;
    /* TXTP mode: dav1d maps FILTER_PRED to the filter angle's directional
     * mode (dav1d_filter_mode_to_y_mode), unlike the neighbour-mode map
     * which uses DC_PRED. */
    {
        static const int stb_filter_mode_to_y_mode[5] =
            { STBV_AV1_INTRA_DC, STBV_AV1_INTRA_VERT, STBV_AV1_INTRA_HOR,
              STBV_AV1_INTRA_HD, STBV_AV1_INTRA_DC };
        int ym = intra.y_mode;
        if (ym == STBV_AV1_INTRA_FILTER) {
            ym = stb_filter_mode_to_y_mode[intra.y_angle < 0 ? 0 :
                 (intra.y_angle > 4 ? 4 : intra.y_angle)];
        }
        c.y_mode_txtp = ym;
    }

    /* Coefficients: intra blocks use one transform size across the whole
     * block.  IBC blocks use a variable TX tree for luma and fixed uv_tx
     * for chroma (dav1d decode.c:1352 read_vartx_tree). */
    {
        int txw4 = stbv_av1_tx_dims[tx0].w;
        int txh4 = stbv_av1_tx_dims[tx0].h;
        int y4, x4, cx4, cy4, pl, r;
        int uv_txw4 = stbv_av1_tx_dims[uv_tx].w;
        int uv_txh4 = stbv_av1_tx_dims[uv_tx].h;
        int first = 1;
        int qy4, qx4, qh4, qw4, sch4, scw4;

        if (!block_skip) {
            for (qy4 = by4; qy4 < by4 + bh4; qy4 += 16) {
                qh4 = by4 + bh4 - qy4;
                if (qh4 > 16) qh4 = 16;
                sch4 = (qh4 + ss_ver) >> ss_ver;
                for (qx4 = bx4; qx4 < bx4 + bw4; qx4 += 16) {
                    qw4 = bx4 + bw4 - qx4;
                    if (qw4 > 16) qw4 = 16;
                    scw4 = (qw4 + ss_hor) >> ss_hor;

                    if (intra_flag) {
                        /* Intra: fixed tx0 luma + fixed uv_tx chroma */
                        for (y4 = qy4; y4 < qy4 + qh4; y4 += txh4) {
                            for (x4 = qx4; x4 < qx4 + qw4; x4 += txw4) {
                                r = stbv_av1_leaf_tx_plane(msac, cdf, &c,
                                                           x4, y4,
                                                           tx0, 0, &state->res,
                                                           bw4, bh4,
                                                           first ? out : NULL);
                                first = 0;
                                if (r) return -4;
                            }
                        }
                    } else {
                        /* IBC: variable TX tree for luma (dav1d read_vartx_tree +
                         * read_coef_tree).  Chroma uses fixed uv_tx. */
                        int ytxw = stbv_av1_tx_dims[max_tx].w;
                        int ytxh = stbv_av1_tx_dims[max_tx].h;
                        if (stbv_av1_tx_dims[max_tx].max > STBV_AV1_TX_4X4 &&
                            frame && frame->txfm_mode == 1) {
                            /* Variable TX: recursively read split bools from MSAC
                             * and decode coefficients at each leaf. */
                            int ty4, tx4;
                            for (ty4 = qy4; ty4 < qy4 + qh4; ty4 += ytxh) {
                                for (tx4 = qx4; tx4 < qx4 + qw4; tx4 += ytxw) {
                                    r = stbv_av1_decode_tx_tree(msac, cdf,
                                        &state->tx, max_tx, tx4, ty4,
                                        stbv_av1_ibc_luma_leaf, &c);
                                    if (r) return -4;
                                }
                            }
                        } else {
                            /* Fixed max_tx luma (non-switchable or lossless) */
                            for (y4 = qy4; y4 < qy4 + qh4; y4 += txh4) {
                                for (x4 = qx4; x4 < qx4 + qw4; x4 += txw4) {
                                    r = stbv_av1_leaf_tx_plane(msac, cdf, &c,
                                                               x4, y4,
                                                               max_tx, 0,
                                                               &state->res,
                                                               bw4, bh4,
                                                               first ? out : NULL);
                                    first = 0;
                                    if (r) return -4;
                                }
                            }
                        }
                    }

                    /* Chroma: fixed uv_tx (both intra and IBC). */
                    if (!has_chroma) continue;
                    {
                        int cbx4 = qx4 >> ss_hor;
                        int cby4 = qy4 >> ss_ver;
                        for (pl = 0; pl < 2; pl++) {
                            for (cy4 = cby4; cy4 < cby4 + sch4;
                                 cy4 += uv_txh4) {
                                for (cx4 = cbx4; cx4 < cbx4 + scw4;
                                     cx4 += uv_txw4) {
                                    r = stbv_av1_leaf_tx_plane(msac, cdf,
                                        &c, cx4, cy4, uv_tx, pl + 1,
                                        &state->cres[pl], cbw4, cbh4,
                                        NULL);
                                    if (r) return -4;
                                }
                            }
                        }
                    }
                }
            }
        } else {
            /* dav1d read_coef_blocks marks the full block edges 0x40. */
            /* dav1d memsets context with UNCLIPPED b_dim; clipping here
             * left unit 383 unmarked for boundary blocks (bh4=7 case),
             * corrupting skip_ctx for every later block in the SB row. */
            stbv_av1_res_mark_unc(&state->res, bx4, by4, bw4_unc, bh4_unc,
                                  (stbv_u8)0x40);
            if (has_chroma) {
                int cbx4 = bx4 >> ss_hor;
                int cby4 = by4 >> ss_ver;
                for (pl = 0; pl < 2; pl++)
                    stbv_av1_res_mark_unc(&state->cres[pl], cbx4, cby4,
                                          (bw4_unc + ss_hor) >> ss_hor,
                                          (bh4_unc + ss_ver) >> ss_ver,
                                          (stbv_u8)0x40);
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
            /* Skip blocks have no coefficients, but intra prediction must
             * still be written (dav1d recon_b_intra: prediction always runs,
             * skip suppresses only the residual).  Without this, skip-block
             * chroma planes remain zero (calloc), producing green output. */
            if (c.recon) {
                int txw4 = stbv_av1_tx_dims[tx0].w;
                int txh4 = stbv_av1_tx_dims[tx0].h;
                int uv_txw4 = stbv_av1_tx_dims[uv_tx].w;
                int uv_txh4 = stbv_av1_tx_dims[uv_tx].h;
                int qy4, qx4, qh4, qw4, sch4, scw4;
                int txtp_skip = lossless ? STBV_AV1_TX_WHT_WHT
                                         : STBV_AV1_TX_DCT_DCT;
                for (qy4 = by4; qy4 < by4 + bh4; qy4 += 16) {
                    qh4 = by4 + bh4 - qy4;
                    if (qh4 > 16) qh4 = 16;
                    sch4 = (qh4 + ss_ver) >> ss_ver;
                    for (qx4 = bx4; qx4 < bx4 + bw4; qx4 += 16) {
                        int y4, x4, cy4, cx4;
                        qw4 = bx4 + bw4 - qx4;
                        if (qw4 > 16) qw4 = 16;
                        scw4 = (qw4 + ss_hor) >> ss_hor;
                        if (c.recon->luma_txb) {
                            for (y4 = qy4; y4 < qy4 + qh4; y4 += txh4)
                                for (x4 = qx4; x4 < qx4 + qw4; x4 += txw4)
                                    c.recon->luma_txb(c.recon->ud, x4, y4,
                                        tx0, txtp_skip, -1, NULL);
                        }
                        if (has_chroma && c.recon->chroma_txb) {
                            int cbx4 = qx4 >> ss_hor;
                            int cby4 = qy4 >> ss_ver;
                            for (pl = 0; pl < 2; pl++)
                                for (cy4 = cby4; cy4 < cby4 + sch4;
                                     cy4 += uv_txh4)
                                    for (cx4 = cbx4; cx4 < cbx4 + scw4;
                                         cx4 += uv_txw4)
                                        c.recon->chroma_txb(c.recon->ud,
                                            pl, cx4, cy4, uv_tx, txtp_skip,
                                            -1, NULL);
                        }
                    }
                }
            }
        }

        /* Tx neighbour map: dav1d sets it to the block tx dimensions over the
         * whole block edge after reconstruction; skipped blocks use the
         * block's default tx size (dav1d b_dim[2+i], set_ctx skip path).
         * dav1d also writes to tx_intra (MAX tx lw/lh) which is used by
         * get_tx_ctx for the next block's TX context. */
        {
            int txm = (int)block_skip ? max_tx : tx0;
            for (i = 0; i < bw4 && (unsigned int)(bx4 + i) < state->tx.above_n; i++)
                state->tx.above_tx[bx4 + i] =
                    (stbv_u8)stbv_av1_tx_dims[txm].lw;
            for (i = 0; i < bh4 && (unsigned int)(by4 + i) < state->tx.left_n; i++)
                state->tx.left_tx[by4 + i] =
                    (stbv_u8)stbv_av1_tx_dims[txm].lh;
            /* tx_intra: for intra blocks store decoded TX lw/lh; for IBC
             * blocks store max TX (dav1d: intra set_ctx uses t_dim->lw/lh
             * which is the decoded TX; IBC set_ctx uses b_dim[2+i] which
             * is the max TX). get_tx_ctx() compares this against the next
             * block's max TX to form the TX size context. */
            {
                int lw, lh;
                if (intra_flag) {
                    lw = stbv_av1_tx_dims[tx0].lw;
                    lh = stbv_av1_tx_dims[tx0].lh;
                } else {
                    lw = stbv_av1_tx_dims[max_tx].lw;
                    lh = stbv_av1_tx_dims[max_tx].lh;
                }
                if (state->tx.above_tx_intra) {
                    for (i = 0; i < bw4 && (unsigned int)(bx4 + i) < state->tx.above_n; i++)
                        state->tx.above_tx_intra[bx4 + i] = (stbv_u8)lw;
                }
                if (state->tx.left_tx_intra) {
                    for (i = 0; i < bh4 && (unsigned int)(by4 + i) < state->tx.left_n; i++)
                        state->tx.left_tx_intra[by4 + i] = (stbv_u8)lh;
                }
            }
        }
    }

    /* Neighbour context writes: dav1d set_ctx runs AFTER reconstruction,
     * so prediction inside the loop above sees the pre-block state. */
    if (state->intra.above_mode) {
        for (i = 0; i < bw4 && (unsigned)(bx4 + i) < state->intra.above_count; i++)
            state->intra.above_mode[bx4 + i] = (stbv_u8)y_mode_nofilt;
    }
    if (state->intra.left_mode) {
        for (i = 0; i < bh4 && (unsigned)(by4 + i) < state->intra.left_count; i++)
            state->intra.left_mode[by4 + i] = (stbv_u8)y_mode_nofilt;
    }
    if (has_chroma && state->intra.above_uvmode) {
        const int cbx4u = bx4 >> ss_hor;
        for (i = 0; i < cbw4_unc && (unsigned)(cbx4u + i) < state->intra.above_uv_count; i++)
            state->intra.above_uvmode[cbx4u + i] = (stbv_u8)intra.uv_mode;
    }
    if (has_chroma && state->intra.left_uvmode) {
        const int cby4u = by4 >> ss_ver;
        for (i = 0; i < cbh4_unc && (unsigned)(cby4u + i) < state->intra.left_uv_count; i++)
            state->intra.left_uvmode[cby4u + i] = (stbv_u8)intra.uv_mode;
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

    /* IBC MV neighbour splat (dav1d decode.c splat_intrabc_mv + set_ctx).
     * For IBC blocks, store the decoded MV in the above/left arrays so
     * subsequent IBC blocks can use it as a prediction candidate. */
    if (!intra_flag) {
        if (state->above_ibc_mv_y && state->above_ibc_valid) {
            for (i = 0; i < bw4 && (unsigned)(bx4 + i) < state->above_ibc_mv_n; i++) {
                state->above_ibc_mv_y[bx4 + i] = c.ibc_mv_y;
                state->above_ibc_mv_x[bx4 + i] = c.ibc_mv_x;
                state->above_ibc_valid[bx4 + i] = 1;
            }
        }
        if (state->left_ibc_mv_y && state->left_ibc_valid) {
            for (i = 0; i < bh4 && (unsigned)(by4 + i) < state->left_ibc_mv_n; i++) {
                state->left_ibc_mv_y[by4 + i] = c.ibc_mv_y;
                state->left_ibc_mv_x[by4 + i] = c.ibc_mv_x;
                state->left_ibc_valid[by4 + i] = 1;
            }
        }
    }
    return 0;
}

#endif /* STB_AV1_LEAF_H */
