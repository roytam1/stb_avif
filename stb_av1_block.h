/*
 * stb_av1_block.h - first scalar intra leaf-block reconstruction layer
 *
 * This glues the currently ported scalar pieces together.  It deliberately
 * accepts the already-decoded intra mode / transform parameters; frame/tile
 * syntax remains outside this layer.
 */
#ifndef STB_AV1_BLOCK_H
#define STB_AV1_BLOCK_H

#ifndef STBV_I32_DEFINED
#error "stb_av1_block.h requires stbv_i32"
#endif
#ifndef STBV_U8_DEFINED
#error "stb_av1_block.h requires stbv_u8"
#endif

/*
 * edge points at the top-left sample.  The predictor expects:
 *   edge[0]       top-left
 *   edge[1..]     top/right reference samples
 *   edge[-1..]    left/bottom reference samples
 *
 * The caller must provide enough samples for the selected directional mode.
 */
static int stbv_av1_recon_intra_block(struct stb_av1_msac *msac,
                                      stbv_av1_cdf *cdf,
                                      stbv_u8 *dst, int stride,
                                      const stbv_u8 *edge,
                                      int n, int mode, int angle,
                                      int tx_type, int chroma,
                                      int skip_ctx, int dc_sign_ctx,
                                      int dq_dc, int dq_ac, int dq_shift)
{
    stbv_i32 cf[32 * 32];
    stbv_i32 residual[32 * 32];
    stbv_u8 pred[32 * 32];
    int eob, x, y, v;

    if (n != 4 && n != 8 && n != 16 && n != 32)
        return -1;

    eob = stbv_av1_decode_coeffs_square(msac, cdf,
                                        n == 4 ? 0 : n == 8 ? 1 :
                                        n == 16 ? 2 : 3,
                                        chroma, 0, n, dq_dc, dq_ac,
                                        dq_shift, skip_ctx, dc_sign_ctx,
                                        cf, NULL);
    if (eob < 0)
        return eob;

    if (stbv_av1_inv_txfm_square(cf, n, eob, tx_type, residual))
        return -3;

    stbv_av1_intra_predict(pred, n, edge, n, n, mode, angle);

    for (y = 0; y < n; y++) {
        for (x = 0; x < n; x++) {
            v = (int)pred[y * n + x] + (int)residual[y * n + x];
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            dst[y * stride + x] = (stbv_u8)v;
        }
    }
    return eob;
}

#endif
