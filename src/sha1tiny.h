#ifndef SHA1TINY_H
#define SHA1TINY_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[5];
    uint64_t total;
    uint8_t block[64];
    size_t used;
} Sha1Tiny;

void sha1tiny_init(Sha1Tiny *s);
void sha1tiny_update(Sha1Tiny *s, const void *data, size_t len);
void sha1tiny_final(Sha1Tiny *s, uint8_t out[20]);

#endif
