#ifndef OPENSY_CHAINPARAMSSEEDS_H
#define OPENSY_CHAINPARAMSSEEDS_H
/**
 * List of fixed seed nodes for the OpenSY network
 * Generated: December 8, 2025 - Network Launch Day! 🇸🇾
 *
 * IMPORTANT: Only include IPs of nodes that are actually running!
 * Non-existent IPs cause connection timeouts and waste resources.
 *
 * Each entry is BIP155 serialized: networkID (1 byte) + COMPACTSIZE(addr_len) + addr + port (2 bytes BE)
 * For IPv4: 0x01 + 0x04 + 4 bytes IP + 2 bytes port
 * For IPv6: 0x02 + 0x10 + 16 bytes IP + 2 bytes port
 *
 * To add new seeds:
 * 1. Ensure node has 99%+ uptime and port 9633 open
 * 2. Convert IP to hex bytes (e.g., 157.175.40.131 -> 0x9d, 0xaf, 0x28, 0x83)
 * 3. Port 9633 in big-endian is 0x25, 0xa1
 * 4. Update chainparams_seed_main_size count
 */

// Mainnet seeds - Only include actually running nodes!
static const uint8_t chainparams_seed_main[] = {
    // node1.opensyria.net (AWS Bahrain me-south-1) - 15.184.134.119:9633 [Elastic IP]
    0x01,                         // BIP155 network ID for IPv4
    0x04,                         // COMPACTSIZE: address length = 4 bytes
    0x0f, 0xb8, 0x86, 0x77,       // 15.184.134.119
    0x25, 0xa1,                   // Port 9633 (big-endian: 0x25a1)

    // node2.opensyria.net (AWS US us-east-1) - 13.219.175.17:9633 [Elastic IP]
    0x01,                         // BIP155 network ID for IPv4
    0x04,                         // COMPACTSIZE: address length = 4 bytes
    0x0d, 0xdb, 0xaf, 0x11,       // 13.219.175.17
    0x25, 0xa1,                   // Port 9633

    // node3.opensyria.net (AWS Tokyo ap-northeast-1) - 54.178.171.252:9633 [Elastic IP]
    0x01,                         // BIP155 network ID for IPv4
    0x04,                         // COMPACTSIZE: address length = 4 bytes
    0x36, 0xb2, 0xab, 0xfc,       // 54.178.171.252
    0x25, 0xa1,                   // Port 9633

    // node4.opensyria.net (Oracle Cloud Riyadh me-riyadh-1) - 84.8.111.37:9633
    0x01,                         // BIP155 network ID for IPv4
    0x04,                         // COMPACTSIZE: address length = 4 bytes
    0x54, 0x08, 0x6f, 0x25,       // 84.8.111.37
    0x25, 0xa1                    // Port 9633

    // ==========================================================================
    // ⚠️  DECENTRALIZATION NOTICE [H-01/H-02]
    // ==========================================================================
    // All seeds above are operated by OpenSY Foundation. This creates:
    //   - Single point of failure if Foundation infrastructure goes down
    //   - Potential eclipse attack vector if seeds become compromised
    //
    // PRIORITY: Recruit 2-3 independent community seed operators!
    // Apply at: https://github.com/opensyria/OpenSY/issues/new?template=seed_application.yml
    //
    // Reserved slots for community fixed seeds (uncomment when approved):
    // ,  // comma needed before first community seed!
    //
    // // community-node1.example.com (Provider, Region) - IP:9633
    // 0x01, 0x04, 0xXX, 0xXX, 0xXX, 0xXX, 0x25, 0xa1
    //
    // // community-node2.example.com (Provider, Region) - IP:9633
    // 0x01, 0x04, 0xXX, 0xXX, 0xXX, 0xXX, 0x25, 0xa1
    // ==========================================================================

    // ==========================================================================
    // IPv6 SEEDS - TODO: Add when infrastructure supports dual-stack
    // ==========================================================================
    // Example format for IPv6 node:
    //
    // node1-ipv6.opensyria.net - [2001:db8::1]:9633
    // 0x02,                                                     // BIP155 network ID for IPv6
    // 0x10,                                                     // COMPACTSIZE: address length = 16 bytes
    // 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,          // First 8 bytes of IPv6
    // 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,          // Last 8 bytes of IPv6
    // 0x25, 0xa1                                                // Port 9633
    //
    // Conversion: Expand IPv6 to full 16 bytes, each pair becomes 0xNN
    // Issue tracking: https://github.com/opensyria/SYL/issues/IPv6-seeds
};
constexpr size_t chainparams_seed_main_size = 4;  // 4 IPv4 seeds (update when adding IPv6)

// Signet seeds - empty until OpenSY signet nodes are established
static const uint8_t chainparams_seed_signet[] = {0x00};
constexpr size_t chainparams_seed_signet_size = 0;

// Testnet seeds - empty until OpenSY testnet nodes are established
static const uint8_t chainparams_seed_test[] = {0x00};
constexpr size_t chainparams_seed_test_size = 0;

// Testnet4 seeds - empty until OpenSY testnet4 nodes are established
static const uint8_t chainparams_seed_testnet4[] = {0x00};
constexpr size_t chainparams_seed_testnet4_size = 0;

#endif // OPENSY_CHAINPARAMSSEEDS_H
