#
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT
#
# Unit tests for battleship_core. Desktop-only (see parent CMakeLists.txt);
# GoogleTest is fetched the same way LVGL is fetched in the top-level file.

include(FetchContent)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(googletest)

add_executable(battleship_core_tests
    test_board.cpp
    test_tracking_board.cpp
    test_fleet.cpp
    test_game.cpp
    test_player_view.cpp
    test_snapshot.cpp
    test_ai.cpp
    test_snapshot_summary.cpp
    test_template_commentator.cpp
    test_net_frame.cpp
    test_net_reliable_link.cpp
    test_net_session.cpp
    test_net_tcp.cpp
    test_lora_policy.cpp
    test_lora_packet.cpp
    test_lora_link.cpp
    support/fake_radio_bus.cpp
    test_i18n_catalog.cpp
    test_i18n_settings.cpp
)

target_include_directories(battleship_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(battleship_core_tests
    PRIVATE
        battleship_core
        battleship_commentary
        battleship_net
        battleship_i18n
        GTest::gtest
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(battleship_core_tests)
