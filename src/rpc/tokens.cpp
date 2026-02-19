// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/tokens.h>

#include <core_io.h>
#include <key_io.h>
#include <node/context.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <script/src20.h>
#include <tokens/tokendb.h>
#include <tokens/tokenvalidation.h>
#include <univalue.h>
#include <util/time.h>
#include <validation.h>
#include <wallet/wallet.h>

#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>

using node::NodeContext;

namespace {

// RPC pagination limits
static constexpr size_t RPC_DEFAULT_COUNT = 100;
static constexpr size_t RPC_MAX_COUNT = 1000;
static constexpr size_t RPC_MIN_COUNT = 1;
static constexpr int RPC_MAX_START_HEIGHT = 2100000000; // ~65 years at 1 block/sec

/**
 * RPC Rate Limiter
 * 
 * Limits expensive RPC calls to prevent DoS attacks.
 * Uses a token bucket algorithm with configurable limits per endpoint.
 */
class RPCRateLimiter {
public:
    // Rate limit configuration
    static constexpr int64_t DEFAULT_WINDOW_SECONDS = 60;       // 1 minute window
    static constexpr size_t DEFAULT_MAX_CALLS = 30;             // 30 calls per minute
    static constexpr size_t HEAVY_MAX_CALLS = 10;               // 10 calls per minute for heavy endpoints
    static constexpr size_t MAX_TRACKED_IPS = 10000;            // Limit memory usage
    
private:
    mutable std::mutex m_mutex;
    
    // Maps: (endpoint, peer_id) -> list of timestamps
    std::map<std::pair<std::string, std::string>, std::deque<int64_t>> m_call_times;
    
    // Clean old entries periodically
    int64_t m_last_cleanup{0};
    // AUDIT FIX [L-03]: Reduced from 300s to 60s for tighter memory control
    // under sustained unique IP traffic
    static constexpr int64_t CLEANUP_INTERVAL_SECONDS = 60;  // 1 minute
    
    void CleanupOldEntries(int64_t now) {
        if (now - m_last_cleanup < CLEANUP_INTERVAL_SECONDS) {
            return;
        }
        m_last_cleanup = now;
        
        // Remove entries older than the window
        for (auto it = m_call_times.begin(); it != m_call_times.end(); ) {
            auto& times = it->second;
            while (!times.empty() && times.front() < now - DEFAULT_WINDOW_SECONDS) {
                times.pop_front();
            }
            if (times.empty()) {
                it = m_call_times.erase(it);
            } else {
                ++it;
            }
        }
        
        // If still too many entries, remove oldest
        while (m_call_times.size() > MAX_TRACKED_IPS) {
            m_call_times.erase(m_call_times.begin());
        }
    }
    
public:
    /**
     * Check if a call should be rate limited
     * @param endpoint The RPC endpoint name
     * @param peer_id Peer identifier (IP or session)
     * @param max_calls Maximum calls allowed in window
     * @return true if should be allowed, false if rate limited
     */
    bool CheckAndRecord(const std::string& endpoint, const std::string& peer_id, 
                        size_t max_calls = DEFAULT_MAX_CALLS) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        int64_t now = GetTime();
        CleanupOldEntries(now);
        
        auto key = std::make_pair(endpoint, peer_id);
        auto& times = m_call_times[key];
        
        // Remove old entries outside the window
        while (!times.empty() && times.front() < now - DEFAULT_WINDOW_SECONDS) {
            times.pop_front();
        }
        
        // Check if over limit
        if (times.size() >= max_calls) {
            return false;  // Rate limited
        }
        
        // Record this call
        times.push_back(now);
        return true;
    }
    
    /**
     * Get remaining calls for an endpoint/peer
     */
    size_t GetRemainingCalls(const std::string& endpoint, const std::string& peer_id,
                             size_t max_calls = DEFAULT_MAX_CALLS) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        int64_t now = GetTime();
        auto key = std::make_pair(endpoint, peer_id);
        auto it = m_call_times.find(key);
        
        if (it == m_call_times.end()) {
            return max_calls;
        }
        
        // Count calls in window
        size_t count = 0;
        for (auto time : it->second) {
            if (time >= now - DEFAULT_WINDOW_SECONDS) {
                ++count;
            }
        }
        
        return count < max_calls ? max_calls - count : 0;
    }
};

// Global rate limiter instance - use a pointer that is intentionally never deleted
// to avoid static destruction order issues (SIGABRT on shutdown).
// This small memory "leak" is intentional and acceptable for a singleton that
// lives for the entire program lifetime.
static RPCRateLimiter* g_token_rpc_rate_limiter = nullptr;

static RPCRateLimiter& GetRateLimiter() {
    // Thread-safe initialization using call_once
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
        g_token_rpc_rate_limiter = new RPCRateLimiter();
    });
    return *g_token_rpc_rate_limiter;
}

/**
 * Check rate limit and throw if exceeded
 * @param request The RPC request
 * @param endpoint Endpoint name
 * @param max_calls Max calls per minute
 */
void CheckRPCRateLimit(const JSONRPCRequest& request, const std::string& endpoint,
                       size_t max_calls = RPCRateLimiter::DEFAULT_MAX_CALLS) {
    // AUDIT FIX: Use better peer identification for rate limiting
    // Previously all unauthenticated clients shared one "anonymous" bucket,
    // allowing one attacker to exhaust rate limits for everyone.
    std::string peer_id;
    if (!request.authUser.empty()) {
        // Authenticated user - use username
        peer_id = request.authUser;
    } else if (!request.peerAddr.empty()) {
        // Unauthenticated - use peer IP address for tracking
        peer_id = request.peerAddr;
    } else {
        // Fallback for local/internal calls - use generic ID
        // (local calls are trusted so rate limiting is less critical)
        peer_id = "local";
    }
    
    if (!GetRateLimiter().CheckAndRecord(endpoint, peer_id, max_calls)) {
        throw JSONRPCError(RPC_MISC_ERROR, 
            strprintf("Rate limit exceeded for %s. Please wait before making more requests.", endpoint));
    }
}

/**
 * Validate and clamp count parameter for RPC pagination.
 * Prevents DoS via excessively large result sets.
 * @param param   UniValue containing count (can be null)
 * @param default_val Default count if param is null
 * @return Validated count clamped to [RPC_MIN_COUNT, RPC_MAX_COUNT]
 */
size_t ValidateRpcCount(const UniValue& param, size_t default_val = RPC_DEFAULT_COUNT)
{
    if (param.isNull()) {
        return default_val;
    }
    int64_t count = param.getInt<int64_t>();
    if (count < static_cast<int64_t>(RPC_MIN_COUNT)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            strprintf("count must be at least %d", RPC_MIN_COUNT));
    }
    if (count > static_cast<int64_t>(RPC_MAX_COUNT)) {
        return RPC_MAX_COUNT;  // Silently clamp to max
    }
    return static_cast<size_t>(count);
}

/**
 * Validate start_height parameter for RPC pagination.
 * Prevents integer overflow in database queries.
 * @param param   UniValue containing start_height (can be null)
 * @param default_val Default height if param is null
 * @return Validated height in [0, RPC_MAX_START_HEIGHT]
 */
int ValidateRpcStartHeight(const UniValue& param, int default_val = 0)
{
    if (param.isNull()) {
        return default_val;
    }
    int64_t height = param.getInt<int64_t>();
    if (height < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "start_height cannot be negative");
    }
    if (height > RPC_MAX_START_HEIGHT) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            strprintf("start_height cannot exceed %d", RPC_MAX_START_HEIGHT));
    }
    return static_cast<int>(height);
}

/** Convert TokenInfo to JSON */
UniValue TokenInfoToJSON(const tokens::TokenInfo& info)
{
    UniValue result(UniValue::VOBJ);
    result.pushKV("token_id", info.token_id.GetHex());
    result.pushKV("ticker", info.ticker);
    result.pushKV("name", info.name);
    result.pushKV("decimals", info.decimals);
    result.pushKV("total_supply", info.total_supply);
    result.pushKV("circulating_supply", info.circulating_supply);
    result.pushKV("metadata_hash", info.metadata_hash.GetHex());
    result.pushKV("issuance_txid", info.issuance_txid.GetHex());
    result.pushKV("issuance_height", info.issuance_height);
    result.pushKV("issuance_time", info.issuance_time);
    result.pushKV("holder_count", info.holder_count);
    result.pushKV("transfer_count", info.transfer_count);
    
    // AUDIT FIX [M-R6]: Use integer arithmetic + string formatting instead of
    // double division, which loses precision for values > 2^53 (~9 * 10^15).
    // With MAX_MONEY = 2.1 * 10^18, double cannot represent all values exactly.
    auto FormatWithDecimals = [](uint64_t value, int decimals) -> std::string {
        if (decimals <= 0) return std::to_string(value);
        uint64_t divisor = 1;
        for (int i = 0; i < decimals; ++i) divisor *= 10;
        uint64_t whole = value / divisor;
        uint64_t frac = value % divisor;
        // Format fractional part with leading zeros, then strip trailing zeros
        std::string frac_str = std::to_string(frac);
        while (static_cast<int>(frac_str.size()) < decimals) frac_str = "0" + frac_str;
        // Remove trailing zeros for cleaner output
        size_t last_nonzero = frac_str.find_last_not_of('0');
        if (last_nonzero != std::string::npos) {
            frac_str = frac_str.substr(0, last_nonzero + 1);
        } else {
            frac_str = "0";
        }
        return std::to_string(whole) + "." + frac_str;
    };
    result.pushKV("total_supply_formatted", FormatWithDecimals(info.total_supply, info.decimals));
    result.pushKV("circulating_supply_formatted", FormatWithDecimals(info.circulating_supply, info.decimals));
    
    return result;
}

/** Convert TokenBalance to JSON */
UniValue TokenBalanceToJSON(const tokens::TokenBalance& balance, const tokens::TokenInfo* info = nullptr)
{
    UniValue result(UniValue::VOBJ);
    result.pushKV("token_id", balance.token_id.GetHex());
    result.pushKV("balance", balance.balance);
    
    if (info) {
        result.pushKV("ticker", info->ticker);
        result.pushKV("name", info->name);
        result.pushKV("decimals", info->decimals);
        
        // AUDIT FIX [M-R6]: Integer arithmetic for formatted balance (no float precision loss)
        auto FormatBalance = [](uint64_t value, int decimals) -> std::string {
            if (decimals <= 0) return std::to_string(value);
            uint64_t divisor = 1;
            for (int i = 0; i < decimals; ++i) divisor *= 10;
            uint64_t whole = value / divisor;
            uint64_t frac = value % divisor;
            std::string frac_str = std::to_string(frac);
            while (static_cast<int>(frac_str.size()) < decimals) frac_str = "0" + frac_str;
            size_t last_nonzero = frac_str.find_last_not_of('0');
            if (last_nonzero != std::string::npos) {
                frac_str = frac_str.substr(0, last_nonzero + 1);
            } else {
                frac_str = "0";
            }
            return std::to_string(whole) + "." + frac_str;
        };
        result.pushKV("balance_formatted", FormatBalance(balance.balance, info->decimals));
    }
    
    return result;
}

/** Convert TokenTransferRecord to JSON */
UniValue TransferRecordToJSON(const tokens::TokenTransferRecord& record)
{
    UniValue result(UniValue::VOBJ);
    result.pushKV("token_id", record.token_id.GetHex());
    result.pushKV("txid", record.txid.GetHex());
    result.pushKV("amount", record.amount);
    result.pushKV("height", record.height);
    result.pushKV("time", record.time);
    
    // Convert scripts to addresses if possible
    CTxDestination from_dest, to_dest;
    if (ExtractDestination(record.from_address, from_dest)) {
        result.pushKV("from", EncodeDestination(from_dest));
    }
    if (ExtractDestination(record.to_address, to_dest)) {
        result.pushKV("to", EncodeDestination(to_dest));
    }
    
    return result;
}

/** Check that token database is available */
void EnsureTokenDB()
{
    if (!tokens::g_tokendb || !tokens::g_tokendb->IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "Token database not available");
    }
}

} // namespace

// ADVISORY constant prepended to all SRC-20 RPC help descriptions.
// The SRC-20 token layer is an overlay index and is NOT part of consensus.
// See doc/src20-spec.md for the full specification.
static const std::string SRC20_ADVISORY =
    "ADVISORY: SRC-20 tokens are a non-consensus overlay. Token state is indexed locally "
    "and may diverge between node versions. Token balances are NOT enforced by miners or "
    "validated during block acceptance. Do not rely on token state for high-value settlement "
    "until a future consensus-commitment upgrade (see doc/src20-spec.md). ";

// RPC: gettokeninfo
static RPCHelpMan gettokeninfo()
{
    return RPCHelpMan{"gettokeninfo",
        SRC20_ADVISORY + "Get information about an SRC-20 token.",
        {
            {"token_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The token ID (hex)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", /*optional=*/false, "",
            {
                {RPCResult::Type::STR_HEX, "token_id", "Token identifier"},
                {RPCResult::Type::STR, "ticker", "Token ticker symbol"},
                {RPCResult::Type::STR, "name", "Token name"},
                {RPCResult::Type::NUM, "decimals", "Decimal places"},
                {RPCResult::Type::NUM, "total_supply", "Total supply in smallest units"},
                {RPCResult::Type::NUM, "circulating_supply", "Circulating supply"},
                {RPCResult::Type::STR_HEX, "metadata_hash", "Metadata hash"},
                {RPCResult::Type::STR_HEX, "issuance_txid", "Issuance transaction ID"},
                {RPCResult::Type::NUM, "issuance_height", "Block height of issuance"},
                {RPCResult::Type::NUM, "issuance_time", "Unix timestamp of issuance"},
                {RPCResult::Type::NUM, "holder_count", "Number of holders"},
                {RPCResult::Type::NUM, "transfer_count", "Number of transfers"},
                {RPCResult::Type::STR, "total_supply_formatted", "Human-readable total supply"},
                {RPCResult::Type::STR, "circulating_supply_formatted", "Human-readable circulating supply"},
            }
        },
        RPCExamples{
            HelpExampleCli("gettokeninfo", "\"abc123...\"")
            + HelpExampleRpc("gettokeninfo", "\"abc123...\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            EnsureTokenDB();
            // AUDIT FIX [RPC-RL]: Rate-limit gettokeninfo to prevent lookup spam.
            CheckRPCRateLimit(request, "gettokeninfo");

            std::string token_id_hex = request.params[0].get_str();
            auto token_id = src20::TokenId::FromHex(token_id_hex);
            if (!token_id) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid token ID");
            }

            auto info = tokens::g_tokendb->GetTokenInfo(*token_id);
            if (!info) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Token not found");
            }

            return TokenInfoToJSON(*info);
        },
    };
}

// RPC: gettokenbyname
static RPCHelpMan gettokenbyname()
{
    return RPCHelpMan{"gettokenbyname",
        SRC20_ADVISORY + "Get token information by ticker symbol.",
        {
            {"ticker", RPCArg::Type::STR, RPCArg::Optional::NO, "The token ticker (e.g., 'TEST')"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", /*optional=*/false, "", {}  // Same as gettokeninfo
        },
        RPCExamples{
            HelpExampleCli("gettokenbyname", "\"TEST\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            EnsureTokenDB();
            // AUDIT FIX [RPC-RL]: Rate-limit gettokenbyname to prevent lookup spam.
            CheckRPCRateLimit(request, "gettokenbyname");

            std::string ticker = request.params[0].get_str();
            
            auto info = tokens::g_tokendb->GetTokenByTicker(ticker);
            if (!info) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Token not found");
            }

            return TokenInfoToJSON(*info);
        },
    };
}

// RPC: gettokenbalance
static RPCHelpMan gettokenbalance()
{
    return RPCHelpMan{"gettokenbalance",
        SRC20_ADVISORY + "Get token balance for an address.",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The address to check"},
            {"token_id", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "Optional token ID filter"},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::OBJ, "", /*optional=*/false, "",
                    {
                        {RPCResult::Type::STR_HEX, "token_id", "Token ID"},
                        {RPCResult::Type::STR, "ticker", "Token ticker"},
                        {RPCResult::Type::STR, "name", "Token name"},
                        {RPCResult::Type::NUM, "decimals", "Decimal places"},
                        {RPCResult::Type::NUM, "balance", "Balance in smallest units"},
                        {RPCResult::Type::STR, "balance_formatted", "Human-readable balance"},
                    }
                }
            }
        },
        RPCExamples{
            HelpExampleCli("gettokenbalance", "\"syl1...\"")
            + HelpExampleCli("gettokenbalance", "\"syl1...\" \"abc123...\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            EnsureTokenDB();
            // SECURITY FIX [L-10]: Add rate limiting to gettokenbalance.
            // Previously this endpoint had no rate limit unlike other token RPCs.
            CheckRPCRateLimit(request, "gettokenbalance");

            std::string address_str = request.params[0].get_str();
            CTxDestination dest = DecodeDestination(address_str);
            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
            }
            CScript address = GetScriptForDestination(dest);

            UniValue result(UniValue::VARR);

            if (!request.params[1].isNull()) {
                // Single token query
                std::string token_id_hex = request.params[1].get_str();
                auto token_id = src20::TokenId::FromHex(token_id_hex);
                if (!token_id) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid token ID");
                }

                uint64_t balance = tokens::g_tokendb->GetBalance(address, *token_id);
                auto info = tokens::g_tokendb->GetTokenInfo(*token_id);
                
                tokens::TokenBalance tb(*token_id, address, balance);
                result.push_back(TokenBalanceToJSON(tb, info ? &*info : nullptr));
            } else {
                // All tokens for address
                auto balances = tokens::g_tokendb->GetAddressBalances(address);
                for (const auto& balance : balances) {
                    auto info = tokens::g_tokendb->GetTokenInfo(balance.token_id);
                    result.push_back(TokenBalanceToJSON(balance, info ? &*info : nullptr));
                }
            }

            return result;
        },
    };
}

// RPC: listtokens
static RPCHelpMan listtokens()
{
    return RPCHelpMan{"listtokens",
        SRC20_ADVISORY + "List all registered SRC-20 tokens.",
        {
            {"count", RPCArg::Type::NUM, RPCArg::Default{100}, "Maximum number of tokens to return"},
            {"start", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "Token ID to start from (for pagination)"},
        },
        RPCResult{
            RPCResult::Type::ARR, "", /*optional=*/false, "",
            {
                {RPCResult::Type::OBJ, "", /*optional=*/false, "",
                    {
                        {RPCResult::Type::STR_HEX, "token_id", "The token ID"},
                        {RPCResult::Type::STR, "ticker", "Token ticker symbol"},
                        {RPCResult::Type::STR, "name", "Token name"},
                        {RPCResult::Type::NUM, "decimals", "Decimal places"},
                        {RPCResult::Type::NUM, "total_supply", "Total supply in smallest units"},
                        {RPCResult::Type::STR, "total_supply_formatted", "Human-readable total supply"},
                        {RPCResult::Type::NUM, "circulating_supply", "Circulating supply in smallest units"},
                        {RPCResult::Type::STR, "circulating_supply_formatted", "Human-readable circulating supply"},
                        {RPCResult::Type::STR_HEX, "issuance_txid", "Issuance transaction ID"},
                        {RPCResult::Type::NUM, "issuance_height", "Block height of issuance"},
                        {RPCResult::Type::NUM, "issuance_time", "Unix timestamp of issuance"},
                        {RPCResult::Type::STR_HEX, "metadata_hash", "Optional metadata hash"},
                        {RPCResult::Type::NUM, "holder_count", "Number of unique holders"},
                        {RPCResult::Type::NUM, "transfer_count", "Number of transfers"},
                    }
                }
            }
        },
        RPCExamples{
            HelpExampleCli("listtokens", "")
            + HelpExampleCli("listtokens", "50")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            EnsureTokenDB();
            CheckRPCRateLimit(request, "listtokens", RPCRateLimiter::HEAVY_MAX_CALLS);

            size_t count = ValidateRpcCount(request.params[0]);

            std::optional<src20::TokenId> start;
            if (!request.params[1].isNull()) {
                start = src20::TokenId::FromHex(request.params[1].get_str());
            }

            auto tokens = tokens::g_tokendb->ListTokens(start, count);

            UniValue result(UniValue::VARR);
            for (const auto& info : tokens) {
                result.push_back(TokenInfoToJSON(info));
            }

            return result;
        },
    };
}

// RPC: gettokenholders
static RPCHelpMan gettokenholders()
{
    return RPCHelpMan{"gettokenholders",
        SRC20_ADVISORY + "Get holders of an SRC-20 token.",
        {
            {"token_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The token ID"},
            {"min_balance", RPCArg::Type::NUM, RPCArg::Default{0}, "Minimum balance to include"},
            {"count", RPCArg::Type::NUM, RPCArg::Default{100}, "Maximum holders to return"},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::OBJ, "", /*optional=*/false, "",
                    {
                        {RPCResult::Type::STR, "address", "Holder address"},
                        {RPCResult::Type::NUM, "balance", "Token balance"},
                        {RPCResult::Type::NUM, "percentage", "Percentage of total supply"},
                    }
                }
            }
        },
        RPCExamples{
            HelpExampleCli("gettokenholders", "\"abc123...\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            EnsureTokenDB();
            CheckRPCRateLimit(request, "gettokenholders", RPCRateLimiter::HEAVY_MAX_CALLS);

            std::string token_id_hex = request.params[0].get_str();
            auto token_id = src20::TokenId::FromHex(token_id_hex);
            if (!token_id) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid token ID");
            }

            auto info = tokens::g_tokendb->GetTokenInfo(*token_id);
            if (!info) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Token not found");
            }

            uint64_t min_balance = 0;
            if (!request.params[1].isNull()) {
                min_balance = request.params[1].getInt<uint64_t>();
            }

            size_t count = ValidateRpcCount(request.params[2]);

            auto holders = tokens::g_tokendb->GetTokenHolders(*token_id, min_balance, count);

            UniValue result(UniValue::VARR);
            for (const auto& holder : holders) {
                UniValue obj(UniValue::VOBJ);
                
                CTxDestination dest;
                if (ExtractDestination(holder.address, dest)) {
                    obj.pushKV("address", EncodeDestination(dest));
                } else {
                    obj.pushKV("address", HexStr(holder.address));
                }
                
                obj.pushKV("balance", holder.balance);
                
                // Percentage with 2 decimal places via integer arithmetic.
                // balance * 10000 could overflow int64_t for very large values,
                // so we compute in two steps: whole % first, then fractional.
                int64_t pct_whole = (info->total_supply > 0) ? (holder.balance / (info->total_supply / 100)) : 0;
                int64_t remainder = (info->total_supply > 0) ? (holder.balance % (info->total_supply / 100)) : 0;
                int64_t pct_frac = (info->total_supply > 0) ? (remainder * 100 / (info->total_supply / 100)) : 0;
                // Clamp to avoid display issues
                if (pct_whole > 100) pct_whole = 100;
                if (pct_frac < 0) pct_frac = 0;
                char pct_buf[16];
                std::snprintf(pct_buf, sizeof(pct_buf), "%d.%02d",
                              static_cast<int>(pct_whole), static_cast<int>(pct_frac));
                obj.pushKV("percentage", std::string(pct_buf));
                
                result.push_back(obj);
            }

            return result;
        },
    };
}

// RPC: gettokenhistory
static RPCHelpMan gettokenhistory()
{
    return RPCHelpMan{"gettokenhistory",
        SRC20_ADVISORY + "Get transfer history for a token.",
        {
            {"token_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The token ID"},
            {"start_height", RPCArg::Type::NUM, RPCArg::Default{0}, "Starting block height"},
            {"count", RPCArg::Type::NUM, RPCArg::Default{100}, "Maximum records to return"},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::OBJ, "", /*optional=*/false, "",
                    {
                        {RPCResult::Type::STR_HEX, "txid", "Transaction ID"},
                        {RPCResult::Type::STR, "from", "Sender address"},
                        {RPCResult::Type::STR, "to", "Recipient address"},
                        {RPCResult::Type::NUM, "amount", "Amount transferred"},
                        {RPCResult::Type::NUM, "height", "Block height"},
                        {RPCResult::Type::NUM, "time", "Block time"},
                    }
                }
            }
        },
        RPCExamples{
            HelpExampleCli("gettokenhistory", "\"abc123...\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            EnsureTokenDB();
            CheckRPCRateLimit(request, "gettokenhistory", RPCRateLimiter::HEAVY_MAX_CALLS);

            std::string token_id_hex = request.params[0].get_str();
            auto token_id = src20::TokenId::FromHex(token_id_hex);
            if (!token_id) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid token ID");
            }

            int start_height = ValidateRpcStartHeight(request.params[1]);
            size_t count = ValidateRpcCount(request.params[2]);

            auto history = tokens::g_tokendb->GetTokenHistory(*token_id, start_height, count);

            UniValue result(UniValue::VARR);
            for (const auto& record : history) {
                result.push_back(TransferRecordToJSON(record));
            }

            return result;
        },
    };
}

// RPC: issuetoken (requires wallet)
static RPCHelpMan issuetoken()
{
    return RPCHelpMan{"issuetoken",
        SRC20_ADVISORY +
        "Prepare data to issue a new SRC-20 token. "
        "Returns the OP_RETURN script needed for manual transaction creation. "
        "NOTE: For automatic transaction creation and broadcasting, use 'walletissuetoken' instead. "
        "Use createrawtransaction with this output for manual transaction construction.",
        {
            {"ticker", RPCArg::Type::STR, RPCArg::Optional::NO, "Token ticker (1-4 uppercase chars)"},
            {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "Token name (max 32 chars)"},
            {"decimals", RPCArg::Type::NUM, RPCArg::Optional::NO, "Decimal places (0-18)"},
            {"supply", RPCArg::Type::NUM, RPCArg::Optional::NO, "Total supply (in smallest units)"},
            {"metadata_hash", RPCArg::Type::STR_HEX, RPCArg::Default{""}, "Optional metadata hash (32 bytes)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", /*optional=*/false, "",
            {
                {RPCResult::Type::STR, "warning", "Advisory about SRC-20 non-consensus nature"},
                {RPCResult::Type::STR, "note", "Instructions for transaction creation"},
                {RPCResult::Type::STR_HEX, "op_return_hex", "Hex-encoded OP_RETURN script"},
                {RPCResult::Type::STR, "ticker", "Token ticker"},
                {RPCResult::Type::STR, "name", "Token name"},
                {RPCResult::Type::NUM, "decimals", "Decimal places"},
                {RPCResult::Type::NUM, "total_supply", "Total supply"},
            }
        },
        RPCExamples{
            HelpExampleCli("issuetoken", "\"TEST\" \"Test Token\" 8 1000000000000")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            EnsureTokenDB();
            CheckRPCRateLimit(request, "issuetoken", RPCRateLimiter::HEAVY_MAX_CALLS);

            // Build issuance data
            src20::TokenIssuance issuance;
            issuance.ticker = request.params[0].get_str();
            issuance.name = request.params[1].get_str();
            issuance.decimals = request.params[2].getInt<uint8_t>();
            issuance.total_supply = request.params[3].getInt<uint64_t>();
            
            if (!request.params[4].isNull() && !request.params[4].get_str().empty()) {
                std::string hash_hex = request.params[4].get_str();
                if (!issuance.metadata_hash.FromHex(hash_hex)) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid metadata hash");
                }
            }

            // Validate
            if (!issuance.IsValid()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid issuance parameters");
            }

            if (tokens::g_tokendb->TickerExists(issuance.ticker)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Ticker already exists");
            }

            if (src20::reserved::IsReservedTicker(issuance.ticker)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Ticker is reserved");
            }

            // Build the issuance script
            CScript op_return_script = src20::BuildIssuanceScript(issuance);

            // For automatic transaction creation, use walletissuetoken RPC
            // This command returns raw script data for manual transaction construction
            UniValue result(UniValue::VOBJ);
            result.pushKV("warning", "ADVISORY: SRC-20 token state is a non-consensus overlay. Balances are NOT enforced by miners. Do not rely on token state for high-value settlement until a consensus-commitment upgrade.");
            result.pushKV("note", "For automatic transaction creation, use 'walletissuetoken'. This output is for manual transaction construction with createrawtransaction.");
            result.pushKV("op_return_hex", HexStr(op_return_script));
            result.pushKV("ticker", issuance.ticker);
            result.pushKV("name", issuance.name);
            result.pushKV("decimals", issuance.decimals);
            result.pushKV("total_supply", issuance.total_supply);

            return result;
        },
    };
}

// RPC: transfertoken (requires wallet)
static RPCHelpMan transfertoken()
{
    return RPCHelpMan{"transfertoken",
        SRC20_ADVISORY +
        "Prepare data to transfer SRC-20 tokens. "
        "Returns the scripts needed for manual transaction creation. "
        "NOTE: For automatic transaction creation and broadcasting, use 'wallettransfertoken' instead.",
        {
            {"token_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The token ID"},
            {"to_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Recipient address"},
            {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "Amount to transfer (in smallest units)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", /*optional=*/false, "",
            {
                {RPCResult::Type::STR, "warning", "Advisory about SRC-20 non-consensus nature"},
                {RPCResult::Type::STR, "note", "Instructions for manual transaction construction"},
                {RPCResult::Type::STR_HEX, "op_return_hex", "Hex-encoded OP_RETURN script"},
                {RPCResult::Type::STR_HEX, "recipient_script_hex", "Hex-encoded recipient script"},
                {RPCResult::Type::STR_HEX, "token_id", "Token ID"},
                {RPCResult::Type::NUM, "amount", "Transfer amount"},
                {RPCResult::Type::STR, "to", "Recipient address"},
            }
        },
        RPCExamples{
            HelpExampleCli("transfertoken", "\"abc123...\" \"syl1...\" 100000000")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            EnsureTokenDB();
            CheckRPCRateLimit(request, "transfertoken");

            std::string token_id_hex = request.params[0].get_str();
            auto token_id = src20::TokenId::FromHex(token_id_hex);
            if (!token_id) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid token ID");
            }

            if (!tokens::g_tokendb->TokenExists(*token_id)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Token not found");
            }

            std::string to_address_str = request.params[1].get_str();
            CTxDestination to_dest = DecodeDestination(to_address_str);
            if (!IsValidDestination(to_dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid recipient address");
            }

            uint64_t amount = request.params[2].getInt<uint64_t>();
            if (amount == 0) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount must be greater than zero");
            }

            // Build transfer data
            src20::TokenTransfer transfer(*token_id, amount);
            CScript op_return_script = src20::BuildTransferScript(transfer);
            CScript recipient_script = GetScriptForDestination(to_dest);

            // For automatic transaction creation, use wallettransfertoken RPC
            UniValue result(UniValue::VOBJ);
            result.pushKV("warning", "ADVISORY: SRC-20 token state is a non-consensus overlay. Balances are NOT enforced by miners. Do not rely on token state for high-value settlement until a consensus-commitment upgrade.");
            result.pushKV("note", "For automatic transaction creation, use 'wallettransfertoken'. Manual construction: Output 0 = OP_RETURN, Output 1 = recipient, Output 2 = change.");
            result.pushKV("op_return_hex", HexStr(op_return_script));
            result.pushKV("recipient_script_hex", HexStr(recipient_script));
            result.pushKV("token_id", token_id_hex);
            result.pushKV("amount", amount);
            result.pushKV("to", to_address_str);

            return result;
        },
    };
}

// RPC: burntoken (requires wallet)
static RPCHelpMan burntoken()
{
    return RPCHelpMan{"burntoken",
        SRC20_ADVISORY +
        "Prepare data to burn SRC-20 tokens (permanently destroy). "
        "Returns the OP_RETURN script needed for manual transaction creation. "
        "NOTE: For automatic transaction creation and broadcasting, use 'walletburntoken' instead.",
        {
            {"token_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The token ID"},
            {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "Amount to burn (in smallest units)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", /*optional=*/false, "",
            {
                {RPCResult::Type::STR, "warning", "Advisory about SRC-20 non-consensus nature"},
                {RPCResult::Type::STR, "note", "Instructions for manual transaction construction"},
                {RPCResult::Type::STR_HEX, "op_return_hex", "Hex-encoded OP_RETURN script"},
                {RPCResult::Type::STR_HEX, "token_id", "Token ID"},
                {RPCResult::Type::NUM, "amount", "Amount to burn"},
            }
        },
        RPCExamples{
            HelpExampleCli("burntoken", "\"abc123...\" 100000000")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            EnsureTokenDB();
            CheckRPCRateLimit(request, "burntoken");

            std::string token_id_hex = request.params[0].get_str();
            auto token_id = src20::TokenId::FromHex(token_id_hex);
            if (!token_id) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid token ID");
            }

            if (!tokens::g_tokendb->TokenExists(*token_id)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Token not found");
            }

            uint64_t amount = request.params[1].getInt<uint64_t>();
            if (amount == 0) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount must be greater than zero");
            }

            // Build burn data
            src20::TokenBurn burn(*token_id, amount);
            CScript op_return_script = src20::BuildBurnScript(burn);

            // For automatic transaction creation, use walletburntoken RPC
            UniValue result(UniValue::VOBJ);
            result.pushKV("warning", "ADVISORY: SRC-20 token state is a non-consensus overlay. Balances are NOT enforced by miners. Do not rely on token state for high-value settlement until a consensus-commitment upgrade.");
            result.pushKV("note", "For automatic transaction creation, use 'walletburntoken'. Manual construction: Output 0 = OP_RETURN.");
            result.pushKV("op_return_hex", HexStr(op_return_script));
            result.pushKV("token_id", token_id_hex);
            result.pushKV("amount", amount);

            return result;
        },
    };
}

// RPC: gettokenstats
static RPCHelpMan gettokenstats()
{
    return RPCHelpMan{"gettokenstats",
        SRC20_ADVISORY + "Get overall SRC-20 token statistics.",
        {},
        RPCResult{
            RPCResult::Type::OBJ, "", /*optional=*/false, "",
            {
                {RPCResult::Type::NUM, "token_count", "Total registered tokens"},
                {RPCResult::Type::NUM, "transfer_count", "Total transfers"},
                {RPCResult::Type::STR_HEX, "best_block", "Last indexed block"},
            }
        },
        RPCExamples{
            HelpExampleCli("gettokenstats", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            EnsureTokenDB();
            // AUDIT FIX [RPC-RL]: Rate-limit gettokenstats to prevent stats-query spam.
            CheckRPCRateLimit(request, "gettokenstats");

            UniValue result(UniValue::VOBJ);
            result.pushKV("token_count", tokens::g_tokendb->GetTokenCount());
            result.pushKV("transfer_count", tokens::g_tokendb->GetTransferCount());
            result.pushKV("best_block", tokens::g_tokendb->GetBestBlock().GetHex());

            return result;
        },
    };
}

// RPC: decodesrc20
static RPCHelpMan decodesrc20()
{
    return RPCHelpMan{"decodesrc20",
        SRC20_ADVISORY + "Decode an SRC-20 OP_RETURN script.",
        {
            {"hexstring", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The hex-encoded script"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", /*optional=*/false, "",
            {
                {RPCResult::Type::STR, "action", "ISSUE, TRANSFER, or BURN"},
                {RPCResult::Type::OBJ, "data", /*optional=*/false, "Decoded operation data", {}}
            }
        },
        RPCExamples{
            HelpExampleCli("decodesrc20", "\"6a...\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            // AUDIT FIX [RPC-RL]: Rate-limit decodesrc20 to prevent parsing spam.
            CheckRPCRateLimit(request, "decodesrc20");

            std::string hex = request.params[0].get_str();
            std::vector<unsigned char> script_data = ParseHex(hex);
            CScript script(script_data.begin(), script_data.end());

            if (!src20::IsSRC20Script(script)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Not a valid SRC-20 script");
            }

            auto op = src20::ParseSRC20Script(script);
            if (!op) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Failed to parse SRC-20 script");
            }

            UniValue result(UniValue::VOBJ);
            result.pushKV("action", src20::ActionToString(op->action));

            UniValue data(UniValue::VOBJ);
            switch (op->action) {
                case src20::TokenAction::ISSUE: {
                    const auto* issuance = op->GetIssuance();
                    if (issuance) {
                        data.pushKV("ticker", issuance->ticker);
                        data.pushKV("name", issuance->name);
                        data.pushKV("decimals", issuance->decimals);
                        data.pushKV("total_supply", issuance->total_supply);
                        data.pushKV("metadata_hash", issuance->metadata_hash.GetHex());
                    }
                    break;
                }
                case src20::TokenAction::TRANSFER: {
                    const auto* transfer = op->GetTransfer();
                    if (transfer) {
                        data.pushKV("token_id", transfer->token_id.GetHex());
                        data.pushKV("amount", transfer->amount);
                    }
                    break;
                }
                case src20::TokenAction::BURN: {
                    const auto* burn = op->GetBurn();
                    if (burn) {
                        data.pushKV("token_id", burn->token_id.GetHex());
                        data.pushKV("amount", burn->amount);
                    }
                    break;
                }
                default:
                    break;
            }

            result.pushKV("data", data);
            return result;
        },
    };
}

// RPC: getreservedtickers
static RPCHelpMan getreservedtickers()
{
    return RPCHelpMan{"getreservedtickers",
        SRC20_ADVISORY + "Get the list of reserved SRC-20 token tickers.",
        {},
        RPCResult{
            RPCResult::Type::OBJ, "", /*optional=*/false, "",
            {
                {RPCResult::Type::ARR, "tickers", "List of reserved tickers",
                    {
                        {RPCResult::Type::STR, "", "A reserved ticker symbol"}
                    }
                },
                {RPCResult::Type::NUM, "count", "Total number of reserved tickers"},
            }
        },
        RPCExamples{
            HelpExampleCli("getreservedtickers", "") +
            HelpExampleRpc("getreservedtickers", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            // AUDIT FIX [RPC-RL]: Rate-limit getreservedtickers to prevent enumeration spam.
            CheckRPCRateLimit(request, "getreservedtickers");

            std::vector<std::string> reserved = src20::reserved::GetReservedTickers();

            UniValue tickers(UniValue::VARR);
            for (const auto& ticker : reserved) {
                tickers.push_back(ticker);
            }

            UniValue result(UniValue::VOBJ);
            result.pushKV("tickers", tickers);
            result.pushKV("count", static_cast<int64_t>(reserved.size()));

            return result;
        },
    };
}

void RegisterTokenRPCCommands(CRPCTable& t)
{
    static const CRPCCommand commands[]{
        {"tokens", &gettokeninfo},
        {"tokens", &gettokenbyname},
        {"tokens", &gettokenbalance},
        {"tokens", &listtokens},
        {"tokens", &gettokenholders},
        {"tokens", &gettokenhistory},
        {"tokens", &issuetoken},
        {"tokens", &transfertoken},
        {"tokens", &burntoken},
        {"tokens", &gettokenstats},
        {"tokens", &decodesrc20},
        {"tokens", &getreservedtickers},
    };

    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
