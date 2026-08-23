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
} stbv_av1_tx_state;

static void stbv_av1_tx_state_init(stbv_av1_tx_state *s,
                                   stbv_u8 *above_tx, unsigned int above_n,
                                   stbv_u8 *left_tx, unsigned int left_n)
{
    if (!s) return;
    s->above_tx = above_tx;
    s->left_tx = left_tx;
    s->above_n = above_n;
    s->left_n = left_n;
    /* dav1d resets the above contexts once per frame (reset_context(&f->a));
     * a missing neighbour reads 0xff, compares "larger or equal" to any real
     * transform, and contributes 1 to the tx-size context.  The left context
     * is also reset at frame start; per row only the left is re-reset. */
    if (above_tx) memset(above_tx, 0xff, above_n);
    if (left_tx) memset(left_tx, 0xff, left_n);
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

static int stbv_av1_tx_tree_rec(struct stb_av1_msac *msac,
                                stbv_av1_cdf *cdf,
                                stbv_av1_tx_state *s,
                                int from,
                                int depth,
                                int x4,
                                int y4,
                                stbv_av1_tx_leaf_fn leaf,
                                void *opaque)
{
    int txw, txh, cat, a, l, split;
    int sub, subw, subh;

    if (!msac || !cdf || !s || from < STBV_AV1_TX_4X4 ||
        from > STBV_AV1_TX_64X64)
        return -1;

    txw = stbv_av1_tx_dims[from].lw;
    txh = stbv_av1_tx_dims[from].lh;
    split = 0;

    if (depth < 2 && from > STBV_AV1_TX_4X4) {
        /* This is exactly the context used by dav1d read_tx_tree():
         * a = above tx is smaller than txw, l = left tx is smaller than txh.
         */
        a = stbv_av1_tx_is_smaller(s->above_tx, x4, txw, s->above_n);
        l = stbv_av1_tx_is_smaller(s->left_tx,  y4, txh, s->left_n);
        cat = 2 * (STBV_AV1_TX_64X64 - stbv_av1_tx_dims[from].max) - depth;
        if (cat < 0) cat = 0;
        if (cat > 6) cat = 6;
        split = stb_av1_msac_bool_adapt(msac,
                    &cdf->txpart[(cat * 3 + a + l) * 2]);
    }

    if (split && stbv_av1_tx_dims[from].max > STBV_AV1_TX_8X8) {
        sub = stbv_av1_tx_dims[from].sub;
        subw = stbv_av1_tx_dims[sub].w;
        subh = stbv_av1_tx_dims[sub].h;

        if (stbv_av1_tx_tree_rec(msac, cdf, s, sub, depth + 1,
                                 x4, y4, leaf, opaque)) return -2;
        if (txw >= txh) {
            if (stbv_av1_tx_tree_rec(msac, cdf, s, sub, depth + 1,
                                     x4 + subw, y4, leaf, opaque)) return -3;
        }
        if (txh >= txw) {
            if (stbv_av1_tx_tree_rec(msac, cdf, s, sub, depth + 1,
                                     x4, y4 + subh, leaf, opaque)) return -4;
            if (txw >= txh) {
                if (stbv_av1_tx_tree_rec(msac, cdf, s, sub, depth + 1,
                                         x4 + subw, y4 + subh,
                                         leaf, opaque)) return -5;
            }
        }
        return 0;
    }

    /* dav1d updates the above and left tx maps at every leaf. */
    {
        int i;
        for (i = 0; i < txw && (unsigned int)(x4 + i) < s->above_n; i++)
            s->above_tx[x4 + i] = (stbv_u8)(split ? STBV_AV1_TX_4X4 : txw);
        for (i = 0; i < txh && (unsigned int)(y4 + i) < s->left_n; i++)
            s->left_tx[y4 + i] = (stbv_u8)(split ? STBV_AV1_TX_4X4 : txh);
    }

    if (leaf)
        return leaf(x4, y4, from, opaque);
    return 0;
}

static int stbv_av1_decode_tx_tree(struct stb_av1_msac *msac,
                                   stbv_av1_cdf *cdf,
                                   stbv_av1_tx_state *s,
                                   int max_tx,
                                   int x4,
                                   int y4,
                                   stbv_av1_tx_leaf_fn leaf,
                                   void *opaque)
{
    return stbv_av1_tx_tree_rec(msac, cdf, s, max_tx, 0,
                                x4, y4, leaf, opaque);
}

#endif /* STB_AV1_TXSTATE_H */
