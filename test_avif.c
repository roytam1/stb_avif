/* Test program for stb_avif.h */
/* Compile with: cc -pedantic -std=c89 -Wall -Wextra -o test_avif test_avif.c -lm */

#define STB_AVIF_IMPLEMENTATION
#include "stb_avif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int load_and_decode(const char *filename, int req_channels)
{
    unsigned char *data;
    unsigned char *img;
    int w, h, c;
    long len;
    FILE *f;
    size_t bytes_read;

    f = fopen(filename, "rb");
    if (!f) {
        printf("  FAIL: Cannot open %s\n", filename);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);

    data = (unsigned char *)malloc((size_t)len);
    if (!data) {
        fclose(f);
        printf("  FAIL: Out of memory\n");
        return 0;
    }

    bytes_read = fread(data, 1, (size_t)len, f);
    fclose(f);

    if ((long)bytes_read != len) {
        printf("  FAIL: Read error\n");
        free(data);
        return 0;
    }

    img = stb_avif_load_from_memory(data, (int)len, &w, &h, &c, req_channels);
    if (!img) {
        printf("  FAIL: %s - %s\n", filename, stb_avif_failure_reason());
        free(data);
        return 0;
    }

    printf("  OK: %s - %dx%d, %d channels, %d bytes/pixel\n",
           filename, w, h, c, c);

    stb_avif_free(img);
    free(data);
    return 1;
}

int main(void)
{
    int pass = 0;
    int fail = 0;
    int i;

    /* Test file list */
    const char *files[] = {
        "example_avif/fox.profile0.8bpc.yuv420.avif",
        "example_avif/fox.profile0.10bpc.yuv420.avif",
        "example_avif/kimono.avif",
        "example_avif/G-0trmKXsAA1sQZ-thumb.avif",
        "example_avif/G-0trmKXsAA1sQZ.avif",
        "example_avif/Gb5RU6RWoAAQQ1n.avif",
        "example_avif/red-at-12-oclock-with-color-profile-10bpc.avif",
        "example_avif/steam_2253100.avif"
    };
    int num_files = (int)(sizeof(files) / sizeof(files[0]));

    printf("stb_avif.h test\n");
    printf("===============\n\n");

    /* Test: request 4 channels (RGBA) */
    printf("--- Testing with req_channels=4 ---\n");
    for (i = 0; i < num_files; i++) {
        if (load_and_decode(files[i], 4))
            pass++;
        else
            fail++;
    }

    /* Test: request 3 channels (RGB) */
    printf("\n--- Testing with req_channels=3 ---\n");
    for (i = 0; i < num_files; i++) {
        if (load_and_decode(files[i], 3))
            pass++;
        else
            fail++;
    }

    /* Test: request 0 channels (auto) */
    printf("\n--- Testing with req_channels=0 ---\n");
    for (i = 0; i < num_files; i++) {
        if (load_and_decode(files[i], 0))
            pass++;
        else
            fail++;
    }

    printf("\n===============\n");
    printf("Results: %d passed, %d failed out of %d\n",
           pass, fail, pass + fail);

    return fail > 0 ? 1 : 0;
}
