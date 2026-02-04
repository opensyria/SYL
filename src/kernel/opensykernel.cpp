// Copyright (c) 2022-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define OPENSYKERNEL_BUILD

#include <kernel/opensykernel.h>

#include <chain.h>
#include <coins.h>
#include <consensus/amount.h>
#include <consensus/validation.h>
#include <kernel/caches.h>
#include <kernel/chainparams.h>
#include <kernel/checks.h>
#include <kernel/context.h>
#include <kernel/cs_main.h>
#include <kernel/notifications_interface.h>
#include <kernel/warning.h>
#include <logging.h>
#include <node/blockstorage.h>
#include <node/chainstate.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <serialize.h>
#include <streams.h>
#include <sync.h>
#include <tinyformat.h>
#include <uint256.h>
#include <undo.h>
#include <util/fs.h>
#include <util/result.h>
#include <util/signalinterrupt.h>
#include <util/task_runner.h>
#include <util/translation.h>
#include <validation.h>
#include <validationinterface.h>

#include <cassert>
#include <cstddef>
#include <cstring>
#include <exception>
#include <functional>
#include <list>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using util::ImmediateTaskRunner;

// Define G_TRANSLATION_FUN symbol in libopensykernel library so users of the
// library aren't required to export this symbol
extern const std::function<std::string(const char*)> G_TRANSLATION_FUN{nullptr};

static const kernel::Context osck_context_static{};

namespace {

bool is_valid_flag_combination(script_verify_flags flags)
{
    if (flags & SCRIPT_VERIFY_CLEANSTACK && ~flags & (SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS)) return false;
    if (flags & SCRIPT_VERIFY_WITNESS && ~flags & SCRIPT_VERIFY_P2SH) return false;
    return true;
}

class WriterStream
{
private:
    osck_WriteBytes m_writer;
    void* m_user_data;

public:
    WriterStream(osck_WriteBytes writer, void* user_data)
        : m_writer{writer}, m_user_data{user_data} {}

    //
    // Stream subset
    //
    void write(std::span<const std::byte> src)
    {
        if (m_writer(std::data(src), src.size(), m_user_data) != 0) {
            throw std::runtime_error("Failed to write serialization data");
        }
    }

    template <typename T>
    WriterStream& operator<<(const T& obj)
    {
        ::Serialize(*this, obj);
        return *this;
    }
};

template <typename C, typename CPP>
struct Handle {
    static C* ref(CPP* cpp_type)
    {
        return reinterpret_cast<C*>(cpp_type);
    }

    static const C* ref(const CPP* cpp_type)
    {
        return reinterpret_cast<const C*>(cpp_type);
    }

    template <typename... Args>
    static C* create(Args&&... args)
    {
        auto cpp_obj{std::make_unique<CPP>(std::forward<Args>(args)...)};
        return reinterpret_cast<C*>(cpp_obj.release());
    }

    static C* copy(const C* ptr)
    {
        auto cpp_obj{std::make_unique<CPP>(get(ptr))};
        return reinterpret_cast<C*>(cpp_obj.release());
    }

    static const CPP& get(const C* ptr)
    {
        return *reinterpret_cast<const CPP*>(ptr);
    }

    static CPP& get(C* ptr)
    {
        return *reinterpret_cast<CPP*>(ptr);
    }

    static void operator delete(void* ptr)
    {
        delete reinterpret_cast<CPP*>(ptr);
    }
};

} // namespace

struct osck_BlockTreeEntry: Handle<osck_BlockTreeEntry, CBlockIndex> {};
struct osck_Block : Handle<osck_Block, std::shared_ptr<const CBlock>> {};
struct osck_BlockValidationState : Handle<osck_BlockValidationState, BlockValidationState> {};

namespace {

BCLog::Level get_bclog_level(osck_LogLevel level)
{
    switch (level) {
    case osck_LogLevel_INFO: {
        return BCLog::Level::Info;
    }
    case osck_LogLevel_DEBUG: {
        return BCLog::Level::Debug;
    }
    case osck_LogLevel_TRACE: {
        return BCLog::Level::Trace;
    }
    }
    assert(false);
}

BCLog::LogFlags get_bclog_flag(osck_LogCategory category)
{
    switch (category) {
    case osck_LogCategory_BENCH: {
        return BCLog::LogFlags::BENCH;
    }
    case osck_LogCategory_BLOCKSTORAGE: {
        return BCLog::LogFlags::BLOCKSTORAGE;
    }
    case osck_LogCategory_COINDB: {
        return BCLog::LogFlags::COINDB;
    }
    case osck_LogCategory_LEVELDB: {
        return BCLog::LogFlags::LEVELDB;
    }
    case osck_LogCategory_MEMPOOL: {
        return BCLog::LogFlags::MEMPOOL;
    }
    case osck_LogCategory_PRUNE: {
        return BCLog::LogFlags::PRUNE;
    }
    case osck_LogCategory_RAND: {
        return BCLog::LogFlags::RAND;
    }
    case osck_LogCategory_REINDEX: {
        return BCLog::LogFlags::REINDEX;
    }
    case osck_LogCategory_VALIDATION: {
        return BCLog::LogFlags::VALIDATION;
    }
    case osck_LogCategory_KERNEL: {
        return BCLog::LogFlags::KERNEL;
    }
    case osck_LogCategory_ALL: {
        return BCLog::LogFlags::ALL;
    }
    }
    assert(false);
}

osck_SynchronizationState cast_state(SynchronizationState state)
{
    switch (state) {
    case SynchronizationState::INIT_REINDEX:
        return osck_SynchronizationState_INIT_REINDEX;
    case SynchronizationState::INIT_DOWNLOAD:
        return osck_SynchronizationState_INIT_DOWNLOAD;
    case SynchronizationState::POST_INIT:
        return osck_SynchronizationState_POST_INIT;
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

osck_Warning cast_osck_warning(kernel::Warning warning)
{
    switch (warning) {
    case kernel::Warning::UNKNOWN_NEW_RULES_ACTIVATED:
        return osck_Warning_UNKNOWN_NEW_RULES_ACTIVATED;
    case kernel::Warning::LARGE_WORK_INVALID_CHAIN:
        return osck_Warning_LARGE_WORK_INVALID_CHAIN;
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

struct LoggingConnection {
    std::unique_ptr<std::list<std::function<void(const std::string&)>>::iterator> m_connection;
    void* m_user_data;
    std::function<void(void* user_data)> m_deleter;

    LoggingConnection(osck_LogCallback callback, void* user_data, osck_DestroyCallback user_data_destroy_callback)
    {
        LOCK(cs_main);

        auto connection{LogInstance().PushBackCallback([callback, user_data](const std::string& str) { callback(user_data, str.c_str(), str.length()); })};

        // Only start logging if we just added the connection.
        if (LogInstance().NumConnections() == 1 && !LogInstance().StartLogging()) {
            LogError("Logger start failed.");
            LogInstance().DeleteCallback(connection);
            if (user_data && user_data_destroy_callback) {
                user_data_destroy_callback(user_data);
            }
            throw std::runtime_error("Failed to start logging");
        }

        m_connection = std::make_unique<std::list<std::function<void(const std::string&)>>::iterator>(connection);
        m_user_data = user_data;
        m_deleter = user_data_destroy_callback;

        LogDebug(BCLog::KERNEL, "Logger connected.");
    }

    ~LoggingConnection()
    {
        LOCK(cs_main);
        LogDebug(BCLog::KERNEL, "Logger disconnecting.");

        // Switch back to buffering by calling DisconnectTestLogger if the
        // connection that we are about to remove is the last one.
        if (LogInstance().NumConnections() == 1) {
            LogInstance().DisconnectTestLogger();
        } else {
            LogInstance().DeleteCallback(*m_connection);
        }

        m_connection.reset();
        if (m_user_data && m_deleter) {
            m_deleter(m_user_data);
        }
    }
};

class KernelNotifications final : public kernel::Notifications
{
private:
    osck_NotificationInterfaceCallbacks m_cbs;

public:
    KernelNotifications(osck_NotificationInterfaceCallbacks cbs)
        : m_cbs{cbs}
    {
    }

    ~KernelNotifications()
    {
        if (m_cbs.user_data && m_cbs.user_data_destroy) {
            m_cbs.user_data_destroy(m_cbs.user_data);
        }
        m_cbs.user_data_destroy = nullptr;
        m_cbs.user_data = nullptr;
    }

    kernel::InterruptResult blockTip(SynchronizationState state, const CBlockIndex& index, double verification_progress) override
    {
        if (m_cbs.block_tip) m_cbs.block_tip(m_cbs.user_data, cast_state(state), osck_BlockTreeEntry::ref(&index), verification_progress);
        return {};
    }
    void headerTip(SynchronizationState state, int64_t height, int64_t timestamp, bool presync) override
    {
        if (m_cbs.header_tip) m_cbs.header_tip(m_cbs.user_data, cast_state(state), height, timestamp, presync ? 1 : 0);
    }
    void progress(const bilingual_str& title, int progress_percent, bool resume_possible) override
    {
        if (m_cbs.progress) m_cbs.progress(m_cbs.user_data, title.original.c_str(), title.original.length(), progress_percent, resume_possible ? 1 : 0);
    }
    void warningSet(kernel::Warning id, const bilingual_str& message) override
    {
        if (m_cbs.warning_set) m_cbs.warning_set(m_cbs.user_data, cast_osck_warning(id), message.original.c_str(), message.original.length());
    }
    void warningUnset(kernel::Warning id) override
    {
        if (m_cbs.warning_unset) m_cbs.warning_unset(m_cbs.user_data, cast_osck_warning(id));
    }
    void flushError(const bilingual_str& message) override
    {
        if (m_cbs.flush_error) m_cbs.flush_error(m_cbs.user_data, message.original.c_str(), message.original.length());
    }
    void fatalError(const bilingual_str& message) override
    {
        if (m_cbs.fatal_error) m_cbs.fatal_error(m_cbs.user_data, message.original.c_str(), message.original.length());
    }
};

class KernelValidationInterface final : public CValidationInterface
{
public:
    osck_ValidationInterfaceCallbacks m_cbs;

    explicit KernelValidationInterface(const osck_ValidationInterfaceCallbacks vi_cbs) : m_cbs{vi_cbs} {}

    ~KernelValidationInterface()
    {
        if (m_cbs.user_data && m_cbs.user_data_destroy) {
            m_cbs.user_data_destroy(m_cbs.user_data);
        }
        m_cbs.user_data = nullptr;
        m_cbs.user_data_destroy = nullptr;
    }

protected:
    void BlockChecked(const std::shared_ptr<const CBlock>& block, const BlockValidationState& stateIn) override
    {
        if (m_cbs.block_checked) {
            m_cbs.block_checked(m_cbs.user_data,
                                osck_Block::copy(osck_Block::ref(&block)),
                                osck_BlockValidationState::ref(&stateIn));
        }
    }

    void NewPoWValidBlock(const CBlockIndex* pindex, const std::shared_ptr<const CBlock>& block) override
    {
        if (m_cbs.pow_valid_block) {
            m_cbs.pow_valid_block(m_cbs.user_data,
                                  osck_Block::copy(osck_Block::ref(&block)),
                                  osck_BlockTreeEntry::ref(pindex));
        }
    }

    void BlockConnected(ChainstateRole role, const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex) override
    {
        if (m_cbs.block_connected) {
            m_cbs.block_connected(m_cbs.user_data,
                                  osck_Block::copy(osck_Block::ref(&block)),
                                  osck_BlockTreeEntry::ref(pindex));
        }
    }

    void BlockDisconnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex) override
    {
        if (m_cbs.block_disconnected) {
            m_cbs.block_disconnected(m_cbs.user_data,
                                     osck_Block::copy(osck_Block::ref(&block)),
                                     osck_BlockTreeEntry::ref(pindex));
        }
    }
};

struct ContextOptions {
    mutable Mutex m_mutex;
    std::unique_ptr<const CChainParams> m_chainparams GUARDED_BY(m_mutex);
    std::shared_ptr<KernelNotifications> m_notifications GUARDED_BY(m_mutex);
    std::shared_ptr<KernelValidationInterface> m_validation_interface GUARDED_BY(m_mutex);
};

class Context
{
public:
    std::unique_ptr<kernel::Context> m_context;

    std::shared_ptr<KernelNotifications> m_notifications;

    std::unique_ptr<util::SignalInterrupt> m_interrupt;

    std::unique_ptr<ValidationSignals> m_signals;

    std::unique_ptr<const CChainParams> m_chainparams;

    std::shared_ptr<KernelValidationInterface> m_validation_interface;

    Context(const ContextOptions* options, bool& sane)
        : m_context{std::make_unique<kernel::Context>()},
          m_interrupt{std::make_unique<util::SignalInterrupt>()}
    {
        if (options) {
            LOCK(options->m_mutex);
            if (options->m_chainparams) {
                m_chainparams = std::make_unique<const CChainParams>(*options->m_chainparams);
            }
            if (options->m_notifications) {
                m_notifications = options->m_notifications;
            }
            if (options->m_validation_interface) {
                m_signals = std::make_unique<ValidationSignals>(std::make_unique<ImmediateTaskRunner>());
                m_validation_interface = options->m_validation_interface;
                m_signals->RegisterSharedValidationInterface(m_validation_interface);
            }
        }

        if (!m_chainparams) {
            m_chainparams = CChainParams::Main();
        }
        if (!m_notifications) {
            m_notifications = std::make_shared<KernelNotifications>(osck_NotificationInterfaceCallbacks{
                nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr});
        }

        if (!kernel::SanityChecks(*m_context)) {
            sane = false;
        }
    }

    ~Context()
    {
        if (m_signals) {
            m_signals->UnregisterSharedValidationInterface(m_validation_interface);
        }
    }
};

//! Helper struct to wrap the ChainstateManager-related Options
struct ChainstateManagerOptions {
    mutable Mutex m_mutex;
    ChainstateManager::Options m_chainman_options GUARDED_BY(m_mutex);
    node::BlockManager::Options m_blockman_options GUARDED_BY(m_mutex);
    std::shared_ptr<const Context> m_context;
    node::ChainstateLoadOptions m_chainstate_load_options GUARDED_BY(m_mutex);

    ChainstateManagerOptions(const std::shared_ptr<const Context>& context, const fs::path& data_dir, const fs::path& blocks_dir)
        : m_chainman_options{ChainstateManager::Options{
              .chainparams = *context->m_chainparams,
              .datadir = data_dir,
              .notifications = *context->m_notifications,
              .signals = context->m_signals.get()}},
          m_blockman_options{node::BlockManager::Options{
              .chainparams = *context->m_chainparams,
              .blocks_dir = blocks_dir,
              .notifications = *context->m_notifications,
              .block_tree_db_params = DBParams{
                  .path = data_dir / "blocks" / "index",
                  .cache_bytes = kernel::CacheSizes{DEFAULT_KERNEL_CACHE}.block_tree_db,
              }}},
          m_context{context}, m_chainstate_load_options{node::ChainstateLoadOptions{}}
    {
    }
};

struct ChainMan {
    std::unique_ptr<ChainstateManager> m_chainman;
    std::shared_ptr<const Context> m_context;

    ChainMan(std::unique_ptr<ChainstateManager> chainman, std::shared_ptr<const Context> context)
        : m_chainman(std::move(chainman)), m_context(std::move(context)) {}
};

} // namespace

struct osck_Transaction : Handle<osck_Transaction, std::shared_ptr<const CTransaction>> {};
struct osck_TransactionOutput : Handle<osck_TransactionOutput, CTxOut> {};
struct osck_ScriptPubkey : Handle<osck_ScriptPubkey, CScript> {};
struct osck_LoggingConnection : Handle<osck_LoggingConnection, LoggingConnection> {};
struct osck_ContextOptions : Handle<osck_ContextOptions, ContextOptions> {};
struct osck_Context : Handle<osck_Context, std::shared_ptr<const Context>> {};
struct osck_ChainParameters : Handle<osck_ChainParameters, CChainParams> {};
struct osck_ChainstateManagerOptions : Handle<osck_ChainstateManagerOptions, ChainstateManagerOptions> {};
struct osck_ChainstateManager : Handle<osck_ChainstateManager, ChainMan> {};
struct osck_Chain : Handle<osck_Chain, CChain> {};
struct osck_BlockSpentOutputs : Handle<osck_BlockSpentOutputs, std::shared_ptr<CBlockUndo>> {};
struct osck_TransactionSpentOutputs : Handle<osck_TransactionSpentOutputs, CTxUndo> {};
struct osck_Coin : Handle<osck_Coin, Coin> {};
struct osck_BlockHash : Handle<osck_BlockHash, uint256> {};
struct osck_TransactionInput : Handle<osck_TransactionInput, CTxIn> {};
struct osck_TransactionOutPoint: Handle<osck_TransactionOutPoint, COutPoint> {};
struct osck_Txid: Handle<osck_Txid, Txid> {};

osck_Transaction* osck_transaction_create(const void* raw_transaction, size_t raw_transaction_len)
{
    if (raw_transaction == nullptr && raw_transaction_len != 0) {
        return nullptr;
    }
    try {
        DataStream stream{std::span{reinterpret_cast<const std::byte*>(raw_transaction), raw_transaction_len}};
        return osck_Transaction::create(std::make_shared<const CTransaction>(deserialize, TX_WITH_WITNESS, stream));
    } catch (...) {
        return nullptr;
    }
}

size_t osck_transaction_count_outputs(const osck_Transaction* transaction)
{
    return osck_Transaction::get(transaction)->vout.size();
}

const osck_TransactionOutput* osck_transaction_get_output_at(const osck_Transaction* transaction, size_t output_index)
{
    const CTransaction& tx = *osck_Transaction::get(transaction);
    assert(output_index < tx.vout.size());
    return osck_TransactionOutput::ref(&tx.vout[output_index]);
}

size_t osck_transaction_count_inputs(const osck_Transaction* transaction)
{
    return osck_Transaction::get(transaction)->vin.size();
}

const osck_TransactionInput* osck_transaction_get_input_at(const osck_Transaction* transaction, size_t input_index)
{
    assert(input_index < osck_Transaction::get(transaction)->vin.size());
    return osck_TransactionInput::ref(&osck_Transaction::get(transaction)->vin[input_index]);
}

const osck_Txid* osck_transaction_get_txid(const osck_Transaction* transaction)
{
    return osck_Txid::ref(&osck_Transaction::get(transaction)->GetHash());
}

osck_Transaction* osck_transaction_copy(const osck_Transaction* transaction)
{
    return osck_Transaction::copy(transaction);
}

int osck_transaction_to_bytes(const osck_Transaction* transaction, osck_WriteBytes writer, void* user_data)
{
    try {
        WriterStream ws{writer, user_data};
        ws << TX_WITH_WITNESS(osck_Transaction::get(transaction));
        return 0;
    } catch (...) {
        return -1;
    }
}

void osck_transaction_destroy(osck_Transaction* transaction)
{
    delete transaction;
}

osck_ScriptPubkey* osck_script_pubkey_create(const void* script_pubkey, size_t script_pubkey_len)
{
    if (script_pubkey == nullptr && script_pubkey_len != 0) {
        return nullptr;
    }
    auto data = std::span{reinterpret_cast<const uint8_t*>(script_pubkey), script_pubkey_len};
    return osck_ScriptPubkey::create(data.begin(), data.end());
}

int osck_script_pubkey_to_bytes(const osck_ScriptPubkey* script_pubkey_, osck_WriteBytes writer, void* user_data)
{
    const auto& script_pubkey{osck_ScriptPubkey::get(script_pubkey_)};
    return writer(script_pubkey.data(), script_pubkey.size(), user_data);
}

osck_ScriptPubkey* osck_script_pubkey_copy(const osck_ScriptPubkey* script_pubkey)
{
    return osck_ScriptPubkey::copy(script_pubkey);
}

void osck_script_pubkey_destroy(osck_ScriptPubkey* script_pubkey)
{
    delete script_pubkey;
}

osck_TransactionOutput* osck_transaction_output_create(const osck_ScriptPubkey* script_pubkey, int64_t amount)
{
    return osck_TransactionOutput::create(amount, osck_ScriptPubkey::get(script_pubkey));
}

osck_TransactionOutput* osck_transaction_output_copy(const osck_TransactionOutput* output)
{
    return osck_TransactionOutput::copy(output);
}

const osck_ScriptPubkey* osck_transaction_output_get_script_pubkey(const osck_TransactionOutput* output)
{
    return osck_ScriptPubkey::ref(&osck_TransactionOutput::get(output).scriptPubKey);
}

int64_t osck_transaction_output_get_amount(const osck_TransactionOutput* output)
{
    return osck_TransactionOutput::get(output).nValue;
}

void osck_transaction_output_destroy(osck_TransactionOutput* output)
{
    delete output;
}

int osck_script_pubkey_verify(const osck_ScriptPubkey* script_pubkey,
                              const int64_t amount,
                              const osck_Transaction* tx_to,
                              const osck_TransactionOutput** spent_outputs_, size_t spent_outputs_len,
                              const unsigned int input_index,
                              const osck_ScriptVerificationFlags flags,
                              osck_ScriptVerifyStatus* status)
{
    // Assert that all specified flags are part of the interface before continuing
    assert((flags & ~osck_ScriptVerificationFlags_ALL) == 0);

    if (!is_valid_flag_combination(script_verify_flags::from_int(flags))) {
        if (status) *status = osck_ScriptVerifyStatus_ERROR_INVALID_FLAGS_COMBINATION;
        return 0;
    }

    if (flags & osck_ScriptVerificationFlags_TAPROOT && spent_outputs_ == nullptr) {
        if (status) *status = osck_ScriptVerifyStatus_ERROR_SPENT_OUTPUTS_REQUIRED;
        return 0;
    }

    if (status) *status = osck_ScriptVerifyStatus_OK;

    const CTransaction& tx{*osck_Transaction::get(tx_to)};
    std::vector<CTxOut> spent_outputs;
    if (spent_outputs_ != nullptr) {
        assert(spent_outputs_len == tx.vin.size());
        spent_outputs.reserve(spent_outputs_len);
        for (size_t i = 0; i < spent_outputs_len; i++) {
            const CTxOut& tx_out{osck_TransactionOutput::get(spent_outputs_[i])};
            spent_outputs.push_back(tx_out);
        }
    }

    assert(input_index < tx.vin.size());
    PrecomputedTransactionData txdata{tx};

    if (spent_outputs_ != nullptr && flags & osck_ScriptVerificationFlags_TAPROOT) {
        txdata.Init(tx, std::move(spent_outputs));
    }

    bool result = VerifyScript(tx.vin[input_index].scriptSig,
                               osck_ScriptPubkey::get(script_pubkey),
                               &tx.vin[input_index].scriptWitness,
                               script_verify_flags::from_int(flags),
                               TransactionSignatureChecker(&tx, input_index, amount, txdata, MissingDataBehavior::FAIL),
                               nullptr);
    return result ? 1 : 0;
}

osck_TransactionInput* osck_transaction_input_copy(const osck_TransactionInput* input)
{
    return osck_TransactionInput::copy(input);
}

const osck_TransactionOutPoint* osck_transaction_input_get_out_point(const osck_TransactionInput* input)
{
    return osck_TransactionOutPoint::ref(&osck_TransactionInput::get(input).prevout);
}

void osck_transaction_input_destroy(osck_TransactionInput* input)
{
    delete input;
}

osck_TransactionOutPoint* osck_transaction_out_point_copy(const osck_TransactionOutPoint* out_point)
{
    return osck_TransactionOutPoint::copy(out_point);
}

uint32_t osck_transaction_out_point_get_index(const osck_TransactionOutPoint* out_point)
{
    return osck_TransactionOutPoint::get(out_point).n;
}

const osck_Txid* osck_transaction_out_point_get_txid(const osck_TransactionOutPoint* out_point)
{
    return osck_Txid::ref(&osck_TransactionOutPoint::get(out_point).hash);
}

void osck_transaction_out_point_destroy(osck_TransactionOutPoint* out_point)
{
    delete out_point;
}

osck_Txid* osck_txid_copy(const osck_Txid* txid)
{
    return osck_Txid::copy(txid);
}

void osck_txid_to_bytes(const osck_Txid* txid, unsigned char output[32])
{
    std::memcpy(output, osck_Txid::get(txid).begin(), 32);
}

int osck_txid_equals(const osck_Txid* txid1, const osck_Txid* txid2)
{
    return osck_Txid::get(txid1) == osck_Txid::get(txid2);
}

void osck_txid_destroy(osck_Txid* txid)
{
    delete txid;
}

void osck_logging_set_options(const osck_LoggingOptions options)
{
    LOCK(cs_main);
    LogInstance().m_log_timestamps = options.log_timestamps;
    LogInstance().m_log_time_micros = options.log_time_micros;
    LogInstance().m_log_threadnames = options.log_threadnames;
    LogInstance().m_log_sourcelocations = options.log_sourcelocations;
    LogInstance().m_always_print_category_level = options.always_print_category_levels;
}

void osck_logging_set_level_category(osck_LogCategory category, osck_LogLevel level)
{
    LOCK(cs_main);
    if (category == osck_LogCategory_ALL) {
        LogInstance().SetLogLevel(get_bclog_level(level));
    }

    LogInstance().AddCategoryLogLevel(get_bclog_flag(category), get_bclog_level(level));
}

void osck_logging_enable_category(osck_LogCategory category)
{
    LogInstance().EnableCategory(get_bclog_flag(category));
}

void osck_logging_disable_category(osck_LogCategory category)
{
    LogInstance().DisableCategory(get_bclog_flag(category));
}

void osck_logging_disable()
{
    LogInstance().DisableLogging();
}

osck_LoggingConnection* osck_logging_connection_create(osck_LogCallback callback, void* user_data, osck_DestroyCallback user_data_destroy_callback)
{
    try {
        return osck_LoggingConnection::create(callback, user_data, user_data_destroy_callback);
    } catch (const std::exception&) {
        return nullptr;
    }
}

void osck_logging_connection_destroy(osck_LoggingConnection* connection)
{
    delete connection;
}

osck_ChainParameters* osck_chain_parameters_create(const osck_ChainType chain_type)
{
    switch (chain_type) {
    case osck_ChainType_MAINNET: {
        return osck_ChainParameters::ref(const_cast<CChainParams*>(CChainParams::Main().release()));
    }
    case osck_ChainType_TESTNET: {
        return osck_ChainParameters::ref(const_cast<CChainParams*>(CChainParams::TestNet().release()));
    }
    case osck_ChainType_TESTNET_4: {
        return osck_ChainParameters::ref(const_cast<CChainParams*>(CChainParams::TestNet4().release()));
    }
    case osck_ChainType_SIGNET: {
        return osck_ChainParameters::ref(const_cast<CChainParams*>(CChainParams::SigNet({}).release()));
    }
    case osck_ChainType_REGTEST: {
        return osck_ChainParameters::ref(const_cast<CChainParams*>(CChainParams::RegTest({}).release()));
    }
    }
    assert(false);
}

osck_ChainParameters* osck_chain_parameters_copy(const osck_ChainParameters* chain_parameters)
{
    return osck_ChainParameters::copy(chain_parameters);
}

void osck_chain_parameters_destroy(osck_ChainParameters* chain_parameters)
{
    delete chain_parameters;
}

osck_ContextOptions* osck_context_options_create()
{
    return osck_ContextOptions::create();
}

void osck_context_options_set_chainparams(osck_ContextOptions* options, const osck_ChainParameters* chain_parameters)
{
    // Copy the chainparams, so the caller can free it again
    LOCK(osck_ContextOptions::get(options).m_mutex);
    osck_ContextOptions::get(options).m_chainparams = std::make_unique<const CChainParams>(osck_ChainParameters::get(chain_parameters));
}

void osck_context_options_set_notifications(osck_ContextOptions* options, osck_NotificationInterfaceCallbacks notifications)
{
    // The KernelNotifications are copy-initialized, so the caller can free them again.
    LOCK(osck_ContextOptions::get(options).m_mutex);
    osck_ContextOptions::get(options).m_notifications = std::make_shared<KernelNotifications>(notifications);
}

void osck_context_options_set_validation_interface(osck_ContextOptions* options, osck_ValidationInterfaceCallbacks vi_cbs)
{
    LOCK(osck_ContextOptions::get(options).m_mutex);
    osck_ContextOptions::get(options).m_validation_interface = std::make_shared<KernelValidationInterface>(vi_cbs);
}

void osck_context_options_destroy(osck_ContextOptions* options)
{
    delete options;
}

osck_Context* osck_context_create(const osck_ContextOptions* options)
{
    bool sane{true};
    const ContextOptions* opts = options ? &osck_ContextOptions::get(options) : nullptr;
    auto context{std::make_shared<const Context>(opts, sane)};
    if (!sane) {
        LogError("Kernel context sanity check failed.");
        return nullptr;
    }
    return osck_Context::create(context);
}

osck_Context* osck_context_copy(const osck_Context* context)
{
    return osck_Context::copy(context);
}

int osck_context_interrupt(osck_Context* context)
{
    return (*osck_Context::get(context)->m_interrupt)() ? 0 : -1;
}

void osck_context_destroy(osck_Context* context)
{
    delete context;
}

const osck_BlockTreeEntry* osck_block_tree_entry_get_previous(const osck_BlockTreeEntry* entry)
{
    if (!osck_BlockTreeEntry::get(entry).pprev) {
        LogInfo("Genesis block has no previous.");
        return nullptr;
    }

    return osck_BlockTreeEntry::ref(osck_BlockTreeEntry::get(entry).pprev);
}

osck_ValidationMode osck_block_validation_state_get_validation_mode(const osck_BlockValidationState* block_validation_state_)
{
    auto& block_validation_state = osck_BlockValidationState::get(block_validation_state_);
    if (block_validation_state.IsValid()) return osck_ValidationMode_VALID;
    if (block_validation_state.IsInvalid()) return osck_ValidationMode_INVALID;
    return osck_ValidationMode_INTERNAL_ERROR;
}

osck_BlockValidationResult osck_block_validation_state_get_block_validation_result(const osck_BlockValidationState* block_validation_state_)
{
    auto& block_validation_state = osck_BlockValidationState::get(block_validation_state_);
    switch (block_validation_state.GetResult()) {
    case BlockValidationResult::BLOCK_RESULT_UNSET:
        return osck_BlockValidationResult_UNSET;
    case BlockValidationResult::BLOCK_CONSENSUS:
        return osck_BlockValidationResult_CONSENSUS;
    case BlockValidationResult::BLOCK_CACHED_INVALID:
        return osck_BlockValidationResult_CACHED_INVALID;
    case BlockValidationResult::BLOCK_INVALID_HEADER:
        return osck_BlockValidationResult_INVALID_HEADER;
    case BlockValidationResult::BLOCK_MUTATED:
        return osck_BlockValidationResult_MUTATED;
    case BlockValidationResult::BLOCK_MISSING_PREV:
        return osck_BlockValidationResult_MISSING_PREV;
    case BlockValidationResult::BLOCK_INVALID_PREV:
        return osck_BlockValidationResult_INVALID_PREV;
    case BlockValidationResult::BLOCK_TIME_FUTURE:
        return osck_BlockValidationResult_TIME_FUTURE;
    case BlockValidationResult::BLOCK_HEADER_LOW_WORK:
        return osck_BlockValidationResult_HEADER_LOW_WORK;
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

osck_ChainstateManagerOptions* osck_chainstate_manager_options_create(const osck_Context* context, const char* data_dir, size_t data_dir_len, const char* blocks_dir, size_t blocks_dir_len)
{
    if (data_dir == nullptr || data_dir_len == 0 || blocks_dir == nullptr || blocks_dir_len == 0) {
        LogError("Failed to create chainstate manager options: dir must be non-null and non-empty");
        return nullptr;
    }
    try {
        fs::path abs_data_dir{fs::absolute(fs::PathFromString({data_dir, data_dir_len}))};
        fs::create_directories(abs_data_dir);
        fs::path abs_blocks_dir{fs::absolute(fs::PathFromString({blocks_dir, blocks_dir_len}))};
        fs::create_directories(abs_blocks_dir);
        return osck_ChainstateManagerOptions::create(osck_Context::get(context), abs_data_dir, abs_blocks_dir);
    } catch (const std::exception& e) {
        LogError("Failed to create chainstate manager options: %s", e.what());
        return nullptr;
    }
}

void osck_chainstate_manager_options_set_worker_threads_num(osck_ChainstateManagerOptions* opts, int worker_threads)
{
    LOCK(osck_ChainstateManagerOptions::get(opts).m_mutex);
    osck_ChainstateManagerOptions::get(opts).m_chainman_options.worker_threads_num = worker_threads;
}

void osck_chainstate_manager_options_destroy(osck_ChainstateManagerOptions* options)
{
    delete options;
}

int osck_chainstate_manager_options_set_wipe_dbs(osck_ChainstateManagerOptions* chainman_opts, int wipe_block_tree_db, int wipe_chainstate_db)
{
    if (wipe_block_tree_db == 1 && wipe_chainstate_db != 1) {
        LogError("Wiping the block tree db without also wiping the chainstate db is currently unsupported.");
        return -1;
    }
    auto& opts{osck_ChainstateManagerOptions::get(chainman_opts)};
    LOCK(opts.m_mutex);
    opts.m_blockman_options.block_tree_db_params.wipe_data = wipe_block_tree_db == 1;
    opts.m_chainstate_load_options.wipe_chainstate_db = wipe_chainstate_db == 1;
    return 0;
}

void osck_chainstate_manager_options_update_block_tree_db_in_memory(
    osck_ChainstateManagerOptions* chainman_opts,
    int block_tree_db_in_memory)
{
    auto& opts{osck_ChainstateManagerOptions::get(chainman_opts)};
    LOCK(opts.m_mutex);
    opts.m_blockman_options.block_tree_db_params.memory_only = block_tree_db_in_memory == 1;
}

void osck_chainstate_manager_options_update_chainstate_db_in_memory(
    osck_ChainstateManagerOptions* chainman_opts,
    int chainstate_db_in_memory)
{
    auto& opts{osck_ChainstateManagerOptions::get(chainman_opts)};
    LOCK(opts.m_mutex);
    opts.m_chainstate_load_options.coins_db_in_memory = chainstate_db_in_memory == 1;
}

osck_ChainstateManager* osck_chainstate_manager_create(
    const osck_ChainstateManagerOptions* chainman_opts)
{
    auto& opts{osck_ChainstateManagerOptions::get(chainman_opts)};
    std::unique_ptr<ChainstateManager> chainman;
    try {
        LOCK(opts.m_mutex);
        chainman = std::make_unique<ChainstateManager>(*opts.m_context->m_interrupt, opts.m_chainman_options, opts.m_blockman_options);
    } catch (const std::exception& e) {
        LogError("Failed to create chainstate manager: %s", e.what());
        return nullptr;
    }

    try {
        const auto chainstate_load_opts{WITH_LOCK(opts.m_mutex, return opts.m_chainstate_load_options)};

        kernel::CacheSizes cache_sizes{DEFAULT_KERNEL_CACHE};
        auto [status, chainstate_err]{node::LoadChainstate(*chainman, cache_sizes, chainstate_load_opts)};
        if (status != node::ChainstateLoadStatus::SUCCESS) {
            LogError("Failed to load chain state from your data directory: %s", chainstate_err.original);
            return nullptr;
        }
        std::tie(status, chainstate_err) = node::VerifyLoadedChainstate(*chainman, chainstate_load_opts);
        if (status != node::ChainstateLoadStatus::SUCCESS) {
            LogError("Failed to verify loaded chain state from your datadir: %s", chainstate_err.original);
            return nullptr;
        }

        for (Chainstate* chainstate : WITH_LOCK(chainman->GetMutex(), return chainman->GetAll())) {
            BlockValidationState state;
            if (!chainstate->ActivateBestChain(state, nullptr)) {
                LogError("Failed to connect best block: %s", state.ToString());
                return nullptr;
            }
        }
    } catch (const std::exception& e) {
        LogError("Failed to load chainstate: %s", e.what());
        return nullptr;
    }

    return osck_ChainstateManager::create(std::move(chainman), opts.m_context);
}

const osck_BlockTreeEntry* osck_chainstate_manager_get_block_tree_entry_by_hash(const osck_ChainstateManager* chainman, const osck_BlockHash* block_hash)
{
    auto block_index = WITH_LOCK(osck_ChainstateManager::get(chainman).m_chainman->GetMutex(),
                                 return osck_ChainstateManager::get(chainman).m_chainman->m_blockman.LookupBlockIndex(osck_BlockHash::get(block_hash)));
    if (!block_index) {
        LogDebug(BCLog::KERNEL, "A block with the given hash is not indexed.");
        return nullptr;
    }
    return osck_BlockTreeEntry::ref(block_index);
}

void osck_chainstate_manager_destroy(osck_ChainstateManager* chainman)
{
    {
        LOCK(osck_ChainstateManager::get(chainman).m_chainman->GetMutex());
        for (Chainstate* chainstate : osck_ChainstateManager::get(chainman).m_chainman->GetAll()) {
            if (chainstate->CanFlushToDisk()) {
                chainstate->ForceFlushStateToDisk();
                chainstate->ResetCoinsViews();
            }
        }
    }

    delete chainman;
}

int osck_chainstate_manager_import_blocks(osck_ChainstateManager* chainman, const char** block_file_paths_data, size_t* block_file_paths_lens, size_t block_file_paths_data_len)
{
    try {
        std::vector<fs::path> import_files;
        import_files.reserve(block_file_paths_data_len);
        for (uint32_t i = 0; i < block_file_paths_data_len; i++) {
            if (block_file_paths_data[i] != nullptr) {
                import_files.emplace_back(std::string{block_file_paths_data[i], block_file_paths_lens[i]}.c_str());
            }
        }
        auto& chainman_ref{*osck_ChainstateManager::get(chainman).m_chainman};
        node::ImportBlocks(chainman_ref, import_files);
        WITH_LOCK(::cs_main, chainman_ref.UpdateIBDStatus());
    } catch (const std::exception& e) {
        LogError("Failed to import blocks: %s", e.what());
        return -1;
    }
    return 0;
}

osck_Block* osck_block_create(const void* raw_block, size_t raw_block_length)
{
    if (raw_block == nullptr && raw_block_length != 0) {
        return nullptr;
    }
    auto block{std::make_shared<CBlock>()};

    DataStream stream{std::span{reinterpret_cast<const std::byte*>(raw_block), raw_block_length}};

    try {
        stream >> TX_WITH_WITNESS(*block);
    } catch (...) {
        LogDebug(BCLog::KERNEL, "Block decode failed.");
        return nullptr;
    }

    return osck_Block::create(block);
}

osck_Block* osck_block_copy(const osck_Block* block)
{
    return osck_Block::copy(block);
}

size_t osck_block_count_transactions(const osck_Block* block)
{
    return osck_Block::get(block)->vtx.size();
}

const osck_Transaction* osck_block_get_transaction_at(const osck_Block* block, size_t index)
{
    assert(index < osck_Block::get(block)->vtx.size());
    return osck_Transaction::ref(&osck_Block::get(block)->vtx[index]);
}

int osck_block_to_bytes(const osck_Block* block, osck_WriteBytes writer, void* user_data)
{
    try {
        WriterStream ws{writer, user_data};
        ws << TX_WITH_WITNESS(*osck_Block::get(block));
        return 0;
    } catch (...) {
        return -1;
    }
}

osck_BlockHash* osck_block_get_hash(const osck_Block* block)
{
    return osck_BlockHash::create(osck_Block::get(block)->GetHash());
}

void osck_block_destroy(osck_Block* block)
{
    delete block;
}

osck_Block* osck_block_read(const osck_ChainstateManager* chainman, const osck_BlockTreeEntry* entry)
{
    auto block{std::make_shared<CBlock>()};
    if (!osck_ChainstateManager::get(chainman).m_chainman->m_blockman.ReadBlock(*block, osck_BlockTreeEntry::get(entry))) {
        LogError("Failed to read block.");
        return nullptr;
    }
    return osck_Block::create(block);
}

int32_t osck_block_tree_entry_get_height(const osck_BlockTreeEntry* entry)
{
    return osck_BlockTreeEntry::get(entry).nHeight;
}

const osck_BlockHash* osck_block_tree_entry_get_block_hash(const osck_BlockTreeEntry* entry)
{
    return osck_BlockHash::ref(osck_BlockTreeEntry::get(entry).phashBlock);
}

int osck_block_tree_entry_equals(const osck_BlockTreeEntry* entry1, const osck_BlockTreeEntry* entry2)
{
    return &osck_BlockTreeEntry::get(entry1) == &osck_BlockTreeEntry::get(entry2);
}

osck_BlockHash* osck_block_hash_create(const unsigned char block_hash[32])
{
    return osck_BlockHash::create(std::span<const unsigned char>{block_hash, 32});
}

osck_BlockHash* osck_block_hash_copy(const osck_BlockHash* block_hash)
{
    return osck_BlockHash::copy(block_hash);
}

void osck_block_hash_to_bytes(const osck_BlockHash* block_hash, unsigned char output[32])
{
    std::memcpy(output, osck_BlockHash::get(block_hash).begin(), 32);
}

int osck_block_hash_equals(const osck_BlockHash* hash1, const osck_BlockHash* hash2)
{
    return osck_BlockHash::get(hash1) == osck_BlockHash::get(hash2);
}

void osck_block_hash_destroy(osck_BlockHash* hash)
{
    delete hash;
}

osck_BlockSpentOutputs* osck_block_spent_outputs_read(const osck_ChainstateManager* chainman, const osck_BlockTreeEntry* entry)
{
    auto block_undo{std::make_shared<CBlockUndo>()};
    if (osck_BlockTreeEntry::get(entry).nHeight < 1) {
        LogDebug(BCLog::KERNEL, "The genesis block does not have any spent outputs.");
        return osck_BlockSpentOutputs::create(block_undo);
    }
    if (!osck_ChainstateManager::get(chainman).m_chainman->m_blockman.ReadBlockUndo(*block_undo, osck_BlockTreeEntry::get(entry))) {
        LogError("Failed to read block spent outputs data.");
        return nullptr;
    }
    return osck_BlockSpentOutputs::create(block_undo);
}

osck_BlockSpentOutputs* osck_block_spent_outputs_copy(const osck_BlockSpentOutputs* block_spent_outputs)
{
    return osck_BlockSpentOutputs::copy(block_spent_outputs);
}

size_t osck_block_spent_outputs_count(const osck_BlockSpentOutputs* block_spent_outputs)
{
    return osck_BlockSpentOutputs::get(block_spent_outputs)->vtxundo.size();
}

const osck_TransactionSpentOutputs* osck_block_spent_outputs_get_transaction_spent_outputs_at(const osck_BlockSpentOutputs* block_spent_outputs, size_t transaction_index)
{
    assert(transaction_index < osck_BlockSpentOutputs::get(block_spent_outputs)->vtxundo.size());
    const auto* tx_undo{&osck_BlockSpentOutputs::get(block_spent_outputs)->vtxundo.at(transaction_index)};
    return osck_TransactionSpentOutputs::ref(tx_undo);
}

void osck_block_spent_outputs_destroy(osck_BlockSpentOutputs* block_spent_outputs)
{
    delete block_spent_outputs;
}

osck_TransactionSpentOutputs* osck_transaction_spent_outputs_copy(const osck_TransactionSpentOutputs* transaction_spent_outputs)
{
    return osck_TransactionSpentOutputs::copy(transaction_spent_outputs);
}

size_t osck_transaction_spent_outputs_count(const osck_TransactionSpentOutputs* transaction_spent_outputs)
{
    return osck_TransactionSpentOutputs::get(transaction_spent_outputs).vprevout.size();
}

void osck_transaction_spent_outputs_destroy(osck_TransactionSpentOutputs* transaction_spent_outputs)
{
    delete transaction_spent_outputs;
}

const osck_Coin* osck_transaction_spent_outputs_get_coin_at(const osck_TransactionSpentOutputs* transaction_spent_outputs, size_t coin_index)
{
    assert(coin_index < osck_TransactionSpentOutputs::get(transaction_spent_outputs).vprevout.size());
    const Coin* coin{&osck_TransactionSpentOutputs::get(transaction_spent_outputs).vprevout.at(coin_index)};
    return osck_Coin::ref(coin);
}

osck_Coin* osck_coin_copy(const osck_Coin* coin)
{
    return osck_Coin::copy(coin);
}

uint32_t osck_coin_confirmation_height(const osck_Coin* coin)
{
    return osck_Coin::get(coin).nHeight;
}

int osck_coin_is_coinbase(const osck_Coin* coin)
{
    return osck_Coin::get(coin).IsCoinBase() ? 1 : 0;
}

const osck_TransactionOutput* osck_coin_get_output(const osck_Coin* coin)
{
    return osck_TransactionOutput::ref(&osck_Coin::get(coin).out);
}

void osck_coin_destroy(osck_Coin* coin)
{
    delete coin;
}

int osck_chainstate_manager_process_block(
    osck_ChainstateManager* chainman,
    const osck_Block* block,
    int* _new_block)
{
    bool new_block;
    auto result = osck_ChainstateManager::get(chainman).m_chainman->ProcessNewBlock(osck_Block::get(block), /*force_processing=*/true, /*min_pow_checked=*/true, /*new_block=*/&new_block);
    if (_new_block) {
        *_new_block = new_block ? 1 : 0;
    }
    return result ? 0 : -1;
}

const osck_Chain* osck_chainstate_manager_get_active_chain(const osck_ChainstateManager* chainman)
{
    return osck_Chain::ref(&WITH_LOCK(osck_ChainstateManager::get(chainman).m_chainman->GetMutex(), return osck_ChainstateManager::get(chainman).m_chainman->ActiveChain()));
}

int osck_chain_get_height(const osck_Chain* chain)
{
    LOCK(::cs_main);
    return osck_Chain::get(chain).Height();
}

const osck_BlockTreeEntry* osck_chain_get_by_height(const osck_Chain* chain, int height)
{
    LOCK(::cs_main);
    return osck_BlockTreeEntry::ref(osck_Chain::get(chain)[height]);
}

int osck_chain_contains(const osck_Chain* chain, const osck_BlockTreeEntry* entry)
{
    LOCK(::cs_main);
    return osck_Chain::get(chain).Contains(&osck_BlockTreeEntry::get(entry)) ? 1 : 0;
}
