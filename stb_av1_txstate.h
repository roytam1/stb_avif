/*
 * stb_av1_txstate.h - AV1 transform-neighbour state
 *
 * The layout follows the part of dav1d's BlockContext used by read_tx_tree():
 * one transform-size value per 4x4 column on the above edge and one per 4x4
 * row on the left edge.  Values are expressed as transform log2-minus-2
 * dimensions (TX_4X4 == 0, TX_8X8 == 1, ...), just like dav1d's tx context.
 */
#ifndef STB_AV1_TXSTATE_H
#define STB_AV1_TXSTATE_H

#include <stddef.h>
#include <string.h>

#ifndef STB_AV1_TX_H
#error "include stb_av1_tx.h first"
#endif

typedef struct stbv_av1_tx_state {
    stbv_u8 *above_tx;
    stbv_u8 *left_tx;
    unsigned int above_n;
    unsigned int left_n;
    /* dav1d's tx_intra: stores the MAX transform size (lw/lh) per 4x4
     * position, used by get_tx_ctx() for the next block's TX context.
     * above_tx/left_tx store the decoded TX (updated by read_tx_tree at
     * leaf nodes) and are used by the tree split decision. */
    stbv_u8 *above_tx_intra;
    stbv_u8 *left_tx_intra;
} stbv_av1_tx_state;

static void stbv_av1_tx_state_init(stbv_av1_tx_state *s,
                                   stbv_u8 *above_tx, unsigned int above_n,
                                   stbv_u8 *left_tx, unsigned int left_n,
                                   stbv_u8 *above_tx_intra,
                                   stbv_u8 *left_tx_intra)
{
    if (!s) return;
    s->above_tx = above_tx;
    s->left_tx = left_tx;
    s->above_n = above_n;
    s->left_n = left_n;
    s->above_tx_intra = above_tx_intra;
    s->left_tx_intra = left_tx_intra;
    /* dav1d resets the above contexts once per frame (reset_context(&f->a));
     * a missing neighbour reads 0xff, compares "larger or equal" to any real
     * transform, and contributes 1 to the tx-size context.  The left context
     * is also reset at frame start; per row only the left is re-reset. */
    if (above_tx) memset(above_tx, 0xff, above_n);
    if (left_tx) memset(left_tx, 0xff, left_n);
    if (above_tx_intra) memset(above_tx_intra, 0xff, above_n);
    if (left_tx_intra) memset(left_tx_intra, 0xff, left_n);
}

static int stbv_av1_tx_is_smaller(const stbv_u8 *edge, int pos4, int tx_dim,
                                  unsigned int n)
{
    if (!edge || pos4 < 0 || (unsigned int)pos4 >= n)
        return 0;
    return edge[pos4] < tx_dim;
}

static int stbv_av1_tx_is_large(const stbv_u8 *edge, int pos4, int tx_dim,
                                unsigned int n)
{
    /* dav1d's tx_intra is int8_t and reset to -1: a missing neighbour is
     * never "larger or equal" than any real transform (log2 0..4).  The
     * 0xff sentinel must therefore compare as false. */
    if (!edge || pos4 < 0 || (unsigned int)pos4 >= n)
        return 0;
    return edge[pos4] != 0xff && edge[pos4] >= tx_dim;
}

/* dav1d resets only the LEFT transform context at each superblock row;
 * the above contexts persist across rows (reset once per frame). */
static void stbv_av1_tx_state_reset_row(stbv_av1_tx_state *s)
{
    if (!s) return;
    if (s->left_tx) memset(s->left_tx, 0xff, s->left_n);
    if (s->left_tx_intra) memset(s->left_tx_intra, 0xff, s->left_n);
}

/*
 * Decode and write one variable-transform tree.  This is the scalar form of
 * dav1d's read_tx_tree().  x4/y4 are frame-local 4x4 coordinates; the edge
 * arrays are tile/superblock-local and are therefore indexed modulo 32.
 *
 * The callback is called for every transform leaf.  For each leaf, *tx_out
 * is the selected transform size.  The callback can then decode transform
 * type and coefficients at that exact location.
 */
typedef int (*stbv_av1_tx_leaf_fn)(int x4, int y4, int tx,
                                   void *opaque);

/*
 * Phase 1: Read split bools and store in masks.
 * Matches dav1d's read_tx_tree() - only reads split decisions from MSAC,
 * does NOT decode coefficients.  Updates above/left TX context at leaves.
 * x4/y4 are frame-local 4x4 coordinates for context array indexing.
 * x_off/y_off are 0-based offsets within the TX hierarchy for mask indexing.
 */
static void stbv_av1_tx_tree_read_splits(struct stb_av1_msac *msac,
                                         stbv_av1_cdf *cdf,
                                         stbv_av1_tx_state *s,
                                         int from, int depth,
                                         stbv_u16 *masks,
                                         int x4, int y4,
                                         int x_off, int y_off)
{
    const int txw = stbv_av1_tx_dims[from].lw;
    const int txh = stbv_av1_tx_dims[from].lh;
    int is_split = 0;

    if (depth < 2 && from > STBV_AV1_TX_4X4) {
        const int cat = 2 * (STBV_AV1_TX_64X64 - stbv_av1_tx_dims[from].max) - depth;
        const int a = stbv_av1_tx_is_smaller(s->above_tx, x4, txw, s->above_n);
        const int l = stbv_av1_tx_is_smaller(s->left_tx,  y4, txh, s->left_n);
        int cat_clamp = cat < 0 ? 0 : (cat > 6 ? 6 : cat);
        is_split = stb_av1_msac_bool_adapt(msac,
                    &cdf->txpart[(cat_clamp * 3 + a + l) * 2]);
        if (is_split)
            masks[depth] |= 1 << (y_off * 4 + x_off);
    } else {
        is_split = 0;
    }

    if (is_split && stbv_av1_tx_dims[from].max > STBV_AV1_TX_8X8) {
        const int sub = stbv_av1_tx_dims[from].sub;
        const int subw4 = stbv_av1_tx_dims[sub].w;
        const int subh4 = stbv_av1_tx_dims[sub].h;

        stbv_av1_tx_tree_read_splits(msac, cdf, s, sub, depth + 1,
                                     masks, x4, y4,
                                     x_off * 2 + 0, y_off * 2 + 0);
        if (txw >= txh)
            stbv_av1_tx_tree_read_splits(msac, cdf, s, sub, depth + 1,
                                         masks, x4 + subw4, y4,
                                         x_off * 2 + 1, y_off * 2 + 0);
        if (txh >= txw) {
            stbv_av1_tx_tree_read_splits(msac, cdf, s, sub, depth + 1,
                                         masks, x4, y4 + subh4,
                                         x_off * 2 + 0, y_off * 2 + 1);
            if (txw >= txh)
                stbv_av1_tx_tree_read_splits(msac, cdf, s, sub, depth + 1,
                                             masks, x4 + subw4, y4 + subh4,
                                             x_off * 2 + 1, y_off * 2 + 1);
        }
    } else {
        /* Leaf: update above/left tx context.
         * dav1d stores the log2 TX dimension (txw/txh) and writes
         * 1<<txw bytes (dav1d_memset_pow2[lw]). */
        int i;
        for (i = 0; i < (1 << txw) && (unsigned int)(x4 + i) < s->above_n; i++)
            s->above_tx[x4 + i] = (stbv_u8)(is_split ? STBV_AV1_TX_4X4 : txw);
        for (i = 0; i < (1 << txh) && (unsigned int)(y4 + i) < s->left_n; i++)
            s->left_tx[y4 + i] = (stbv_u8)(is_split ? STBV_AV1_TX_4X4 : txh);
    }
}

/*
 * Phase 2: Traverse the split masks and decode coefficients at leaves.
 * Matches dav1d's read_coef_tree() - uses the pre-computed tx_split masks
 * to traverse the tree, calling the leaf callback at each leaf.
 * x4/y4 are frame-local 4x4 coordinates for the leaf callback.
 * x_off/y_off are 0-based offsets within the TX hierarchy for mask indexing.
 */
static int stbv_av1_tx_tree_read_coefs(struct stb_av1_msac *msac,
                                       stbv_av1_cdf *cdf,
                                       stbv_av1_tx_state *s,
                                       int from, int depth,
                                       const stbv_u16 *masks,
                                       int x4, int y4,
                                       int x_off, int y_off,
                                       stbv_av1_tx_leaf_fn leaf,
                                       void *opaque)
{
    const int txw = stbv_av1_tx_dims[from].w;
    const int txh = stbv_av1_tx_dims[from].h;

    if (depth < 2 && masks[depth] &&
        (masks[depth] & (1 << (y_off * 4 + x_off))))
    {
        const int sub = stbv_av1_tx_dims[from].sub;
        const int subw4 = stbv_av1_tx_dims[sub].w;
        const int subh4 = stbv_av1_tx_dims[sub].h;

        if (stbv_av1_tx_tree_read_coefs(msac, cdf, s, sub, depth + 1, masks,
                                        x4, y4,
                                        x_off * 2 + 0, y_off * 2 + 0,
                                        leaf, opaque)) return -1;
        if (txw >= txh) {
            if (stbv_av1_tx_tree_read_coefs(msac, cdf, s, sub, depth + 1, masks,
                                            x4 + subw4, y4,
                                            x_off * 2 + 1, y_off * 2 + 0,
                                            leaf, opaque)) return -2;
        }
        if (txh >= txw) {
            if (stbv_av1_tx_tree_read_coefs(msac, cdf, s, sub, depth + 1, masks,
                                            x4, y4 + subh4,
                                            x_off * 2 + 0, y_off * 2 + 1,
                                            leaf, opaque)) return -3;
            if (txw >= txh) {
                if (stbv_av1_tx_tree_read_coefs(msac, cdf, s, sub, depth + 1, masks,
                                                x4 + subw4, y4 + subh4,
                                                x_off * 2 + 1, y_off * 2 + 1,
                                                leaf, opaque)) return -4;
            }
        }
        return 0;
    }

    if (leaf) {
        return leaf(x4, y4, from, opaque);
    }
    return 0;
}

/*
 * Two-pass TX tree decode matching dav1d's architecture:
 *   Pass 1: read_tx_tree - read all split bools, store in masks,
 *            update above/left TX context at leaves.
 *   Pass 2: read_coef_tree - traverse masks, decode coefficients at leaves.
 *
 * This ensures MSAC consumption order matches dav1d exactly.
 */
static int stbv_av1_decode_tx_tree(struct stb_av1_msac *msac,
                                   stbv_av1_cdf *cdf,
                                   stbv_av1_tx_state *s,
                                   int max_tx,
                                   int x4,
                                   int y4,
                                   stbv_av1_tx_leaf_fn leaf,
                                   void *opaque)
{
    stbv_u16 tx_split[2] = { 0, 0 };
    const stbv_av1_tx_dim *const ytx = &stbv_av1_tx_dims[max_tx];
    const int bw4 = ytx->w;
    const int bh4 = ytx->h;
    int x, y, x_off, y_off;

    if (!msac || !cdf || !s || max_tx < STBV_AV1_TX_4X4 ||
        max_tx >= STBV_AV1_N_TX_SIZES)
        return -1;

    /* Pass 1: Read all split bools (dav1d read_tx_tree) */
    for (y = 0, y_off = 0; y < bh4; y += ytx->h, y_off++) {
        for (x = 0, x_off = 0; x < bw4; x += ytx->w, x_off++) {
            stbv_av1_tx_tree_read_splits(msac, cdf, s, max_tx, 0,
                                         tx_split,
                                         x4 + x, y4 + y,
                                         x_off, y_off);
        }
    }

    /* Pass 2: Decode coefficients at leaves (dav1d read_coef_tree) */
    for (y = 0, y_off = 0; y < bh4; y += ytx->h, y_off++) {
        for (x = 0, x_off = 0; x < bw4; x += ytx->w, x_off++) {
            int r = stbv_av1_tx_tree_read_coefs(msac, cdf, s, max_tx, 0,
                                                tx_split,
                                                x4 + x, y4 + y,
                                                x_off, y_off,
                                                leaf, opaque);
            if (r) return r;
        }
    }

    return 0;
}

#endif /* STB_AV1_TXSTATE_H */
