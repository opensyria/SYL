// Copyright (c) 2025 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <script/script.h>
#include <script/src20.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <uint256.h>
#include <util/chaintype.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

void initialize_src20_fuzz()
{
    SelectParams(ChainType::REGTEST);
}

// Main fuzz target for SRC-20 script parsing
FUZZ_TARGET(src20_parse, .init = initialize_src20_fuzz)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());

    // Build a potentially valid SRC-20 script from fuzzed input
    CScript script;
    
    // Always start with OP_RETURN
    script << OP_RETURN;
    
    // Sometimes use valid protocol ID, sometimes garbage
    if (fuzzed_data.ConsumeBool()) {
        // Valid protocol ID
        std::vector<uint8_t> proto_id(src20::SRC20_PROTOCOL_ID.begin(), src20::SRC20_PROTOCOL_ID.end());
        script << proto_id;
    } else {
        // Random bytes as protocol ID
        const size_t id_len = fuzzed_data.ConsumeIntegralInRange<size_t>(0, 10);
        script << fuzzed_data.ConsumeBytes<uint8_t>(id_len);
    }
    
    // Add version and action
    script << std::vector<uint8_t>{fuzzed_data.ConsumeIntegral<uint8_t>()};  // version
    script << std::vector<uint8_t>{fuzzed_data.ConsumeIntegral<uint8_t>()};  // action
    
    // Add remaining random payload
    const size_t payload_len = fuzzed_data.ConsumeIntegralInRange<size_t>(0, 256);
    script << fuzzed_data.ConsumeBytes<uint8_t>(payload_len);
    
    // Test parsing - should not crash
    (void)src20::IsSRC20Script(script);
    (void)src20::ParseSRC20Script(script);
    (void)src20::GetOpReturnData(script);
}

// Fuzz target for raw script parsing (completely random input)
FUZZ_TARGET(src20_raw, .init = initialize_src20_fuzz)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());
    
    // Generate raw script bytes
    const std::vector<uint8_t> raw = fuzzed_data.ConsumeRemainingBytes<uint8_t>();
    CScript script(raw.begin(), raw.end());
    
    // Test all parsing functions - should not crash
    (void)src20::IsSRC20Script(script);
    (void)src20::ParseSRC20Script(script);
    (void)src20::GetOpReturnData(script);
}

// Fuzz target for issuance script building and parsing roundtrip
FUZZ_TARGET(src20_issuance_roundtrip, .init = initialize_src20_fuzz)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());
    
    // Generate valid-ish issuance data
    src20::TokenIssuance issuance;
    
    // Ticker: 1-4 uppercase alphanumeric
    const size_t ticker_len = fuzzed_data.ConsumeIntegralInRange<size_t>(1, src20::MAX_TICKER_LENGTH);
    std::string ticker;
    for (size_t i = 0; i < ticker_len; ++i) {
        char c = fuzzed_data.ConsumeIntegralInRange<char>('A', 'Z');
        ticker += c;
    }
    issuance.ticker = ticker;
    
    // Name: 1-32 printable ASCII
    const size_t name_len = fuzzed_data.ConsumeIntegralInRange<size_t>(1, src20::MAX_NAME_LENGTH);
    std::string name;
    for (size_t i = 0; i < name_len; ++i) {
        char c = fuzzed_data.ConsumeIntegralInRange<char>('A', 'z');
        name += c;
    }
    issuance.name = name;
    
    // Decimals: 0-18
    issuance.decimals = fuzzed_data.ConsumeIntegralInRange<uint8_t>(0, src20::MAX_DECIMALS);
    
    // Supply: non-zero
    issuance.total_supply = fuzzed_data.ConsumeIntegralInRange<uint64_t>(1, UINT64_MAX);
    
    // Metadata hash
    const std::optional<uint256> metadata = ConsumeDeserializable<uint256>(fuzzed_data);
    if (metadata) {
        issuance.metadata_hash = *metadata;
    }
    
    // Build and parse roundtrip
    CScript script = src20::BuildIssuanceScript(issuance);
    
    // Parse it back - should succeed
    auto parsed = src20::ParseSRC20Script(script);
    
    // Verify roundtrip if parsing succeeded
    if (parsed && parsed->GetIssuance()) {
        const auto* issuance_out = parsed->GetIssuance();
        assert(issuance_out->ticker == issuance.ticker);
        assert(issuance_out->decimals == issuance.decimals);
        assert(issuance_out->total_supply == issuance.total_supply);
    }
}

// Fuzz target for transfer script building and parsing roundtrip
FUZZ_TARGET(src20_transfer_roundtrip, .init = initialize_src20_fuzz)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());
    
    // Generate valid transfer data
    src20::TokenTransfer transfer;
    
    // Token ID from fuzzed hash
    const std::optional<uint256> token_hash = ConsumeDeserializable<uint256>(fuzzed_data);
    if (!token_hash) return;
    transfer.token_id = src20::TokenId(*token_hash);
    
    // Amount: non-zero
    transfer.amount = fuzzed_data.ConsumeIntegralInRange<uint64_t>(1, UINT64_MAX);
    
    // Build and parse roundtrip
    CScript script = src20::BuildTransferScript(transfer);
    
    // Parse it back
    auto parsed = src20::ParseSRC20Script(script);
    
    // Verify roundtrip if parsing succeeded
    if (parsed && parsed->GetTransfer()) {
        const auto* transfer_out = parsed->GetTransfer();
        assert(transfer_out->token_id == transfer.token_id);
        assert(transfer_out->amount == transfer.amount);
    }
}

// Fuzz target for burn script building and parsing roundtrip
FUZZ_TARGET(src20_burn_roundtrip, .init = initialize_src20_fuzz)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());
    
    // Generate valid burn data
    src20::TokenBurn burn;
    
    // Token ID from fuzzed hash
    const std::optional<uint256> token_hash = ConsumeDeserializable<uint256>(fuzzed_data);
    if (!token_hash) return;
    burn.token_id = src20::TokenId(*token_hash);
    
    // Amount: non-zero
    burn.amount = fuzzed_data.ConsumeIntegralInRange<uint64_t>(1, UINT64_MAX);
    
    // Build and parse roundtrip
    CScript script = src20::BuildBurnScript(burn);
    
    // Parse it back
    auto parsed = src20::ParseSRC20Script(script);
    
    // Verify roundtrip if parsing succeeded
    if (parsed && parsed->GetBurn()) {
        const auto* burn_out = parsed->GetBurn();
        assert(burn_out->token_id == burn.token_id);
        assert(burn_out->amount == burn.amount);
    }
}

// Fuzz target for malformed script rejection
FUZZ_TARGET(src20_malformed, .init = initialize_src20_fuzz)
{
    FuzzedDataProvider fuzzed_data(buffer.data(), buffer.size());
    
    CScript script;
    
    // Generate various malformed scripts
    const int pattern = fuzzed_data.ConsumeIntegralInRange<int>(0, 6);
    
    switch (pattern) {
        case 0:
            // No OP_RETURN
            script << OP_DUP << OP_HASH160;
            break;
        case 1:
            // OP_RETURN but no data
            script << OP_RETURN;
            break;
        case 2:
            // OP_RETURN with too little data
            script << OP_RETURN << std::vector<uint8_t>{'S', 'R'};
            break;
        case 3:
            // Valid prefix but truncated
            script << OP_RETURN << std::vector<uint8_t>{'S', 'R', 'C', '2', '0'};
            break;
        case 4:
            // Valid prefix and version but no action
            script << OP_RETURN 
                   << std::vector<uint8_t>{'S', 'R', 'C', '2', '0'} 
                   << std::vector<uint8_t>{src20::SRC20_VERSION};
            break;
        case 5:
            // Invalid version
            script << OP_RETURN 
                   << std::vector<uint8_t>{'S', 'R', 'C', '2', '0'} 
                   << std::vector<uint8_t>{0xFF};
            break;
        case 6:
            // Invalid action
            script << OP_RETURN 
                   << std::vector<uint8_t>{'S', 'R', 'C', '2', '0'} 
                   << std::vector<uint8_t>{src20::SRC20_VERSION}
                   << std::vector<uint8_t>{0xFF};  // Invalid action
            break;
    }
    
    // Add some random extra bytes
    const size_t extra_len = fuzzed_data.ConsumeIntegralInRange<size_t>(0, 100);
    if (extra_len > 0) {
        script << fuzzed_data.ConsumeBytes<uint8_t>(extra_len);
    }
    
    // These should not crash and should gracefully handle malformed input
    (void)src20::IsSRC20Script(script);
    (void)src20::ParseSRC20Script(script);
    (void)src20::GetOpReturnData(script);
}
