#include "sha1tiny.h"
#include <string.h>

static uint32_t rol32(uint32_t x, unsigned n) {
    return (x << n) | (x >> (32 - n));
}

static void transform(Sha1Tiny *s, const uint8_t block[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)block[i*4] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) |
               (uint32_t)block[i*4+3];
    }
    for (int i = 16; i < 80; ++i)
        w[i] = rol32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    uint32_t a=s->h[0], b=s->h[1], c=s->h[2], d=s->h[3], e=s->h[4];

    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }

        uint32_t t = rol32(a,5) + f + e + k + w[i];
        e=d; d=c; c=rol32(b,30); b=a; a=t;
    }

    s->h[0]+=a; s->h[1]+=b; s->h[2]+=c; s->h[3]+=d; s->h[4]+=e;
}

void sha1tiny_init(Sha1Tiny *s) {
    s->h[0]=0x67452301u; s->h[1]=0xEFCDAB89u; s->h[2]=0x98BADCFEu;
    s->h[3]=0x10325476u; s->h[4]=0xC3D2E1F0u;
    s->total=0; s->used=0;
}

void sha1tiny_update(Sha1Tiny *s, const void *data_, size_t len) {
    const uint8_t *data=(const uint8_t*)data_;
    s->total += len;

    while (len) {
        size_t n = 64 - s->used;
        if (n > len) n = len;
        memcpy(s->block + s->used, data, n);
        s->used += n;
        data += n;
        len -= n;

        if (s->used == 64) {
            transform(s, s->block);
            s->used = 0;
        }
    }
}

void sha1tiny_final(Sha1Tiny *s, uint8_t out[20]) {
    uint64_t bits = s->total * 8u;
    s->block[s->used++] = 0x80;

    if (s->used > 56) {
        while (s->used < 64) s->block[s->used++] = 0;
        transform(s, s->block);
        s->used = 0;
    }

    while (s->used < 56) s->block[s->used++] = 0;
    for (int i = 7; i >= 0; --i)
        s->block[s->used++] = (uint8_t)(bits >> (i*8));

    transform(s, s->block);

    for (int i = 0; i < 5; ++i) {
        out[i*4]   = (uint8_t)(s->h[i] >> 24);
        out[i*4+1] = (uint8_t)(s->h[i] >> 16);
        out[i*4+2] = (uint8_t)(s->h[i] >> 8);
        out[i*4+3] = (uint8_t)s->h[i];
    }
}
