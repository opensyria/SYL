// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_KERNEL_OPENSYKERNEL_WRAPPER_H
#define OPENSY_KERNEL_OPENSYKERNEL_WRAPPER_H

#include <kernel/opensykernel.h>

#include <array>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace osyk {

enum class LogCategory : osck_LogCategory {
    ALL = osck_LogCategory_ALL,
    BENCH = osck_LogCategory_BENCH,
    BLOCKSTORAGE = osck_LogCategory_BLOCKSTORAGE,
    COINDB = osck_LogCategory_COINDB,
    LEVELDB = osck_LogCategory_LEVELDB,
    MEMPOOL = osck_LogCategory_MEMPOOL,
    PRUNE = osck_LogCategory_PRUNE,
    RAND = osck_LogCategory_RAND,
    REINDEX = osck_LogCategory_REINDEX,
    VALIDATION = osck_LogCategory_VALIDATION,
    KERNEL = osck_LogCategory_KERNEL
};

enum class LogLevel : osck_LogLevel {
    TRACE_LEVEL = osck_LogLevel_TRACE,
    DEBUG_LEVEL = osck_LogLevel_DEBUG,
    INFO_LEVEL = osck_LogLevel_INFO
};

enum class ChainType : osck_ChainType {
    MAINNET = osck_ChainType_MAINNET,
    TESTNET = osck_ChainType_TESTNET,
    TESTNET_4 = osck_ChainType_TESTNET_4,
    SIGNET = osck_ChainType_SIGNET,
    REGTEST = osck_ChainType_REGTEST
};

enum class SynchronizationState : osck_SynchronizationState {
    INIT_REINDEX = osck_SynchronizationState_INIT_REINDEX,
    INIT_DOWNLOAD = osck_SynchronizationState_INIT_DOWNLOAD,
    POST_INIT = osck_SynchronizationState_POST_INIT
};

enum class Warning : osck_Warning {
    UNKNOWN_NEW_RULES_ACTIVATED = osck_Warning_UNKNOWN_NEW_RULES_ACTIVATED,
    LARGE_WORK_INVALID_CHAIN = osck_Warning_LARGE_WORK_INVALID_CHAIN
};

enum class ValidationMode : osck_ValidationMode {
    VALID = osck_ValidationMode_VALID,
    INVALID = osck_ValidationMode_INVALID,
    INTERNAL_ERROR = osck_ValidationMode_INTERNAL_ERROR
};

enum class BlockValidationResult : osck_BlockValidationResult {
    UNSET = osck_BlockValidationResult_UNSET,
    CONSENSUS = osck_BlockValidationResult_CONSENSUS,
    CACHED_INVALID = osck_BlockValidationResult_CACHED_INVALID,
    INVALID_HEADER = osck_BlockValidationResult_INVALID_HEADER,
    MUTATED = osck_BlockValidationResult_MUTATED,
    MISSING_PREV = osck_BlockValidationResult_MISSING_PREV,
    INVALID_PREV = osck_BlockValidationResult_INVALID_PREV,
    TIME_FUTURE = osck_BlockValidationResult_TIME_FUTURE,
    HEADER_LOW_WORK = osck_BlockValidationResult_HEADER_LOW_WORK
};

enum class ScriptVerifyStatus : osck_ScriptVerifyStatus {
    OK = osck_ScriptVerifyStatus_OK,
    ERROR_INVALID_FLAGS_COMBINATION = osck_ScriptVerifyStatus_ERROR_INVALID_FLAGS_COMBINATION,
    ERROR_SPENT_OUTPUTS_REQUIRED = osck_ScriptVerifyStatus_ERROR_SPENT_OUTPUTS_REQUIRED,
};

enum class ScriptVerificationFlags : osck_ScriptVerificationFlags {
    NONE = osck_ScriptVerificationFlags_NONE,
    P2SH = osck_ScriptVerificationFlags_P2SH,
    DERSIG = osck_ScriptVerificationFlags_DERSIG,
    NULLDUMMY = osck_ScriptVerificationFlags_NULLDUMMY,
    CHECKLOCKTIMEVERIFY = osck_ScriptVerificationFlags_CHECKLOCKTIMEVERIFY,
    CHECKSEQUENCEVERIFY = osck_ScriptVerificationFlags_CHECKSEQUENCEVERIFY,
    WITNESS = osck_ScriptVerificationFlags_WITNESS,
    TAPROOT = osck_ScriptVerificationFlags_TAPROOT,
    ALL = osck_ScriptVerificationFlags_ALL
};

template <typename T>
struct is_bitmask_enum : std::false_type {
};

template <>
struct is_bitmask_enum<ScriptVerificationFlags> : std::true_type {
};

template <typename T>
concept BitmaskEnum = is_bitmask_enum<T>::value;

template <BitmaskEnum T>
constexpr T operator|(T lhs, T rhs)
{
    return static_cast<T>(
        static_cast<std::underlying_type_t<T>>(lhs) | static_cast<std::underlying_type_t<T>>(rhs));
}

template <BitmaskEnum T>
constexpr T operator&(T lhs, T rhs)
{
    return static_cast<T>(
        static_cast<std::underlying_type_t<T>>(lhs) & static_cast<std::underlying_type_t<T>>(rhs));
}

template <BitmaskEnum T>
constexpr T operator^(T lhs, T rhs)
{
    return static_cast<T>(
        static_cast<std::underlying_type_t<T>>(lhs) ^ static_cast<std::underlying_type_t<T>>(rhs));
}

template <BitmaskEnum T>
constexpr T operator~(T value)
{
    return static_cast<T>(~static_cast<std::underlying_type_t<T>>(value));
}

template <BitmaskEnum T>
constexpr T& operator|=(T& lhs, T rhs)
{
    return lhs = lhs | rhs;
}

template <BitmaskEnum T>
constexpr T& operator&=(T& lhs, T rhs)
{
    return lhs = lhs & rhs;
}

template <BitmaskEnum T>
constexpr T& operator^=(T& lhs, T rhs)
{
    return lhs = lhs ^ rhs;
}

template <typename T>
T check(T ptr)
{
    if (ptr == nullptr) {
        throw std::runtime_error("failed to instantiate osyk object");
    }
    return ptr;
}

template <typename Collection, typename ValueType>
class Iterator
{
public:
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept = std::random_access_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = ValueType;

private:
    const Collection* m_collection;
    size_t m_idx;

public:
    Iterator() = default;
    Iterator(const Collection* ptr) : m_collection{ptr}, m_idx{0} {}
    Iterator(const Collection* ptr, size_t idx) : m_collection{ptr}, m_idx{idx} {}

    // This is just a view, so return a copy.
    auto operator*() const { return (*m_collection)[m_idx]; }
    auto operator->() const { return (*m_collection)[m_idx]; }

    auto& operator++() { m_idx++; return *this; }
    auto operator++(int) { Iterator tmp = *this; ++(*this); return tmp; }

    auto& operator--() { m_idx--; return *this; }
    auto operator--(int) { auto temp = *this; --m_idx; return temp; }

    auto& operator+=(difference_type n) { m_idx += n; return *this; }
    auto& operator-=(difference_type n) { m_idx -= n; return *this; }

    auto operator+(difference_type n) const { return Iterator(m_collection, m_idx + n); }
    auto operator-(difference_type n) const { return Iterator(m_collection, m_idx - n); }

    auto operator-(const Iterator& other) const { return static_cast<difference_type>(m_idx) - static_cast<difference_type>(other.m_idx); }

    ValueType operator[](difference_type n) const { return (*m_collection)[m_idx + n]; }

    auto operator<=>(const Iterator& other) const { return m_idx <=> other.m_idx; }

    bool operator==(const Iterator& other) const { return m_collection == other.m_collection && m_idx == other.m_idx; }

private:
    friend Iterator operator+(difference_type n, const Iterator& it) { return it + n; }
};

template <typename Container, typename SizeFunc, typename GetFunc>
concept IndexedContainer = requires(const Container& c, SizeFunc size_func, GetFunc get_func, std::size_t i) {
    { std::invoke(size_func, c) } -> std::convertible_to<std::size_t>;
    { std::invoke(get_func, c, i) }; // Return type is deduced
};

template <typename Container, auto SizeFunc, auto GetFunc>
    requires IndexedContainer<Container, decltype(SizeFunc), decltype(GetFunc)>
class Range
{
public:
    using value_type = std::invoke_result_t<decltype(GetFunc), const Container&, size_t>;
    using difference_type = std::ptrdiff_t;
    using iterator = Iterator<Range, value_type>;
    using const_iterator = iterator;

private:
    const Container* m_container;

public:
    explicit Range(const Container& container) : m_container(&container)
    {
        static_assert(std::ranges::random_access_range<Range>);
    }

    iterator begin() const { return iterator(this, 0); }
    iterator end() const { return iterator(this, size()); }

    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    size_t size() const { return std::invoke(SizeFunc, *m_container); }

    bool empty() const { return size() == 0; }

    value_type operator[](size_t index) const { return std::invoke(GetFunc, *m_container, index); }

    value_type at(size_t index) const
    {
        if (index >= size()) {
            throw std::out_of_range("Index out of range");
        }
        return (*this)[index];
    }

    value_type front() const { return (*this)[0]; }
    value_type back() const { return (*this)[size() - 1]; }
};

#define MAKE_RANGE_METHOD(method_name, ContainerType, SizeFunc, GetFunc, container_expr) \
    auto method_name() const & { \
        return Range<ContainerType, SizeFunc, GetFunc>{container_expr}; \
    } \
    auto method_name() const && = delete;

template <typename T>
std::vector<std::byte> write_bytes(const T* object, int (*to_bytes)(const T*, osck_WriteBytes, void*))
{
    std::vector<std::byte> bytes;
    struct UserData {
        std::vector<std::byte>* bytes;
        std::exception_ptr exception;
    };
    UserData user_data = UserData{.bytes = &bytes, .exception = nullptr};

    constexpr auto const write = +[](const void* buffer, size_t len, void* user_data) -> int {
        auto& data = *reinterpret_cast<UserData*>(user_data);
        auto& bytes = *data.bytes;
        try {
            auto const* first = static_cast<const std::byte*>(buffer);
            auto const* last = first + len;
            bytes.insert(bytes.end(), first, last);
            return 0;
        } catch (...) {
            data.exception = std::current_exception();
            return -1;
        }
    };

    if (to_bytes(object, write, &user_data) != 0) {
        std::rethrow_exception(user_data.exception);
    }
    return bytes;
}

template <typename CType>
class View
{
protected:
    const CType* m_ptr;

public:
    explicit View(const CType* ptr) : m_ptr{check(ptr)} {}

    const CType* get() const { return m_ptr; }
};

template <typename CType, CType* (*CopyFunc)(const CType*), void (*DestroyFunc)(CType*)>
class Handle
{
protected:
    CType* m_ptr;

public:
    explicit Handle(CType* ptr) : m_ptr{check(ptr)} {}

    // Copy constructors
    Handle(const Handle& other)
        : m_ptr{check(CopyFunc(other.m_ptr))} {}
    Handle& operator=(const Handle& other)
    {
        if (this != &other) {
            Handle temp(other);
            std::swap(m_ptr, temp.m_ptr);
        }
        return *this;
    }

    // Move constructors
    Handle(Handle&& other) noexcept : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }
    Handle& operator=(Handle&& other) noexcept
    {
        DestroyFunc(m_ptr);
        m_ptr = std::exchange(other.m_ptr, nullptr);
        return *this;
    }

    template <typename ViewType>
        requires std::derived_from<ViewType, View<CType>>
    Handle(const ViewType& view)
        : Handle{CopyFunc(view.get())}
    {
    }

    ~Handle() { DestroyFunc(m_ptr); }

    CType* get() { return m_ptr; }
    const CType* get() const { return m_ptr; }
};

template <typename CType, void (*DestroyFunc)(CType*)>
class UniqueHandle
{
protected:
    struct Deleter {
        void operator()(CType* ptr) const noexcept
        {
            if (ptr) DestroyFunc(ptr);
        }
    };
    std::unique_ptr<CType, Deleter> m_ptr;

public:
    explicit UniqueHandle(CType* ptr) : m_ptr{check(ptr)} {}

    CType* get() { return m_ptr.get(); }
    const CType* get() const { return m_ptr.get(); }
};

class Transaction;
class TransactionOutput;

template <typename Derived>
class ScriptPubkeyApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

    friend Derived;
    ScriptPubkeyApi() = default;

public:
    bool Verify(int64_t amount,
                const Transaction& tx_to,
                std::span<const TransactionOutput> spent_outputs,
                unsigned int input_index,
                ScriptVerificationFlags flags,
                ScriptVerifyStatus& status) const;

    std::vector<std::byte> ToBytes() const
    {
        return write_bytes(impl(), osck_script_pubkey_to_bytes);
    }
};

class ScriptPubkeyView : public View<osck_ScriptPubkey>, public ScriptPubkeyApi<ScriptPubkeyView>
{
public:
    explicit ScriptPubkeyView(const osck_ScriptPubkey* ptr) : View{ptr} {}
};

class ScriptPubkey : public Handle<osck_ScriptPubkey, osck_script_pubkey_copy, osck_script_pubkey_destroy>, public ScriptPubkeyApi<ScriptPubkey>
{
public:
    explicit ScriptPubkey(std::span<const std::byte> raw)
        : Handle{osck_script_pubkey_create(raw.data(), raw.size())} {}

    ScriptPubkey(const ScriptPubkeyView& view)
        : Handle(view) {}
};

template <typename Derived>
class TransactionOutputApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

    friend Derived;
    TransactionOutputApi() = default;

public:
    int64_t Amount() const
    {
        return osck_transaction_output_get_amount(impl());
    }

    ScriptPubkeyView GetScriptPubkey() const
    {
        return ScriptPubkeyView{osck_transaction_output_get_script_pubkey(impl())};
    }
};

class TransactionOutputView : public View<osck_TransactionOutput>, public TransactionOutputApi<TransactionOutputView>
{
public:
    explicit TransactionOutputView(const osck_TransactionOutput* ptr) : View{ptr} {}
};

class TransactionOutput : public Handle<osck_TransactionOutput, osck_transaction_output_copy, osck_transaction_output_destroy>, public TransactionOutputApi<TransactionOutput>
{
public:
    explicit TransactionOutput(const ScriptPubkey& script_pubkey, int64_t amount)
        : Handle{osck_transaction_output_create(script_pubkey.get(), amount)} {}

    TransactionOutput(const TransactionOutputView& view)
        : Handle(view) {}
};

template <typename Derived>
class TxidApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

    friend Derived;
    TxidApi() = default;

public:
    bool operator==(const TxidApi& other) const
    {
        return osck_txid_equals(impl(), other.impl()) != 0;
    }

    bool operator!=(const TxidApi& other) const
    {
        return osck_txid_equals(impl(), other.impl()) == 0;
    }

    std::array<std::byte, 32> ToBytes() const
    {
        std::array<std::byte, 32> hash;
        osck_txid_to_bytes(impl(), reinterpret_cast<unsigned char*>(hash.data()));
        return hash;
    }
};

class TxidView : public View<osck_Txid>, public TxidApi<TxidView>
{
public:
    explicit TxidView(const osck_Txid* ptr) : View{ptr} {}
};

class Txid : public Handle<osck_Txid, osck_txid_copy, osck_txid_destroy>, public TxidApi<Txid>
{
public:
    Txid(const TxidView& view)
        : Handle(view) {}
};

template <typename Derived>
class OutPointApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

    friend Derived;
    OutPointApi() = default;

public:
    uint32_t index() const
    {
        return osck_transaction_out_point_get_index(impl());
    }

    TxidView Txid() const
    {
        return TxidView{osck_transaction_out_point_get_txid(impl())};
    }
};

class OutPointView : public View<osck_TransactionOutPoint>, public OutPointApi<OutPointView>
{
public:
    explicit OutPointView(const osck_TransactionOutPoint* ptr) : View{ptr} {}
};

class OutPoint : public Handle<osck_TransactionOutPoint, osck_transaction_out_point_copy, osck_transaction_out_point_destroy>, public OutPointApi<OutPoint>
{
public:
    OutPoint(const OutPointView& view)
        : Handle(view) {}
};

template <typename Derived>
class TransactionInputApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

    friend Derived;
    TransactionInputApi() = default;

public:
    OutPointView OutPoint() const
    {
        return OutPointView{osck_transaction_input_get_out_point(impl())};
    }
};

class TransactionInputView : public View<osck_TransactionInput>, public TransactionInputApi<TransactionInputView>
{
public:
    explicit TransactionInputView(const osck_TransactionInput* ptr) : View{ptr} {}
};

class TransactionInput : public Handle<osck_TransactionInput, osck_transaction_input_copy, osck_transaction_input_destroy>, public TransactionInputApi<TransactionInput>
{
public:
    TransactionInput(const TransactionInputView& view)
        : Handle(view) {}
};

template <typename Derived>
class TransactionApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

public:
    size_t CountOutputs() const
    {
        return osck_transaction_count_outputs(impl());
    }

    size_t CountInputs() const
    {
        return osck_transaction_count_inputs(impl());
    }

    TransactionOutputView GetOutput(size_t index) const
    {
        return TransactionOutputView{osck_transaction_get_output_at(impl(), index)};
    }

    TransactionInputView GetInput(size_t index) const
    {
        return TransactionInputView{osck_transaction_get_input_at(impl(), index)};
    }

    TxidView Txid() const
    {
        return TxidView{osck_transaction_get_txid(impl())};
    }

    MAKE_RANGE_METHOD(Outputs, Derived, &TransactionApi<Derived>::CountOutputs, &TransactionApi<Derived>::GetOutput, *static_cast<const Derived*>(this))

    MAKE_RANGE_METHOD(Inputs, Derived, &TransactionApi<Derived>::CountInputs, &TransactionApi<Derived>::GetInput, *static_cast<const Derived*>(this))

    std::vector<std::byte> ToBytes() const
    {
        return write_bytes(impl(), osck_transaction_to_bytes);
    }
};

class TransactionView : public View<osck_Transaction>, public TransactionApi<TransactionView>
{
public:
    explicit TransactionView(const osck_Transaction* ptr) : View{ptr} {}
};

class Transaction : public Handle<osck_Transaction, osck_transaction_copy, osck_transaction_destroy>, public TransactionApi<Transaction>
{
public:
    explicit Transaction(std::span<const std::byte> raw_transaction)
        : Handle{osck_transaction_create(raw_transaction.data(), raw_transaction.size())} {}

    Transaction(const TransactionView& view)
        : Handle{view} {}
};

template <typename Derived>
bool ScriptPubkeyApi<Derived>::Verify(int64_t amount,
                                      const Transaction& tx_to,
                                      const std::span<const TransactionOutput> spent_outputs,
                                      unsigned int input_index,
                                      ScriptVerificationFlags flags,
                                      ScriptVerifyStatus& status) const
{
    const osck_TransactionOutput** spent_outputs_ptr = nullptr;
    std::vector<const osck_TransactionOutput*> raw_spent_outputs;
    if (spent_outputs.size() > 0) {
        raw_spent_outputs.reserve(spent_outputs.size());

        for (const auto& output : spent_outputs) {
            raw_spent_outputs.push_back(output.get());
        }
        spent_outputs_ptr = raw_spent_outputs.data();
    }
    auto result = osck_script_pubkey_verify(
        impl(),
        amount,
        tx_to.get(),
        spent_outputs_ptr, spent_outputs.size(),
        input_index,
        static_cast<osck_ScriptVerificationFlags>(flags),
        reinterpret_cast<osck_ScriptVerifyStatus*>(&status));
    return result == 1;
}

template <typename Derived>
class BlockHashApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

public:
    bool operator==(const Derived& other) const
    {
        return osck_block_hash_equals(impl(), other.get()) != 0;
    }

    bool operator!=(const Derived& other) const
    {
        return osck_block_hash_equals(impl(), other.get()) == 0;
    }

    std::array<std::byte, 32> ToBytes() const
    {
        std::array<std::byte, 32> hash;
        osck_block_hash_to_bytes(impl(), reinterpret_cast<unsigned char*>(hash.data()));
        return hash;
    }
};

class BlockHashView: public View<osck_BlockHash>, public BlockHashApi<BlockHashView>
{
public:
    explicit BlockHashView(const osck_BlockHash* ptr) : View{ptr} {}
};

class BlockHash : public Handle<osck_BlockHash, osck_block_hash_copy, osck_block_hash_destroy>, public BlockHashApi<BlockHash>
{
public:
    explicit BlockHash(const std::array<std::byte, 32>& hash)
        : Handle{osck_block_hash_create(reinterpret_cast<const unsigned char*>(hash.data()))} {}

    explicit BlockHash(osck_BlockHash* hash)
        : Handle{hash} {}

    BlockHash(const BlockHashView& view)
        : Handle{view} {}
};

class Block : public Handle<osck_Block, osck_block_copy, osck_block_destroy>
{
public:
    Block(const std::span<const std::byte> raw_block)
        : Handle{osck_block_create(raw_block.data(), raw_block.size())}
    {
    }

    Block(osck_Block* block) : Handle{block} {}

    size_t CountTransactions() const
    {
        return osck_block_count_transactions(get());
    }

    TransactionView GetTransaction(size_t index) const
    {
        return TransactionView{osck_block_get_transaction_at(get(), index)};
    }

    MAKE_RANGE_METHOD(Transactions, Block, &Block::CountTransactions, &Block::GetTransaction, *this)

    BlockHash GetHash() const
    {
        return BlockHash{osck_block_get_hash(get())};
    }

    std::vector<std::byte> ToBytes() const
    {
        return write_bytes(get(), osck_block_to_bytes);
    }
};

inline void logging_disable()
{
    osck_logging_disable();
}

inline void logging_set_options(const osck_LoggingOptions& logging_options)
{
    osck_logging_set_options(logging_options);
}

inline void logging_set_level_category(LogCategory category, LogLevel level)
{
    osck_logging_set_level_category(static_cast<osck_LogCategory>(category), static_cast<osck_LogLevel>(level));
}

inline void logging_enable_category(LogCategory category)
{
    osck_logging_enable_category(static_cast<osck_LogCategory>(category));
}

inline void logging_disable_category(LogCategory category)
{
    osck_logging_disable_category(static_cast<osck_LogCategory>(category));
}

template <typename T>
concept Log = requires(T a, std::string_view message) {
    { a.LogMessage(message) } -> std::same_as<void>;
};

template <Log T>
class Logger : UniqueHandle<osck_LoggingConnection, osck_logging_connection_destroy>
{
public:
    Logger(std::unique_ptr<T> log)
        : UniqueHandle{osck_logging_connection_create(
              +[](void* user_data, const char* message, size_t message_len) { static_cast<T*>(user_data)->LogMessage({message, message_len}); },
              log.release(),
              +[](void* user_data) { delete static_cast<T*>(user_data); })}
    {
    }
};

class BlockTreeEntry : public View<osck_BlockTreeEntry>
{
public:
    BlockTreeEntry(const osck_BlockTreeEntry* entry)
        : View{entry}
    {
    }

    bool operator==(const BlockTreeEntry& other) const
    {
        return osck_block_tree_entry_equals(get(), other.get()) != 0;
    }

    std::optional<BlockTreeEntry> GetPrevious() const
    {
        auto entry{osck_block_tree_entry_get_previous(get())};
        if (!entry) return std::nullopt;
        return entry;
    }

    int32_t GetHeight() const
    {
        return osck_block_tree_entry_get_height(get());
    }

    BlockHashView GetHash() const
    {
        return BlockHashView{osck_block_tree_entry_get_block_hash(get())};
    }
};

class KernelNotifications
{
public:
    virtual ~KernelNotifications() = default;

    virtual void BlockTipHandler(SynchronizationState state, BlockTreeEntry entry, double verification_progress) {}

    virtual void HeaderTipHandler(SynchronizationState state, int64_t height, int64_t timestamp, bool presync) {}

    virtual void ProgressHandler(std::string_view title, int progress_percent, bool resume_possible) {}

    virtual void WarningSetHandler(Warning warning, std::string_view message) {}

    virtual void WarningUnsetHandler(Warning warning) {}

    virtual void FlushErrorHandler(std::string_view error) {}

    virtual void FatalErrorHandler(std::string_view error) {}
};

class BlockValidationState
{
private:
    const osck_BlockValidationState* m_state;

public:
    BlockValidationState(const osck_BlockValidationState* state) : m_state{state} {}

    BlockValidationState(const BlockValidationState&) = delete;
    BlockValidationState& operator=(const BlockValidationState&) = delete;
    BlockValidationState(BlockValidationState&&) = delete;
    BlockValidationState& operator=(BlockValidationState&&) = delete;

    ValidationMode GetValidationMode() const
    {
        return static_cast<ValidationMode>(osck_block_validation_state_get_validation_mode(m_state));
    }

    BlockValidationResult GetBlockValidationResult() const
    {
        return static_cast<BlockValidationResult>(osck_block_validation_state_get_block_validation_result(m_state));
    }
};

class ValidationInterface
{
public:
    virtual ~ValidationInterface() = default;

    virtual void BlockChecked(Block block, const BlockValidationState state) {}

    virtual void PowValidBlock(BlockTreeEntry entry, Block block) {}

    virtual void BlockConnected(Block block, BlockTreeEntry entry) {}

    virtual void BlockDisconnected(Block block, BlockTreeEntry entry) {}
};

class ChainParams : public Handle<osck_ChainParameters, osck_chain_parameters_copy, osck_chain_parameters_destroy>
{
public:
    ChainParams(ChainType chain_type)
        : Handle{osck_chain_parameters_create(static_cast<osck_ChainType>(chain_type))} {}
};

class ContextOptions : public UniqueHandle<osck_ContextOptions, osck_context_options_destroy>
{
public:
    ContextOptions() : UniqueHandle{osck_context_options_create()} {}

    void SetChainParams(ChainParams& chain_params)
    {
        osck_context_options_set_chainparams(get(), chain_params.get());
    }

    template <typename T>
    void SetNotifications(std::shared_ptr<T> notifications)
    {
        static_assert(std::is_base_of_v<KernelNotifications, T>);
        auto heap_notifications = std::make_unique<std::shared_ptr<T>>(std::move(notifications));
        using user_type = std::shared_ptr<T>*;
        osck_context_options_set_notifications(
            get(),
            osck_NotificationInterfaceCallbacks{
                .user_data = heap_notifications.release(),
                .user_data_destroy = +[](void* user_data) { delete static_cast<user_type>(user_data); },
                .block_tip = +[](void* user_data, osck_SynchronizationState state, const osck_BlockTreeEntry* entry, double verification_progress) { (*static_cast<user_type>(user_data))->BlockTipHandler(static_cast<SynchronizationState>(state), BlockTreeEntry{entry}, verification_progress); },
                .header_tip = +[](void* user_data, osck_SynchronizationState state, int64_t height, int64_t timestamp, int presync) { (*static_cast<user_type>(user_data))->HeaderTipHandler(static_cast<SynchronizationState>(state), height, timestamp, presync == 1); },
                .progress = +[](void* user_data, const char* title, size_t title_len, int progress_percent, int resume_possible) { (*static_cast<user_type>(user_data))->ProgressHandler({title, title_len}, progress_percent, resume_possible == 1); },
                .warning_set = +[](void* user_data, osck_Warning warning, const char* message, size_t message_len) { (*static_cast<user_type>(user_data))->WarningSetHandler(static_cast<Warning>(warning), {message, message_len}); },
                .warning_unset = +[](void* user_data, osck_Warning warning) { (*static_cast<user_type>(user_data))->WarningUnsetHandler(static_cast<Warning>(warning)); },
                .flush_error = +[](void* user_data, const char* error, size_t error_len) { (*static_cast<user_type>(user_data))->FlushErrorHandler({error, error_len}); },
                .fatal_error = +[](void* user_data, const char* error, size_t error_len) { (*static_cast<user_type>(user_data))->FatalErrorHandler({error, error_len}); },
            });
    }

    template <typename T>
    void SetValidationInterface(std::shared_ptr<T> validation_interface)
    {
        static_assert(std::is_base_of_v<ValidationInterface, T>);
        auto heap_vi = std::make_unique<std::shared_ptr<T>>(std::move(validation_interface));
        using user_type = std::shared_ptr<T>*;
        osck_context_options_set_validation_interface(
            get(),
            osck_ValidationInterfaceCallbacks{
                .user_data = heap_vi.release(),
                .user_data_destroy = +[](void* user_data) { delete static_cast<user_type>(user_data); },
                .block_checked = +[](void* user_data, osck_Block* block, const osck_BlockValidationState* state) { (*static_cast<user_type>(user_data))->BlockChecked(Block{block}, BlockValidationState{state}); },
                .pow_valid_block = +[](void* user_data, osck_Block* block, const osck_BlockTreeEntry* entry) { (*static_cast<user_type>(user_data))->PowValidBlock(BlockTreeEntry{entry}, Block{block}); },
                .block_connected = +[](void* user_data, osck_Block* block, const osck_BlockTreeEntry* entry) { (*static_cast<user_type>(user_data))->BlockConnected(Block{block}, BlockTreeEntry{entry}); },
                .block_disconnected = +[](void* user_data, osck_Block* block, const osck_BlockTreeEntry* entry) { (*static_cast<user_type>(user_data))->BlockDisconnected(Block{block}, BlockTreeEntry{entry}); },
            });
    }
};

class Context : public Handle<osck_Context, osck_context_copy, osck_context_destroy>
{
public:
    Context(ContextOptions& opts)
        : Handle{osck_context_create(opts.get())} {}

    Context()
        : Handle{osck_context_create(ContextOptions{}.get())} {}

    bool interrupt()
    {
        return osck_context_interrupt(get()) == 0;
    }
};

class ChainstateManagerOptions : public UniqueHandle<osck_ChainstateManagerOptions, osck_chainstate_manager_options_destroy>
{
public:
    ChainstateManagerOptions(const Context& context, std::string_view data_dir, std::string_view blocks_dir)
        : UniqueHandle{osck_chainstate_manager_options_create(
              context.get(), data_dir.data(), data_dir.length(), blocks_dir.data(), blocks_dir.length())}
    {
    }

    void SetWorkerThreads(int worker_threads)
    {
        osck_chainstate_manager_options_set_worker_threads_num(get(), worker_threads);
    }

    bool SetWipeDbs(bool wipe_block_tree, bool wipe_chainstate)
    {
        return osck_chainstate_manager_options_set_wipe_dbs(get(), wipe_block_tree, wipe_chainstate) == 0;
    }

    void UpdateBlockTreeDbInMemory(bool block_tree_db_in_memory)
    {
        osck_chainstate_manager_options_update_block_tree_db_in_memory(get(), block_tree_db_in_memory);
    }

    void UpdateChainstateDbInMemory(bool chainstate_db_in_memory)
    {
        osck_chainstate_manager_options_update_chainstate_db_in_memory(get(), chainstate_db_in_memory);
    }
};

class ChainView : public View<osck_Chain>
{
public:
    explicit ChainView(const osck_Chain* ptr) : View{ptr} {}

    int32_t Height() const
    {
        return osck_chain_get_height(get());
    }

    int CountEntries() const
    {
        return osck_chain_get_height(get()) + 1;
    }

    BlockTreeEntry GetByHeight(int height) const
    {
        auto index{osck_chain_get_by_height(get(), height)};
        if (!index) throw std::runtime_error("No entry in the chain at the provided height");
        return index;
    }

    bool Contains(BlockTreeEntry& entry) const
    {
        return osck_chain_contains(get(), entry.get());
    }

    MAKE_RANGE_METHOD(Entries, ChainView, &ChainView::CountEntries, &ChainView::GetByHeight, *this)
};

template <typename Derived>
class CoinApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

    friend Derived;
    CoinApi() = default;

public:
    uint32_t GetConfirmationHeight() const { return osck_coin_confirmation_height(impl()); }

    bool IsCoinbase() const { return osck_coin_is_coinbase(impl()) == 1; }

    TransactionOutputView GetOutput() const
    {
        return TransactionOutputView{osck_coin_get_output(impl())};
    }
};

class CoinView : public View<osck_Coin>, public CoinApi<CoinView>
{
public:
    explicit CoinView(const osck_Coin* ptr) : View{ptr} {}
};

class Coin : public Handle<osck_Coin, osck_coin_copy, osck_coin_destroy>, public CoinApi<Coin>
{
public:
    Coin(osck_Coin* coin) : Handle{coin} {}

    Coin(const CoinView& view) : Handle{view} {}
};

template <typename Derived>
class TransactionSpentOutputsApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

    friend Derived;
    TransactionSpentOutputsApi() = default;

public:
    size_t Count() const
    {
        return osck_transaction_spent_outputs_count(impl());
    }

    CoinView GetCoin(size_t index) const
    {
        return CoinView{osck_transaction_spent_outputs_get_coin_at(impl(), index)};
    }

    MAKE_RANGE_METHOD(Coins, Derived, &TransactionSpentOutputsApi<Derived>::Count, &TransactionSpentOutputsApi<Derived>::GetCoin, *static_cast<const Derived*>(this))
};

class TransactionSpentOutputsView : public View<osck_TransactionSpentOutputs>, public TransactionSpentOutputsApi<TransactionSpentOutputsView>
{
public:
    explicit TransactionSpentOutputsView(const osck_TransactionSpentOutputs* ptr) : View{ptr} {}
};

class TransactionSpentOutputs : public Handle<osck_TransactionSpentOutputs, osck_transaction_spent_outputs_copy, osck_transaction_spent_outputs_destroy>,
                                public TransactionSpentOutputsApi<TransactionSpentOutputs>
{
public:
    TransactionSpentOutputs(osck_TransactionSpentOutputs* transaction_spent_outputs) : Handle{transaction_spent_outputs} {}

    TransactionSpentOutputs(const TransactionSpentOutputsView& view) : Handle{view} {}
};

class BlockSpentOutputs : public Handle<osck_BlockSpentOutputs, osck_block_spent_outputs_copy, osck_block_spent_outputs_destroy>
{
public:
    BlockSpentOutputs(osck_BlockSpentOutputs* block_spent_outputs)
        : Handle{block_spent_outputs}
    {
    }

    size_t Count() const
    {
        return osck_block_spent_outputs_count(get());
    }

    TransactionSpentOutputsView GetTxSpentOutputs(size_t tx_undo_index) const
    {
        return TransactionSpentOutputsView{osck_block_spent_outputs_get_transaction_spent_outputs_at(get(), tx_undo_index)};
    }

    MAKE_RANGE_METHOD(TxsSpentOutputs, BlockSpentOutputs, &BlockSpentOutputs::Count, &BlockSpentOutputs::GetTxSpentOutputs, *this)
};

class ChainMan : UniqueHandle<osck_ChainstateManager, osck_chainstate_manager_destroy>
{
public:
    ChainMan(const Context& context, const ChainstateManagerOptions& chainman_opts)
        : UniqueHandle{osck_chainstate_manager_create(chainman_opts.get())}
    {
    }

    bool ImportBlocks(const std::span<const std::string> paths)
    {
        std::vector<const char*> c_paths;
        std::vector<size_t> c_paths_lens;
        c_paths.reserve(paths.size());
        c_paths_lens.reserve(paths.size());
        for (const auto& path : paths) {
            c_paths.push_back(path.c_str());
            c_paths_lens.push_back(path.length());
        }

        return osck_chainstate_manager_import_blocks(get(), c_paths.data(), c_paths_lens.data(), c_paths.size()) == 0;
    }

    bool ProcessBlock(const Block& block, bool* new_block)
    {
        int _new_block;
        int res = osck_chainstate_manager_process_block(get(), block.get(), &_new_block);
        if (new_block) *new_block = _new_block == 1;
        return res == 0;
    }

    ChainView GetChain() const
    {
        return ChainView{osck_chainstate_manager_get_active_chain(get())};
    }

    std::optional<BlockTreeEntry> GetBlockTreeEntry(const BlockHash& block_hash) const
    {
        auto entry{osck_chainstate_manager_get_block_tree_entry_by_hash(get(), block_hash.get())};
        if (!entry) return std::nullopt;
        return entry;
    }

    std::optional<Block> ReadBlock(const BlockTreeEntry& entry) const
    {
        auto block{osck_block_read(get(), entry.get())};
        if (!block) return std::nullopt;
        return block;
    }

    BlockSpentOutputs ReadBlockSpentOutputs(const BlockTreeEntry& entry) const
    {
        return osck_block_spent_outputs_read(get(), entry.get());
    }
};

} // namespace osyk

#endif // OPENSY_KERNEL_OPENSYKERNEL_WRAPPER_H
