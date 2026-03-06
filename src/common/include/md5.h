#pragma once
#include <cstdint>
#include <cstring>

// Drop-in replacement for <openssl/md5.h>.
// Provides MD5_CTX, MD5_Init, MD5_Update, MD5_Final, MD5_DIGEST_LENGTH.

static constexpr int MD5_DIGEST_LENGTH = 16;

struct MD5_CTX {
    uint32_t state[4];
    uint64_t count;     // total bytes processed
    uint8_t  buffer[64];
};

namespace md5_detail {

static constexpr uint32_t T[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

static inline uint32_t rot(uint32_t x, uint32_t n) { return (x << n) | (x >> (32 - n)); }

static void transform(uint32_t s[4], const uint8_t blk[64]) {
    uint32_t a = s[0], b = s[1], c = s[2], d = s[3], x[16];
    for (int i = 0; i < 16; i++)
        x[i] = (uint32_t)blk[i*4]       | ((uint32_t)blk[i*4+1] << 8) |
               ((uint32_t)blk[i*4+2] << 16) | ((uint32_t)blk[i*4+3] << 24);

#define FF(a,b,c,d,k,s,i) a = b + rot(a + ((b&c)|(~b&d))  + x[k] + T[i], s)
#define GG(a,b,c,d,k,s,i) a = b + rot(a + ((b&d)|(c&~d))  + x[k] + T[i], s)
#define HH(a,b,c,d,k,s,i) a = b + rot(a + (b^c^d)         + x[k] + T[i], s)
#define II(a,b,c,d,k,s,i) a = b + rot(a + (c^(b|~d))      + x[k] + T[i], s)

    FF(a,b,c,d, 0, 7, 0); FF(d,a,b,c, 1,12, 1); FF(c,d,a,b, 2,17, 2); FF(b,c,d,a, 3,22, 3);
    FF(a,b,c,d, 4, 7, 4); FF(d,a,b,c, 5,12, 5); FF(c,d,a,b, 6,17, 6); FF(b,c,d,a, 7,22, 7);
    FF(a,b,c,d, 8, 7, 8); FF(d,a,b,c, 9,12, 9); FF(c,d,a,b,10,17,10); FF(b,c,d,a,11,22,11);
    FF(a,b,c,d,12, 7,12); FF(d,a,b,c,13,12,13); FF(c,d,a,b,14,17,14); FF(b,c,d,a,15,22,15);

    GG(a,b,c,d, 1, 5,16); GG(d,a,b,c, 6, 9,17); GG(c,d,a,b,11,14,18); GG(b,c,d,a, 0,20,19);
    GG(a,b,c,d, 5, 5,20); GG(d,a,b,c,10, 9,21); GG(c,d,a,b,15,14,22); GG(b,c,d,a, 4,20,23);
    GG(a,b,c,d, 9, 5,24); GG(d,a,b,c,14, 9,25); GG(c,d,a,b, 3,14,26); GG(b,c,d,a, 8,20,27);
    GG(a,b,c,d,13, 5,28); GG(d,a,b,c, 2, 9,29); GG(c,d,a,b, 7,14,30); GG(b,c,d,a,12,20,31);

    HH(a,b,c,d, 5, 4,32); HH(d,a,b,c, 8,11,33); HH(c,d,a,b,11,16,34); HH(b,c,d,a,14,23,35);
    HH(a,b,c,d, 1, 4,36); HH(d,a,b,c, 4,11,37); HH(c,d,a,b, 7,16,38); HH(b,c,d,a,10,23,39);
    HH(a,b,c,d,13, 4,40); HH(d,a,b,c, 0,11,41); HH(c,d,a,b, 3,16,42); HH(b,c,d,a, 6,23,43);
    HH(a,b,c,d, 9, 4,44); HH(d,a,b,c,12,11,45); HH(c,d,a,b,15,16,46); HH(b,c,d,a, 2,23,47);

    II(a,b,c,d, 0, 6,48); II(d,a,b,c, 7,10,49); II(c,d,a,b,14,15,50); II(b,c,d,a, 5,21,51);
    II(a,b,c,d,12, 6,52); II(d,a,b,c, 3,10,53); II(c,d,a,b,10,15,54); II(b,c,d,a, 1,21,55);
    II(a,b,c,d, 8, 6,56); II(d,a,b,c,15,10,57); II(c,d,a,b, 6,15,58); II(b,c,d,a,13,21,59);
    II(a,b,c,d, 4, 6,60); II(d,a,b,c,11,10,61); II(c,d,a,b, 2,15,62); II(b,c,d,a, 9,21,63);

#undef FF
#undef GG
#undef HH
#undef II

    s[0] += a; s[1] += b; s[2] += c; s[3] += d;
}

} // namespace md5_detail

inline void MD5_Init(MD5_CTX* ctx) {
    ctx->count    = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
}

inline void MD5_Update(MD5_CTX* ctx, const void* data, size_t len) {
    const uint8_t* in  = static_cast<const uint8_t*>(data);
    size_t         off = static_cast<size_t>(ctx->count % 64);
    ctx->count += len;

    if (off) {
        size_t space = 64 - off;
        if (len < space) { memcpy(ctx->buffer + off, in, len); return; }
        memcpy(ctx->buffer + off, in, space);
        md5_detail::transform(ctx->state, ctx->buffer);
        in  += space;
        len -= space;
    }
    while (len >= 64) {
        md5_detail::transform(ctx->state, in);
        in  += 64;
        len -= 64;
    }
    memcpy(ctx->buffer, in, len);
}

inline void MD5_Final(uint8_t digest[16], MD5_CTX* ctx) {
    uint64_t bits  = ctx->count * 8;
    size_t   off   = static_cast<size_t>(ctx->count % 64);

    ctx->buffer[off++] = 0x80;
    if (off > 56) {
        if (off < 64) memset(ctx->buffer + off, 0, 64 - off);
        md5_detail::transform(ctx->state, ctx->buffer);
        off = 0;
    }
    memset(ctx->buffer + off, 0, 56 - off);
    for (int i = 0; i < 8; i++)
        ctx->buffer[56 + i] = static_cast<uint8_t>(bits >> (i * 8));
    md5_detail::transform(ctx->state, ctx->buffer);

    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            digest[i*4+j] = static_cast<uint8_t>(ctx->state[i] >> (j * 8));
}
