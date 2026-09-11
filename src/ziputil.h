#pragma once
#include <stddef.h>

typedef struct {
    char name[512];
    unsigned int method;
    unsigned int flags;
    unsigned long crc32;
    unsigned long comp_size;
    unsigned long uncomp_size;
    unsigned long local_offset;
} ZipEntry;

typedef int (*zip_iter_cb)(const ZipEntry *entry, void *user);

int zip_foreach(const char *zip_path, zip_iter_cb cb, void *user);
int zip_extract_named(const char *zip_path, const char *entry_name, const char *out_path);
int zip_extract_all(const char *zip_path, const char *dest_dir);
