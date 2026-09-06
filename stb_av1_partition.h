/*
 * Minimal AV1 partition/block geometry helpers derived from dav1d 1.5.4.
 *
 * Copyright (c) 2018, VideoLAN and dav1d authors
 * Copyright (c) 2018, Two Orioles, LLC
 * All rights reserved.
 *
 * This file is intended for use with stb_avif's scalar AV1 decoder.
 */
#ifndef STB_AV1_PARTITION_H
#define STB_AV1_PARTITION_H

#ifndef STBV_U8_DEFINED
#error "stb_av1_partition.h requires stbv_u8 from stb_avif.h"
#endif

/* AV1 block partition types.  The numeric order is significant: it is the
 * order used by the partition CDFs in dav1d. */
enum stbv_av1_partition_type {
    STBV_AV1_PARTITION_NONE = 0,
    STBV_AV1_PARTITION_H,
    STBV_AV1_PARTITION_V,
    STBV_AV1_PARTITION_SPLIT,
    STBV_AV1_PARTITION_T_TOP_SPLIT,
    STBV_AV1_PARTITION_T_BOTTOM_SPLIT,
    STBV_AV1_PARTITION_T_LEFT_SPLIT,
    STBV_AV1_PARTITION_T_RIGHT_SPLIT,
    STBV_AV1_PARTITION_H4,
    STBV_AV1_PARTITION_V4,
    STBV_AV1_N_PARTITIONS
};

enum stbv_av1_block_level {
    STBV_AV1_BL_128X128 = 0,
    STBV_AV1_BL_64X64,
    STBV_AV1_BL_32X32,
    STBV_AV1_BL_16X16,
    STBV_AV1_BL_8X8,
    STBV_AV1_N_BL_LEVELS
};

enum stbv_av1_block_size {
    STBV_AV1_BS_128x128 = 0,
    STBV_AV1_BS_128x64,
    STBV_AV1_BS_64x128,
    STBV_AV1_BS_64x64,
    STBV_AV1_BS_64x32,
    STBV_AV1_BS_64x16,
    STBV_AV1_BS_32x64,
    STBV_AV1_BS_32x32,
    STBV_AV1_BS_32x16,
    STBV_AV1_BS_32x8,
    STBV_AV1_BS_16x64,
    STBV_AV1_BS_16x32,
    STBV_AV1_BS_16x16,
    STBV_AV1_BS_16x8,
    STBV_AV1_BS_16x4,
    STBV_AV1_BS_8x32,
    STBV_AV1_BS_8x16,
    STBV_AV1_BS_8x8,
    STBV_AV1_BS_8x4,
    STBV_AV1_BS_4x16,
    STBV_AV1_BS_4x8,
    STBV_AV1_BS_4x4,
    STBV_AV1_N_BS_SIZES
};

/* Number of 4x4 units in width/height and log2(width/height) for each block
 * size.  This is the useful subset of dav1d_block_dimensions[]. */
static const stbv_u8 stbv_av1_block_dimensions[STBV_AV1_N_BS_SIZES][4] = {
    {32,32,5,5}, {32,16,5,4}, {16,32,4,5}, {16,16,4,4},
    {16,8,4,3}, {16,4,4,2}, {8,16,3,4}, {8,8,3,3},
    {8,4,3,2}, {8,2,3,1}, {4,16,2,4}, {4,8,2,3},
    {4,4,2,2}, {4,2,2,1}, {4,1,2,0}, {2,8,1,3},
    {2,4,1,2}, {2,2,1,1}, {2,1,1,0}, {1,4,0,2},
    {1,2,0,1}, {1,1,0,0}
};

/* For each block level and partition, the first and (where applicable)
 * second child block sizes.  0xff means that the partition is not legal at
 * that level. */
static const stbv_u8 stbv_av1_block_sizes[STBV_AV1_N_BL_LEVELS]
                                      [STBV_AV1_N_PARTITIONS][2] = {
    /* 128x128 */
    {
        {STBV_AV1_BS_128x128, 0xff},
        {STBV_AV1_BS_128x64,  0xff},
        {STBV_AV1_BS_64x128,  0xff},
        {0xff, 0xff},
        {STBV_AV1_BS_64x64, STBV_AV1_BS_128x64},
        {STBV_AV1_BS_128x64, STBV_AV1_BS_64x64},
        {STBV_AV1_BS_64x64, STBV_AV1_BS_64x128},
        {STBV_AV1_BS_64x128, STBV_AV1_BS_64x64},
        {0xff, 0xff}, {0xff, 0xff}
    },
    /* 64x64 */
    {
        {STBV_AV1_BS_64x64, 0xff},
        {STBV_AV1_BS_64x32, 0xff},
        {STBV_AV1_BS_32x64, 0xff},
        {0xff, 0xff},
        {STBV_AV1_BS_32x32, STBV_AV1_BS_64x32},
        {STBV_AV1_BS_64x32, STBV_AV1_BS_32x32},
        {STBV_AV1_BS_32x32, STBV_AV1_BS_32x64},
        {STBV_AV1_BS_32x64, STBV_AV1_BS_32x32},
        {STBV_AV1_BS_64x16, 0xff},
        {STBV_AV1_BS_16x64, 0xff}
    },
    /* 32x32 */
    {
        {STBV_AV1_BS_32x32, 0xff},
        {STBV_AV1_BS_32x16, 0xff},
        {STBV_AV1_BS_16x32, 0xff},
        {0xff, 0xff},
        {STBV_AV1_BS_16x16, STBV_AV1_BS_32x16},
        {STBV_AV1_BS_32x16, STBV_AV1_BS_16x16},
        {STBV_AV1_BS_16x16, STBV_AV1_BS_16x32},
        {STBV_AV1_BS_16x32, STBV_AV1_BS_16x16},
        {STBV_AV1_BS_32x8, 0xff},
        {STBV_AV1_BS_8x32, 0xff}
    },
    /* 16x16 */
    {
        {STBV_AV1_BS_16x16, 0xff},
        {STBV_AV1_BS_16x8, 0xff},
        {STBV_AV1_BS_8x16, 0xff},
        {0xff, 0xff},
        {STBV_AV1_BS_8x8, STBV_AV1_BS_16x8},
        {STBV_AV1_BS_16x8, STBV_AV1_BS_8x8},
        {STBV_AV1_BS_8x8, STBV_AV1_BS_8x16},
        {STBV_AV1_BS_8x16, STBV_AV1_BS_8x8},
        {STBV_AV1_BS_16x4, 0xff},
        {STBV_AV1_BS_4x16, 0xff}
    },
    /* 8x8 */
    {
        {STBV_AV1_BS_8x8, 0xff},
        {STBV_AV1_BS_8x4, 0xff},
        {STBV_AV1_BS_4x8, 0xff},
        {STBV_AV1_BS_4x4, 0xff},
        {0xff, 0xff}, {0xff, 0xff}, {0xff, 0xff}, {0xff, 0xff},
        {0xff, 0xff}, {0xff, 0xff}
    }
};

static const stbv_u8 stbv_av1_partition_type_count[STBV_AV1_N_BL_LEVELS] = {
    7, 9, 9, 9, 3
};

/* Convert a partition-tree level to the corresponding CDF slice. */
static stbv_u16 *stbv_av1_partition_cdf(stbv_u16 *cdf,
                                        int level, int ctx)
{
    return cdf + level * 64 + ctx * 16;
}

/* This is the same context derivation used by dav1d's get_partition_ctx().
 * The caller stores the partition-depth mask in an 8x8-unit grid. */
static int stbv_av1_partition_ctx(const stbv_u8 *above,
                                  const stbv_u8 *left,
                                  int stride,
                                  int xb8, int yb8, int level)
{
    int a = (above[xb8] >> (4 - level)) & 1;
    int l = (left[yb8 * stride] >> (4 - level)) & 1;
    return a + (l << 1);
}

/* Set the partition-depth mask for the 8x8 cells covered by a block. */
static void stbv_av1_partition_mark(stbv_u8 *grid, int stride,
                                    int x8, int y8, int w8, int h8,
                                    int level)
{
    int y, x;
    stbv_u8 bit = (stbv_u8)(1 << (4 - level));
    for (y = 0; y < h8; y++) {
        for (x = 0; x < w8; x++)
            grid[(y8 + y) * stride + x8 + x] = bit;
    }
}

#endif /* STB_AV1_PARTITION_H */
