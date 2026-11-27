// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
//
// Tests for i18n/settings.h -- the flat key=value persistence for the
// Settings screen's language choice (PLAN.md: "設定は設定ファイルに永続化...
// し、起動時に復元。標準（未設定時）は英語"). Redirects XDG_CONFIG_HOME to a
// scratch directory per test so this never touches the real
// ~/.config/battleship on the machine running the tests.

#include "i18n/settings.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using i18n::Lang;

namespace {

class I18nSettingsTest : public ::testing::Test {
protected:
    void SetUp() override {
        scratch_dir_ = std::filesystem::temp_directory_path() /
                       ("battleship_i18n_settings_test_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
                        "_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::filesystem::remove_all(scratch_dir_);
        std::filesystem::create_directories(scratch_dir_);

        if (const char* existing = std::getenv("XDG_CONFIG_HOME")) {
            had_previous_xdg_ = true;
            previous_xdg_ = existing;
        } else {
            had_previous_xdg_ = false;
        }
        setenv("XDG_CONFIG_HOME", scratch_dir_.string().c_str(), 1);

        i18n::set_language(Lang::EN);  // known baseline before each test
        i18n::set_net_transport_pref(i18n::NetTransportPref::Ble);
        i18n::set_antenna_confirmed(false);
    }

    void TearDown() override {
        std::filesystem::remove_all(scratch_dir_);
        if (had_previous_xdg_) {
            setenv("XDG_CONFIG_HOME", previous_xdg_.c_str(), 1);
        } else {
            unsetenv("XDG_CONFIG_HOME");
        }
        i18n::set_language(Lang::EN);  // don't leak state into other test files
        i18n::set_net_transport_pref(i18n::NetTransportPref::Ble);
        i18n::set_antenna_confirmed(false);
    }

    std::filesystem::path scratch_dir_;
    bool had_previous_xdg_ = false;
    std::string previous_xdg_;
};

}  // namespace

TEST_F(I18nSettingsTest, LoadSettingsWithNoFileLeavesDefaultLanguage) {
    i18n::load_settings();
    EXPECT_EQ(i18n::current_language(), Lang::EN);
}

TEST_F(I18nSettingsTest, SaveLanguageThenLoadRestoresIt) {
    i18n::save_language(Lang::JA);
    EXPECT_EQ(i18n::current_language(), Lang::JA);  // save_language also applies it live

    // Simulate a restart: reset the in-memory language, then reload.
    i18n::set_language(Lang::EN);
    i18n::load_settings();
    EXPECT_EQ(i18n::current_language(), Lang::JA);
}

TEST_F(I18nSettingsTest, SaveLanguageCreatesFileUnderConfigDir) {
    i18n::save_language(Lang::ZH);
    const auto path = i18n::settings_file_path();
    ASSERT_FALSE(path.empty());
    EXPECT_TRUE(std::filesystem::exists(path));
}

TEST_F(I18nSettingsTest, OverwritingSavedLanguagePersistsLatestChoice) {
    i18n::save_language(Lang::JA);
    i18n::save_language(Lang::ZH);

    i18n::set_language(Lang::EN);
    i18n::load_settings();
    EXPECT_EQ(i18n::current_language(), Lang::ZH);
}

TEST_F(I18nSettingsTest, LoadSettingsIgnoresUnknownKeysAndMalformedLines) {
    const auto dir = std::filesystem::path(i18n::settings_file_path()).parent_path();
    std::filesystem::create_directories(dir);
    std::ofstream file(i18n::settings_file_path());
    file << "some_future_key=some_value\n";
    file << "not_a_valid_line_no_equals\n";
    file << "language=ja\n";
    file.close();

    i18n::load_settings();
    EXPECT_EQ(i18n::current_language(), Lang::JA);
}

// ---- Phase 6: connection-type preference (PLAN.md "選択は設定に永続化") ----

TEST_F(I18nSettingsTest, TransportPrefDefaultsToBleWithNoFile) {
    i18n::load_settings();
    EXPECT_EQ(i18n::net_transport_pref(), i18n::NetTransportPref::Ble);
}

TEST_F(I18nSettingsTest, SaveTransportPrefThenLoadRestoresIt) {
    i18n::save_net_transport_pref(i18n::NetTransportPref::Wifi);
    EXPECT_EQ(i18n::net_transport_pref(), i18n::NetTransportPref::Wifi);  // applied live

    // Simulate a restart: reset the in-memory value, then reload.
    i18n::set_net_transport_pref(i18n::NetTransportPref::Ble);
    i18n::load_settings();
    EXPECT_EQ(i18n::net_transport_pref(), i18n::NetTransportPref::Wifi);
}

TEST_F(I18nSettingsTest, SavingTransportPreservesLanguageAndViceVersa) {
    // Both save_*() entry points rewrite the whole file; neither may drop
    // the other key (regression guard for the shared settings.conf).
    i18n::save_language(Lang::ZH);
    i18n::save_net_transport_pref(i18n::NetTransportPref::Wifi);
    i18n::save_language(Lang::JA);  // must not clobber transport=wifi

    i18n::set_language(Lang::EN);
    i18n::set_net_transport_pref(i18n::NetTransportPref::Ble);
    i18n::load_settings();
    EXPECT_EQ(i18n::current_language(), Lang::JA);
    EXPECT_EQ(i18n::net_transport_pref(), i18n::NetTransportPref::Wifi);
}

TEST_F(I18nSettingsTest, LoadSettingsIgnoresUnknownTransportValue) {
    const auto dir = std::filesystem::path(i18n::settings_file_path()).parent_path();
    std::filesystem::create_directories(dir);
    std::ofstream file(i18n::settings_file_path());
    file << "transport=carrier_pigeon\n";
    file.close();

    i18n::load_settings();
    EXPECT_EQ(i18n::net_transport_pref(), i18n::NetTransportPref::Ble);  // default kept
}

// ---- Phase 8: LoRa transport preference + antenna confirmation ----

TEST_F(I18nSettingsTest, LoraPrefRoundTrips) {
    i18n::save_net_transport_pref(i18n::NetTransportPref::Lora);
    EXPECT_EQ(i18n::net_transport_pref(), i18n::NetTransportPref::Lora);

    i18n::set_net_transport_pref(i18n::NetTransportPref::Ble);
    i18n::load_settings();
    EXPECT_EQ(i18n::net_transport_pref(), i18n::NetTransportPref::Lora);

    std::ifstream file(i18n::settings_file_path());
    std::stringstream contents;
    contents << file.rdbuf();
    EXPECT_NE(contents.str().find("transport=lora\n"), std::string::npos) << contents.str();
}

TEST_F(I18nSettingsTest, AntennaConfirmedDefaultsToFalse) {
    i18n::load_settings();
    EXPECT_FALSE(i18n::antenna_confirmed());
}

TEST_F(I18nSettingsTest, AntennaConfirmedPersists) {
    i18n::save_antenna_confirmed(true);
    EXPECT_TRUE(i18n::antenna_confirmed());  // applied live

    i18n::set_antenna_confirmed(false);
    i18n::load_settings();
    EXPECT_TRUE(i18n::antenna_confirmed());
}

TEST_F(I18nSettingsTest, AntennaConfirmedSurvivesOtherSaves) {
    // Every save_*() rewrites the whole file; the antenna flag must not be
    // dropped by a later language or transport save (and vice versa).
    i18n::save_antenna_confirmed(true);
    i18n::save_language(Lang::ZH);
    i18n::save_net_transport_pref(i18n::NetTransportPref::Lora);

    i18n::set_antenna_confirmed(false);
    i18n::set_language(Lang::EN);
    i18n::set_net_transport_pref(i18n::NetTransportPref::Ble);
    i18n::load_settings();
    EXPECT_TRUE(i18n::antenna_confirmed());
    EXPECT_EQ(i18n::current_language(), Lang::ZH);
    EXPECT_EQ(i18n::net_transport_pref(), i18n::NetTransportPref::Lora);
}

TEST_F(I18nSettingsTest, UnknownTransportKeyFallsBackToBle) {
    const auto dir = std::filesystem::path(i18n::settings_file_path()).parent_path();
    std::filesystem::create_directories(dir);
    std::ofstream file(i18n::settings_file_path());
    file << "transport=LoRa\n";  // keys are case-sensitive: not "lora"
    file << "antenna_confirmed=maybe\n";
    file.close();

    i18n::load_settings();
    EXPECT_EQ(i18n::net_transport_pref(), i18n::NetTransportPref::Ble);
    EXPECT_FALSE(i18n::antenna_confirmed());
}
