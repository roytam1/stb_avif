/*
 * stb_av1_obu.h - AV1 OBU plumbing for the scalar decoder
 *
 * The OBU framing rules follow dav1d 1.5.4 src/obu.c.  This layer is
 * intentionally independent of the legacy Boolean reader in stb_avif.h.
 *
 * First target:
 *   - sequence header OBU
 *   - key/intra frame header OBU
 *   - combined FRAME OBU
 *   - TILE_GROUP OBU
 *   - one or more tile groups, but only tile 0 is exposed to the first
 *     reconstruction stage
 */
#ifndef STB_AV1_OBU_H
#define STB_AV1_OBU_H

#include <stddef.h>

#define STB_AV1_OBU_FLAG_FORBIDDEN 0x80
#define STB_AV1_OBU_FLAG_EXTENSION 0x04
#define STB_AV1_OBU_FLAG_SIZE      0x02

#ifndef STB_AV1_OBU_SEQUENCE_HEADER
#define STB_AV1_OBU_SEQUENCE_HEADER 1
#define STB_AV1_OBU_TEMPORAL_DELIMITER 2
#define STB_AV1_OBU_FRAME_HEADER 3
#define STB_AV1_OBU_TILE_GROUP 4
#define STB_AV1_OBU_METADATA 5
#define STB_AV1_OBU_FRAME 6
#define STB_AV1_OBU_REDUNDANT_FRAME_HEADER 7
#define STB_AV1_OBU_TILE_LIST 8
#define STB_AV1_OBU_PADDING 15
#endif

struct stb_av1_obu {
    unsigned int type;
    unsigned int extension;
    unsigned int temporal_id;
    unsigned int spatial_id;
    const stbv_u8 *data;
    size_t size;
};

struct stb_av1_internal_stream {
    struct stb_av1_seqhdr seq;
    struct stb_av1_framehdr frame;
    int have_seq;
    int have_frame;

    const stbv_u8 *tile_data;
    size_t tile_size;
    unsigned int tile_start;
    unsigned int tile_end;
};

/* Read one OBU from an already byte-aligned stream. */
static int stb_av1_read_obu(struct stb_av1_getbits *gb,
                            struct stb_av1_obu *obu)
{
    unsigned int hdr;
    unsigned int has_extension;
    unsigned int has_size;
    unsigned int reserved;
    unsigned int v;
    size_t start;
    size_t payload_start;
    size_t payload_size;

    if (!gb || !obu || gb->error || gb->bits_left)
        return -1;

    start = (size_t)(gb->ptr - gb->ptr_start);
    if (start >= (size_t)(gb->ptr_end - gb->ptr_start))
        return 1; /* end of stream */

    hdr = stb_av1_get_bits(gb, 8);
    if (hdr & STB_AV1_OBU_FLAG_FORBIDDEN)
        return -1;

    obu->type = (hdr >> 3) & 15U;
    has_extension = (hdr & STB_AV1_OBU_FLAG_EXTENSION) != 0;
    has_size = (hdr & STB_AV1_OBU_FLAG_SIZE) != 0;
    reserved = hdr & 1U;
    if (reserved)
        return -1;

    obu->extension = 0;
    obu->temporal_id = 0;
    obu->spatial_id = 0;

    if (has_extension) {
        v = stb_av1_get_bits(gb, 8);
        obu->temporal_id = (v >> 5) & 7U;
        obu->spatial_id = (v >> 3) & 3U;
        if (v & 7U)
            return -1;
        obu->extension = 1;
    }

    if (has_size) {
        unsigned int size = stb_av1_get_uleb128(gb);
        if (gb->error)
            return -1;
        payload_start = (size_t)(gb->ptr - gb->ptr_start);
        payload_size = (size_t)size;
        if (payload_size > (size_t)(gb->ptr_end - gb->ptr_start) - payload_start)
            return -1;
    } else {
        payload_start = (size_t)(gb->ptr - gb->ptr_start);
        payload_size = (size_t)(gb->ptr_end - gb->ptr_start) - payload_start;
    }

    obu->data = gb->ptr_start + payload_start;
    obu->size = payload_size;

    /* Move to the end of this OBU.  The payload itself is consumed later by
       a separate GetBits instance, so the outer reader remains byte based. */
    gb->ptr = gb->ptr_start + payload_start + payload_size;
    gb->bits_left = 0;
    gb->state = 0;
    return 0;
}

static int stb_av1_parse_obu_payload_seq(struct stb_av1_internal_stream *st,
                                         const struct stb_av1_obu *obu)
{
    struct stb_av1_getbits gb;
    int res;

    stb_av1_getbits_init(&gb, obu->data, obu->size);
    res = stb_av1_parse_seqhdr(&st->seq, &gb);
    if (res < 0 || gb.error)
        return -1;
    st->have_seq = 1;
    return 0;
}

static int stb_av1_parse_obu_payload_frame(struct stb_av1_internal_stream *st,
                                           const struct stb_av1_obu *obu,
                                           int combined)
{
    struct stb_av1_getbits gb;
    struct stb_av1_tile_span tile;
    int res;

    if (!st->have_seq)
        return -1;

    stb_av1_getbits_init(&gb, obu->data, obu->size);
    res = stb_av1_parse_framehdr(&st->frame, &st->seq, &gb);
    if (res < 0 || gb.error)
        return -1;
    st->have_frame = 1;

    if (!combined)
        return 0;

    if (st->frame.show_existing_frame)
        return -1;

    /* A FRAME OBU contains frame-header bits followed immediately by the
       tile-group header.  parse_tile_group() consumes that header and leaves
       the reader at the first MSAC byte. */
    if (stb_av1_parse_tile_group(&st->frame, &gb, &tile) < 0)
        return -1;

    st->tile_data = tile.data;
    st->tile_size = tile.size;
    st->tile_start = tile.start;
    st->tile_end = tile.end;
    return 0;
}

/*
 * Parse enough of an AV1 still-image stream to expose the first tile.
 * Returns 0 on success, -1 on malformed/unsupported input.
 */
static int stb_av1_parse_internal_stream(struct stb_av1_internal_stream *st,
                                         const stbv_u8 *data, size_t size)
{
    struct stb_av1_getbits outer;
    struct stb_av1_obu obu;
    int r;

    if (!st || !data || !size)
        return -1;

    {
        unsigned char *p = (unsigned char *)st;
        size_t n = sizeof(*st);
        while (n--) *p++ = 0;
    }

    stb_av1_getbits_init(&outer, data, size);

    while ((size_t)(outer.ptr - outer.ptr_start) < size) {
        r = stb_av1_read_obu(&outer, &obu);
        if (r == 1)
            break;
        if (r < 0)
            return -1;

        switch (obu.type) {
        case STB_AV1_OBU_SEQUENCE_HEADER:
            if (stb_av1_parse_obu_payload_seq(st, &obu) < 0)
                return -1;
            break;

        case STB_AV1_OBU_FRAME:
            if (stb_av1_parse_obu_payload_frame(st, &obu, 1) < 0)
                return -1;
            if (st->tile_data)
                return 0;
            break;

        case STB_AV1_OBU_FRAME_HEADER:
        case STB_AV1_OBU_REDUNDANT_FRAME_HEADER:
            if (stb_av1_parse_obu_payload_frame(st, &obu, 0) < 0)
                return -1;
            break;

        case STB_AV1_OBU_TILE_GROUP: {
            struct stb_av1_getbits gb;
            struct stb_av1_tile_span tile;
            if (!st->have_frame)
                return -1;
            stb_av1_getbits_init(&gb, obu.data, obu.size);
            if (stb_av1_parse_tile_group(&st->frame, &gb, &tile) < 0)
                return -1;
            st->tile_data = tile.data;
            st->tile_size = tile.size;
            st->tile_start = tile.start;
            st->tile_end = tile.end;
            return 0;
        }

        default:
            break;
        }
    }

    return st->have_seq && st->have_frame && st->tile_data ? 0 : -1;
}

#endif
