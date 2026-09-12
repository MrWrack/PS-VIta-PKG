#ifndef VITA_PNG_FIX_H
#define VITA_PNG_FIX_H

int vita_fix_sce_sys_pngs(const char *install_dir, int *fixed_count);


/* Convert an extracted VPK icon to a simple 128x128 RGB8 PNG for preview only. */
int vita_make_preview_rgb128(const char *path);

int vita_decode_preview_rgba128(const char *path, unsigned char *out_rgba, unsigned int out_size);

#endif
