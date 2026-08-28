/* test_avif2png.c - decode AVIF files to PPM for visual verification */
/* Compile: cc -std=c89 -o test_avif2png test_avif2png.c -lm */

#define STB_AVIF_IMPLEMENTATION
#include "stb_avif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int decode_to_ppm(const char *avif_path, const char *ppm_path, int req_channels)
{
    unsigned char *data;
    unsigned char *img;
    int w, h, c;
    long len;
    FILE *f;
    FILE *out;
    size_t bytes_read;
    int row;

    /* Read AVIF file */
    f = fopen(avif_path, "rb");
    if (!f) {
        fprintf(stderr, "  FAIL: Cannot open %s\n", avif_path);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);

    data = (unsigned char *)malloc((size_t)len);
    if (!data) {
        fclose(f);
        fprintf(stderr, "  FAIL: Out of memory\n");
        return 0;
    }

    bytes_read = fread(data, 1, (size_t)len, f);
    fclose(f);

    if ((long)bytes_read != len) {
        fprintf(stderr, "  FAIL: Read error\n");
        free(data);
        return 0;
    }

    /* Decode with stb_avif */
    img = stb_avif_load_from_memory(data, (int)len, &w, &h, &c, req_channels);
    if (!img) {
        fprintf(stderr, "  FAIL: %s - %s\n", avif_path, stb_avif_failure_reason());
        free(data);
        return 0;
    }

    /* Write PPM */
    out = fopen(ppm_path, "wb");
    if (!out) {
        fprintf(stderr, "  FAIL: Cannot write %s\n", ppm_path);
        stb_avif_free(img);
        free(data);
        return 0;
    }

    /* PPM header (P6 = binary RGB) */
    if (c == 1) {
        /* Grayscale: use PGM */
        fprintf(out, "P5\n%d %d\n255\n", w, h);
    } else {
        /* Color: use PPM */
        fprintf(out, "P6\n%d %d\n255\n", w, h);
    }

    /* Write pixel data (strip alpha for PPM — P6 expects 3 bytes/pixel) */
    if (c == 1) {
        for (row = 0; row < h; row++) {
            fwrite(img + row * w, 1, (size_t)(w), out);
        }
    } else {
        /* Write RGB only (skip alpha byte) */
        for (row = 0; row < h; row++) {
            int col;
            for (col = 0; col < w; col++) {
                unsigned char *pix = img + (row * w + col) * c;
                fwrite(pix, 1, 3, out);
            }
        }
    }

    fclose(out);

    /* Auxiliary alpha plane -> sidecar PGM when present */
    {
        int astride = 0;
        unsigned char *aplane = stb_avif_last_alpha(&astride);
        if (aplane && c == 4) {
            FILE *fa;
            char apgmpath[1024];
            size_t nl = strlen(ppm_path);
            memcpy(apgmpath, ppm_path, nl + 1);
            if (nl >= 4 && apgmpath[nl-4] == '.') strcpy(apgmpath + nl - 4, ".pgm");
            else strcat(apgmpath, ".pgm");
            fa = fopen(apgmpath, "wb");
            if (fa) {
                int r2;
                fprintf(fa, "P5\n%d %d\n255\n", w, h);
                for (r2 = 0; r2 < h; r2++)
                    fwrite(aplane + (size_t)r2 * astride, 1, (size_t)w, fa);
                fclose(fa);
                printf("  OK: alpha -> %s\n", apgmpath);
            }
        }
    }

    printf("  OK: %dx%d, %d chan -> %s\n", w, h, c, ppm_path);

    stb_avif_free(img);
    free(data);
    return 1;
}

int main(int argc, char *argv[])
{
    int pass = 0;
    int fail = 0;
    int i;

#ifdef STB_DBG_TRACE
    setvbuf(stderr, NULL, _IOFBF, 1 << 24);
#endif

    /* Default file list */
    const char *files[] = {
"example_avif/app-icon.avif",
"example_avif/avif-yuv444p.avif",
"example_avif/avif-yuv444p10le.avif",
"example_avif/avif-yuv444p12le.avif",
"example_avif/fox.profile0.10bpc.yuv420.avif",
"example_avif/fox.profile0.8bpc.yuv420.avif",
"example_avif/fox.profile1.8bpc.yuv444.avif",
"example_avif/fox.profile1.10bpc.yuv444.avif",
"example_avif/fox.profile2.12bpc.yuv444.avif",
"example_avif/G-0trmKXsAA1sQZ-thumb.avif",
"example_avif/G-0trmKXsAA1sQZ.avif",
"example_avif/Gb5RU6RWoAAQQ1n.avif",
"example_avif/hato.profile0.10bpc.yuv420.avif",
"example_avif/hato.profile0.10bpc.yuv420.monochrome.avif",
"example_avif/hato.profile0.8bpc.yuv420.avif",
"example_avif/hato.profile0.8bpc.yuv420.monochrome.avif",
"example_avif/hato.profile2.10bpc.yuv422.avif",
"example_avif/hato.profile2.10bpc.yuv422.monochrome.avif",
"example_avif/hato.profile2.12bpc.yuv422.avif",
"example_avif/hato.profile2.12bpc.yuv422.monochrome.avif",
"example_avif/hato.profile2.8bpc.yuv422.avif",
"example_avif/hato.profile2.8bpc.yuv422.monochrome.avif",
"example_avif/illustration.avif",
"example_avif/kimono.avif",
"example_avif/red-at-12-oclock-with-color-profile-10bpc.avif",
"example_avif/steam_2253100.avif"
    };
    int num;

    if (argc > 1) {
        /* Use command-line args as file list */
        num = argc - 1;
    } else {
        num = (int)(sizeof(files) / sizeof(files[0]));
    }

    printf("stb_avif -> PPM converter\n");
    printf("=========================\n\n");

#ifdef _WIN32
    system("mkdir output_ppm >nul 2>&1");
#else
    system("mkdir -p output_ppm");
#endif

    for (i = 0; i < num; i++) {
        const char *src;
        char dst[512];
        const char *base;
        const char *slash;
        int req_chan = 4;

        printf("[%d/%d] ", i + 1, num);
        fflush(stdout);

        if (argc > 1) {
            src = argv[i + 1];
        } else {
            src = files[i];
        }

        /* Build output path: replace .avif with .ppm */
        base = src;
        slash = strrchr(src, '/');
        { const char *bs = strrchr(src, '\\'); if (bs && (!slash || bs > slash)) slash = bs; }
        if (slash) base = slash + 1;

        strcpy(dst, "output_ppm/");
        strncat(dst, base, sizeof(dst) - 20);
        {
            char *dot;
            dot = strrchr(dst, '.');
            if (dot) strcpy(dot, ".ppm");
            else strcat(dst, ".ppm");
        }

        if (decode_to_ppm(src, dst, req_chan))
            pass++;
        else
            fail++;
    }

    printf("\n=========================\n");
    printf("Results: %d passed, %d failed out of %d\n",
           pass, fail, pass + fail);

    return fail > 0 ? 1 : 0;
}
