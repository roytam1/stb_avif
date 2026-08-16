/*
 * stb_av1_seqhdr.h - AV1 sequence header parser
 *
 * Portions are adapted from dav1d 1.5.4 src/obu.c parse_seq_hdr().
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef STB_AV1_SEQHDR_H
#define STB_AV1_SEQHDR_H

#define STB_AV1_MAX_OPERATING_POINTS 32

enum stb_av1_seq_layout {
    STB_AV1_LAYOUT_I400 = 0,
    STB_AV1_LAYOUT_I420 = 1,
    STB_AV1_LAYOUT_I422 = 2,
    STB_AV1_LAYOUT_I444 = 3
};

enum stb_av1_seq_chr {
    STB_AV1_CHR_UNKNOWN = 0,
    STB_AV1_CHR_420 = 1,
    STB_AV1_CHR_422 = 2,
    STB_AV1_CHR_444 = 3
};

struct stb_av1_seq_op {
    unsigned int idc;
    unsigned int major_level;
    unsigned int minor_level;
    unsigned int tier;
    unsigned int decoder_model_param_present;
    unsigned int display_model_param_present;
    unsigned int initial_display_delay;
    unsigned int decoder_buffer_delay;
    unsigned int encoder_buffer_delay;
    unsigned int low_delay_mode;
};

struct stb_av1_seqhdr {
    unsigned int profile;
    unsigned int still_picture;
    unsigned int reduced_still_picture_header;

    unsigned int timing_info_present;
    unsigned int num_units_in_tick;
    unsigned int time_scale;
    unsigned int equal_picture_interval;
    unsigned int num_ticks_per_picture;
    unsigned int decoder_model_info_present;
    unsigned int encoder_decoder_buffer_delay_length;
    unsigned int num_units_in_decoding_tick;
    unsigned int buffer_removal_delay_length;
    unsigned int frame_presentation_delay_length;
    unsigned int display_model_info_present;
    unsigned int num_operating_points;
    struct stb_av1_seq_op operating_points[STB_AV1_MAX_OPERATING_POINTS];

    unsigned int width_n_bits;
    unsigned int height_n_bits;
    unsigned int max_width;
    unsigned int max_height;

    unsigned int frame_id_numbers_present;
    unsigned int delta_frame_id_n_bits;
    unsigned int frame_id_n_bits;

    unsigned int sb128;
    unsigned int filter_intra;
    unsigned int intra_edge_filter;
    unsigned int inter_intra;
    unsigned int masked_compound;
    unsigned int warped_motion;
    unsigned int dual_filter;
    unsigned int order_hint;
    unsigned int jnt_comp;
    unsigned int ref_frame_mvs;
    unsigned int screen_content_tools;
    unsigned int force_integer_mv;
    unsigned int order_hint_n_bits;

    unsigned int super_res;
    unsigned int cdef;
    unsigned int restoration;

    unsigned int hbd;
    unsigned int monochrome;
    unsigned int color_description_present;
    unsigned int pri;
    unsigned int trc;
    unsigned int mtrx;
    unsigned int color_range;
    unsigned int layout;
    unsigned int ss_hor;
    unsigned int ss_ver;
    unsigned int chr;
    unsigned int separate_uv_delta_q;
    unsigned int film_grain_present;
};

static unsigned int stb_av1_seq_vlc(struct stb_av1_getbits *gb)
{
    unsigned int leading = 0;
    unsigned int v;
    while (!stb_av1_get_bit(gb)) {
        leading++;
        if (leading >= 32) {
            gb->error = 1;
            return 0;
        }
    }
    if (!leading)
        return 0;
    v = stb_av1_get_bits(gb, (int)leading);
    return ((1U << leading) - 1U) + v;
}

/* Returns 0 on success, -1 on malformed input. */
static int stb_av1_parse_seqhdr(struct stb_av1_seqhdr *h,
                                struct stb_av1_getbits *gb)
{
    unsigned int i;

    /* dav1d starts with memset(). Do it explicitly for C89 portability. */
    unsigned char *p = (unsigned char *)h;
    size_t n = sizeof(*h);
    while (n--)
        *p++ = 0;

    h->profile = stb_av1_get_bits(gb, 3);
    if (h->profile > 2)
        return -1;

    h->still_picture = stb_av1_get_bit(gb);
    h->reduced_still_picture_header = stb_av1_get_bit(gb);
    if (h->reduced_still_picture_header && !h->still_picture)
        return -1;

    if (h->reduced_still_picture_header) {
        h->num_operating_points = 1;
        h->operating_points[0].major_level = stb_av1_get_bits(gb, 3);
        h->operating_points[0].minor_level = stb_av1_get_bits(gb, 2);
        h->operating_points[0].initial_display_delay = 10;
    } else {
        h->timing_info_present = stb_av1_get_bit(gb);
        if (h->timing_info_present) {
            h->num_units_in_tick = stb_av1_get_bits(gb, 32);
            h->time_scale = stb_av1_get_bits(gb, 32);
            h->equal_picture_interval = stb_av1_get_bit(gb);
            if (h->equal_picture_interval) {
                h->num_ticks_per_picture = stb_av1_seq_vlc(gb) + 1;
                if (gb->error)
                    return -1;
            }
            h->decoder_model_info_present = stb_av1_get_bit(gb);
            if (h->decoder_model_info_present) {
                h->encoder_decoder_buffer_delay_length =
                    stb_av1_get_bits(gb, 5) + 1;
                h->num_units_in_decoding_tick = stb_av1_get_bits(gb, 32);
                h->buffer_removal_delay_length = stb_av1_get_bits(gb, 5) + 1;
                h->frame_presentation_delay_length = stb_av1_get_bits(gb, 5) + 1;
            }
        }

        h->display_model_info_present = stb_av1_get_bit(gb);
        h->num_operating_points = stb_av1_get_bits(gb, 5) + 1;
        if (h->num_operating_points > STB_AV1_MAX_OPERATING_POINTS)
            return -1;

        for (i = 0; i < h->num_operating_points; i++) {
            struct stb_av1_seq_op *op = &h->operating_points[i];
            op->idc = stb_av1_get_bits(gb, 12);
            if (op->idc && (!(op->idc & 0xffU) || !(op->idc & 0xf00U)))
                return -1;
            op->major_level = 2 + stb_av1_get_bits(gb, 3);
            op->minor_level = stb_av1_get_bits(gb, 2);
            if (op->major_level > 3)
                op->tier = stb_av1_get_bit(gb);
            if (h->decoder_model_info_present) {
                op->decoder_model_param_present = stb_av1_get_bit(gb);
                if (op->decoder_model_param_present) {
                    op->decoder_buffer_delay = stb_av1_get_bits(
                        gb, (int)h->encoder_decoder_buffer_delay_length);
                    op->encoder_buffer_delay = stb_av1_get_bits(
                        gb, (int)h->encoder_decoder_buffer_delay_length);
                    op->low_delay_mode = stb_av1_get_bit(gb);
                }
            }
            if (h->display_model_info_present)
                op->display_model_param_present = stb_av1_get_bit(gb);
            op->initial_display_delay = op->display_model_param_present ?
                stb_av1_get_bits(gb, 4) + 1 : 10;
        }
    }

    h->width_n_bits = stb_av1_get_bits(gb, 4) + 1;
    h->height_n_bits = stb_av1_get_bits(gb, 4) + 1;
    h->max_width = stb_av1_get_bits(gb, (int)h->width_n_bits) + 1;
    h->max_height = stb_av1_get_bits(gb, (int)h->height_n_bits) + 1;

    if (!h->reduced_still_picture_header) {
        h->frame_id_numbers_present = stb_av1_get_bit(gb);
        if (h->frame_id_numbers_present) {
            h->delta_frame_id_n_bits = stb_av1_get_bits(gb, 4) + 2;
            h->frame_id_n_bits = stb_av1_get_bits(gb, 3) +
                                 h->delta_frame_id_n_bits + 1;
        }
    }

    h->sb128 = stb_av1_get_bit(gb);
    h->filter_intra = stb_av1_get_bit(gb);
    h->intra_edge_filter = stb_av1_get_bit(gb);

    if (h->reduced_still_picture_header) {
        h->screen_content_tools = 2; /* DAV1D_ADAPTIVE */
        h->force_integer_mv = 2;     /* DAV1D_ADAPTIVE */
    } else {
        h->inter_intra = stb_av1_get_bit(gb);
        h->masked_compound = stb_av1_get_bit(gb);
        h->warped_motion = stb_av1_get_bit(gb);
        h->dual_filter = stb_av1_get_bit(gb);
        h->order_hint = stb_av1_get_bit(gb);
        if (h->order_hint) {
            h->jnt_comp = stb_av1_get_bit(gb);
            h->ref_frame_mvs = stb_av1_get_bit(gb);
        }
        h->screen_content_tools = stb_av1_get_bit(gb) ?
            2 : stb_av1_get_bit(gb);
        h->force_integer_mv = h->screen_content_tools ?
            (stb_av1_get_bit(gb) ? 2 : stb_av1_get_bit(gb)) : 2;
        if (h->order_hint)
            h->order_hint_n_bits = stb_av1_get_bits(gb, 3) + 1;
    }

    h->super_res = stb_av1_get_bit(gb);
    h->cdef = stb_av1_get_bit(gb);
    h->restoration = stb_av1_get_bit(gb);

    h->hbd = stb_av1_get_bit(gb);
    if (h->profile == 2 && h->hbd)
        h->hbd += stb_av1_get_bit(gb);
    if (h->profile != 1)
        h->monochrome = stb_av1_get_bit(gb);

    h->color_description_present = stb_av1_get_bit(gb);
    if (h->color_description_present) {
        h->pri = stb_av1_get_bits(gb, 8);
        h->trc = stb_av1_get_bits(gb, 8);
        h->mtrx = stb_av1_get_bits(gb, 8);
    } else {
        h->pri = 2;  /* UNKNOWN */
        h->trc = 2;  /* UNKNOWN */
        h->mtrx = 2; /* UNKNOWN */
    }

    if (h->monochrome) {
        h->color_range = stb_av1_get_bit(gb);
        h->layout = STB_AV1_LAYOUT_I400;
        h->ss_hor = h->ss_ver = 1;
        h->chr = STB_AV1_CHR_UNKNOWN;
    } else if (h->pri == 1 && h->trc == 13 && h->mtrx == 0) {
        /* BT.709 / sRGB / identity matrix special case. */
        h->layout = STB_AV1_LAYOUT_I444;
        h->color_range = 1;
        if (h->profile != 1 && !(h->profile == 2 && h->hbd == 2))
            return -1;
    } else {
        h->color_range = stb_av1_get_bit(gb);
        switch (h->profile) {
        case 0:
            h->layout = STB_AV1_LAYOUT_I420;
            h->ss_hor = h->ss_ver = 1;
            break;
        case 1:
            h->layout = STB_AV1_LAYOUT_I444;
            break;
        case 2:
            if (h->hbd == 2) {
                h->ss_hor = stb_av1_get_bit(gb);
                if (h->ss_hor)
                    h->ss_ver = stb_av1_get_bit(gb);
            } else {
                h->ss_hor = 1;
            }
            h->layout = h->ss_hor ?
                (h->ss_ver ? STB_AV1_LAYOUT_I420 : STB_AV1_LAYOUT_I422) :
                STB_AV1_LAYOUT_I444;
            break;
        default:
            return -1;
        }
        h->chr = (h->ss_hor && h->ss_ver) ?
            stb_av1_get_bits(gb, 2) : STB_AV1_CHR_UNKNOWN;
    }

    if (!h->monochrome)
        h->separate_uv_delta_q = stb_av1_get_bit(gb);

    h->film_grain_present = stb_av1_get_bit(gb);

    if (gb->error)
        return -1;
    return 0;
}

#endif /* STB_AV1_SEQHDR_H */
