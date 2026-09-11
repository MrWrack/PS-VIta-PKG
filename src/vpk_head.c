#include "vpk_head.h"
#include "sha1tiny.h"
#include "sfo.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

extern const unsigned char _binary_resources_head_bin_start[];
extern const unsigned char _binary_resources_head_bin_end[];

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

/* Fake-package digest used by Vita package promotion. */
static void fpkg_digest(const uint8_t *data, unsigned int len, uint8_t out[16]) {
    Sha1Tiny s;
    uint8_t sha[20];
    uint8_t b[64];

    sha1tiny_init(&s);
    sha1tiny_update(&s, data, len);
    sha1tiny_final(&s, sha);

    memset(b, 0, sizeof(b));
    memcpy(&b[0],  &sha[4], 8);
    memcpy(&b[8],  &sha[4], 8);
    memcpy(&b[16], &sha[12], 4);
    b[20] = sha[16];
    b[21] = sha[1];
    b[22] = sha[2];
    b[23] = sha[3];
    memcpy(&b[24], &b[16], 8);

    sha1tiny_init(&s);
    sha1tiny_update(&s, b, sizeof(b));
    sha1tiny_final(&s, sha);
    memcpy(out, sha, 16);
}

static int write_all(const char *path, const void *buf, unsigned int size) {
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return fd;

    const uint8_t *p = (const uint8_t *)buf;
    unsigned int done = 0;
    while (done < size) {
        int r = sceIoWrite(fd, p + done, size - done);
        if (r < 0) {
            sceIoClose(fd);
            return r;
        }
        if (r == 0) {
            sceIoClose(fd);
            return -1;
        }
        done += (unsigned int)r;
    }

    sceIoClose(fd);
    return 0;
}

static int valid_titleid(const char *s) {
    if (!s || strlen(s) != 9) return 0;
    for (int i = 0; i < 9; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (isalpha(c) && !isupper(c)) return 0;
        if (!(isalnum(c) || c == '_')) return 0;
    }
    return 1;
}

int vpk_make_head_bin(const char *install_dir) {
    char sfo_path[512];
    char package_dir[512];
    char head_path[512];
    char titleid[32] = {0};
    char contentid[64] = {0};

    snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", install_dir);
    snprintf(package_dir, sizeof(package_dir), "%s/sce_sys/package", install_dir);
    snprintf(head_path, sizeof(head_path), "%s/head.bin", package_dir);

    SceIoStat st;
    if (sceIoGetstat(head_path, &st) >= 0)
        return 0;

    if (sfo_get_string(sfo_path, "TITLE_ID", titleid, sizeof(titleid)) < 0)
        return -1001;

    if (!valid_titleid(titleid))
        return -1002;

    /* CONTENT_ID is optional for normal homebrew VPKs. */
    sfo_get_string(sfo_path, "CONTENT_ID", contentid, sizeof(contentid));

    size_t size = (size_t)(_binary_resources_head_bin_end - _binary_resources_head_bin_start);
    if (size < 0x200)
        return -1003;

    uint8_t *head = (uint8_t *)malloc(size);
    if (!head)
        return -1004;
    memcpy(head, _binary_resources_head_bin_start, size);

    char fallback[48];
    snprintf(fallback, sizeof(fallback),
             "EP9000-%s_00-0000000000000000", titleid);

    const char *cid = contentid[0] ? contentid : fallback;
    memset(head + 0x30, 0, 48);
    strncpy((char *)(head + 0x30), cid, 47);

    uint8_t mac[16];

    uint32_t len = be32(head + 0xD0);
    if ((uint64_t)len + 16 > size) {
        free(head);
        return -1005;
    }
    fpkg_digest(head, len, mac);
    memcpy(head + len, mac, 16);

    uint32_t off = be32(head + 0x08);
    len = be32(head + 0x10);
    uint32_t out = be32(head + 0xD4);
    if (len < 64 || (uint64_t)off + len > size || (uint64_t)out + 16 > size) {
        free(head);
        return -1006;
    }
    fpkg_digest(head + off, len - 64, mac);
    memcpy(head + out, mac, 16);

    len = be32(head + 0xE8);
    if ((uint64_t)len + 16 > size) {
        free(head);
        return -1007;
    }
    fpkg_digest(head, len, mac);
    memcpy(head + len, mac, 16);

    sceIoMkdir(package_dir, 0777);
    int r = write_all(head_path, head, (unsigned int)size);
    free(head);
    return r;
}
