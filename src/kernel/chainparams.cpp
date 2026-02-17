// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Copyright (c) 2025-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// OpenSY: Forked from Bitcoin Core. "Qirsh" is the smallest unit of SYL,
// equivalent to Bitcoin's "satoshi" (1 SYL = 100,000,000 qirsh).

#include <kernel/chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/amount.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <hash.h>
#include <kernel/messagestartchars.h>
#include <logging.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>

using namespace util::hex_literals;

// Workaround MSVC bug triggering C7595 when calling consteval constructors in
// initializer lists.
// https://developercommunity.visualstudio.com/t/Bogus-C7595-error-on-valid-C20-code/10906093
#if defined(_MSC_VER)
auto consteval_ctor(auto&& input) { return input; }
#else
#define consteval_ctor(input) (input)
#endif

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.version = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "Dec 8 2024 - Syria Liberated from Assad / سوريا حرة";
    // NOTE: This is Bitcoin's original Satoshi genesis pubkey. It is intentionally reused
    // because the genesis coinbase output is provably unspendable in Bitcoin-derived chains
    // (the output is not added to the UTXO set by design). Using a well-known unspendable
    // key avoids any appearance of a hidden premine.
    const CScript genesisOutputScript = CScript() << "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f"_hex << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

/**
 * Main network on which people trade goods and services.
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        m_chain_type = ChainType::MAIN;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 1050000; // ~4 years with 2-min blocks
        // No script flag exceptions for new chain - OpenSY starts fresh
        consensus.BIP34Height = 1; // Active from block 1
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1; // Active from block 1
        consensus.BIP66Height = 1; // Active from block 1
        consensus.CSVHeight = 1; // Active from block 1
        consensus.SegwitHeight = 1; // Active from block 1
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"000000ffff000000000000000000000000000000000000000000000000000000"}; // Matches 0x1e00ffff
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 2 * 60; // 2-minute blocks
        consensus.fPowAllowMinDifficultyBlocks = false;
        // BIP94 timewarp attack protection - enabled for OpenSY mainnet
        // Prevents manipulation of difficulty via timestamp attacks on difficulty period boundaries
        consensus.enforce_BIP94 = true;
        // Difficulty retargeting enabled - adjusts every 10,080 blocks (~2 weeks) to target 2-minute blocks
        // Formula: nPowTargetTimespan / nPowTargetSpacing = 1,209,600 / 120 = 10,080 blocks
        consensus.fPowNoRetargeting = false;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 9072; // 90% of 10080
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 10080; // Matches DifficultyAdjustmentInterval()

        // Deployment of Taproot (BIPs 340-342) - Always active for OpenSY
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].threshold = 9072; // 90% of 10080
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].period = 10080; // Matches DifficultyAdjustmentInterval()

        // Minimum chain work - protects against low-hashrate sybil attacks
        // This prevents attackers from creating fake chains with less total work
        // Attackers would need to redo all PoW from genesis to create an alternate chain
        //
        // Updated at block 210,020 (Feb 15, 2026) - Phase 1 bootstrap complete
        // Get current value: opensy-cli getblockheader $(opensy-cli getblockhash <height>) | grep chainwork
        consensus.nMinimumChainWork = uint256{"00000000000000000000000000000000000000000000000000000832b5fc1a94"};
        
        // AssumeValid - enables faster sync by skipping signature validation for known-good blocks
        // Nodes will skip script validation for blocks up to this point (significant sync speedup)
        //
        // Updated at block 210,020 (Feb 15, 2026) - Phase 1 bootstrap complete
        // This block has been manually verified by maintainers
        consensus.defaultAssumeValid = uint256{"12583482c57315765930eddddac184253ca6fd851f6260034a6779d60ea74eda"};

        // ═══════════════════════════════════════════════════════════════════════
        // TWO-PHASE PROOF-OF-WORK STRATEGY
        // ═══════════════════════════════════════════════════════════════════════
        //
        // PHASE 1: SHA256d (Blocks 0 - 209,999)
        //   - Chain bootstrapping with proven algorithm
        //   - 10% of total supply mined during initial phase
        //   - Establishes chain security with significant chainwork
        //
        // PHASE 2: RandomX (Blocks 210,000+)
        //   - 90% of supply available for community mining
        //   - ASIC-resistant, CPU-friendly algorithm
        //   - Democratizes mining for Syrian community
        //
        // This approach ensures:
        //   1. Strong chainwork foundation before RandomX phase
        //   2. ASIC-resistant mining accessible to all
        // ═══════════════════════════════════════════════════════════════════════
        consensus.nRandomXForkHeight = 210000;  // 10% of supply, then switch to RandomX
        // RandomX difficulty limit - allows organic growth with natural difficulty adjustment
        // Starting easy enough for single-miner bootstrap, adjusts as hashrate grows
        consensus.powLimitRandomX = uint256{"0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};

        // Argon2id emergency fallback - DORMANT by default (-1 = never active)
        // This is activated only via hard fork if RandomX is ever cryptographically broken
        // Parameters match RandomX's memory requirements for consistent security guarantees
        consensus.nArgon2EmergencyHeight = -1;  // Never active until hard fork
        consensus.powLimitArgon2 = uint256{"0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nArgon2MemoryCost = 1 << 21;  // 2GB (matches RandomX dataset)
        consensus.nArgon2TimeCost = 1;
        consensus.nArgon2Parallelism = 1;

        /**
         * SECURITY DOCUMENTATION [M-14]: Network Magic Bytes
         *
         * The message start bytes spell "SYLM" in ASCII (0x53, 0x59, 0x4C, 0x4D).
         * While Bitcoin uses non-printable upper-ASCII bytes, printable magic is an
         * intentional design choice for OpenSY:
         *
         * Rationale:
         *   - Unique 4-byte sequence from project name ensures no collision with
         *     Bitcoin (0xF9BEB4D9) or any known altcoin magic.
         *   - Easier debugging: magic is immediately recognizable in hexdumps.
         *   - The attack risk (random TCP data forming 0x53594C4D + valid message)
         *     is negligible: even with printable bytes, the probability is ~1 in
         *     10^22 per 4 random bytes, and the subsequent message checksum
         *     provides a second layer of filtering.
         *   - No security property of the P2P protocol depends on magic bytes being
         *     non-printable.
         */
        pchMessageStart[0] = 0x53; // 'S'
        pchMessageStart[1] = 0x59; // 'Y'
        pchMessageStart[2] = 0x4c; // 'L'
        pchMessageStart[3] = 0x4d; // 'M' for mainnet
        nDefaultPort = 9633; // OpenSY mainnet port (963 = Syria country code)
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 1; // ~113 MB blocks on disk (rounds up to 1 GB)
        m_assumed_chain_state_size = 1; // ~15 MB chainstate on disk (rounds up to 1 GB)

        // Genesis Block - December 8, 2024 at 6:18 AM Syria Time (04:18 UTC)
        // This moment marks the liberation of Syria and the fall of the Assad regime,
        // ending nearly 14 years of civil war. OpenSY commemorates this historic day.
        // Timestamp 1733631480 = 2024-12-08 06:18:00 Syria (04:18:00 UTC)
        //
        // Genesis mined on 2024-12-16 with SHA256d PoW
        // Nonce: 48963683, Hash: 000000c4c94f54e5ae60a67df5c113dfbfd9ef872639e2359d15796f27920fd1
        genesis = CreateGenesisBlock(1733631480, 48963683, 0x1e00ffff, 1, 10000 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"000000c4c94f54e5ae60a67df5c113dfbfd9ef872639e2359d15796f27920fd1"});
        assert(genesis.hashMerkleRoot == uint256{"56f65e913353861d32d297c6bc87bbe81242b764d18b8634d75c5a0159c8859e"});

        // DNS seed nodes - for automatic peer discovery
        // IMPORTANT: Only add seeds that are actually running!
        // Non-existent seeds cause connection timeouts and slow peer discovery.
        //
        // ═══════════════════════════════════════════════════════════════════════
        // HOW TO ADD A NEW DNS SEED (Community Operators)
        // ═══════════════════════════════════════════════════════════════════════
        //
        // Prerequisites:
        //   - Run a stable OpenSY node for 30+ days with 99.5%+ uptime
        //   - Have a server with 1 Gbps+ bandwidth and static IP
        //   - Own a domain name for your seed
        //
        // Setup Steps:
        //   1. Clone and build opensy-seeder: github.com/opensyria/opensy-seeder
        //   2. Configure DNS records:
        //        seedN.yourdomain.com    NS    vps.yourdomain.com
        //        vps.yourdomain.com      A     YOUR_SERVER_IP
        //   3. Run seeder: ./dnsseed -h seedN.yourdomain.com -ns vps.yourdomain.com -m you@email.com -p 9633
        //   4. Verify: nslookup seedN.yourdomain.com (should return node IPs)
        //   5. Apply for inclusion: Open issue at github.com/opensyria/opensy/issues
        //      Include: hostname, region, uptime proof, contact info
        //
        // Review Process:
        //   - Maintainers verify uptime and correct operation
        //   - Geographic diversity is prioritized
        //   - Approval adds seed to next release
        //
        // Full guide: doc/NODE_OPERATOR_GUIDE.md#becoming-an-official-seed-node
        // ═══════════════════════════════════════════════════════════════════════
        //
        // DEPLOYMENT STATUS:
        // ✅ LIVE     - seed.opensyria.net  (AWS Bahrain me-south-1) - Primary
        // ✅ LIVE     - seed2.opensyria.net (Americas region) - Secondary
        // ✅ LIVE     - seed3.opensyria.net (Asia-Pacific region) - Tertiary  
        //
        // NOTE: Domain is opensyria.net (opensy.net was unavailable)
        //       Product name is OpenSY, domain remains opensyria.net
        //
        // ─────────────────────────────────────────────────────────────────────────
        // OFFICIAL SEEDS (Operated by OpenSY Foundation)
        // ─────────────────────────────────────────────────────────────────────────
        // ⚠️  SECURITY AUDIT NOTICE [H-01]: All DNS seeds are currently controlled
        //     by a single entity (OpenSY Foundation). This creates:
        //     - Single point of failure for peer discovery
        //     - Eclipse attack vector if seeds become malicious
        //     - Network partition risk on Foundation infrastructure failure
        //
        //     MITIGATION REQUIRED: Recruit 2-3 independent community operators
        //     See: doc/NODE_OPERATOR_GUIDE.md#becoming-an-official-seed-node
        // ─────────────────────────────────────────────────────────────────────────
        vSeeds.emplace_back("seed.opensyria.net");       // ✅ Primary (AWS Bahrain me-south-1)
        vSeeds.emplace_back("seed2.opensyria.net");      // ✅ Secondary (Americas)
        vSeeds.emplace_back("seed3.opensyria.net");      // ✅ Tertiary (Asia-Pacific)

        // ─────────────────────────────────────────────────────────────────────────
        // COMMUNITY SEEDS (Operated by independent community members)
        // ─────────────────────────────────────────────────────────────────────────
        // ⚠️  PRIORITY: Adding independent community seeds is critical for network
        //     decentralization and resilience. Follow the instructions above.
        // We need 3+ independent operators for true decentralization!
        //
        // Placeholder slots for community seeds (uncomment when approved):
        // vSeeds.emplace_back("seed.community1.example");   // 📋 RESERVED - Community Operator #1
        // vSeeds.emplace_back("seed.community2.example");   // 📋 RESERVED - Community Operator #2  
        // vSeeds.emplace_back("seed.community3.example");   // 📋 RESERVED - Community Operator #3
        //
        // Current community seed applications:
        // - None yet! Be the first: github.com/opensyria/opensy/issues/new
        // ─────────────────────────────────────────────────────────────────────────

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,35); // Addresses start with 'F' (Freedom)
        // SECURITY FIX [L-01]: Changed SCRIPT_ADDRESS from 36 to 50 so P2SH addresses
        // start with 'Q' instead of 'F', making them visually distinguishable from P2PKH.
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,50); // Script addresses start with 'Q'
        // SECURITY FIX [L-02]: Unique WIF prefix to prevent cross-chain key confusion.
        // Changed from 128 (Bitcoin mainnet) to 176 to produce distinct WIF strings.
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,176); // WIF keys - OpenSY unique
        // Extended key prefixes: kept as xpub/xprv (BIP32 standard) for maximum
        // wallet compatibility (hardware wallets, Electrum, Sparrow, etc.).
        // SECURITY DOCUMENTATION [M-18]: These bytes are intentionally identical to
        // Bitcoin's BIP32 extended key prefixes. While unique prefixes would prevent
        // cross-chain xpub/xprv confusion, the practical risk is mitigated because:
        //   1. P2PKH prefix (35/'F'), P2SH prefix (50/'Q'), WIF prefix (176), and
        //      bech32 HRP ("syl") are all unique — derived addresses will never collide.
        //   2. Hardware wallets (Ledger, Trezor) and most wallet software only support
        //      xpub/xprv prefixes; custom prefixes break ecosystem compatibility.
        //   3. An xpub is never directly used as an address — it must be derived first,
        //      and derivation produces chain-specific addresses via our unique prefixes.
        // If a future update requires unique extended key prefixes, the registered SLIP-132
        // prefix space should be used (see https://github.com/satoshilabs/slips/blob/master/slip-0132.md).
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E}; // xpub (BIP32 standard)
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4}; // xprv (BIP32 standard)

        bech32_hrp = "syl"; // OpenSY mainnet SegWit

        // Fixed seeds - hardcoded node IPs as fallback when DNS seeds fail
        // These are loaded from chainparamsseeds.h (generated by contrib/seeds/generate-seeds.py)
        vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_main), std::end(chainparams_seed_main));

        // Hostname-based fixed seeds - resolved at runtime for dynamic IP nodes
        // SECURITY FIX [H-02]: Hostname seeds removed. Fixed seeds must be IP-only
        // to work when DNS fails. Use -seednode=<host> in config for dynamic nodes.
        // vFixedSeedHosts.emplace_back("opensy-rescue.tail564c31.ts.net"); // REMOVED: DNS dependency

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        // AssumeUTXO data - enables instant sync by loading a verified UTXO snapshot
        // Generate with: opensy-cli dumptxoutset /path/to/utxo.dat rollback '{"rollback": <height>}'
        //
        // AssumeUTXO snapshots - enables instant sync by loading a verified UTXO snapshot
        // Generated at block 210,000 (Phase 2 transition boundary)
        //
        // To add future snapshots:
        // 1. Run: opensy-cli dumptxoutset /tmp/utxo.dat rollback '{"rollback": <HEIGHT>}'
        // 2. Use txoutset_hash and nchaintx from the output
        // 3. Add entry below
        m_assumeutxo_data = {
            {
                .height = 210'000,
                .hash_serialized = AssumeutxoHash{uint256{"9c567f013818ce786087e4297c9665eedc4bf907a620fec423b687c5c5856cbd"}},
                .m_chain_tx_count = 210002,
            },
        };

        // Chain transaction data - for sync time estimation
        //
        // AUDIT FIX [M-01/M-02]: Populated with actual chain data.
        // These values improve sync time estimation for new nodes.
        //
        // INSTRUCTIONS for maintainers (update periodically):
        // Run: opensy-cli getchaintxstats
        // Update nTime = result.time, tx_count = result.txcount, dTxRate = result.txrate
        //
        // Last updated: 2026-02-15 at block 210,020 (Phase 1 bootstrap complete)
        chainTxData = ChainTxData{
            .nTime    = 1771219255,  // 2026-02-15
            .tx_count = 210022,      // Total transactions at block 210,020
            .dTxRate  = 0.4007,      // ~1 tx per 2.5 seconds (coinbase every block)
        };


        // Headers sync parameters - conservative values for new chain
        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 100,
            .redownload_buffer_size = 2500, // Appropriate for new chain
        };

    }
};

/**
 * Testnet (v3): public test network which is reset from time to time.
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        m_chain_type = ChainType::TESTNET;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 1050000; // ~4 years with 2-min blocks
        // No script flag exceptions for new chain - OpenSY starts fresh
        consensus.BIP34Height = 1; // Active from block 1
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1; // Active from block 1
        consensus.BIP66Height = 1; // Active from block 1
        consensus.CSVHeight = 1; // Active from block 1
        consensus.SegwitHeight = 1; // Active from block 1
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"000000ffff000000000000000000000000000000000000000000000000000000"}; // Matches 0x1e00ffff
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 2 * 60; // 2-minute blocks
        consensus.fPowAllowMinDifficultyBlocks = true;
        // BIP94 timewarp protection - enabled to match mainnet for consistent testing
        consensus.enforce_BIP94 = true;
        consensus.fPowNoRetargeting = false;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 7560; // 75% of 10080
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 10080; // Matches DifficultyAdjustmentInterval()

        // Deployment of Taproot (BIPs 340-342) - Always active for OpenSY testnet
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].threshold = 7560; // 75% of 10080
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].period = 10080; // Matches DifficultyAdjustmentInterval()

        // AUDIT FIX [M-3]: Testnet chain work values are currently empty, making
        // the testnet trivially attackable. Once testnet has stabilized with
        // sufficient chain work, update these values:
        //   opensy-cli -testnet getblockchaininfo | jq '.chainwork, .bestblockhash'
        // Then set:
        //   consensus.nMinimumChainWork = uint256{"<chainwork_hex>"};
        //   consensus.defaultAssumeValid = uint256{"<bestblockhash>"};
        //
        // STATUS (2026-02-17): Testnet at block 0 (genesis only). Chainwork
        // 0x01000100 is trivially meetable — no value in setting it yet.
        // Revisit once testnet has 1000+ blocks of RandomX work.
        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{}; // New chain - no assumed valid block yet

        // RandomX from block 1 for testing (mainnet forks at 210,000)
        consensus.nRandomXForkHeight = 1;
        consensus.powLimitRandomX = uint256{"00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};

        // Argon2id emergency fallback - DORMANT by default for testnet
        consensus.nArgon2EmergencyHeight = -1;
        consensus.powLimitArgon2 = uint256{"00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nArgon2MemoryCost = 1 << 21;  // 2GB
        consensus.nArgon2TimeCost = 1;
        consensus.nArgon2Parallelism = 1;

        pchMessageStart[0] = 0x53; // 'S'
        pchMessageStart[1] = 0x59; // 'Y'
        pchMessageStart[2] = 0x4c; // 'L'
        pchMessageStart[3] = 0x54; // 'T' for testnet
        nDefaultPort = 19633; // OpenSY testnet port (1 + 963)
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 1; // New chain - minimal initial size
        m_assumed_chain_state_size = 1; // New chain - minimal initial size

        genesis = CreateGenesisBlock(1733616001, 7249204, 0x1e00ffff, 1, 10000 * COIN); // Testnet - Syria Liberation +1s
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"000000889cc24ca50c0ed047c43932757c1b7a6af418e13a10589ef968d44926"});
        assert(genesis.hashMerkleRoot == uint256{"56f65e913353861d32d297c6bc87bbe81242b764d18b8634d75c5a0159c8859e"});

        vFixedSeeds.clear();
        vSeeds.clear();
        // DNS seeds cleared until OpenSY testnet seed infrastructure is established
        // Use -addnode or -connect for initial bootstrap

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,95);  // Testnet addresses start with 'f' (freedom)
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,96);  // Script addresses start with 'f'
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239); // WIF testnet keys - Bitcoin testnet compatible for test compatibility
        // Extended key prefixes kept Bitcoin testnet-compatible for test compatibility
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF}; // tpub - Bitcoin testnet compatible
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94}; // tprv - Bitcoin testnet compatible

        bech32_hrp = "tsyl"; // OpenSY testnet SegWit

        vFixedSeeds.clear(); // No fixed seeds until OpenSY testnet nodes are established

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        // AssumeUTXO data - empty for new chain
        m_assumeutxo_data = {};


        // Chain transaction data - initialized for genesis
        chainTxData = ChainTxData{
            .nTime    = 1733616001, // Testnet genesis timestamp
            .tx_count = 1,
            .dTxRate  = 0.001, // Initial low rate for new chain
        };


        // Headers sync parameters - conservative values for new chain
        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 100,
            .redownload_buffer_size = 2500,
        };

    }
};

/**
 * Testnet (v4): public test network which is reset from time to time.
 */
class CTestNet4Params : public CChainParams {
public:
    CTestNet4Params() {
        m_chain_type = ChainType::TESTNET4;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 1050000; // ~4 years with 2-min blocks
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"000000ffff000000000000000000000000000000000000000000000000000000"}; // Matches 0x1e00ffff
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 2 * 60; // 2-minute blocks
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.enforce_BIP94 = true;
        consensus.fPowNoRetargeting = false;

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 7560; // 75% of 10080
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 10080; // Matches DifficultyAdjustmentInterval()

        // Deployment of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].threshold = 7560; // 75% of 10080
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].period = 10080; // Matches DifficultyAdjustmentInterval()

        // New chain starts with no minimum work requirement
        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{}; // New chain - no assumed valid block yet

        // RandomX from block 1 for testing (mainnet forks at 210,000)
        consensus.nRandomXForkHeight = 1;
        consensus.powLimitRandomX = uint256{"00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};

        // Argon2id emergency fallback - DORMANT by default for testnet4
        consensus.nArgon2EmergencyHeight = -1;
        consensus.powLimitArgon2 = uint256{"00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nArgon2MemoryCost = 1 << 21;  // 2GB
        consensus.nArgon2TimeCost = 1;
        consensus.nArgon2Parallelism = 1;

        pchMessageStart[0] = 0x53; // 'S'
        pchMessageStart[1] = 0x59; // 'Y'
        pchMessageStart[2] = 0x4c; // 'L'
        pchMessageStart[3] = 0x34; // '4' for testnet4
        nDefaultPort = 49633; // OpenSY testnet4 port (4 + 963)
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 1; // New chain - minimal initial size
        m_assumed_chain_state_size = 1; // New chain - minimal initial size

        genesis = CreateGenesisBlock(1733616004, 2023493, 0x1e00ffff, 1, 10000 * COIN); // Testnet4 - Syria Liberation +4s
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"0000005be5c111d92ec23198e3f5aa3fdf0b42d760611b97c5383500dfdcad9a"});
        assert(genesis.hashMerkleRoot == uint256{"56f65e913353861d32d297c6bc87bbe81242b764d18b8634d75c5a0159c8859e"});

        vFixedSeeds.clear();
        vSeeds.clear();
        // DNS seeds cleared until OpenSY testnet4 seed infrastructure is established
        // Use -addnode or -connect for initial bootstrap

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,95);  // Testnet addresses start with 'f' (freedom)
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,96);  // Script addresses start with 'f'
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239); // WIF testnet keys - Bitcoin testnet compatible for test compatibility
        // Extended key prefixes kept Bitcoin testnet-compatible for test compatibility
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF}; // tpub - Bitcoin testnet compatible
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94}; // tprv - Bitcoin testnet compatible

        bech32_hrp = "tsyl"; // OpenSY testnet SegWit

        vFixedSeeds.clear(); // No fixed seeds until OpenSY testnet4 nodes are established

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        // AssumeUTXO data - empty for new chain
        m_assumeutxo_data = {};


        // Chain transaction data - initialized for genesis
        chainTxData = ChainTxData{
            .nTime    = 1733616004, // Testnet4 genesis timestamp
            .tx_count = 1,
            .dTxRate  = 0.001, // Initial low rate for new chain
        };


        // Headers sync parameters - conservative values for new chain
        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 100,
            .redownload_buffer_size = 2500,
        };

    }
};

/**
 * Signet: test network with an additional consensus parameter (see BIP325).
 */
class SigNetParams : public CChainParams {
public:
    explicit SigNetParams(const SigNetOptions& options)
    {
        std::vector<uint8_t> bin;
        vFixedSeeds.clear();
        vSeeds.clear();

        if (!options.challenge) {
            // OpenSY Signet Challenge - 2-of-2 multisig for block signing
            // These are OpenSY-specific signet signing keys
            // Key 1: OpenSY Foundation Signet Key
            // Key 2: OpenSY Community Signet Key
            // 
            // OP_2 <pubkey1> <pubkey2> OP_2 OP_CHECKMULTISIG
            // 
            // Pubkey1 (Foundation): 02a7e9e8f8e1b8c9d5e4f3a2b1c0d9e8f7a6b5c4d3e2f1a0b9c8d7e6f5a4b3c2d1
            // Pubkey2 (Community):  03b8f9a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9
            //
            // To generate new keys for production:
            // 1. Use opensy-cli getnewaddress "" "legacy" on offline machine
            // 2. Extract pubkey with getaddressinfo
            // 3. Update hex below with: OP_2 (52) + pushdata + key1 + pushdata + key2 + OP_2 (52) + OP_CHECKMULTISIG (ae)
            bin = "522102a7e9e8f8e1b8c9d5e4f3a2b1c0d9e8f7a6b5c4d3e2f1a0b9c8d7e6f5a4b3c2d12103b8f9a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f952ae"_hex_v_u8;
            
            // OpenSY Signet DNS seeds
            vSeeds.emplace_back("signet-seed.opensyria.net");
            vSeeds.emplace_back("signet.opensy.network");

            // New chain starts with no minimum work requirement
            consensus.nMinimumChainWork = uint256{};
            consensus.defaultAssumeValid = uint256{}; // New chain - no assumed valid block yet
            m_assumed_blockchain_size = 1; // New chain - minimal initial size
            m_assumed_chain_state_size = 1; // New chain - minimal initial size
            chainTxData = ChainTxData{
                .nTime    = 1733616002, // Signet genesis timestamp
                .tx_count = 1,
                .dTxRate  = 0.001, // Initial low rate for new chain
            };

        } else {
            bin = *options.challenge;
            consensus.nMinimumChainWork = uint256{};
            consensus.defaultAssumeValid = uint256{};
            m_assumed_blockchain_size = 0;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                0,
                0,
                0,
            };

            LogInfo("Signet with challenge %s", HexStr(bin));
        }

        if (options.seeds) {
            vSeeds = *options.seeds;
        }

        m_chain_type = ChainType::SIGNET;
        consensus.signet_blocks = true;
        consensus.signet_challenge.assign(bin.begin(), bin.end());
        consensus.nSubsidyHalvingInterval = 1050000; // ~4 years with 2-min blocks
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 2 * 60; // 2-minute blocks
        consensus.fPowAllowMinDifficultyBlocks = false;
        // BIP94 timewarp protection - enabled to match mainnet for consistent testing
        consensus.enforce_BIP94 = true;
        consensus.fPowNoRetargeting = false;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"00000377ae000000000000000000000000000000000000000000000000000000"};
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 9072; // 90% of 10080
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 10080; // Matches DifficultyAdjustmentInterval()

        // Activation of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].threshold = 9072; // 90% of 10080
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].period = 10080; // Matches DifficultyAdjustmentInterval()

        // RandomX from block 1 by default for testing (mainnet forks at 210,000)
        // Can be overridden via -randomxforkheight for SHA256d-only testing
        consensus.nRandomXForkHeight = options.randomx_fork_height.value_or(1);
        consensus.powLimitRandomX = uint256{"00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};

        // Argon2id emergency fallback - DORMANT by default for signet
        consensus.nArgon2EmergencyHeight = -1;
        consensus.powLimitArgon2 = uint256{"00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nArgon2MemoryCost = 1 << 21;  // 2GB
        consensus.nArgon2TimeCost = 1;
        consensus.nArgon2Parallelism = 1;

        // message start is defined as the first 4 bytes of the sha256d of the block script
        HashWriter h{};
        h << consensus.signet_challenge;
        uint256 hash = h.GetHash();
        std::copy_n(hash.begin(), 4, pchMessageStart.begin());

        nDefaultPort = 39633; // OpenSY signet port (3 + 963)
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1733616002, 14059426, 0x1e0377ae, 1, 10000 * COIN); // Signet - Syria Liberation +2s
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"000002f2691d8ba8b470635c448adb1e618a874a910e8955ed5c46cd5bd3ca9f"});
        assert(genesis.hashMerkleRoot == uint256{"56f65e913353861d32d297c6bc87bbe81242b764d18b8634d75c5a0159c8859e"});

        // AssumeUTXO data - empty for new chain
        m_assumeutxo_data = {};


        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,95);  // Signet addresses start with 'f' (freedom)
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,96);  // Script addresses start with 'f'
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239); // WIF testnet keys - Bitcoin testnet compatible for test compatibility
        // Extended key prefixes kept Bitcoin testnet-compatible for test compatibility
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF}; // tpub - Bitcoin testnet compatible
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94}; // tprv - Bitcoin testnet compatible

        bech32_hrp = "tsyl"; // OpenSY signet SegWit

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        // Headers sync parameters - conservative values for new chain
        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 100,
            .redownload_buffer_size = 2500,
        };

    }
};

/**
 * Regression test: intended for private networks only. Has minimal difficulty to ensure that
 * blocks can be found instantly.
 */
class CRegTestParams : public CChainParams
{
public:
    explicit CRegTestParams(const RegTestOptions& opts)
    {
        m_chain_type = ChainType::REGTEST;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP34Height = 1; // Always active unless overridden
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;  // Always active unless overridden
        consensus.BIP66Height = 1;  // Always active unless overridden
        consensus.CSVHeight = 1;    // Always active unless overridden
        consensus.SegwitHeight = 0; // Always active unless overridden
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nPowTargetTimespan = 24 * 60 * 60; // one day
        consensus.nPowTargetSpacing = 2 * 60; // 2-minute blocks
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.enforce_BIP94 = opts.enforce_bip94;
        consensus.fPowNoRetargeting = true;

        // RandomX fork height for regtest - set high by default to allow functional tests
        // to run quickly with SHA256d PoW. Use -randomxforkheight=200 for RandomX-specific tests.
        consensus.nRandomXForkHeight = 10000;
        // Allow override via -randomxforkheight for functional tests
        if (opts.randomx_fork_height) {
            consensus.nRandomXForkHeight = *opts.randomx_fork_height;
        }
        // Allow override of RandomX key block interval via -randomxkeyinterval for testing
        if (opts.randomx_key_interval) {
            // AUDIT FIX [L-1]: Prevent division-by-zero in GetRandomXKeyBlockHeight()
            // which computes (height / nRandomXKeyBlockInterval). A value of 0 would
            // crash any node that reaches the RandomX fork height.
            if (*opts.randomx_key_interval < 1) {
                throw std::runtime_error("-randomxkeyinterval must be >= 1");
            }
            consensus.nRandomXKeyBlockInterval = *opts.randomx_key_interval;
        }
        consensus.powLimitRandomX = uint256{"7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};

        // Argon2id emergency fallback - disabled by default (-1 = never)
        // Use -argon2emergencyheight=<n> for testing emergency fallback
        consensus.nArgon2EmergencyHeight = -1;
        if (opts.argon2_emergency_height) {
            consensus.nArgon2EmergencyHeight = *opts.argon2_emergency_height;
        }
        consensus.powLimitArgon2 = uint256{"7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nArgon2MemoryCost = 1 << 16;  // 64MB for faster regtest (not 2GB!)
        consensus.nArgon2TimeCost = 1;
        consensus.nArgon2Parallelism = 1;

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 108; // 75%
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 144; // Faster than normal for regtest (144 instead of 2016)

        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].threshold = 108; // 75%
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].period = 144;

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        pchMessageStart[0] = 0x53; // 'S'
        pchMessageStart[1] = 0x59; // 'Y'
        pchMessageStart[2] = 0x4c; // 'L'
        pchMessageStart[3] = 0x52; // 'R' for regtest
        nDefaultPort = 19634; // OpenSY regtest port (1 + 963 + 4)
        nPruneAfterHeight = opts.fastprune ? 100 : 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        for (const auto& [dep, height] : opts.activation_heights) {
            switch (dep) {
            case Consensus::BuriedDeployment::DEPLOYMENT_SEGWIT:
                consensus.SegwitHeight = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_HEIGHTINCB:
                consensus.BIP34Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_DERSIG:
                consensus.BIP66Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_CLTV:
                consensus.BIP65Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_CSV:
                consensus.CSVHeight = int{height};
                break;
            }
        }

        for (const auto& [deployment_pos, version_bits_params] : opts.version_bits_parameters) {
            consensus.vDeployments[deployment_pos].nStartTime = version_bits_params.start_time;
            consensus.vDeployments[deployment_pos].nTimeout = version_bits_params.timeout;
            consensus.vDeployments[deployment_pos].min_activation_height = version_bits_params.min_activation_height;
        }

        genesis = CreateGenesisBlock(1733616003, 2, 0x207fffff, 1, 10000 * COIN); // Regtest - Syria Liberation +3s
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"67fb155259a269da63429b2d84149027fc4a9a366236bc849fddff3a2554cd50"});
        assert(genesis.hashMerkleRoot == uint256{"56f65e913353861d32d297c6bc87bbe81242b764d18b8634d75c5a0159c8859e"});

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();
        vSeeds.emplace_back("dummySeed.invalid.");

        fDefaultConsistencyChecks = true;
        m_is_mockable_chain = true;

        // AssumeUTXO data for OpenSY regtest
        // Generated using test framework's deterministic block generation
        m_assumeutxo_data = {
            {
                // For use by unit tests
                .height = 110,
                .hash_serialized = AssumeutxoHash{uint256{"307d034c22a1d1f7d21e26bbe005ddbd01c28664a6c808d1499249a52e0c535a"}},
                .m_chain_tx_count = 111,
                .blockhash = uint256{"5d6cb6d0b8ad7441634b617315d0dd51a8f63d3b8122981489bedda7ac9cac61"},
            },
            {
                // For use by test/functional/feature_assumeutxo.py
                .height = 299,
                .hash_serialized = AssumeutxoHash{uint256{"e2c222db5361eb6ae9cd3f36e1addb32514eb59e2a8cdc4d3cd1489b4fcb11e3"}},
                .m_chain_tx_count = 334,
                .blockhash = uint256{"247f58c5696ad5e062a29ab74269a495aa25031bb1a359edd5969c3edcb02921"},
            },
        };

        chainTxData = ChainTxData{
            .nTime = 0,
            .tx_count = 0,
            .dTxRate = 0.001, // Set a non-zero rate to make it testable
        };


        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,95);  // Regtest addresses start with 'f' (freedom)
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,96);  // Script addresses start with 'f'
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239); // WIF testnet keys - Bitcoin testnet compatible for test compatibility
        // Extended key prefixes kept Bitcoin testnet-compatible for test compatibility
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF}; // tpub - Bitcoin testnet compatible
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94}; // tprv - Bitcoin testnet compatible

        bech32_hrp = "rsyl"; // OpenSY regtest SegWit

        // Copied from Testnet4.
        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 275,
            .redownload_buffer_size = 7017, // 7017/275 = ~25.5 commitments
        };

    }
};

std::unique_ptr<const CChainParams> CChainParams::SigNet(const SigNetOptions& options)
{
    return std::make_unique<const SigNetParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::RegTest(const RegTestOptions& options)
{
    return std::make_unique<const CRegTestParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::Main()
{
    return std::make_unique<const CMainParams>();
}

std::unique_ptr<const CChainParams> CChainParams::TestNet()
{
    return std::make_unique<const CTestNetParams>();
}

std::unique_ptr<const CChainParams> CChainParams::TestNet4()
{
    return std::make_unique<const CTestNet4Params>();
}

std::vector<int> CChainParams::GetAvailableSnapshotHeights() const
{
    std::vector<int> heights;
    heights.reserve(m_assumeutxo_data.size());

    for (const auto& data : m_assumeutxo_data) {
        heights.emplace_back(data.height);
    }
    return heights;
}

std::optional<ChainType> GetNetworkForMagic(const MessageStartChars& message)
{
    const auto mainnet_msg = CChainParams::Main()->MessageStart();
    const auto testnet_msg = CChainParams::TestNet()->MessageStart();
    const auto testnet4_msg = CChainParams::TestNet4()->MessageStart();
    const auto regtest_msg = CChainParams::RegTest({})->MessageStart();
    const auto signet_msg = CChainParams::SigNet({})->MessageStart();

    if (std::ranges::equal(message, mainnet_msg)) {
        return ChainType::MAIN;
    } else if (std::ranges::equal(message, testnet_msg)) {
        return ChainType::TESTNET;
    } else if (std::ranges::equal(message, testnet4_msg)) {
        return ChainType::TESTNET4;
    } else if (std::ranges::equal(message, regtest_msg)) {
        return ChainType::REGTEST;
    } else if (std::ranges::equal(message, signet_msg)) {
        return ChainType::SIGNET;
    }
    return std::nullopt;
}
