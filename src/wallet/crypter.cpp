// Copyright (c) 2009-2021 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/crypter.h>

#include <common/system.h>
#include <crypto/aes.h>
#include <crypto/sha512.h>
#include <logging.h>
#include <util/strencodings.h>

#include <type_traits>
#include <vector>
#include <mutex>

// ============================================================================
// SECURITY FIX M-03: Use libsodium for Argon2id key derivation
// ============================================================================
// Previous implementation used a custom simplified Argon2id-like function.
// This fix uses libsodium's crypto_pwhash_argon2id when available, which is:
//   1. Audited and battle-tested (used by 1Password, Signal, etc.)
//   2. RFC 9106 compliant
//   3. Properly memory-hard with timing attack resistance
// ============================================================================

#ifdef HAVE_LIBSODIUM
#include <sodium.h>
#define USE_LIBSODIUM_WALLET 1
#else
#define USE_LIBSODIUM_WALLET 0
// Fallback to embedded implementation for development/testing only
#include <crypto/blake2b.h>
#endif

namespace wallet {

// ============================================================================
// SECURITY FIX M-03: Argon2id Key Derivation via libsodium
// ============================================================================
// Uses libsodium's crypto_pwhash_argon2id which provides:
//   - RFC 9106 compliance
//   - Memory-hardness resistant to GPU/ASIC attacks
//   - Constant-time operations to prevent timing attacks
//   - Extensive security audits
// ============================================================================

namespace {

#if USE_LIBSODIUM_WALLET

// Initialize libsodium (thread-safe, idempotent)
static bool InitSodium() {
    static bool initialized = false;
    static std::once_flag flag;
    std::call_once(flag, []() {
        if (sodium_init() >= 0) {
            initialized = true;
            // Note: Using libsodium for Argon2id - initialization successful
        } else {
            LogPrintf("ERROR: Failed to initialize libsodium\n");
        }
    });
    return initialized;
}

// Argon2id using libsodium (RFC 9106 compliant, audited implementation)
bool Argon2idDerive(
    const unsigned char* password, size_t password_len,
    const unsigned char* salt, size_t salt_len,
    unsigned int iterations,
    unsigned int memory_kb,
    unsigned int parallelism,
    unsigned char* output, size_t output_len)
{
    if (!InitSodium()) {
        LogPrintf("ERROR: libsodium not initialized, cannot derive key\n");
        return false;
    }

    // libsodium's crypto_pwhash requires salt to be exactly crypto_pwhash_SALTBYTES (16)
    // If our salt differs, we hash it to the correct length
    unsigned char salt_padded[crypto_pwhash_SALTBYTES];
    if (salt_len == crypto_pwhash_SALTBYTES) {
        memcpy(salt_padded, salt, crypto_pwhash_SALTBYTES);
    } else {
        // Hash the salt to exactly 16 bytes using generichash
        crypto_generichash(salt_padded, crypto_pwhash_SALTBYTES, salt, salt_len, nullptr, 0);
    }

    // Convert memory_kb to bytes for libsodium
    // Note: libsodium's memlimit is in bytes, minimum is crypto_pwhash_MEMLIMIT_MIN
    size_t memlimit = static_cast<size_t>(memory_kb) * 1024;
    if (memlimit < crypto_pwhash_MEMLIMIT_MIN) {
        memlimit = crypto_pwhash_MEMLIMIT_MIN;
    }

    // libsodium's opslimit (iterations)
    unsigned long long opslimit = iterations;
    if (opslimit < crypto_pwhash_OPSLIMIT_MIN) {
        opslimit = crypto_pwhash_OPSLIMIT_MIN;
    }

    int result = crypto_pwhash(
        output,
        output_len,
        reinterpret_cast<const char*>(password),
        password_len,
        salt_padded,
        opslimit,
        memlimit,
        crypto_pwhash_ALG_ARGON2ID13
    );

    // Clear sensitive salt data
    sodium_memzero(salt_padded, sizeof(salt_padded));

    if (result != 0) {
        LogPrintf("ERROR: Argon2id key derivation failed (memory allocation or parameter error)\n");
        return false;
    }

    return true;
}

#else // Fallback for development/testing without libsodium

// Blake2b wrapper for Argon2id - uses proper RFC 7693 Blake2b implementation
void Blake2bHash(const unsigned char* input, size_t input_len, unsigned char* output, size_t output_len)
{
    // Use the production Blake2b implementation (RFC 7693)
    // Blake2b supports variable output lengths up to 64 bytes natively
    if (output_len <= CBlake2b::MAX_OUTPUT_SIZE) {
        Blake2b(input, input_len, output, output_len);
    } else {
        // For lengths > 64 bytes, use Blake2b in a chained mode
        // This follows the Argon2 specification for variable-length output
        unsigned char temp[CBlake2b::MAX_OUTPUT_SIZE];
        size_t copied = 0;
        
        // First block includes input
        Blake2b(input, input_len, temp, CBlake2b::MAX_OUTPUT_SIZE);
        size_t to_copy = std::min(output_len, CBlake2b::MAX_OUTPUT_SIZE);
        memcpy(output, temp, to_copy);
        copied += to_copy;
        
        // Subsequent blocks chain from previous
        while (copied < output_len) {
            Blake2b(temp, CBlake2b::MAX_OUTPUT_SIZE, temp, CBlake2b::MAX_OUTPUT_SIZE);
            to_copy = std::min(output_len - copied, CBlake2b::MAX_OUTPUT_SIZE);
            memcpy(output + copied, temp, to_copy);
            copied += to_copy;
        }
        memory_cleanse(temp, sizeof(temp));
    }
}

// Simplified Argon2id-like memory-hard function (DEVELOPMENT ONLY)
// WARNING: This is a simplified implementation for testing without libsodium.
// Production wallets MUST use libsodium for proper security.
bool Argon2idDerive(
    const unsigned char* password, size_t password_len,
    const unsigned char* salt, size_t salt_len,
    unsigned int iterations,
    unsigned int memory_kb,
    unsigned int parallelism,
    unsigned char* output, size_t output_len)
{
    static bool warned = false;
    if (!warned) {
        LogPrintf("**********************************************************************\n");
        LogPrintf("* WARNING: Wallet using fallback Argon2id (libsodium not available) *\n");
        LogPrintf("* Install libsodium and rebuild for production wallet security!     *\n");
        LogPrintf("**********************************************************************\n");
        warned = true;
    }

    // Memory allocation (capped for safety)
    const size_t memory_bytes = std::min<size_t>(memory_kb * 1024ULL, 256 * 1024 * 1024ULL); // Max 256MB
    const size_t block_size = 1024;
    const size_t num_blocks = memory_bytes / block_size;

    if (num_blocks < 8) return false;

    std::vector<unsigned char> memory;
    try {
        memory.resize(memory_bytes);
    } catch (const std::bad_alloc&) {
        return false;
    }

    // Initial block from password and salt
    std::vector<unsigned char> initial_data;
    initial_data.insert(initial_data.end(), password, password + password_len);
    initial_data.insert(initial_data.end(), salt, salt + salt_len);

    // Initialize first blocks
    for (size_t i = 0; i < num_blocks; i++) {
        unsigned char block_input[64];
        memset(block_input, 0, sizeof(block_input));

        // Mix in initial data
        size_t copy_len = std::min(initial_data.size(), sizeof(block_input) - 8);
        memcpy(block_input, initial_data.data(), copy_len);

        // Include block index
        block_input[56] = i & 0xFF;
        block_input[57] = (i >> 8) & 0xFF;
        block_input[58] = (i >> 16) & 0xFF;
        block_input[59] = (i >> 24) & 0xFF;

        Blake2bHash(block_input, sizeof(block_input), memory.data() + i * block_size, block_size);
    }

    // Memory-hard iterations
    for (unsigned int iter = 0; iter < iterations; iter++) {
        for (size_t i = 0; i < num_blocks; i++) {
            // Reference a pseudo-random previous block
            size_t ref_idx;
            memcpy(&ref_idx, memory.data() + i * block_size, sizeof(ref_idx));
            ref_idx = ref_idx % num_blocks;

            // XOR with referenced block and rehash
            unsigned char temp[block_size];
            for (size_t j = 0; j < block_size; j++) {
                temp[j] = memory[i * block_size + j] ^ memory[ref_idx * block_size + j];
            }

            Blake2bHash(temp, block_size, memory.data() + i * block_size, block_size);
        }
    }

    // Extract output from final blocks
    Blake2bHash(memory.data() + (num_blocks - 1) * block_size, block_size, output, output_len);

    // Clean up
    memory_cleanse(memory.data(), memory.size());

    return true;
}

#endif // USE_LIBSODIUM_WALLET

} // anonymous namespace

int CCrypter::BytesToKeyArgon2id(const std::span<const unsigned char> salt, const SecureString& key_data,
                                  int iterations, unsigned int memory_kb, unsigned int parallelism,
                                  unsigned char* key, unsigned char* iv) const
{
    if (!key || !iv || iterations < 1) {
        return 0;
    }

    // Derive key material using Argon2id
    unsigned char derived[WALLET_CRYPTO_KEY_SIZE + WALLET_CRYPTO_IV_SIZE];

    bool success = Argon2idDerive(
        reinterpret_cast<const unsigned char*>(key_data.data()), key_data.size(),
        salt.data(), salt.size(),
        static_cast<unsigned int>(iterations),
        memory_kb,
        parallelism,
        derived, sizeof(derived)
    );

    if (!success) {
        return 0;
    }

    memcpy(key, derived, WALLET_CRYPTO_KEY_SIZE);
    memcpy(iv, derived + WALLET_CRYPTO_KEY_SIZE, WALLET_CRYPTO_IV_SIZE);
    memory_cleanse(derived, sizeof(derived));

    return WALLET_CRYPTO_KEY_SIZE;
}
int CCrypter::BytesToKeySHA512AES(const std::span<const unsigned char> salt, const SecureString& key_data, int count, unsigned char* key, unsigned char* iv) const
{
    // This mimics the behavior of openssl's EVP_BytesToKey with an aes256cbc
    // cipher and sha512 message digest. Because sha512's output size (64b) is
    // greater than the aes256 block size (16b) + aes256 key size (32b),
    // there's no need to process more than once (D_0).

    if(!count || !key || !iv)
        return 0;

    unsigned char buf[CSHA512::OUTPUT_SIZE];
    CSHA512 di;

    di.Write(UCharCast(key_data.data()), key_data.size());
    di.Write(salt.data(), salt.size());
    di.Finalize(buf);

    for(int i = 0; i != count - 1; i++)
        di.Reset().Write(buf, sizeof(buf)).Finalize(buf);

    memcpy(key, buf, WALLET_CRYPTO_KEY_SIZE);
    memcpy(iv, buf + WALLET_CRYPTO_KEY_SIZE, WALLET_CRYPTO_IV_SIZE);
    memory_cleanse(buf, sizeof(buf));
    return WALLET_CRYPTO_KEY_SIZE;
}

bool CCrypter::SetKeyFromPassphrase(const SecureString& key_data, const std::span<const unsigned char> salt, const unsigned int rounds, const unsigned int derivation_method)
{
    if (rounds < 1 || salt.size() != WALLET_CRYPTO_SALT_SIZE) {
        return false;
    }

    int i = 0;
    if (derivation_method == static_cast<unsigned int>(KeyDerivationMethod::SHA512_AES)) {
        // Legacy SHA512-based key derivation
        i = BytesToKeySHA512AES(salt, key_data, rounds, vchKey.data(), vchIV.data());
    } else if (derivation_method == static_cast<unsigned int>(KeyDerivationMethod::ARGON2ID)) {
        // SECURITY FIX [L-02]: Modern Argon2id key derivation
        // Memory-hard algorithm resistant to GPU/ASIC brute-force attacks
        i = BytesToKeyArgon2id(salt, key_data, rounds,
                               CMasterKey::DEFAULT_ARGON2ID_MEMORY_KB,
                               CMasterKey::DEFAULT_ARGON2ID_PARALLELISM,
                               vchKey.data(), vchIV.data());
    }

    if (i != (int)WALLET_CRYPTO_KEY_SIZE)
    {
        memory_cleanse(vchKey.data(), vchKey.size());
        memory_cleanse(vchIV.data(), vchIV.size());
        return false;
    }

    fKeySet = true;
    return true;
}

bool CCrypter::SetKey(const CKeyingMaterial& new_key, const std::span<const unsigned char> new_iv)
{
    if (new_key.size() != WALLET_CRYPTO_KEY_SIZE || new_iv.size() != WALLET_CRYPTO_IV_SIZE) {
        return false;
    }

    memcpy(vchKey.data(), new_key.data(), new_key.size());
    memcpy(vchIV.data(), new_iv.data(), new_iv.size());

    fKeySet = true;
    return true;
}

bool CCrypter::Encrypt(const CKeyingMaterial& vchPlaintext, std::vector<unsigned char> &vchCiphertext) const
{
    if (!fKeySet)
        return false;

    // max ciphertext len for a n bytes of plaintext is
    // n + AES_BLOCKSIZE bytes
    vchCiphertext.resize(vchPlaintext.size() + AES_BLOCKSIZE);

    AES256CBCEncrypt enc(vchKey.data(), vchIV.data(), true);
    size_t nLen = enc.Encrypt(vchPlaintext.data(), vchPlaintext.size(), vchCiphertext.data());
    if(nLen < vchPlaintext.size())
        return false;
    vchCiphertext.resize(nLen);

    return true;
}

bool CCrypter::Decrypt(const std::span<const unsigned char> ciphertext, CKeyingMaterial& plaintext) const
{
    if (!fKeySet)
        return false;

    // plaintext will always be equal to or lesser than length of ciphertext
    plaintext.resize(ciphertext.size());

    AES256CBCDecrypt dec(vchKey.data(), vchIV.data(), true);
    int len = dec.Decrypt(ciphertext.data(), ciphertext.size(), plaintext.data());
    if (len == 0) {
        return false;
    }
    plaintext.resize(len);
    return true;
}

bool EncryptSecret(const CKeyingMaterial& vMasterKey, const CKeyingMaterial &vchPlaintext, const uint256& nIV, std::vector<unsigned char> &vchCiphertext)
{
    CCrypter cKeyCrypter;
    std::vector<unsigned char> chIV(WALLET_CRYPTO_IV_SIZE);
    memcpy(chIV.data(), &nIV, WALLET_CRYPTO_IV_SIZE);
    if(!cKeyCrypter.SetKey(vMasterKey, chIV))
        return false;
    return cKeyCrypter.Encrypt(vchPlaintext, vchCiphertext);
}

bool DecryptSecret(const CKeyingMaterial& master_key, const std::span<const unsigned char> ciphertext, const uint256& iv, CKeyingMaterial& plaintext)
{
    CCrypter key_crypter;
    static_assert(WALLET_CRYPTO_IV_SIZE <= std::remove_reference_t<decltype(iv)>::size());
    const std::span iv_prefix{iv.data(), WALLET_CRYPTO_IV_SIZE};
    if (!key_crypter.SetKey(master_key, iv_prefix)) {
        return false;
    }
    return key_crypter.Decrypt(ciphertext, plaintext);
}

bool DecryptKey(const CKeyingMaterial& master_key, const std::span<const unsigned char> crypted_secret, const CPubKey& pub_key, CKey& key)
{
    CKeyingMaterial secret;
    if (!DecryptSecret(master_key, crypted_secret, pub_key.GetHash(), secret)) {
        return false;
    }

    if (secret.size() != 32) {
        return false;
    }

    key.Set(secret.begin(), secret.end(), pub_key.IsCompressed());
    return key.VerifyPubKey(pub_key);
}
} // namespace wallet
