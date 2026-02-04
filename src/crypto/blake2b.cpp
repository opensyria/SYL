// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/blake2b.h>
#include <crypto/common.h>
#include <support/cleanse.h>

#include <algorithm>
#include <cassert>
#include <cstring>

/**
 * BLAKE2b implementation based on RFC 7693
 * Reference: https://www.rfc-editor.org/rfc/rfc7693
 * 
 * BLAKE2b is a cryptographic hash function optimized for 64-bit platforms.
 * It is the core hash function used by Argon2id.
 */

namespace {

// BLAKE2b IV (same as SHA-512 IV)
static const uint64_t BLAKE2B_IV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

// BLAKE2b sigma permutation table
static const uint8_t BLAKE2B_SIGMA[12][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
    { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
    { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
    { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
    { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
    { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
    { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
    { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
    { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 }
};

// Rotate right (circular right shift)
inline uint64_t ROTR64(uint64_t x, int n)
{
    return (x >> n) | (x << (64 - n));
}

// BLAKE2b mixing function G
inline void G(uint64_t& a, uint64_t& b, uint64_t& c, uint64_t& d, uint64_t x, uint64_t y)
{
    a = a + b + x;
    d = ROTR64(d ^ a, 32);
    c = c + d;
    b = ROTR64(b ^ c, 24);
    a = a + b + y;
    d = ROTR64(d ^ a, 16);
    c = c + d;
    b = ROTR64(b ^ c, 63);
}

// Load 64-bit little-endian value
inline uint64_t load64(const uint8_t* src)
{
    uint64_t w;
    memcpy(&w, src, sizeof(w));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    w = __builtin_bswap64(w);
#endif
    return w;
}

// Store 64-bit little-endian value
inline void store64(uint8_t* dst, uint64_t w)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    w = __builtin_bswap64(w);
#endif
    memcpy(dst, &w, sizeof(w));
}

} // anonymous namespace

void CBlake2b::Compress(const uint8_t block[128])
{
    uint64_t m[16];
    uint64_t v[16];

    // Load message block
    for (int i = 0; i < 16; i++) {
        m[i] = load64(block + i * 8);
    }

    // Initialize working vector
    for (int i = 0; i < 8; i++) {
        v[i] = h[i];
    }
    v[8] = BLAKE2B_IV[0];
    v[9] = BLAKE2B_IV[1];
    v[10] = BLAKE2B_IV[2];
    v[11] = BLAKE2B_IV[3];
    v[12] = BLAKE2B_IV[4] ^ t[0];
    v[13] = BLAKE2B_IV[5] ^ t[1];
    v[14] = BLAKE2B_IV[6] ^ f[0];
    v[15] = BLAKE2B_IV[7] ^ f[1];

    // Twelve rounds of mixing
    for (int i = 0; i < 12; i++) {
        const uint8_t* s = BLAKE2B_SIGMA[i];

        G(v[0], v[4], v[8],  v[12], m[s[0]],  m[s[1]]);
        G(v[1], v[5], v[9],  v[13], m[s[2]],  m[s[3]]);
        G(v[2], v[6], v[10], v[14], m[s[4]],  m[s[5]]);
        G(v[3], v[7], v[11], v[15], m[s[6]],  m[s[7]]);

        G(v[0], v[5], v[10], v[15], m[s[8]],  m[s[9]]);
        G(v[1], v[6], v[11], v[12], m[s[10]], m[s[11]]);
        G(v[2], v[7], v[8],  v[13], m[s[12]], m[s[13]]);
        G(v[3], v[4], v[9],  v[14], m[s[14]], m[s[15]]);
    }

    // Finalize state
    for (int i = 0; i < 8; i++) {
        h[i] ^= v[i] ^ v[i + 8];
    }
}

CBlake2b::CBlake2b(size_t output_size)
{
    assert(output_size > 0 && output_size <= MAX_OUTPUT_SIZE);
    outlen = output_size;

    // Initialize state with IV
    for (int i = 0; i < 8; i++) {
        h[i] = BLAKE2B_IV[i];
    }

    // XOR with parameter block (first word: digest_length | key_length | fanout | depth)
    h[0] ^= 0x01010000ULL ^ static_cast<uint64_t>(outlen);

    t[0] = 0;
    t[1] = 0;
    f[0] = 0;
    f[1] = 0;
    buflen = 0;
    memset(buf, 0, sizeof(buf));
}

CBlake2b::CBlake2b(const unsigned char* key, size_t key_len, size_t output_size)
{
    assert(output_size > 0 && output_size <= MAX_OUTPUT_SIZE);
    assert(key_len <= 64);
    outlen = output_size;

    // Initialize state with IV
    for (int i = 0; i < 8; i++) {
        h[i] = BLAKE2B_IV[i];
    }

    // XOR with parameter block including key length
    h[0] ^= 0x01010000ULL ^ (static_cast<uint64_t>(key_len) << 8) ^ static_cast<uint64_t>(outlen);

    t[0] = 0;
    t[1] = 0;
    f[0] = 0;
    f[1] = 0;
    buflen = 0;
    memset(buf, 0, sizeof(buf));

    // If keyed, pad key to block size and process
    if (key_len > 0) {
        uint8_t key_block[BLOCKSIZE];
        memset(key_block, 0, BLOCKSIZE);
        memcpy(key_block, key, key_len);
        Write(key_block, BLOCKSIZE);
        memory_cleanse(key_block, BLOCKSIZE);
    }
}

CBlake2b& CBlake2b::Write(const unsigned char* data, size_t len)
{
    if (len == 0) return *this;

    size_t left = buflen;
    size_t fill = BLOCKSIZE - left;

    if (len > fill) {
        // Fill buffer
        memcpy(buf + left, data, fill);
        t[0] += BLOCKSIZE;
        if (t[0] < BLOCKSIZE) t[1]++;  // Overflow
        Compress(buf);
        buflen = 0;
        data += fill;
        len -= fill;

        // Process full blocks
        while (len > BLOCKSIZE) {
            t[0] += BLOCKSIZE;
            if (t[0] < BLOCKSIZE) t[1]++;
            Compress(data);
            data += BLOCKSIZE;
            len -= BLOCKSIZE;
        }
    }

    // Buffer remaining
    memcpy(buf + buflen, data, len);
    buflen += len;

    return *this;
}

void CBlake2b::Finalize(unsigned char* hash)
{
    // Add remaining bytes to counter
    t[0] += buflen;
    if (t[0] < buflen) t[1]++;

    // Set finalization flag
    f[0] = ~0ULL;

    // Pad remaining buffer with zeros
    memset(buf + buflen, 0, BLOCKSIZE - buflen);

    // Final compression
    Compress(buf);

    // Output hash
    uint8_t buffer[64];
    for (int i = 0; i < 8; i++) {
        store64(buffer + i * 8, h[i]);
    }
    memcpy(hash, buffer, outlen);

    memory_cleanse(buffer, sizeof(buffer));
}

CBlake2b& CBlake2b::Reset()
{
    // Re-initialize to default state
    for (int i = 0; i < 8; i++) {
        h[i] = BLAKE2B_IV[i];
    }
    h[0] ^= 0x01010000ULL ^ static_cast<uint64_t>(outlen);

    t[0] = 0;
    t[1] = 0;
    f[0] = 0;
    f[1] = 0;
    buflen = 0;
    memset(buf, 0, sizeof(buf));

    return *this;
}

void Blake2b(const unsigned char* input, size_t input_len,
             unsigned char* output, size_t output_len)
{
    CBlake2b hasher(output_len);
    hasher.Write(input, input_len);
    hasher.Finalize(output);
}

void Blake2bKeyed(const unsigned char* input, size_t input_len,
                  const unsigned char* key, size_t key_len,
                  unsigned char* output, size_t output_len)
{
    CBlake2b hasher(key, key_len, output_len);
    hasher.Write(input, input_len);
    hasher.Finalize(output);
}
