/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "wifi_state.h"

namespace fs = std::filesystem;

namespace {

class WifiStateTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                (std::string("bship_wifi_") +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(root_);
        fs::create_directories(root_);
    }
    void TearDown() override { fs::remove_all(root_); }

    void add_iface(const std::string& name, const std::string& state, bool wireless_dir) {
        fs::create_directories(root_ / name);
        if (wireless_dir) {
            fs::create_directories(root_ / name / "wireless");
        }
        std::ofstream(root_ / name / "operstate") << state;
    }

    bool connected() const { return platform::wifi_connected(root_.string()); }

    fs::path root_;
};

}  // namespace

TEST_F(WifiStateTest, UpIsConnected) {
    add_iface("wlan0", "up\n", true);
    EXPECT_TRUE(connected());
}

TEST_F(WifiStateTest, DormantIsNotConnected) {
    add_iface("wlan0", "dormant\n", true);
    EXPECT_FALSE(connected());
}

TEST_F(WifiStateTest, DownIsNotConnected) {
    add_iface("wlan0", "down\n", true);
    EXPECT_FALSE(connected());
}

TEST_F(WifiStateTest, NoWirelessInterfaceIsNotConnected) {
    add_iface("lo", "unknown\n", false);
    EXPECT_FALSE(connected());
}

TEST_F(WifiStateTest, WiredInterfaceUpIsNotConnected) {
    add_iface("eth0", "up\n", false);
    EXPECT_FALSE(connected());
}

TEST_F(WifiStateTest, WirelessDirWithoutWlanNameCounts) {
    add_iface("wlp2s0", "up\n", true);
    EXPECT_TRUE(connected());
}

TEST_F(WifiStateTest, MissingRootOrOperstateIsNotConnected) {
    EXPECT_FALSE(platform::wifi_connected((root_ / "nope").string()));
    fs::create_directories(root_ / "wlan0" / "wireless");
    EXPECT_FALSE(connected());
}

TEST_F(WifiStateTest, AnyUpInterfaceWins) {
    add_iface("wlan0", "dormant\n", true);
    add_iface("wlan1", "up\n", true);
    EXPECT_TRUE(connected());
}
