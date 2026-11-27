/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#include "net/lora/cap_power.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

#include <gtest/gtest.h>

using battleship::net::lora::CapPower;
using battleship::net::lora::kCapPowerMarker;

namespace {

namespace fs = std::filesystem;

std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

void write_all(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

class LoraCapPower : public ::testing::Test {
protected:
    void SetUp() override {
        std::random_device rd;
        const auto name = std::string("battleship-cap-power-test-") +
                          ::testing::UnitTest::GetInstance()->current_test_info()->name() +
                          "-" + std::to_string(rd());
        dir_ = fs::temp_directory_path() / name;
        fs::create_directories(dir_);
        brightness_ = dir_ / "brightness";
        marker_ = dir_ / kCapPowerMarker;
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    fs::path dir_;
    fs::path brightness_;
    fs::path marker_;
};

}  // namespace

TEST_F(LoraCapPower, AcquireTurnsOnAndMarks) {
    write_all(brightness_, "0\n");

    CapPower power(brightness_.string(), dir_.string(), 0);
    EXPECT_TRUE(power.acquire());
    EXPECT_TRUE(power.owned());
    EXPECT_EQ(read_all(brightness_).substr(0, 1), "1");
    EXPECT_TRUE(fs::exists(marker_));
}

TEST_F(LoraCapPower, ReleaseOnlyTurnsOffWhenOwned) {
    // Owned: release turns power off and removes the marker.
    write_all(brightness_, "0\n");
    {
        CapPower power(brightness_.string(), dir_.string(), 0);
        ASSERT_TRUE(power.acquire());
        power.release();
        EXPECT_FALSE(power.owned());
    }
    EXPECT_EQ(read_all(brightness_).substr(0, 1), "0");
    EXPECT_FALSE(fs::exists(marker_));

    // Not owned (power on by someone else): release must not touch it.
    write_all(brightness_, "1\n");
    {
        CapPower power(brightness_.string(), dir_.string(), 0);
        power.release();
        EXPECT_FALSE(power.owned());
    }
    EXPECT_EQ(read_all(brightness_).substr(0, 1), "1");
    EXPECT_FALSE(fs::exists(marker_));
}

TEST_F(LoraCapPower, ForeignPowerIsLeftOn) {
    // Power already on with no marker: someone else owns it.
    write_all(brightness_, "1\n");

    CapPower power(brightness_.string(), dir_.string(), 0);
    EXPECT_TRUE(power.acquire());
    EXPECT_FALSE(power.owned());
    EXPECT_EQ(read_all(brightness_).substr(0, 1), "1");
    EXPECT_FALSE(fs::exists(marker_));
}

TEST_F(LoraCapPower, StaleMarkerMeansOurs) {
    // Power on plus our marker left behind by a previous run: we own it.
    write_all(brightness_, "1\n");
    write_all(marker_, "");

    {
        CapPower power(brightness_.string(), dir_.string(), 0);
        EXPECT_TRUE(power.acquire());
        EXPECT_TRUE(power.owned());
        power.release();
    }
    EXPECT_EQ(read_all(brightness_).substr(0, 1), "0");
    EXPECT_FALSE(fs::exists(marker_));
}

TEST_F(LoraCapPower, AcquireFailsWithoutFile) {
    // brightness_ does not exist.
    CapPower power(brightness_.string(), dir_.string(), 0);
    EXPECT_FALSE(power.acquire());
    EXPECT_FALSE(power.owned());
    EXPECT_FALSE(fs::exists(marker_));
    EXPECT_FALSE(fs::exists(brightness_));
}

TEST_F(LoraCapPower, FailedPowerOffKeepsMarkerAndOwnership) {
    write_all(brightness_, "0\n");
    CapPower power(brightness_.string(), dir_.string(), 0);
    ASSERT_TRUE(power.acquire());
    ASSERT_TRUE(power.owned());

    // Make the brightness file read-only so the power-off write fails.
    fs::permissions(brightness_, fs::perms::owner_read, fs::perm_options::replace);
    {
        std::ofstream probe(brightness_, std::ios::app);
        if (probe) {
            // Writable anyway (e.g. running as root): the failure path cannot be exercised.
            fs::permissions(brightness_,
                            fs::perms::owner_read | fs::perms::owner_write,
                            fs::perm_options::replace);
            GTEST_SKIP() << "brightness file stays writable; cannot simulate write failure";
        }
    }

    power.release();
    EXPECT_TRUE(power.owned());
    EXPECT_TRUE(fs::exists(marker_));

    // Restore write access; a retry must now power off and clear the marker.
    fs::permissions(brightness_,
                    fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace);
    power.release();
    EXPECT_FALSE(power.owned());
    EXPECT_FALSE(fs::exists(marker_));
    EXPECT_EQ(read_all(brightness_).substr(0, 1), "0");
}

TEST_F(LoraCapPower, DestructorReleases) {
    write_all(brightness_, "0\n");
    {
        CapPower power(brightness_.string(), dir_.string(), 0);
        ASSERT_TRUE(power.acquire());
        ASSERT_TRUE(fs::exists(marker_));
    }
    EXPECT_EQ(read_all(brightness_).substr(0, 1), "0");
    EXPECT_FALSE(fs::exists(marker_));
}
