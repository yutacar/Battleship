/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "i18n/settings.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace i18n {

namespace {

std::string lang_to_key(Lang lang) {
    switch (lang) {
        case Lang::EN:
            return "en";
        case Lang::JA:
            return "ja";
        case Lang::ZH:
            return "zh";
    }
    return "en";
}

bool key_to_lang(const std::string& key, Lang& out) {
    if (key == "en") {
        out = Lang::EN;
        return true;
    }
    if (key == "ja") {
        out = Lang::JA;
        return true;
    }
    if (key == "zh") {
        out = Lang::ZH;
        return true;
    }
    return false;
}

/// In-memory transport preference. Ble by default (PLAN.md Rev.3: BLE is
/// the product's default connection method); load_settings() overwrites it
/// from the file, save_net_transport_pref() from the UI.
NetTransportPref g_transport_pref = NetTransportPref::Ble;

/// In-memory LoRa antenna confirmation (false until the user confirms once).
bool g_antenna_confirmed = false;

std::string transport_to_key(NetTransportPref pref) {
    switch (pref) {
        case NetTransportPref::Ble:
            return "ble";
        case NetTransportPref::Lora:
            return "lora";
    }
    return "ble";
}

bool key_to_transport(const std::string& key, NetTransportPref& out) {
    if (key == "ble") {
        out = NetTransportPref::Ble;
        return true;
    }
    if (key == "wifi") {
        // WiFi play was removed in 1.1.0; existing users who persisted
        // "transport=wifi" fall back to the default (BLE).
        out = NetTransportPref::Ble;
        return true;
    }
    if (key == "lora") {
        out = NetTransportPref::Lora;
        return true;
    }
    return false;
}

std::string config_dir() {
    // XDG Base Directory convention (used on the CM0 Linux target and any
    // sane Linux desktop); falls back to ~/.config when XDG_CONFIG_HOME is
    // unset, which is XDG's own defined fallback. HOME is checked last as a
    // final safety net for minimal environments (e.g. some CI sandboxes)
    // that define neither -- writes/reads then simply fail silently (see
    // load_settings()/save_language(), both tolerate an unusable path).
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return std::string(xdg) + "/battleship";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::string(home) + "/.config/battleship";
    }
    return {};
}

}  // namespace

std::string settings_file_path() {
    const auto dir = config_dir();
    if (dir.empty()) {
        return {};
    }
    return dir + "/settings.conf";
}

void load_settings() {
    const auto path = settings_file_path();
    if (path.empty()) {
        return;
    }

    std::ifstream file(path);
    if (!file) {
        return;  // no saved settings yet -- stay on the caller's default (English)
    }

    std::string line;
    while (std::getline(file, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        // Strip a possible trailing '\r' (file edited/copied from Windows).
        std::string trimmed_value = value;
        while (!trimmed_value.empty() && (trimmed_value.back() == '\r' || trimmed_value.back() == '\n')) {
            trimmed_value.pop_back();
        }

        if (key == "language") {
            Lang lang;
            if (key_to_lang(trimmed_value, lang)) {
                set_language(lang);
            }
        } else if (key == "transport") {
            NetTransportPref pref;
            if (key_to_transport(trimmed_value, pref)) {
                g_transport_pref = pref;
            }
        } else if (key == "antenna_confirmed") {
            // Only the two values this module writes are accepted; anything
            // else keeps the in-memory default (unconfirmed).
            if (trimmed_value == "1") {
                g_antenna_confirmed = true;
            } else if (trimmed_value == "0") {
                g_antenna_confirmed = false;
            }
        }
        // Unknown keys are ignored -- forward/backward compatible with a
        // settings file written by a future version that adds more keys.
    }
}

namespace {

/// Rewrites settings.conf with EVERY known key's current in-memory value.
/// Both save_*() entry points funnel through here so that persisting one
/// setting never drops the other (the file is written truncate-style).
void write_settings_file() {
    const auto dir = config_dir();
    if (dir.empty()) {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return;  // best-effort persistence; the in-memory value already applied
    }

    const auto path = dir + "/settings.conf";
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        return;
    }
    file << "language=" << lang_to_key(current_language()) << "\n";
    file << "transport=" << transport_to_key(g_transport_pref) << "\n";
    file << "antenna_confirmed=" << (g_antenna_confirmed ? "1" : "0") << "\n";
}

}  // namespace

void save_language(Lang lang) {
    set_language(lang);
    write_settings_file();
}

NetTransportPref net_transport_pref() {
    return g_transport_pref;
}

void set_net_transport_pref(NetTransportPref pref) {
    g_transport_pref = pref;
}

void save_net_transport_pref(NetTransportPref pref) {
    g_transport_pref = pref;
    write_settings_file();
}

bool antenna_confirmed() {
    return g_antenna_confirmed;
}

void set_antenna_confirmed(bool confirmed) {
    g_antenna_confirmed = confirmed;
}

void save_antenna_confirmed(bool confirmed) {
    g_antenna_confirmed = confirmed;
    write_settings_file();
}

}  // namespace i18n
