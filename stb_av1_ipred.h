/*
 * stb_av1_ipred.h - scalar 8-bit AV1 intra prediction
 *
 * Intra prediction formulas and tables adapted from dav1d 1.5.4
 * src/ipred_tmpl.c and src/tables.c.
 *
 * Copyright (C) 2018-2021, VideoLAN and dav1d authors
 * Copyright (C) 2018, Two Orioles, LLC
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef STB_AV1_IPRED_H
#define STB_AV1_IPRED_H

#include <stdlib.h>

#define STBV_AV1_IPRED_DC          0
#define STBV_AV1_IPRED_VERT        1
#define STBV_AV1_IPRED_HOR         2
#define STBV_AV1_IPRED_DDL         3
#define STBV_AV1_IPRED_DDR         4
#define STBV_AV1_IPRED_VR          5
#define STBV_AV1_IPRED_HD          6
#define STBV_AV1_IPRED_HU          7
#define STBV_AV1_IPRED_VL          8
#define STBV_AV1_IPRED_SMOOTH      9
#define STBV_AV1_IPRED_SMOOTH_V   10
#define STBV_AV1_IPRED_SMOOTH_H   11
#define STBV_AV1_IPRED_PAETH      12
#define STBV_AV1_IPRED_FILTER     13

static const unsigned char stbv_av1_sm_weights[128] = {
      0,0, 255,128, 255,149,85,64,
      255,197,146,105,73,50,37,32,
      255,225,196,170,145,123,102,84,68,54,43,33,26,20,17,16,
      255,240,225,210,196,182,169,157,145,133,122,111,101,92,83,74,
      66,59,52,45,39,34,29,25,21,17,14,12,10,9,8,8,
      255,248,240,233,225,218,210,203,196,189,182,176,169,163,156,150,
      144,138,133,127,121,116,111,106,101,96,91,86,82,77,73,69,
      65,61,57,54,50,47,44,41,38,35,32,29,27,25,22,20,
      18,16,15,13,12,10,9,8,7,6,6,5,5,4,4,4
};

static const unsigned short stbv_av1_dr_deriv[44] = {
    0,1023,0,547,372,0,0,273,215,0,178,151,0,132,116,0,102,0,
    90,80,0,71,64,0,57,51,0,45,40,0,35,31,0,27,23,0,19,15,0,11,7,3
};

static int stbv_av1_clip8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

static unsigned stbv_av1_ctz_pow2(unsigned v)
{
    unsigned n = 0;
    while (v > 1U) { v >>= 1; n++; }
    return n;
}

static unsigned stbv_av1_dc_top(const unsigned char *tl, int w)
{
    unsigned dc = (unsigned)(w >> 1);
    int i;
    for (i = 0; i < w; i++) dc += tl[1 + i];
    return dc >> stbv_av1_ctz_pow2((unsigned)w);
}

static unsigned stbv_av1_dc_left(const unsigned char *tl, int h)
{
    unsigned dc = (unsigned)(h >> 1);
    int i;
    for (i = 0; i < h; i++) dc += tl[-(1 + i)];
    return dc >> stbv_av1_ctz_pow2((unsigned)h);
}

static unsigned stbv_av1_dc_both(const unsigned char *tl, int w, int h)
{
    unsigned dc = (unsigned)((w + h) >> 1);
    int i;
    for (i = 0; i < w; i++) dc += tl[1 + i];
    for (i = 0; i < h; i++) dc += tl[-(1 + i)];
    dc >>= stbv_av1_ctz_pow2((unsigned)(w + h));
    if (w != h) {
        if (w > h * 2 || h > w * 2)
            dc = (dc * 0x3334U) >> 16;
        else
            dc = (dc * 0x5556U) >> 16;
    }
    return dc;
}

static void stbv_av1_pred_dc(unsigned char *dst, int stride,
                             const unsigned char *tl, int w, int h)
{
    unsigned dc = stbv_av1_dc_both(tl, w, h);
    int x, y;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) dst[x] = (unsigned char)dc;
        dst += stride;
    }
}

static void stbv_av1_pred_vert(unsigned char *dst, int stride,
                               const unsigned char *tl, int w, int h)
{
    int y;
    for (y = 0; y < h; y++) {
        int x;
        for (x = 0; x < w; x++) dst[x] = tl[1 + x];
        dst += stride;
    }
}

static void stbv_av1_pred_hor(unsigned char *dst, int stride,
                              const unsigned char *tl, int w, int h)
{
    int y, x;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) dst[x] = tl[-1 - y];
        dst += stride;
    }
}

static void stbv_av1_pred_paeth(unsigned char *dst, int stride,
                                const unsigned char *tl, int w, int h)
{
    int y, x;
    int top_left = tl[0];
    for (y = 0; y < h; y++) {
        int left = tl[-1-y];
        for (x = 0; x < w; x++) {
            int top = tl[1+x];
            int base = left + top - top_left;
            int dl = abs(left-base), dt = abs(top-base), d = abs(top_left-base);
            dst[x] = (unsigned char)(dl <= dt && dl <= d ? left : dt <= d ? top : top_left);
        }
        dst += stride;
    }
}

static void stbv_av1_pred_smooth(unsigned char *dst, int stride,
                                 const unsigned char *tl, int w, int h)
{
    const unsigned char *wh = stbv_av1_sm_weights + w;
    const unsigned char *wv = stbv_av1_sm_weights + h;
    int right = tl[w], bottom = tl[-h];
    int x, y;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int p = wv[y]*tl[1+x] + (256-wv[y])*bottom +
                    wh[x]*tl[-1-y] + (256-wh[x])*right;
            dst[x] = (unsigned char)((p + 256) >> 9);
        }
        dst += stride;
    }
}

static void stbv_av1_pred_smooth_v(unsigned char *dst, int stride,
                                   const unsigned char *tl, int w, int h)
{
    const unsigned char *wv = stbv_av1_sm_weights + h;
    int bottom = tl[-h], x, y;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++)
            dst[x] = (unsigned char)((wv[y]*tl[1+x] + (256-wv[y])*bottom + 128) >> 8);
        dst += stride;
    }
}

static void stbv_av1_pred_smooth_h(unsigned char *dst, int stride,
                                   const unsigned char *tl, int w, int h)
{
    const unsigned char *wh = stbv_av1_sm_weights + w;
    int right = tl[w], x, y;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++)
            dst[x] = (unsigned char)((wh[x]*tl[-1-y] + (256-wh[x])*right + 128) >> 8);
        dst += stride;
    }
}

/* No intra-edge filtering/upsampling in this first scalar path.  The angle
 * interpolation itself is the same as dav1d's z1/z2/z3 predictors. */
static void stbv_av1_pred_z1(unsigned char *dst, int stride,
                             const unsigned char *tl, int w, int h, int angle)
{
    int idx = angle >> 1;
    int dx = stbv_av1_dr_deriv[idx];
    int y, x, xpos;
    if (idx < 0) idx = 0;
    if (idx > 43) idx = 43;
    dx = stbv_av1_dr_deriv[idx];
    xpos = dx;
    for (y = 0; y < h; y++, xpos += dx, dst += stride) {
        int frac = xpos & 63;
        int base = xpos >> 6;
        for (x = 0; x < w; x++, base++) {
            int v;
            if (base + 1 < w + ((w < h) ? w : h)) {
                v = tl[1+base] * (64-frac) + tl[2+base] * frac;
                dst[x] = (unsigned char)((v+32)>>6);
            } else dst[x] = tl[w + ((w < h) ? w : h)];
        }
    }
}

static void stbv_av1_pred_z3(unsigned char *dst, int stride,
                             const unsigned char *tl, int w, int h, int angle)
{
    int idx = (270-angle) >> 1;
    int dy;
    int x, y, ypos;
    if (idx < 0) idx = 0;
    if (idx > 43) idx = 43;
    dy = stbv_av1_dr_deriv[idx];
    ypos = dy;
    for (x = 0; x < w; x++, ypos += dy) {
        int frac = ypos & 63;
        int base = ypos >> 6;
        for (y = 0; y < h; y++, base++) {
            int v;
            if (base + 1 < h + ((w < h) ? w : h)) {
                v = tl[-1-base] * (64-frac) + tl[-2-base] * frac;
                dst[y*stride+x] = (unsigned char)((v+32)>>6);
            } else dst[y*stride+x] = tl[-(h + ((w < h) ? w : h))];
        }
    }
}

static void stbv_av1_pred_z2(unsigned char *dst, int stride,
                             const unsigned char *tl, int w, int h, int angle)
{
    int dxidx = (180-angle) >> 1;
    int dyidx = (angle-90) >> 1;
    int dx, dy, x, y;
    int xpos;
    if (dxidx < 0) dxidx = 0;
    if (dxidx > 43) dxidx = 43;
    if (dyidx < 0) dyidx = 0;
    if (dyidx > 43) dyidx = 43;
    dx = stbv_av1_dr_deriv[dxidx];
    dy = stbv_av1_dr_deriv[dyidx];
    for (y = 0; y < h; y++) {
        xpos = ((1 << 6) - dx) + 0;
        for (x = 0; x < w; x++, xpos += 64) {
            int bx = xpos >> 6, fx = xpos & 63;
            int yp = (y << 6) - dy;
            int v;
            if (bx >= 0) {
                v = tl[bx] * (64-fx) + tl[bx+1] * fx;
            } else {
                int by = yp >> 6, fy = yp & 63;
                v = tl[by] * (64-fy) + tl[by-1] * fy;
            }
            dst[x] = (unsigned char)((v+32)>>6);
        }
        dst += stride;
    }
}

static void stbv_av1_intra_predict(unsigned char *dst, int stride,
                                   const unsigned char *topleft,
                                   int w, int h, int mode, int angle)
{
    if (mode == STBV_AV1_IPRED_DC) stbv_av1_pred_dc(dst,stride,topleft,w,h);
    else if (mode == STBV_AV1_IPRED_VERT) stbv_av1_pred_vert(dst,stride,topleft,w,h);
    else if (mode == STBV_AV1_IPRED_HOR) stbv_av1_pred_hor(dst,stride,topleft,w,h);
    else if (mode == STBV_AV1_IPRED_SMOOTH) stbv_av1_pred_smooth(dst,stride,topleft,w,h);
    else if (mode == STBV_AV1_IPRED_SMOOTH_V) stbv_av1_pred_smooth_v(dst,stride,topleft,w,h);
    else if (mode == STBV_AV1_IPRED_SMOOTH_H) stbv_av1_pred_smooth_h(dst,stride,topleft,w,h);
    else if (mode == STBV_AV1_IPRED_PAETH) stbv_av1_pred_paeth(dst,stride,topleft,w,h);
    else if (angle < 90) stbv_av1_pred_z1(dst,stride,topleft,w,h,angle);
    else if (angle < 180) stbv_av1_pred_z2(dst,stride,topleft,w,h,angle);
    else stbv_av1_pred_z3(dst,stride,topleft,w,h,angle);
}

#endif
