// Copyright (c) 2009-2021 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_WALLET_CRYPTER_H
#define OPENSY_WALLET_CRYPTER_H

#include <serialize.h>
#include <support/allocators/secure.h>
#include <script/signingprovider.h>


namespace wallet {
const unsigned int WALLET_CRYPTO_KEY_SIZE = 32;
const unsigned int WALLET_CRYPTO_SALT_SIZE = 8;
const unsigned int WALLET_CRYPTO_IV_SIZE = 16;

/**
 * Private key encryption is done based on a CMasterKey,
 * which holds a salt and random encryption key.
 *
 * CMasterKeys are encrypted using AES-256-CBC using a key
 * derived using derivation method nDerivationMethod
 * (0 == EVP_sha512(), 1 == Argon2id) and derivation iterations nDeriveIterations.
 * vchOtherDerivationParameters is provided for alternative algorithms
 * which may require more parameters (e.g., Argon2id memory cost).
 *
 * Wallet Private Keys are then encrypted using AES-256-CBC
 * with the double-sha256 of the public key as the IV, and the
 * master key's key as the encryption key (see keystore.[ch]).
 *
 * SECURITY FIX [L-02]: Wallet Encryption Key Derivation
 * Added support for Argon2id key derivation (method 1) which provides:
 * - Memory-hard key derivation resistant to GPU/ASIC attacks
 * - Better protection against brute-force on weak passwords
 * - New wallets default to Argon2id, legacy SHA512 still supported
 */

/** Key derivation methods */
enum class KeyDerivationMethod : unsigned int {
    SHA512_AES = 0,   //!< Legacy: SHA512-based key derivation
    ARGON2ID = 1,     //!< Modern: Argon2id memory-hard key derivation
};

/** Master key for wallet encryption */
class CMasterKey
{
public:
    std::vector<unsigned char> vchCryptedKey;
    std::vector<unsigned char> vchSalt;
    //! 0 = EVP_sha512() (legacy), 1 = Argon2id (recommended for new wallets)
    unsigned int nDerivationMethod;
    unsigned int nDeriveIterations;
    //! Use this for more parameters to key derivation
    //! For Argon2id: [0-3] = memory cost (KB), [4-7] = parallelism
    std::vector<unsigned char> vchOtherDerivationParameters;

    //! Default/minimum number of key derivation rounds
    // For SHA512: 25000 rounds is just under 0.1 seconds on a 1.86 GHz Pentium M
    // For Argon2id: iterations are much more expensive, use lower value
    static constexpr unsigned int DEFAULT_DERIVE_ITERATIONS = 25000;
    static constexpr unsigned int DEFAULT_ARGON2ID_ITERATIONS = 3;  // Argon2id iterations
    static constexpr unsigned int DEFAULT_ARGON2ID_MEMORY_KB = 65536;  // 64MB memory cost
    static constexpr unsigned int DEFAULT_ARGON2ID_PARALLELISM = 4;  // 4 parallel lanes

    SERIALIZE_METHODS(CMasterKey, obj)
    {
        READWRITE(obj.vchCryptedKey, obj.vchSalt, obj.nDerivationMethod, obj.nDeriveIterations, obj.vchOtherDerivationParameters);
    }

    CMasterKey()
    {
        nDeriveIterations = DEFAULT_DERIVE_ITERATIONS;
        nDerivationMethod = 0;
        vchOtherDerivationParameters = std::vector<unsigned char>(0);
    }
};

typedef std::vector<unsigned char, secure_allocator<unsigned char> > CKeyingMaterial;

namespace wallet_crypto_tests
{
    class TestCrypter;
}

/** Encryption/decryption context with key information */
class CCrypter
{
friend class wallet_crypto_tests::TestCrypter; // for test access to chKey/chIV
private:
    std::vector<unsigned char, secure_allocator<unsigned char>> vchKey;
    std::vector<unsigned char, secure_allocator<unsigned char>> vchIV;
    bool fKeySet;

    int BytesToKeySHA512AES(std::span<const unsigned char> salt, const SecureString& key_data, int count, unsigned char* key, unsigned char* iv) const;

    /**
     * SECURITY FIX [L-02]: Argon2id key derivation for enhanced brute-force resistance.
     * Uses memory-hard Argon2id algorithm to derive encryption key from passphrase.
     *
     * @param[in] salt Salt bytes for key derivation
     * @param[in] key_data Passphrase
     * @param[in] iterations Time cost (number of iterations)
     * @param[in] memory_kb Memory cost in KB (default 64MB)
     * @param[in] parallelism Degree of parallelism (default 4)
     * @param[out] key Output key buffer (WALLET_CRYPTO_KEY_SIZE bytes)
     * @param[out] iv Output IV buffer (WALLET_CRYPTO_IV_SIZE bytes)
     * @return Number of key bytes derived, or 0 on failure
     */
    int BytesToKeyArgon2id(std::span<const unsigned char> salt, const SecureString& key_data,
                           int iterations, unsigned int memory_kb, unsigned int parallelism,
                           unsigned char* key, unsigned char* iv) const;

public:
    bool SetKeyFromPassphrase(const SecureString& key_data, std::span<const unsigned char> salt, const unsigned int rounds, const unsigned int derivation_method);
    bool Encrypt(const CKeyingMaterial& vchPlaintext, std::vector<unsigned char> &vchCiphertext) const;
    bool Decrypt(std::span<const unsigned char> ciphertext, CKeyingMaterial& plaintext) const;
    bool SetKey(const CKeyingMaterial& new_key, std::span<const unsigned char> new_iv);

    void CleanKey()
    {
        memory_cleanse(vchKey.data(), vchKey.size());
        memory_cleanse(vchIV.data(), vchIV.size());
        fKeySet = false;
    }

    CCrypter()
    {
        fKeySet = false;
        vchKey.resize(WALLET_CRYPTO_KEY_SIZE);
        vchIV.resize(WALLET_CRYPTO_IV_SIZE);
    }

    ~CCrypter()
    {
        CleanKey();
    }
};

bool EncryptSecret(const CKeyingMaterial& vMasterKey, const CKeyingMaterial &vchPlaintext, const uint256& nIV, std::vector<unsigned char> &vchCiphertext);
bool DecryptSecret(const CKeyingMaterial& master_key, std::span<const unsigned char> ciphertext, const uint256& iv, CKeyingMaterial& plaintext);
bool DecryptKey(const CKeyingMaterial& master_key, std::span<const unsigned char> crypted_secret, const CPubKey& pub_key, CKey& key);
} // namespace wallet

#endif // OPENSY_WALLET_CRYPTER_H
