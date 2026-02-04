// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_CRYPTO_BLAKE2B_H
#define OPENSY_CRYPTO_BLAKE2B_H

#include <cstdint>
#include <cstdlib>

/**
 * BLAKE2b cryptographic hash function (RFC 7693)
 * 
 * BLAKE2b is optimized for 64-bit platforms and produces digests of any size
 * between 1 and 64 bytes. It is used as the core hash function in Argon2id.
 * 
 * This implementation is derived from the reference implementation at:
 * https://github.com/BLAKE2/BLAKE2
 */

// BLAKE2b parameter block
struct Blake2bParam {
    uint8_t digest_length;   // 1
    uint8_t key_length;      // 2
    uint8_t fanout;          // 3
    uint8_t depth;           // 4
    uint32_t leaf_length;    // 8
    uint64_t node_offset;    // 16
    uint8_t node_depth;      // 17
    uint8_t inner_length;    // 18
    uint8_t reserved[14];    // 32
    uint8_t salt[16];        // 48
    uint8_t personal[16];    // 64
};

/** A hasher class for BLAKE2b. */
class CBlake2b
{
private:
    uint64_t h[8];           // State
    uint64_t t[2];           // Total bytes processed
    uint64_t f[2];           // Finalization flags
    uint8_t buf[128];        // Buffer for partial blocks
    size_t buflen;           // Buffer length
    size_t outlen;           // Output length

    void Compress(const uint8_t block[128]);

public:
    static const size_t BLOCKSIZE = 128;
    static const size_t MAX_OUTPUT_SIZE = 64;

    /**
     * Initialize BLAKE2b with specified output length
     * @param output_size Desired output size in bytes (1-64)
     */
    explicit CBlake2b(size_t output_size = 64);

    /**
     * Initialize BLAKE2b with a key (for keyed hashing/MAC)
     * @param key The key bytes
     * @param key_len Key length in bytes (0-64)
     * @param output_size Desired output size in bytes (1-64)
     */
    CBlake2b(const unsigned char* key, size_t key_len, size_t output_size = 64);

    /**
     * Add data to be hashed
     */
    CBlake2b& Write(const unsigned char* data, size_t len);

    /**
     * Finalize and produce hash output
     * @param hash Output buffer (must be at least output_size bytes)
     */
    void Finalize(unsigned char* hash);

    /**
     * Reset the hasher for reuse
     */
    CBlake2b& Reset();

    /**
     * Get the configured output size
     */
    size_t OutputSize() const { return outlen; }
};

/**
 * Convenience function for one-shot BLAKE2b hashing
 * @param input Input data
 * @param input_len Input length
 * @param output Output buffer
 * @param output_len Desired output length (1-64)
 */
void Blake2b(const unsigned char* input, size_t input_len,
             unsigned char* output, size_t output_len);

/**
 * Convenience function for keyed BLAKE2b hashing
 * @param input Input data
 * @param input_len Input length
 * @param key Key data
 * @param key_len Key length (0-64)
 * @param output Output buffer
 * @param output_len Desired output length (1-64)
 */
void Blake2bKeyed(const unsigned char* input, size_t input_len,
                  const unsigned char* key, size_t key_len,
                  unsigned char* output, size_t output_len);

#endif // OPENSY_CRYPTO_BLAKE2B_H
