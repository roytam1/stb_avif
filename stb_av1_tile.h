/*
 * stb_av1_tile.h - AV1 tile-group bridge for the scalar decoder.
 *
 * The tile-group header handling follows dav1d 1.5.4 parse_tile_hdr().
 * This first bridge deliberately supports one tile only.  That is enough for
 * the common reduced-still AVIF case and, more importantly, gives the new
 * MSAC/CDF decoder the exact tile byte range instead of the legacy Boolean
 * reader's byte stream.
 */
#ifndef STB_AV1_TILE_H
#define STB_AV1_TILE_H

#include <stddef.h>

struct stb_av1_tile_span {
    const stbv_u8 *data;
    size_t size;
    unsigned int start;
    unsigned int end;
};

/* AV1 byte_alignment(): one 1 bit followed by zero bits until aligned. */
static int stb_av1_byte_align(struct stb_av1_getbits *gb)
{
    int first;
    if (!gb)
        return -1;
    if (!gb->bits_left)
        return 0;
    first = (int)stb_av1_get_bit(gb);
    if (first != 1)
        return -1;
    while (gb->bits_left)
        if (stb_av1_get_bit(gb) != 0)
            return -1;
    return gb->error ? -1 : 0;
}

/* Parse the tile-group header and return the remaining bytes as the tile.
 * For a single tile, dav1d consumes no tile-group header bits at all. */
static int stb_av1_parse_tile_group(const struct stb_av1_framehdr *fh,
                                    struct stb_av1_getbits *gb,
                                    struct stb_av1_tile_span *tile)
{
    unsigned int n_tiles;
    unsigned int have_pos;
    unsigned int nbits;
    if (!fh || !gb || !tile)
        return -1;

    n_tiles = fh->tiling.cols * fh->tiling.rows;
    if (!n_tiles)
        return -1;

    if (stb_av1_byte_align(gb))
        return -1;

    have_pos = n_tiles > 1 ? stb_av1_get_bit(gb) : 0;
    if (have_pos) {
        nbits = fh->tiling.log2_cols + fh->tiling.log2_rows;
        tile->start = stb_av1_get_bits(gb, (int)nbits);
        tile->end = stb_av1_get_bits(gb, (int)nbits);
        if (tile->start > tile->end || tile->end >= n_tiles)
            return -1;
    } else {
        tile->start = 0;
        tile->end = n_tiles - 1;
    }

    if (tile->start != 0 || tile->end != 0)
        return -2; /* multi-tile support is intentionally deferred */

    if (gb->bits_left)
        return -1;
    tile->data = gb->ptr;
    tile->size = (size_t)(gb->ptr_end - gb->ptr);
    return 0;
}

#endif
