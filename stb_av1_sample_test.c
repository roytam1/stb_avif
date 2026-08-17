/*
 * stb_av1_sample_test.c
 *
 * C89 diagnostic for the scalar AV1 decoder. It accepts a low-overhead
 * AV1 OBU stream (not an AVIF container) and validates:
 * OBU -> sequence header -> frame header -> tile -> partition -> intra/TX
 * syntax -> coefficient syntax.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stb_av1_scalar.h"

struct sample_ctx {
    stbv_av1_leaf_state state;
    stbv_u8 above[32];
    stbv_u8 left[32];
    unsigned long leaves;
    unsigned long nonzero;
    unsigned long skipped;
    unsigned long txtp[17];
};

static int sample_leaf(struct stb_av1_tile_decoder *td,
                       const struct stb_av1_tile_leaf_info *li,
                       void *opaque)
{
    struct sample_ctx *c = (struct sample_ctx *)opaque;
    stbv_av1_leaf_tx_result out;
    int r;

    r = stbv_av1_decode_leaf_syntax(&td->msac, &td->cdf, &c->state,
                                    td->frame, li->bs, li->bx, li->by,
                                    &out);
    if (r)
        return r;
    c->leaves++;
    if (out.skipped) c->skipped++;
    else c->nonzero++;
    if (out.txtp >= 0 && out.txtp < 17) c->txtp[out.txtp]++;
    return 0;
}

int main(int argc, char **argv)
{
    FILE *f;
    long n;
    stbv_u8 *data;
    struct stb_av1_internal_stream st;
    struct stb_av1_tile_decoder td;
    struct sample_ctx ctx;
    int r;

    if (argc != 2) {
        fprintf(stderr, "usage: %s file.obu\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return 2; }
    data = (stbv_u8 *)malloc((size_t)n);
    if (!data) { fclose(f); return 2; }
    if (fread(data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f); free(data); return 2;
    }
    fclose(f);

    r = stb_av1_parse_internal_stream(&st, data, (size_t)n);
    if (r) {
        fprintf(stderr, "OBU/header parse failed: %d\n", r);
        free(data); return 1;
    }

    ctx.leaves = 0;
    ctx.nonzero = 0;
    ctx.skipped = 0;
    memset(ctx.txtp, 0, sizeof(ctx.txtp));
    stbv_av1_leaf_state_init(&ctx.state, ctx.above, 32, ctx.left, 32);

    r = stb_av1_decode_tile(&td, &st.seq, &st.frame,
                            st.tile_data, st.tile_size,
                            sample_leaf, &ctx);
    printf("size=%ux%u q=%u tiles=%ux%u\n",
           st.frame.width[0], st.frame.height, st.frame.quant.yac,
           st.frame.tiling.cols, st.frame.tiling.rows);
    printf("tile_bytes=%lu partition_leaves=%u syntax_leaves=%lu nonzero=%lu result=%d\n",
           (unsigned long)st.tile_size, td.leaves, ctx.leaves,
           ctx.nonzero, r);
    printf("skipped=%lu non_skipped=%lu txtp0=%lu txtp1=%lu txtp9=%lu\n", ctx.skipped, ctx.nonzero, ctx.txtp[0], ctx.txtp[1], ctx.txtp[9]);
    free(data);
    return r ? 1 : 0;
}
