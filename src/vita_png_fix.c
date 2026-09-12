#include "vita_png_fix.h"

#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <png.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int ends_png(const char *s) {
    size_t n = strlen(s);
    if (n < 4) return 0;
    return s[n-4] == '.' &&
           tolower((unsigned char)s[n-3]) == 'p' &&
           tolower((unsigned char)s[n-2]) == 'n' &&
           tolower((unsigned char)s[n-1]) == 'g';
}

static int png_to_indexed8(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    unsigned char sig[8];
    if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8)) {
        fclose(fp);
        return -2;
    }

    png_structp rp = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!rp) { fclose(fp); return -3; }

    png_infop ri = png_create_info_struct(rp);
    if (!ri) {
        png_destroy_read_struct(&rp, NULL, NULL);
        fclose(fp);
        return -4;
    }

    if (setjmp(png_jmpbuf(rp))) {
        png_destroy_read_struct(&rp, &ri, NULL);
        fclose(fp);
        return -5;
    }

    png_init_io(rp, fp);
    png_set_sig_bytes(rp, 8);
    png_read_info(rp, ri);

    png_uint_32 width = png_get_image_width(rp, ri);
    png_uint_32 height = png_get_image_height(rp, ri);
    int bit_depth = png_get_bit_depth(rp, ri);
    int color_type = png_get_color_type(rp, ri);

    /* Vita LiveArea resources are small; reject absurd files. */
    if (width == 0 || height == 0 || width > 2048 || height > 2048) {
        png_destroy_read_struct(&rp, &ri, NULL);
        fclose(fp);
        return -6;
    }

    if (bit_depth == 16)
        png_set_strip_16(rp);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(rp);

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(rp);

    if (png_get_valid(rp, ri, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(rp);

    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(rp);

    /* We deliberately remove alpha for maximum Vita promoter compatibility. */
    if (color_type & PNG_COLOR_MASK_ALPHA ||
        png_get_valid(rp, ri, PNG_INFO_tRNS))
        png_set_strip_alpha(rp);

    png_read_update_info(rp, ri);

    size_t rowbytes = png_get_rowbytes(rp, ri);
    unsigned char *pixels = (unsigned char *)malloc(rowbytes * height);
    png_bytep *rows = (png_bytep *)malloc(sizeof(png_bytep) * height);

    if (!pixels || !rows) {
        free(pixels);
        free(rows);
        png_destroy_read_struct(&rp, &ri, NULL);
        fclose(fp);
        return -7;
    }

    for (png_uint_32 y = 0; y < height; ++y)
        rows[y] = pixels + y * rowbytes;

    png_read_image(rp, rows);
    png_read_end(rp, NULL);
    png_destroy_read_struct(&rp, &ri, NULL);
    fclose(fp);

    unsigned char *idx = (unsigned char *)malloc((size_t)width * height);
    png_bytep *out_rows = (png_bytep *)malloc(sizeof(png_bytep) * height);

    if (!idx || !out_rows) {
        free(pixels);
        free(rows);
        free(idx);
        free(out_rows);
        return -8;
    }

    /*
      3-3-2 fixed palette:
      R = top 3 bits, G = top 3 bits, B = top 2 bits.
      Gives exactly 256 palette entries and avoids a large quantizer.
    */
    for (png_uint_32 y = 0; y < height; ++y) {
        unsigned char *src = rows[y];
        unsigned char *dst = idx + (size_t)y * width;

        for (png_uint_32 x = 0; x < width; ++x) {
            unsigned char r = src[x * 3 + 0];
            unsigned char g = src[x * 3 + 1];
            unsigned char b = src[x * 3 + 2];

            dst[x] = (unsigned char)(((r >> 5) << 5) |
                                     ((g >> 5) << 2) |
                                     (b >> 6));
        }

        out_rows[y] = dst;
    }

    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s.vpkmgr_tmp", path);

    FILE *wf = fopen(tmp, "wb");
    if (!wf) {
        free(pixels); free(rows); free(idx); free(out_rows);
        return -9;
    }

    png_structp wp = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop wi = wp ? png_create_info_struct(wp) : NULL;

    if (!wp || !wi) {
        if (wp) png_destroy_write_struct(&wp, NULL);
        fclose(wf);
        remove(tmp);
        free(pixels); free(rows); free(idx); free(out_rows);
        return -10;
    }

    if (setjmp(png_jmpbuf(wp))) {
        png_destroy_write_struct(&wp, &wi);
        fclose(wf);
        remove(tmp);
        free(pixels); free(rows); free(idx); free(out_rows);
        return -11;
    }

    png_init_io(wp, wf);
    png_set_IHDR(wp, wi,
                 width, height,
                 8,
                 PNG_COLOR_TYPE_PALETTE,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    png_color palette[256];
    for (int i = 0; i < 256; ++i) {
        int rr = (i >> 5) & 7;
        int gg = (i >> 2) & 7;
        int bb = i & 3;

        palette[i].red   = (png_byte)((rr * 255) / 7);
        palette[i].green = (png_byte)((gg * 255) / 7);
        palette[i].blue  = (png_byte)((bb * 255) / 3);
    }

    png_set_PLTE(wp, wi, palette, 256);
    png_write_info(wp, wi);
    png_write_image(wp, out_rows);
    png_write_end(wp, NULL);
    png_destroy_write_struct(&wp, &wi);
    fclose(wf);

    free(pixels);
    free(rows);
    free(idx);
    free(out_rows);

    remove(path);
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -12;
    }

    return 0;
}

static int scan_dir(const char *dir, int *fixed) {
    SceUID d = sceIoDopen(dir);
    if (d < 0)
        return d;

    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));

    int r;
    while ((r = sceIoDread(d, &ent)) > 0) {
        if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, "..")) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, ent.d_name);

        if (SCE_S_ISDIR(ent.d_stat.st_mode)) {
            int rr = scan_dir(path, fixed);
            if (rr < 0) {
                sceIoDclose(d);
                return rr;
            }
        } else if (ends_png(ent.d_name)) {
            int rr = png_to_indexed8(path);
            if (rr < 0) {
                sceIoDclose(d);
                return rr;
            }
            (*fixed)++;
        }

        memset(&ent, 0, sizeof(ent));
    }

    sceIoDclose(d);
    return (r < 0) ? r : 0;
}

int vita_fix_sce_sys_pngs(const char *install_dir, int *fixed_count) {
    if (fixed_count)
        *fixed_count = 0;

    char sce_sys[1024];
    snprintf(sce_sys, sizeof(sce_sys), "%s/sce_sys", install_dir);

    int fixed = 0;
    int r = scan_dir(sce_sys, &fixed);

    if (fixed_count)
        *fixed_count = fixed;

    return r;
}


/*
 * Preview-only converter.
 * libvita2d has known edge cases with some PNG layouts.  For covers we first
 * rewrite the source as a plain non-interlaced RGB8 128x128 image.  This is
 * intentionally separate from the indexed PNG8 conversion used by the Vita
 * package promoter.
 */
int vita_make_preview_rgb128(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -101;

    unsigned char sig[8];
    if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8)) {
        fclose(fp);
        return -102;
    }

    png_structp rp = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop ri = rp ? png_create_info_struct(rp) : NULL;
    if (!rp || !ri) {
        if (rp) png_destroy_read_struct(&rp, NULL, NULL);
        fclose(fp);
        return -103;
    }

    unsigned char *pixels = NULL;
    png_bytep *rows = NULL;
    unsigned char *scaled = NULL;
    int ret = 0;

    if (setjmp(png_jmpbuf(rp))) {
        ret = -104;
        goto read_fail;
    }

    png_init_io(rp, fp);
    png_set_sig_bytes(rp, 8);
    png_read_info(rp, ri);

    png_uint_32 w = png_get_image_width(rp, ri);
    png_uint_32 h = png_get_image_height(rp, ri);
    int bit_depth = png_get_bit_depth(rp, ri);
    int color_type = png_get_color_type(rp, ri);

    if (w == 0 || h == 0 || w > 2048 || h > 2048) {
        ret = -105;
        goto read_fail;
    }

    if (bit_depth == 16) png_set_strip_16(rp);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(rp);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(rp);
    if (png_get_valid(rp, ri, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(rp);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(rp);

    /* Preview texture does not need alpha. Composite by simply dropping it. */
    if ((color_type & PNG_COLOR_MASK_ALPHA) || png_get_valid(rp, ri, PNG_INFO_tRNS))
        png_set_strip_alpha(rp);

    png_read_update_info(rp, ri);
    size_t rowbytes = png_get_rowbytes(rp, ri);

    /* After transforms we expect exactly RGB8. */
    if (png_get_bit_depth(rp, ri) != 8 || png_get_color_type(rp, ri) != PNG_COLOR_TYPE_RGB) {
        ret = -106;
        goto read_fail;
    }

    pixels = (unsigned char *)malloc(rowbytes * h);
    rows = (png_bytep *)malloc(sizeof(png_bytep) * h);
    if (!pixels || !rows) {
        ret = -107;
        goto read_fail;
    }

    for (png_uint_32 y = 0; y < h; ++y)
        rows[y] = pixels + (size_t)y * rowbytes;

    png_read_image(rp, rows);
    png_read_end(rp, NULL);

    scaled = (unsigned char *)malloc(128u * 128u * 3u);
    if (!scaled) {
        ret = -108;
        goto read_fail;
    }

    /* Nearest-neighbour is enough for a 128x128 app icon and keeps this tiny. */
    for (unsigned int y = 0; y < 128; ++y) {
        png_uint_32 sy = (png_uint_32)(((unsigned long long)y * h) / 128u);
        if (sy >= h) sy = h - 1;
        for (unsigned int x = 0; x < 128; ++x) {
            png_uint_32 sx = (png_uint_32)(((unsigned long long)x * w) / 128u);
            if (sx >= w) sx = w - 1;
            unsigned char *d = scaled + ((size_t)y * 128u + x) * 3u;
            unsigned char *s = rows[sy] + (size_t)sx * 3u;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        }
    }

read_fail:
    png_destroy_read_struct(&rp, &ri, NULL);
    fclose(fp);
    free(rows);
    free(pixels);
    if (ret < 0) {
        free(scaled);
        return ret;
    }

    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s.cover_tmp", path);
    FILE *wf = fopen(tmp, "wb");
    if (!wf) {
        free(scaled);
        return -109;
    }

    png_structp wp = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop wi = wp ? png_create_info_struct(wp) : NULL;
    if (!wp || !wi) {
        if (wp) png_destroy_write_struct(&wp, NULL);
        fclose(wf);
        remove(tmp);
        free(scaled);
        return -110;
    }

    if (setjmp(png_jmpbuf(wp))) {
        png_destroy_write_struct(&wp, &wi);
        fclose(wf);
        remove(tmp);
        free(scaled);
        return -111;
    }

    png_init_io(wp, wf);
    png_set_IHDR(wp, wi, 128, 128, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(wp, wi);

    png_bytep out_rows[128];
    for (int y = 0; y < 128; ++y)
        out_rows[y] = scaled + (size_t)y * 128u * 3u;

    png_write_image(wp, out_rows);
    png_write_end(wp, NULL);
    png_destroy_write_struct(&wp, &wi);
    fclose(wf);
    free(scaled);

    remove(path);
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -112;
    }

    return 0;
}
