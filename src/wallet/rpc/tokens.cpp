// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cmath>
#include <core_io.h>
#include <key_io.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <script/src20.h>
#include <tokens/tokendb.h>
#include <univalue.h>
#include <wallet/rpc/util.h>
#include <wallet/tokens.h>
#include <wallet/wallet.h>

namespace wallet {

// ADVISORY constant prepended to all SRC-20 wallet RPC help descriptions.
static const std::string SRC20_ADVISORY =
    "ADVISORY: SRC-20 tokens are a non-consensus overlay. Token state is indexed locally "
    "and may diverge between node versions. Token balances are NOT enforced by miners or "
    "validated during block acceptance. Do not rely on token state for high-value settlement "
    "until a future consensus-commitment upgrade (see doc/src20-spec.md). ";

/** Check that token database is available */
static void EnsureTokenDB()
{
    if (!tokens::g_tokendb || !tokens::g_tokendb->IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "Token database not available");
    }
}

// RPC: walletissuetoken
RPCHelpMan walletissuetoken()
{
    return RPCHelpMan{"walletissuetoken",
        SRC20_ADVISORY +
        "Create and broadcast a token issuance transaction. "
        "Creates a new SRC-20 token with the specified parameters. "
        "The token will be issued to the wallet's new receiving address."
        + HELP_REQUIRING_PASSPHRASE,
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
                {RPCResult::Type::STR_HEX, "txid", "The transaction ID"},
                {RPCResult::Type::STR_HEX, "token_id", "The token ID (will be available after confirmation)"},
                {RPCResult::Type::STR, "ticker", "Token ticker"},
                {RPCResult::Type::STR, "name", "Token name"},
                {RPCResult::Type::NUM, "decimals", "Decimal places"},
                {RPCResult::Type::NUM, "total_supply", "Total supply"},
                {RPCResult::Type::STR, "issuer_address", "Address that will receive the tokens"},
                {RPCResult::Type::STR_AMOUNT, "fee", "Transaction fee"},
            }
        },
        RPCExamples{
            HelpExampleCli("walletissuetoken", "\"TEST\" \"Test Token\" 8 1000000000000")
            + HelpExampleRpc("walletissuetoken", "\"TEST\", \"Test Token\", 8, 1000000000000")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            EnsureTokenDB();
            EnsureWalletIsUnlocked(*pwallet);

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

            // Wait for wallet to sync before creating transaction
            pwallet->BlockUntilSyncedToCurrentChain();

            // Get a new address to receive the tokens
            CTxDestination dest;
            {
                LOCK(pwallet->cs_wallet);
                auto dest_result = pwallet->GetNewDestination(OutputType::BECH32, "token issuance");
                if (!dest_result) {
                    throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, util::ErrorString(dest_result).original);
                }
                dest = *dest_result;
            }

            // Create the transaction using WalletTokenManager
            WalletTokenManager token_manager(*pwallet);
            auto result = token_manager.CreateIssuanceTransaction(issuance, dest);
            if (!result) {
                throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(result).original);
            }

            // Commit the transaction
            const CTransactionRef& tx = result->tx;
            pwallet->CommitTransaction(tx, {}, /*orderForm=*/{});

            // Token ID is derived from the issuance transaction ID
            src20::TokenId token_id(tx->GetHash());

            UniValue ret(UniValue::VOBJ);
            ret.pushKV("warning", "ADVISORY: SRC-20 token state is a non-consensus overlay. Balances are NOT enforced by miners. Do not rely on token state for high-value settlement until a consensus-commitment upgrade.");
            ret.pushKV("txid", tx->GetHash().GetHex());
            ret.pushKV("token_id", token_id.GetHex());
            ret.pushKV("ticker", issuance.ticker);
            ret.pushKV("name", issuance.name);
            ret.pushKV("decimals", issuance.decimals);
            ret.pushKV("total_supply", issuance.total_supply);
            ret.pushKV("issuer_address", EncodeDestination(dest));
            ret.pushKV("fee", ValueFromAmount(result->fee));

            return ret;
        },
    };
}

// RPC: wallettransfertoken
RPCHelpMan wallettransfertoken()
{
    return RPCHelpMan{"wallettransfertoken",
        SRC20_ADVISORY +
        "Transfer SRC-20 tokens to another address."
        + HELP_REQUIRING_PASSPHRASE,
        {
            {"token_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The token ID"},
            {"to_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Recipient address"},
            {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "Amount to transfer (in smallest units)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", /*optional=*/false, "",
            {
                {RPCResult::Type::STR_HEX, "txid", "Transaction ID"},
                {RPCResult::Type::STR_HEX, "token_id", "Token ID"},
                {RPCResult::Type::NUM, "amount", "Amount transferred"},
                {RPCResult::Type::STR, "to", "Recipient address"},
                {RPCResult::Type::STR_AMOUNT, "fee", "Transaction fee"},
            }
        },
        RPCExamples{
            HelpExampleCli("wallettransfertoken", "\"abc123...\" \"syl1...\" 100000000")
            + HelpExampleRpc("wallettransfertoken", "\"abc123...\", \"syl1...\", 100000000")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            EnsureTokenDB();
            EnsureWalletIsUnlocked(*pwallet);

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

            // Wait for wallet to sync before creating transaction
            pwallet->BlockUntilSyncedToCurrentChain();

            // Get token change address (remaining tokens go here)
            CTxDestination change_dest;
            {
                LOCK(pwallet->cs_wallet);
                auto change_result = pwallet->GetNewDestination(OutputType::BECH32, "token change");
                if (!change_result) {
                    throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, util::ErrorString(change_result).original);
                }
                change_dest = *change_result;
            }

            // Create the transaction
            WalletTokenManager token_manager(*pwallet);
            auto result = token_manager.CreateTransferTransaction(*token_id, to_dest, amount, change_dest);
            if (!result) {
                throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(result).original);
            }

            // Commit the transaction
            const CTransactionRef& tx = result->tx;
            pwallet->CommitTransaction(tx, {}, /*orderForm=*/{});

            UniValue ret(UniValue::VOBJ);
            ret.pushKV("warning", "ADVISORY: SRC-20 token state is a non-consensus overlay. Balances are NOT enforced by miners. Do not rely on token state for high-value settlement until a consensus-commitment upgrade.");
            ret.pushKV("txid", tx->GetHash().GetHex());
            ret.pushKV("token_id", token_id_hex);
            ret.pushKV("amount", amount);
            ret.pushKV("to", to_address_str);
            ret.pushKV("fee", ValueFromAmount(result->fee));

            return ret;
        },
    };
}

// RPC: walletburntoken
RPCHelpMan walletburntoken()
{
    return RPCHelpMan{"walletburntoken",
        SRC20_ADVISORY +
        "Burn SRC-20 tokens (permanently destroy)."
        + HELP_REQUIRING_PASSPHRASE,
        {
            {"token_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The token ID"},
            {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "Amount to burn (in smallest units)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", /*optional=*/false, "",
            {
                {RPCResult::Type::STR_HEX, "txid", "Transaction ID"},
                {RPCResult::Type::STR_HEX, "token_id", "Token ID"},
                {RPCResult::Type::NUM, "amount", "Amount burned"},
                {RPCResult::Type::STR_AMOUNT, "fee", "Transaction fee"},
            }
        },
        RPCExamples{
            HelpExampleCli("walletburntoken", "\"abc123...\" 100000000")
            + HelpExampleRpc("walletburntoken", "\"abc123...\", 100000000")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            EnsureTokenDB();
            EnsureWalletIsUnlocked(*pwallet);

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

            // Wait for wallet to sync before creating transaction
            pwallet->BlockUntilSyncedToCurrentChain();

            // Create the transaction
            WalletTokenManager token_manager(*pwallet);
            auto result = token_manager.CreateBurnTransaction(*token_id, amount);
            if (!result) {
                throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(result).original);
            }

            // Commit the transaction
            const CTransactionRef& tx = result->tx;
            pwallet->CommitTransaction(tx, {}, /*orderForm=*/{});

            UniValue ret(UniValue::VOBJ);
            ret.pushKV("warning", "ADVISORY: SRC-20 token state is a non-consensus overlay. Balances are NOT enforced by miners. Do not rely on token state for high-value settlement until a consensus-commitment upgrade.");
            ret.pushKV("txid", tx->GetHash().GetHex());
            ret.pushKV("token_id", token_id_hex);
            ret.pushKV("amount", amount);
            ret.pushKV("fee", ValueFromAmount(result->fee));

            return ret;
        },
    };
}

// RPC: gettokenbalances (wallet-aware version)
RPCHelpMan gettokenbalances()
{
    return RPCHelpMan{"gettokenbalances",
        SRC20_ADVISORY + "Get all token balances for this wallet.",
        {},
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
            HelpExampleCli("gettokenbalances", "")
            + HelpExampleRpc("gettokenbalances", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            EnsureTokenDB();

            LOCK(pwallet->cs_wallet);

            WalletTokenManager token_manager(*pwallet);
            auto balances = token_manager.GetTokenBalances();

            UniValue result(UniValue::VARR);
            for (const auto& balance : balances) {
                UniValue obj(UniValue::VOBJ);
                obj.pushKV("token_id", balance.token_id.GetHex());
                obj.pushKV("ticker", balance.ticker);
                obj.pushKV("name", balance.name);
                obj.pushKV("decimals", balance.decimals);
                obj.pushKV("balance", balance.balance);
                obj.pushKV("balance_formatted", balance.balance_formatted);
                result.push_back(obj);
            }

            return result;
        },
    };
}

// RPC: gettokenhistory (wallet-aware version)
RPCHelpMan gettokentxhistory()
{
    return RPCHelpMan{"gettokentxhistory",
        SRC20_ADVISORY + "Get token transaction history for this wallet.",
        {
            {"token_id", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "Optional token ID filter"},
            {"count", RPCArg::Type::NUM, RPCArg::Default{100}, "Maximum number of transactions to return"},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::OBJ, "", /*optional=*/false, "",
                    {
                        {RPCResult::Type::STR_HEX, "txid", "Transaction ID"},
                        {RPCResult::Type::STR_HEX, "token_id", "Token ID"},
                        {RPCResult::Type::STR, "ticker", "Token ticker"},
                        {RPCResult::Type::STR, "type", "Transaction type: issue, transfer, or burn"},
                        {RPCResult::Type::NUM, "amount", "Amount"},
                        {RPCResult::Type::STR, "amount_formatted", "Human-readable amount"},
                        {RPCResult::Type::STR, "from", /*optional=*/true, "From address (empty for issuance)"},
                        {RPCResult::Type::STR, "to", /*optional=*/true, "To address (empty for burn)"},
                        {RPCResult::Type::BOOL, "is_incoming", "True if receiving tokens"},
                        {RPCResult::Type::NUM, "confirmations", "Number of confirmations"},
                        {RPCResult::Type::NUM, "time", "Transaction time"},
                    }
                }
            }
        },
        RPCExamples{
            HelpExampleCli("gettokentxhistory", "")
            + HelpExampleCli("gettokentxhistory", "\"abc123...\" 50")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            EnsureTokenDB();

            std::optional<src20::TokenId> token_id;
            if (!request.params[0].isNull()) {
                auto parsed = src20::TokenId::FromHex(request.params[0].get_str());
                if (!parsed) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid token ID");
                }
                token_id = *parsed;
            }

            size_t count = 100;
            if (!request.params[1].isNull()) {
                count = request.params[1].getInt<size_t>();
                if (count > 1000) count = 1000;
            }

            LOCK(pwallet->cs_wallet);

            WalletTokenManager token_manager(*pwallet);
            auto history = token_manager.GetTokenHistory(token_id, count);

            UniValue result(UniValue::VARR);
            for (const auto& tx : history) {
                UniValue obj(UniValue::VOBJ);
                obj.pushKV("txid", tx.txid.GetHex());
                obj.pushKV("token_id", tx.token_id.GetHex());
                obj.pushKV("ticker", tx.ticker);
                
                // Determine type based on from/to addresses
                bool has_from = IsValidDestination(tx.from);
                bool has_to = IsValidDestination(tx.to);
                std::string tx_type;
                if (!has_from && has_to) {
                    tx_type = "issue";
                } else if (has_from && !has_to) {
                    tx_type = "burn";
                } else {
                    tx_type = "transfer";
                }
                obj.pushKV("type", tx_type);
                
                obj.pushKV("amount", tx.amount);
                obj.pushKV("amount_formatted", tx.amount_formatted);
                if (has_from) {
                    obj.pushKV("from", EncodeDestination(tx.from));
                }
                if (has_to) {
                    obj.pushKV("to", EncodeDestination(tx.to));
                }
                obj.pushKV("is_incoming", tx.is_incoming);
                obj.pushKV("confirmations", tx.confirmations);
                obj.pushKV("time", tx.time);
                result.push_back(obj);
            }

            return result;
        },
    };
}

std::span<const CRPCCommand> GetWalletTokenRPCCommands()
{
    static const CRPCCommand commands[]{
        {"wallet", &walletissuetoken},
        {"wallet", &wallettransfertoken},
        {"wallet", &walletburntoken},
        {"wallet", &gettokenbalances},
        {"wallet", &gettokentxhistory},
    };
    return commands;
}

} // namespace wallet
