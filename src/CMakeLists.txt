# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

include(AddWindowsResources)

configure_file(${PROJECT_SOURCE_DIR}/cmake/opensy-build-config.h.in opensy-build-config.h USE_SOURCE_PERMISSIONS @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

#=============================
# Subprojects
#=============================
# Subprojects include subdirectories that do or could have tests
# and/or benchmark binaries, such as all subtrees and univalue.
# These need to be included before CMAKE_*_OUTPUT_DIRECTORY variables
# are set, so output locations of subproject tests and libraries are
# not overridden.
include(../cmake/crc32c.cmake)
include(../cmake/leveldb.cmake)
include(../cmake/minisketch.cmake)
if(ENABLE_RANDOMX_POW)
  include(../cmake/randomx.cmake)
endif()
add_subdirectory(univalue)
if (ENABLE_IPC AND NOT WITH_EXTERNAL_LIBMULTIPROCESS)
  include(../cmake/libmultiprocess.cmake)
  add_libmultiprocess(ipc/libmultiprocess)
endif()
include(../cmake/secp256k1.cmake)
add_secp256k1(secp256k1)

# Set top-level target output locations.
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
endif()
if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
endif()
if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
endif()

add_custom_target(generate_build_info
  BYPRODUCTS ${PROJECT_BINARY_DIR}/src/opensy-build-info.h
  COMMAND ${CMAKE_COMMAND} -DBUILD_INFO_HEADER_PATH=${PROJECT_BINARY_DIR}/src/opensy-build-info.h -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/script/GenerateBuildInfo.cmake
  COMMENT "Generating opensy-build-info.h"
  VERBATIM
)
add_library(opensy_clientversion STATIC EXCLUDE_FROM_ALL
  clientversion.cpp
)
target_link_libraries(opensy_clientversion
  PRIVATE
    core_interface
)
add_dependencies(opensy_clientversion generate_build_info)

add_subdirectory(crypto)
add_subdirectory(util)
if(ENABLE_IPC)
  add_subdirectory(ipc)
endif()

add_library(opensy_consensus STATIC EXCLUDE_FROM_ALL
  arith_uint256.cpp
  consensus/merkle.cpp
  consensus/tx_check.cpp
  hash.cpp
  primitives/block.cpp
  primitives/transaction.cpp
  pubkey.cpp
  script/interpreter.cpp
  script/script.cpp
  script/script_error.cpp
  uint256.cpp
)
target_link_libraries(opensy_consensus
  PRIVATE
    core_interface
    opensy_crypto
    secp256k1
)

if(WITH_ZMQ)
  add_subdirectory(zmq)
endif()

# Home for common functionality shared by different executables and libraries.
# Similar to `opensy_util` library, but higher-level.
add_library(opensy_common STATIC EXCLUDE_FROM_ALL
  addresstype.cpp
  base58.cpp
  bech32.cpp
  chain.cpp
  chainparams.cpp
  chainparamsbase.cpp
  coins.cpp
  common/args.cpp
  common/bloom.cpp
  common/config.cpp
  common/init.cpp
  common/interfaces.cpp
  common/messages.cpp
  common/netif.cpp
  common/pcp.cpp
  common/run_command.cpp
  common/settings.cpp
  common/signmessage.cpp
  common/system.cpp
  common/url.cpp
  compressor.cpp
  core_read.cpp
  core_write.cpp
  deploymentinfo.cpp
  external_signer.cpp
  init/common.cpp
  kernel/chainparams.cpp
  key.cpp
  key_io.cpp
  merkleblock.cpp
  musig.cpp
  net_permissions.cpp
  net_types.cpp
  netaddress.cpp
  netbase.cpp
  outputtype.cpp
  policy/feerate.cpp
  policy/policy.cpp
  pow.cpp
  protocol.cpp
  psbt.cpp
  rpc/rawtransaction_util.cpp
  rpc/request.cpp
  rpc/util.cpp
  scheduler.cpp
  script/descriptor.cpp
  script/miniscript.cpp
  script/parsing.cpp
  script/sign.cpp
  script/signingprovider.cpp
  script/solver.cpp
  $<$<BOOL:${ENABLE_SRC20_TOKENS}>:script/src20.cpp>
)
target_link_libraries(opensy_common
  PRIVATE
    core_interface
    opensy_consensus
    opensy_util
    univalue
    secp256k1
    Boost::headers
    $<TARGET_NAME_IF_EXISTS:USDT::headers>
    $<$<PLATFORM_ID:Windows>:ws2_32>
)

include(InstallBinaryComponent)

if(ENABLE_WALLET)
  add_subdirectory(wallet)

  if(BUILD_WALLET_TOOL)
    add_executable(opensy-wallet
      opensy-wallet.cpp
      init/opensy-wallet.cpp
      wallet/wallettool.cpp
    )
    set_target_properties(opensy-wallet PROPERTIES OUTPUT_NAME opensy-wallet)
    set_target_properties(opensy-wallet PROPERTIES OUTPUT_NAME opensy-wallet)
    add_windows_resources(opensy-wallet opensy-wallet-res.rc)
    add_windows_application_manifest(opensy-wallet)
    target_link_libraries(opensy-wallet
      core_interface
      opensy_wallet
      opensy_common
      opensy_util
      Boost::headers
    )
    install_binary_component(opensy-wallet HAS_MANPAGE)
  endif()
endif()


# P2P and RPC server functionality used by `opensyd` and `opensy-qt` executables.
add_library(opensy_node STATIC EXCLUDE_FROM_ALL
  addrdb.cpp
  addrman.cpp
  banman.cpp
  bip324.cpp
  blockencodings.cpp
  blockfilter.cpp
  consensus/tx_verify.cpp
  dbwrapper.cpp
  deploymentstatus.cpp
  flatfile.cpp
  headerssync.cpp
  httprpc.cpp
  httpserver.cpp
  i2p.cpp
  index/base.cpp
  index/blockfilterindex.cpp
  index/coinstatsindex.cpp
  index/txindex.cpp
  init.cpp
  kernel/chain.cpp
  kernel/checks.cpp
  kernel/coinstats.cpp
  kernel/context.cpp
  kernel/cs_main.cpp
  kernel/disconnected_transactions.cpp
  kernel/mempool_removal_reason.cpp
  mapport.cpp
  net.cpp
  net_processing.cpp
  netgroup.cpp
  node/abort.cpp
  node/blockmanager_args.cpp
  node/blockstorage.cpp
  node/caches.cpp
  node/chainstate.cpp
  node/chainstatemanager_args.cpp
  node/coin.cpp
  node/coins_view_args.cpp
  node/connection_types.cpp
  node/context.cpp
  node/database_args.cpp
  node/eviction.cpp
  node/interface_ui.cpp
  node/interfaces.cpp
  node/kernel_notifications.cpp
  node/mempool_args.cpp
  node/mempool_persist.cpp
  node/mempool_persist_args.cpp
  node/miner.cpp
  node/mini_miner.cpp
  node/minisketchwrapper.cpp
  node/peerman_args.cpp
  node/psbt.cpp
  node/timeoffsets.cpp
  node/transaction.cpp
  node/txdownloadman_impl.cpp
  node/txorphanage.cpp
  node/txreconciliation.cpp
  node/utxo_snapshot.cpp
  node/warnings.cpp
  noui.cpp
  policy/ephemeral_policy.cpp
  policy/fees/block_policy_estimator.cpp
  policy/fees/block_policy_estimator_args.cpp
  policy/packages.cpp
  policy/rbf.cpp
  policy/settings.cpp
  policy/truc_policy.cpp
  rest.cpp
  rpc/blockchain.cpp
  rpc/external_signer.cpp
  rpc/fees.cpp
  rpc/mempool.cpp
  rpc/mining.cpp
  rpc/net.cpp
  rpc/node.cpp
  rpc/output_script.cpp
  rpc/rawtransaction.cpp
  rpc/server.cpp
  rpc/server_util.cpp
  rpc/signmessage.cpp
  $<$<BOOL:${ENABLE_SRC20_TOKENS}>:rpc/tokens.cpp>
  rpc/txoutproof.cpp
  script/sigcache.cpp
  signet.cpp
  $<$<BOOL:${ENABLE_SRC20_TOKENS}>:tokens/tokendb.cpp>
  $<$<BOOL:${ENABLE_SRC20_TOKENS}>:tokens/tokennotifications.cpp>
  $<$<BOOL:${ENABLE_SRC20_TOKENS}>:tokens/tokenvalidation.cpp>
  torcontrol.cpp
  txdb.cpp
  txgraph.cpp
  txmempool.cpp
  txrequest.cpp
  validation.cpp
  validationinterface.cpp
  versionbits.cpp
  $<$<TARGET_EXISTS:opensy_wallet>:wallet/init.cpp>
  $<$<NOT:$<TARGET_EXISTS:opensy_wallet>>:dummywallet.cpp>
)
target_link_libraries(opensy_node
  PRIVATE
    core_interface
    opensy_common
    opensy_util
    $<$<BOOL:${ENABLE_RANDOMX_POW}>:randomx>
    $<TARGET_NAME_IF_EXISTS:opensy_zmq>
    leveldb
    minisketch
    univalue
    Boost::headers
    $<TARGET_NAME_IF_EXISTS:libevent::core>
    $<TARGET_NAME_IF_EXISTS:libevent::extra>
    $<TARGET_NAME_IF_EXISTS:libevent::pthreads>
    $<TARGET_NAME_IF_EXISTS:USDT::headers>
)

# Propagate feature flags as compile definitions
if(ENABLE_SRC20_TOKENS)
  target_compile_definitions(opensy_node PRIVATE ENABLE_SRC20_TOKENS)
  target_compile_definitions(opensy_common PRIVATE ENABLE_SRC20_TOKENS)
endif()
if(ENABLE_RANDOMX_POW)
  target_compile_definitions(opensy_node PRIVATE ENABLE_RANDOMX_POW)
  target_compile_definitions(opensy_common PRIVATE ENABLE_RANDOMX_POW)
endif()

# OpenSY wrapper executable that can call other executables.
if(BUILD_OPENSY_BIN)
  add_executable(opensy opensy.cpp)
  set_target_properties(opensy PROPERTIES OUTPUT_NAME opensy)
  set_target_properties(opensy PROPERTIES OUTPUT_NAME opensy)
  add_windows_resources(opensy opensy-res.rc)
  add_windows_application_manifest(opensy)
  target_link_libraries(opensy core_interface opensy_common opensy_util)
  install_binary_component(opensy HAS_MANPAGE)
endif()

# OpenSY opensyd.
if(BUILD_DAEMON)
  add_executable(opensyd
    opensyd.cpp
    init/opensyd.cpp
  )
    set_target_properties(opensyd PROPERTIES OUTPUT_NAME opensyd)
    set_target_properties(opensyd PROPERTIES OUTPUT_NAME opensyd)
  add_windows_resources(opensyd opensyd-res.rc)
  add_windows_application_manifest(opensyd)
  target_link_libraries(opensyd
    core_interface
    opensy_node
    $<TARGET_NAME_IF_EXISTS:opensy_wallet>
  )
  install_binary_component(opensyd HAS_MANPAGE)
endif()
if(ENABLE_IPC AND BUILD_DAEMON)
  add_executable(opensy-node
    opensyd.cpp
    init/opensy-node.cpp
  )
    set_target_properties(opensy-node PROPERTIES OUTPUT_NAME opensy-node)
    set_target_properties(opensy-node PROPERTIES OUTPUT_NAME opensy-node)
  target_link_libraries(opensy-node
    core_interface
    opensy_node
    opensy_ipc
    $<TARGET_NAME_IF_EXISTS:opensy_wallet>
  )
  install_binary_component(opensy-node INTERNAL)
endif()

if(ENABLE_IPC AND BUILD_TESTS)
    # opensy_ipc_test library target is defined here in src/CMakeLists.txt
    # instead of src/test/CMakeLists.txt so capnp files in src/test/ are able to
    # reference capnp files in src/ipc/capnp/ by relative path. The Cap'n Proto
    # compiler only allows importing by relative path when the importing and
    # imported files are underneath the same compilation source prefix, so the
    # source prefix must be src/, not src/test/
    add_library(opensy_ipc_test STATIC EXCLUDE_FROM_ALL
      test/ipc_test.cpp
    )
    target_capnp_sources(opensy_ipc_test ${PROJECT_SOURCE_DIR}
      test/ipc_test.capnp
    )
    add_dependencies(opensy_ipc_test opensy_ipc_headers)
endif()


add_library(opensy_cli STATIC EXCLUDE_FROM_ALL
  compat/stdin.cpp
  rpc/client.cpp
)
target_link_libraries(opensy_cli
  PUBLIC
    core_interface
    univalue
)


# OpenSY RPC client
if(BUILD_CLI)
  add_executable(opensy-cli opensy-cli.cpp)
  set_target_properties(opensy-cli PROPERTIES OUTPUT_NAME opensy-cli)
  set_target_properties(opensy-cli PROPERTIES OUTPUT_NAME opensy-cli)
  add_windows_resources(opensy-cli opensy-cli-res.rc)
  add_windows_application_manifest(opensy-cli)
  target_link_libraries(opensy-cli
    core_interface
    opensy_cli
    opensy_common
    opensy_util
    libevent::core
    libevent::extra
  )
  install_binary_component(opensy-cli HAS_MANPAGE)
endif()


if(BUILD_TX)
  add_executable(opensy-tx opensy-tx.cpp)
  set_target_properties(opensy-tx PROPERTIES OUTPUT_NAME opensy-tx)
  set_target_properties(opensy-tx PROPERTIES OUTPUT_NAME opensy-tx)
  add_windows_resources(opensy-tx opensy-tx-res.rc)
  add_windows_application_manifest(opensy-tx)
  target_link_libraries(opensy-tx
    core_interface
    opensy_common
    opensy_util
    univalue
  )
  install_binary_component(opensy-tx HAS_MANPAGE)
endif()


if(BUILD_UTIL)
  add_executable(opensy-util opensy-util.cpp)
  set_target_properties(opensy-util PROPERTIES OUTPUT_NAME opensy-util)
  set_target_properties(opensy-util PROPERTIES OUTPUT_NAME opensy-util)
  add_windows_resources(opensy-util opensy-util-res.rc)
  add_windows_application_manifest(opensy-util)
  target_link_libraries(opensy-util
    core_interface
    opensy_common
    opensy_util
  )
  install_binary_component(opensy-util HAS_MANPAGE)
endif()


if(BUILD_GUI)
  add_subdirectory(qt)
endif()


if(BUILD_KERNEL_LIB)
  add_subdirectory(kernel)
  if (BUILD_KERNEL_TEST)
    add_subdirectory(test/kernel)
  endif()
  if(BUILD_UTIL_CHAINSTATE)
    add_executable(opensy-chainstate
      opensy-chainstate.cpp
    )
    set_target_properties(opensy-chainstate PROPERTIES OUTPUT_NAME opensy-chainstate)
    set_target_properties(opensy-chainstate PROPERTIES OUTPUT_NAME opensy-chainstate)
    add_windows_application_manifest(opensy-chainstate)
    target_link_libraries(opensy-chainstate
      PRIVATE
        core_interface
        opensykernel
    )
    install_binary_component(opensy-chainstate INTERNAL)
  endif()
endif()


add_subdirectory(test/util)
if(BUILD_BENCH)
  add_subdirectory(bench)
endif()

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

if(BUILD_FUZZ_BINARY)
  add_subdirectory(test/fuzz)
endif()
