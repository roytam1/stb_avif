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
#include "stb_av1_avifbox.h"

struct sample_ctx {
    stbv_av1_leaf_state state;
    stbv_u8 above[32];
    stbv_u8 left[32];
    unsigned long leaves;
    unsigned long nonzero;
    unsigned long skipped;
    unsigned long txtp[17];
    int err;
    int err_x4;
    int err_y4;
    int err_bs;
    int bypass;
};

static int sample_leaf(struct stb_av1_tile_decoder *td,
                       const struct stb_av1_tile_leaf_info *li,
                       void *opaque)
{
    struct sample_ctx *c = (struct sample_ctx *)opaque;
    stbv_av1_leaf_tx_result out;
    int r;

    if (c->bypass) {
        c->leaves++;
        c->skipped++;
        return 0;
    }

    if (li->bx == 288 && li->by == 0 && li->bs == 2) {
        const unsigned char *p = (const unsigned char *)&td->msac;
        int i;
        printf("SAMPLE pre-msac hex:");
        for (i = 0; i < 40; i++) printf(" %02x", p[i]);
        printf("\n");
    }

    r = stbv_av1_decode_leaf_syntax(&td->msac, &td->cdf, &c->state,
                                    td->frame, li->bs, li->bx, li->by,
                                    &out);
    if (r) {
        c->err = r;
        c->err_x4 = li->bx;
        c->err_y4 = li->by;
        c->err_bs = li->bs;
        printf("LEAF (bx=%d by=%d bs=%d): r=%d\n", li->bx, li->by, li->bs, r);
        return r;
    }
    if (li->bx == 272 && li->by == 16 && li->bs == 3) {
        const unsigned char *p = (const unsigned char *)&td->msac;
        int i;
        printf("SAMPLE post-(272,16) hex:");
        for (i = 0; i < 40; i++) printf(" %02x", p[i]);
        printf("\n");
    }
    printf("LEAF (bx=%d by=%d bs=%d): skip=%d txtp=%d eob=%d rng=%u\n",
           li->bx, li->by, li->bs, out.skipped, out.txtp, out.eob,
           td->msac.rng);
    c->leaves++;
    if (out.skipped) c->skipped++;
    else c->nonzero++;
    if (out.txtp >= 0 && out.txtp < 17) c->txtp[out.txtp]++;
    return 0;
}

static void sample_row_start(void *opaque)
{
    struct sample_ctx *c = (struct sample_ctx *)opaque;
    stbv_av1_leaf_state_reset_row(&c->state);
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

    /* If the input is an .avif, unwrap the container first. */
    {
        size_t narg = strlen(argv[1]);
        const stbv_u8 *stream = data;
        size_t stream_size = (size_t)n;
        if (narg >= 5 &&
            argv[1][narg - 5] == '.' && argv[1][narg - 4] == 'a' &&
            argv[1][narg - 3] == 'v' && argv[1][narg - 2] == 'i' &&
            argv[1][narg - 1] == 'f') {
            if (stbv_av1_extract_avif_item(data, (size_t)n, &stream,
                                            &stream_size)) {
                fprintf(stderr, "AVIF container parse failed\n");
                free(data);
                return 1;
            }
            printf("avif: item_data=%ld bytes\n", (long)stream_size);
            memmove(data, stream, stream_size);   /* keep malloc base for free() */
            n = (long)stream_size;
        }
    }

    r = stb_av1_parse_internal_stream(&st, data, (size_t)n);
    if (r) {
        fprintf(stderr, "OBU/header parse failed: %d\n", r);
        free(data); return 1;
    }
    printf("seq: sb128=%u hbd=%u monochrome=%u reduced_still=%u\n",
           st.seq.sb128, st.seq.hbd, st.seq.monochrome,
           st.seq.reduced_still_picture_header);
    printf("frm: frame_type=%u disable_cdf_update=%u allow_screen_content=%u\n",
           st.frame.frame_type, st.frame.disable_cdf_update,
           st.frame.allow_screen_content_tools);
    printf("frm: txfm_mode=%u reduced_txtp_set=%u\n",
           st.frame.txfm_mode, st.frame.reduced_txtp_set);

    ctx.leaves = 0;
    ctx.nonzero = 0;
    ctx.skipped = 0;
    ctx.bypass = getenv("STB_AV1_BYPASS_LEAF") != NULL;
    memset(ctx.txtp, 0, sizeof(ctx.txtp));
    stbv_av1_leaf_state_init(&ctx.state, ctx.above, 32, ctx.left, 32);

    r = stb_av1_decode_tile(&td, &st.seq, &st.frame,
                            st.tile_data, st.tile_size,
                            sample_leaf, &ctx, sample_row_start);
    printf("size=%ux%u q=%u tiles=%ux%u\n",
           st.frame.width[0], st.frame.height, st.frame.quant.yac,
           st.frame.tiling.cols, st.frame.tiling.rows);
    printf("tile_bytes=%lu partition_leaves=%u syntax_leaves=%lu nonzero=%lu result=%d\n",
           (unsigned long)st.tile_size, td.leaves, ctx.leaves,
           ctx.nonzero, r);
    printf("skipped=%lu non_skipped=%lu txtp0=%lu txtp1=%lu txtp9=%lu\n", ctx.skipped, ctx.nonzero, ctx.txtp[0], ctx.txtp[1], ctx.txtp[9]);
    if (ctx.err)
        printf("leaf error=%d at x4=%d y4=%d bs=%d\n",
               ctx.err, ctx.err_x4, ctx.err_y4, ctx.err_bs);
    free(data);
    return r ? 1 : 0;
}
