// Copyright (c) 2024 OpenSY Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_SCRIPT_MEMO_H
#define OPENSY_SCRIPT_MEMO_H

#include <script/script.h>
#include <string>
#include <vector>
#include <optional>

/**
 * OpenSY On-Chain Memo Support
 * 
 * Implements OP_RETURN based memo encoding for transaction notes.
 * Memos are stored on-chain as part of the transaction output.
 * 
 * Format: OP_RETURN <protocol_id> <version> <memo_data>
 * 
 * Protocol ID: "SYMO" (0x53594d4f) - SY Memo
 * Version: 1 byte
 * Memo Data: UTF-8 encoded string (max 80 bytes)
 */

namespace memo {

// Protocol constants
static const std::vector<uint8_t> PROTOCOL_ID = {0x53, 0x59, 0x4d, 0x4f}; // "SYMO"
static const uint8_t CURRENT_VERSION = 0x01;
static const size_t MAX_MEMO_LENGTH = 80;
static const size_t MIN_MEMO_LENGTH = 1;
static const size_t HEADER_SIZE = 5; // 4 bytes protocol + 1 byte version

/**
 * Memo types for different use cases
 */
enum class MemoType : uint8_t {
    TEXT = 0x01,          // Plain text memo
    INVOICE = 0x02,       // Invoice reference
    RECEIPT = 0x03,       // Receipt/order ID
    METADATA = 0x04,      // Arbitrary key-value
    // AUDIT FIX [ISSUE-009]: Renamed from ENCRYPTED to TAGGED.
    // The protocol provides NO cryptographic encryption; the old name was
    // misleading and could give users a false sense of privacy.
    // TAGGED marks application-specific opaque payloads.
    TAGGED = 0x10,        // Application-tagged opaque payload (NOT encrypted)
};

/**
 * Represents a parsed memo from an OP_RETURN output
 */
struct ParsedMemo {
    uint8_t version;
    MemoType type;
    std::string content;
    std::vector<uint8_t> raw_data;
    bool is_valid;
    std::string error;
    
    ParsedMemo() : version(0), type(MemoType::TEXT), is_valid(false) {}
};

/**
 * Result of memo script creation
 */
struct MemoScript {
    CScript script;
    bool success;
    std::string error;
    size_t data_size;
};

/**
 * Create an OP_RETURN script containing a text memo
 * 
 * @param memo The memo text (UTF-8, max 80 bytes)
 * @return MemoScript with the created script or error
 */
MemoScript CreateMemoScript(const std::string& memo);

/**
 * Create an OP_RETURN script with specified memo type
 * 
 * @param memo The memo content
 * @param type The memo type
 * @return MemoScript with the created script or error
 */
MemoScript CreateTypedMemoScript(const std::string& memo, MemoType type);

/**
 * Create an OP_RETURN script with raw data
 * 
 * @param data Raw memo data
 * @param type The memo type
 * @return MemoScript with the created script or error
 */
MemoScript CreateRawMemoScript(const std::vector<uint8_t>& data, MemoType type);

/**
 * Parse a memo from an OP_RETURN script
 * 
 * @param script The script to parse
 * @return ParsedMemo with content or error
 */
ParsedMemo ParseMemoScript(const CScript& script);

/**
 * Check if a script is a valid OpenSY memo script
 * 
 * @param script The script to check
 * @return true if script is a valid memo
 */
bool IsMemoScript(const CScript& script);

/**
 * Extract memo from transaction output
 * 
 * @param tx The transaction to search
 * @return Optional memo if found
 */
std::optional<ParsedMemo> ExtractMemoFromTx(const CTransaction& tx);

/**
 * Validate memo content
 * 
 * @param memo The memo to validate
 * @return true if memo is valid
 */
bool ValidateMemoContent(const std::string& memo);

/**
 * Get maximum allowed memo size for network
 * 
 * @return Maximum memo size in bytes
 */
size_t GetMaxMemoSize();

// ============ Implementation ============

inline MemoScript CreateMemoScript(const std::string& memo) {
    return CreateTypedMemoScript(memo, MemoType::TEXT);
}

inline MemoScript CreateTypedMemoScript(const std::string& memo, MemoType type) {
    MemoScript result;
    
    // Validate memo length
    if (memo.empty()) {
        result.success = false;
        result.error = "Memo cannot be empty";
        return result;
    }
    
    if (memo.size() > MAX_MEMO_LENGTH) {
        result.success = false;
        result.error = "Memo exceeds maximum length of " + std::to_string(MAX_MEMO_LENGTH) + " bytes";
        return result;
    }
    
    // AUDIT FIX [ISSUE-010]: Validate UTF-8 encoding for TEXT, INVOICE, and
    // RECEIPT memo types.  These are human-readable and displaying invalid
    // UTF-8 could cause rendering issues or be used for homoglyph attacks.
    if (type == MemoType::TEXT || type == MemoType::INVOICE || type == MemoType::RECEIPT) {
        size_t i = 0;
        while (i < memo.size()) {
            uint8_t c = static_cast<uint8_t>(memo[i]);
            size_t seq_len = 0;
            if (c <= 0x7F) { seq_len = 1; }
            else if ((c & 0xE0) == 0xC0) { seq_len = 2; }
            else if ((c & 0xF0) == 0xE0) { seq_len = 3; }
            else if ((c & 0xF8) == 0xF0) { seq_len = 4; }
            else {
                result.success = false;
                result.error = "Memo contains invalid UTF-8 at byte " + std::to_string(i);
                return result;
            }
            if (i + seq_len > memo.size()) {
                result.success = false;
                result.error = "Memo contains truncated UTF-8 sequence at byte " + std::to_string(i);
                return result;
            }
            // Validate continuation bytes
            for (size_t j = 1; j < seq_len; j++) {
                if ((static_cast<uint8_t>(memo[i + j]) & 0xC0) != 0x80) {
                    result.success = false;
                    result.error = "Memo contains invalid UTF-8 continuation at byte " + std::to_string(i + j);
                    return result;
                }
            }
            // Reject overlong encodings
            if (seq_len == 2 && c < 0xC2) {
                result.success = false;
                result.error = "Memo contains overlong UTF-8 encoding at byte " + std::to_string(i);
                return result;
            }
            i += seq_len;
        }
    }
    
    // Build data payload: protocol_id + version + type + memo
    std::vector<uint8_t> data;
    data.reserve(HEADER_SIZE + 1 + memo.size());
    
    // Add protocol ID
    data.insert(data.end(), PROTOCOL_ID.begin(), PROTOCOL_ID.end());
    
    // Add version
    data.push_back(CURRENT_VERSION);
    
    // Add type
    data.push_back(static_cast<uint8_t>(type));
    
    // Add memo content
    data.insert(data.end(), memo.begin(), memo.end());
    
    // Create OP_RETURN script
    result.script = CScript() << OP_RETURN << data;
    result.success = true;
    result.data_size = data.size();
    
    return result;
}

inline MemoScript CreateRawMemoScript(const std::vector<uint8_t>& data, MemoType type) {
    MemoScript result;
    
    if (data.empty()) {
        result.success = false;
        result.error = "Data cannot be empty";
        return result;
    }
    
    if (data.size() > MAX_MEMO_LENGTH) {
        result.success = false;
        result.error = "Data exceeds maximum length";
        return result;
    }
    
    // Build payload
    std::vector<uint8_t> payload;
    payload.reserve(HEADER_SIZE + 1 + data.size());
    
    payload.insert(payload.end(), PROTOCOL_ID.begin(), PROTOCOL_ID.end());
    payload.push_back(CURRENT_VERSION);
    payload.push_back(static_cast<uint8_t>(type));
    payload.insert(payload.end(), data.begin(), data.end());
    
    result.script = CScript() << OP_RETURN << payload;
    result.success = true;
    result.data_size = payload.size();
    
    return result;
}

inline bool IsMemoScript(const CScript& script) {
    // Must start with OP_RETURN
    if (script.size() < 2 || script[0] != OP_RETURN) {
        return false;
    }
    
    // Extract data from OP_RETURN
    CScript::const_iterator pc = script.begin();
    opcodetype opcode;
    std::vector<uint8_t> data;
    
    // Skip OP_RETURN
    pc++;
    
    // Get pushed data
    if (!script.GetOp(pc, opcode, data)) {
        return false;
    }
    
    // Check minimum size (header + type + at least 1 byte)
    if (data.size() < HEADER_SIZE + 2) {
        return false;
    }
    
    // Check protocol ID
    for (size_t i = 0; i < PROTOCOL_ID.size(); i++) {
        if (data[i] != PROTOCOL_ID[i]) {
            return false;
        }
    }
    
    return true;
}

inline ParsedMemo ParseMemoScript(const CScript& script) {
    ParsedMemo result;
    
    if (!IsMemoScript(script)) {
        result.is_valid = false;
        result.error = "Not a valid memo script";
        return result;
    }
    
    // Extract data
    CScript::const_iterator pc = script.begin();
    opcodetype opcode;
    std::vector<uint8_t> data;
    
    pc++; // Skip OP_RETURN
    script.GetOp(pc, opcode, data);
    
    // Parse version
    result.version = data[PROTOCOL_ID.size()];
    
    // Check version compatibility
    if (result.version > CURRENT_VERSION) {
        result.is_valid = false;
        result.error = "Unsupported memo version";
        return result;
    }
    
    // Parse type
    result.type = static_cast<MemoType>(data[HEADER_SIZE]);
    
    // Extract content
    size_t content_start = HEADER_SIZE + 1;
    if (data.size() > content_start) {
        result.raw_data = std::vector<uint8_t>(data.begin() + content_start, data.end());
        
        // For text types, convert to string
        if (result.type == MemoType::TEXT || 
            result.type == MemoType::INVOICE || 
            result.type == MemoType::RECEIPT) {
            result.content = std::string(result.raw_data.begin(), result.raw_data.end());
        }
    }
    
    result.is_valid = true;
    return result;
}

inline std::optional<ParsedMemo> ExtractMemoFromTx(const CTransaction& tx) {
    for (const CTxOut& output : tx.vout) {
        if (IsMemoScript(output.scriptPubKey)) {
            ParsedMemo memo = ParseMemoScript(output.scriptPubKey);
            if (memo.is_valid) {
                return memo;
            }
        }
    }
    return std::nullopt;
}

inline bool ValidateMemoContent(const std::string& memo) {
    if (memo.empty() || memo.size() > MAX_MEMO_LENGTH) {
        return false;
    }
    
    // Check for valid UTF-8
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(memo.c_str());
    size_t len = memo.size();
    
    for (size_t i = 0; i < len; ) {
        if (bytes[i] < 0x80) {
            // ASCII
            // Reject control characters except tab, newline
            if (bytes[i] < 0x20 && bytes[i] != 0x09 && bytes[i] != 0x0A) {
                return false;
            }
            i++;
        } else if ((bytes[i] & 0xE0) == 0xC0) {
            // 2-byte UTF-8
            if (i + 1 >= len || (bytes[i + 1] & 0xC0) != 0x80) {
                return false;
            }
            i += 2;
        } else if ((bytes[i] & 0xF0) == 0xE0) {
            // 3-byte UTF-8
            if (i + 2 >= len || (bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80) {
                return false;
            }
            i += 3;
        } else if ((bytes[i] & 0xF8) == 0xF0) {
            // 4-byte UTF-8
            if (i + 3 >= len || (bytes[i + 1] & 0xC0) != 0x80 || 
                (bytes[i + 2] & 0xC0) != 0x80 || (bytes[i + 3] & 0xC0) != 0x80) {
                return false;
            }
            i += 4;
        } else {
            return false;
        }
    }
    
    return true;
}

inline size_t GetMaxMemoSize() {
    return MAX_MEMO_LENGTH;
}

} // namespace memo

#endif // OPENSY_SCRIPT_MEMO_H
