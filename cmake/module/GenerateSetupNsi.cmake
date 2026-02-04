# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

function(generate_setup_nsi)
  set(abs_top_srcdir ${PROJECT_SOURCE_DIR})
  set(abs_top_builddir ${PROJECT_BINARY_DIR})
  set(CLIENT_URL ${PROJECT_HOMEPAGE_URL})
  set(CLIENT_TARNAME "opensy")
  set(OPENSY_WRAPPER_NAME "opensy")
  set(OPENSY_GUI_NAME "opensy-qt")
  set(OPENSY_DAEMON_NAME "opensyd")
  set(OPENSY_CLI_NAME "opensy-cli")
  set(OPENSY_TX_NAME "opensy-tx")
  set(OPENSY_WALLET_TOOL_NAME "opensy-wallet")
  set(OPENSY_TEST_NAME "test_opensy")
  set(EXEEXT ${CMAKE_EXECUTABLE_SUFFIX})
  configure_file(${PROJECT_SOURCE_DIR}/share/setup.nsi.in ${PROJECT_BINARY_DIR}/opensy-win64-setup.nsi USE_SOURCE_PERMISSIONS @ONLY)
endfunction()
