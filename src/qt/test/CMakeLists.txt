# Copyright (c) 2024-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

set(CMAKE_AUTOMOC_MOC_OPTIONS "-p${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(test_opensy-qt
  apptests.cpp
  optiontests.cpp
  rpcnestedtests.cpp
  test_main.cpp
  uritests.cpp
  util.cpp
  ../../init/opensy-qt.cpp
)
set_target_properties(test_opensy-qt PROPERTIES OUTPUT_NAME test_opensy-qt)
set_target_properties(test_opensy-qt PROPERTIES OUTPUT_NAME test_opensy-qt)

add_windows_application_manifest(test_opensy-qt)

target_link_libraries(test_opensy-qt
  core_interface
  opensyqt
  test_util
  opensy_node
  Boost::headers
  Qt6::Test
)

import_plugins(test_opensy-qt)

if(ENABLE_WALLET)
  target_sources(test_opensy-qt
    PRIVATE
      addressbooktests.cpp
      wallettests.cpp
      ../../wallet/test/wallet_test_fixture.cpp
  )
endif()

add_test(NAME test_opensy-qt
  COMMAND test_opensy-qt
)
if(WIN32 AND VCPKG_TARGET_TRIPLET)
  set(plugin_path "$<SHELL_PATH:$<PATH:GET_PARENT_PATH,$<PATH:GET_PARENT_PATH,$<TARGET_PROPERTY:Qt6::QWindowsIntegrationPlugin,LOCATION_$<CONFIG>>>>>")
  set_property(TEST test_opensy-qt APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
      # On Windows, vcpkg configures Qt with `-opengl dynamic`, which makes
      # the "minimal" platform plugin unusable due to internal Qt bugs.
      QT_QPA_PLATFORM=set:windows
      QT_PLUGIN_PATH=set:${plugin_path}
  )
  unset(plugin_path)
endif()

install_binary_component(test_opensy-qt INTERNAL)
