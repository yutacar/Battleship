/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Linux/BlueZ implementation of BleTransport/BleScanner over sd-bus
 * (libsystemd). Only compiled when the CMake option BATTLESHIP_BLE is ON --
 * see ble_transport.h for the design overview and NOTES.md Phase 6 for the
 * runtime prerequisites (bluetoothd >= 5.x, user in the `bluetooth` group or
 * equivalent D-Bus policy).
 *
 * sd-bus was chosen over GLib/GDBus (PLAN.md Phase 6): it ships with
 * Raspberry Pi OS / Debian as part of libsystemd (already present on the
 * device for the init system, zero new runtime dependencies), has a plain C
 * API without a GObject type system or a GMainLoop requirement, and its
 * explicit sd_bus_process() pump maps 1:1 onto the ITransport poll() model
 * this codebase is built around -- no glue thread, no event-loop adapter.
 */
#include "net/ble/ble_transport.h"

#include "net/ble/ble_handshake_policy.h"
#include "net/messages.h"  // kBleSingleWriteLimit (single write/notify invariant)

#include <systemd/sd-bus.h>

#include <cstring>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace battleship::net {

namespace {

constexpr const char* kBluez = "org.bluez";
constexpr const char* kAdapterIface = "org.bluez.Adapter1";
constexpr const char* kDeviceIface = "org.bluez.Device1";
constexpr const char* kGattServiceIface = "org.bluez.GattService1";
constexpr const char* kGattCharIface = "org.bluez.GattCharacteristic1";
constexpr const char* kGattManagerIface = "org.bluez.GattManager1";
constexpr const char* kAdvManagerIface = "org.bluez.LEAdvertisingManager1";
constexpr const char* kAdvIface = "org.bluez.LEAdvertisement1";
constexpr const char* kPropsIface = "org.freedesktop.DBus.Properties";
constexpr const char* kAgentManagerIface = "org.bluez.AgentManager1";
constexpr const char* kAgentIface = "org.bluez.Agent1";
constexpr const char* kAgentPath = "/com/battleship/agent";

// Our exported object hierarchy (host/peripheral role).
constexpr const char* kAppRoot = "/com/battleship/gatt";
constexpr const char* kServicePath = "/com/battleship/gatt/service0";
constexpr const char* kRxCharPath = "/com/battleship/gatt/service0/char_rx";
constexpr const char* kTxCharPath = "/com/battleship/gatt/service0/char_tx";
constexpr const char* kAdvPath = "/com/battleship/advertisement0";

/// Monotonic milliseconds (the transport's poll() carries no clock).
uint64_t monotonic_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec) / 1000000u;
}

/// Drains all pending D-Bus traffic without blocking. Every incoming method
/// call (WriteValue/StartNotify/...), async reply and signal is dispatched
/// from here -- i.e. from poll() or from the setup entry points.
void pump(sd_bus* bus) {
    if (!bus) return;
    while (sd_bus_process(bus, nullptr) > 0) {
    }
}

/// Blocking (setup-time only): finds the object path of the first
/// org.bluez.Adapter1 on the system bus. Empty string if none/bluetoothd
/// unreachable.
std::string find_adapter(sd_bus* bus) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    std::string adapter;

    const int rc = sd_bus_call_method(bus, kBluez, "/", "org.freedesktop.DBus.ObjectManager",
                                      "GetManagedObjects", &error, &reply, "");
    if (rc < 0) {
        sd_bus_error_free(&error);
        return adapter;
    }

    // Signature: a{oa{sa{sv}}}
    if (sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}") > 0) {
        while (sd_bus_message_enter_container(reply, 'e', "oa{sa{sv}}") > 0) {
            const char* path = nullptr;
            sd_bus_message_read_basic(reply, 'o', &path);
            sd_bus_message_enter_container(reply, 'a', "{sa{sv}}");
            while (sd_bus_message_enter_container(reply, 'e', "sa{sv}") > 0) {
                const char* iface = nullptr;
                sd_bus_message_read_basic(reply, 's', &iface);
                if (adapter.empty() && iface && std::strcmp(iface, kAdapterIface) == 0 && path) {
                    adapter = path;
                }
                sd_bus_message_skip(reply, "a{sv}");
                sd_bus_message_exit_container(reply);
            }
            sd_bus_message_exit_container(reply);
            sd_bus_message_exit_container(reply);
        }
        sd_bus_message_exit_container(reply);
    }
    sd_bus_message_unref(reply);
    return adapter;
}

/// Blocking (setup-time only): tries Adapter1.Powered = true, then reads it
/// back and returns the actual state. The set can fail (e.g. rfkill
/// soft-block: bluetoothd logs "Failed to set mode: Failed (0x03)"), and BlueZ
/// still ACCEPTS RegisterAdvertisement / StartDiscovery while the adapter is
/// off, silently advertising/scanning nothing -- so callers must check the
/// returned state instead of relying on the later calls to fail.
bool power_on_adapter(sd_bus* bus, const std::string& adapter) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_set_property(bus, kBluez, adapter.c_str(), kAdapterIface, "Powered", &error, "b", 1);
    sd_bus_error_free(&error);

    int powered = 0;
    sd_bus_error get_error = SD_BUS_ERROR_NULL;
    const int rc = sd_bus_get_property_trivial(bus, kBluez, adapter.c_str(), kAdapterIface,
                                               "Powered", &get_error, 'b', &powered);
    sd_bus_error_free(&get_error);
    // If the property cannot be read, don't claim "off": let the normal
    // register/discovery path decide.
    return rc < 0 || powered != 0;
}

/// Reads an `ay` byte array out of `m` into `out`. Returns >= 0 on success.
int read_byte_array(sd_bus_message* m, std::vector<uint8_t>& out) {
    const void* data = nullptr;
    size_t len = 0;
    const int rc = sd_bus_message_read_array(m, 'y', &data, &len);
    if (rc >= 0 && data && len > 0) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        out.insert(out.end(), bytes, bytes + len);
    }
    return rc;
}

bool starts_with(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

}  // namespace

// ====================================================================
// BleScanner
// ====================================================================

struct BleScanner::Impl {
    sd_bus* bus = nullptr;
    std::string adapter;
    bool discovering = false;
    uint32_t last_enumerate_ms = 0;
    bool enumerate_in_flight = false;
    bool clock_initialized = false;
    std::vector<BleScanResult> results;

    ~Impl() { shutdown(); }

    void shutdown() {
        if (bus) {
            if (discovering) {
                // Best-effort; fire the call and flush it out before closing.
                sd_bus_call_method_async(bus, nullptr, kBluez, adapter.c_str(), kAdapterIface,
                                         "StopDiscovery", nullptr, nullptr, "");
                sd_bus_flush(bus);
            }
            sd_bus_unref(bus);  // floating slots (matches/async replies) die with the bus
            bus = nullptr;
        }
        discovering = false;
        enumerate_in_flight = false;
        results.clear();
    }

    static int enumerate_reply_cb(sd_bus_message* reply, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        self->enumerate_in_flight = false;
        if (sd_bus_message_is_method_error(reply, nullptr)) {
            return 0;  // transient; retried on the next interval
        }

        std::vector<BleScanResult> found;
        // Signature: a{oa{sa{sv}}}
        if (sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}") <= 0) {
            return 0;
        }
        while (sd_bus_message_enter_container(reply, 'e', "oa{sa{sv}}") > 0) {
            const char* path = nullptr;
            sd_bus_message_read_basic(reply, 'o', &path);

            std::string name;
            std::string address;
            bool is_device = false;
            bool has_nus_uuid = false;

            sd_bus_message_enter_container(reply, 'a', "{sa{sv}}");
            while (sd_bus_message_enter_container(reply, 'e', "sa{sv}") > 0) {
                const char* iface = nullptr;
                sd_bus_message_read_basic(reply, 's', &iface);
                if (iface && std::strcmp(iface, kDeviceIface) == 0) {
                    is_device = true;
                    sd_bus_message_enter_container(reply, 'a', "{sv}");
                    while (sd_bus_message_enter_container(reply, 'e', "sv") > 0) {
                        const char* key = nullptr;
                        sd_bus_message_read_basic(reply, 's', &key);
                        if (key && std::strcmp(key, "Name") == 0) {
                            const char* value = nullptr;
                            if (sd_bus_message_enter_container(reply, 'v', "s") > 0) {
                                sd_bus_message_read_basic(reply, 's', &value);
                                sd_bus_message_exit_container(reply);
                                if (value) name = value;
                            } else {
                                sd_bus_message_skip(reply, "v");
                            }
                        } else if (key && std::strcmp(key, "Address") == 0) {
                            const char* value = nullptr;
                            if (sd_bus_message_enter_container(reply, 'v', "s") > 0) {
                                sd_bus_message_read_basic(reply, 's', &value);
                                sd_bus_message_exit_container(reply);
                                if (value) address = value;
                            } else {
                                sd_bus_message_skip(reply, "v");
                            }
                        } else if (key && std::strcmp(key, "UUIDs") == 0) {
                            if (sd_bus_message_enter_container(reply, 'v', "as") > 0) {
                                sd_bus_message_enter_container(reply, 'a', "s");
                                const char* uuid = nullptr;
                                while (sd_bus_message_read_basic(reply, 's', &uuid) > 0) {
                                    if (uuid && std::strcmp(uuid, kNusServiceUuid) == 0) {
                                        has_nus_uuid = true;
                                    }
                                }
                                sd_bus_message_exit_container(reply);
                                sd_bus_message_exit_container(reply);
                            } else {
                                sd_bus_message_skip(reply, "v");
                            }
                        } else {
                            sd_bus_message_skip(reply, "v");
                        }
                        sd_bus_message_exit_container(reply);
                    }
                    sd_bus_message_exit_container(reply);
                } else {
                    sd_bus_message_skip(reply, "a{sv}");
                }
                sd_bus_message_exit_container(reply);
            }
            sd_bus_message_exit_container(reply);
            sd_bus_message_exit_container(reply);

            // A Battleship host is recognized by advertised name prefix OR by
            // the NUS service UUID (the 31-byte ADV payload cannot always
            // carry both the 128-bit UUID and the full name, so either
            // signal alone must suffice).
            if (is_device && path && (starts_with(name, kBleNamePrefix) || has_nus_uuid)) {
                BleScanResult result;
                result.device_path = path;
                result.address = address;
                result.name = name.empty() ? address : name;
                found.push_back(std::move(result));
            }
        }
        sd_bus_message_exit_container(reply);

        self->results = std::move(found);
        return 0;
    }
};

BleScanner::BleScanner() : impl_(new Impl) {}
BleScanner::~BleScanner() = default;

bool BleScanner::start() {
    stop();
    start_error_ = BleStartError::None;
    if (sd_bus_open_system(&impl_->bus) < 0) {
        impl_->bus = nullptr;
        return false;
    }
    impl_->adapter = find_adapter(impl_->bus);
    if (impl_->adapter.empty()) {
        impl_->shutdown();
        start_error_ = BleStartError::Other;
        return false;
    }
    if (!power_on_adapter(impl_->bus, impl_->adapter)) {
        impl_->shutdown();
        start_error_ = BleStartError::AdapterOff;
        return false;
    }

    // LE-only discovery filter. Duplicates are fine (we re-enumerate the
    // device list rather than consuming per-advert events). Load-bearing on
    // a dual-mode adapter: a Transport filter makes BlueZ run "filtered
    // discovery", which reports LE adverts even without the General
    // Discoverable bit -- and our host advertises with Discoverable=false
    // (Flags 0x00/0x04, see adv_discoverable_cb) so joiners connect over LE.
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_call_method(impl_->bus, kBluez, impl_->adapter.c_str(), kAdapterIface,
                       "SetDiscoveryFilter", &error, nullptr, "a{sv}", 1, "Transport", "s", "le");
    sd_bus_error_free(&error);

    sd_bus_error start_error = SD_BUS_ERROR_NULL;
    const int rc = sd_bus_call_method(impl_->bus, kBluez, impl_->adapter.c_str(), kAdapterIface,
                                      "StartDiscovery", &start_error, nullptr, "");
    // "InProgress" (another app already scanning) still surfaces devices via
    // the shared BlueZ cache, so treat it as success.
    const bool in_progress =
        sd_bus_error_has_name(&start_error, "org.bluez.Error.InProgress") != 0;
    sd_bus_error_free(&start_error);
    if (rc < 0 && !in_progress) {
        impl_->shutdown();
        return false;
    }
    impl_->discovering = true;
    return true;
}

void BleScanner::poll(uint32_t now_ms) {
    if (!impl_->bus || !impl_->discovering) {
        return;
    }
    pump(impl_->bus);

    if (!impl_->clock_initialized) {
        impl_->clock_initialized = true;
        impl_->last_enumerate_ms = now_ms - 1000;  // enumerate immediately on first poll
    }
    if (!impl_->enumerate_in_flight && now_ms - impl_->last_enumerate_ms >= 1000) {
        impl_->last_enumerate_ms = now_ms;
        const int rc = sd_bus_call_method_async(
            impl_->bus, nullptr, kBluez, "/", "org.freedesktop.DBus.ObjectManager",
            "GetManagedObjects", &Impl::enumerate_reply_cb, impl_.get(), "");
        impl_->enumerate_in_flight = rc >= 0;
    }
    pump(impl_->bus);
}

const std::vector<BleScanResult>& BleScanner::results() const {
    return impl_->results;
}

void BleScanner::stop() {
    impl_->shutdown();
}

// ====================================================================
// BleTransport
// ====================================================================

struct BleTransport::Impl {
    enum class Role { None, Host, Join };

    sd_bus* bus = nullptr;
    std::string adapter;
    Role role = Role::None;
    TransportState state = TransportState::Disconnected;

    // ---- host (peripheral) ----
    std::string local_name;              ///< advertised "BSHIP-<host>"
    std::vector<uint8_t> tx_value;       ///< payload of the notification being emitted
    bool notify_enabled = false;         ///< central subscribed to TX
    bool app_registered = false;
    bool adv_registered = false;
    bool agent_registered = false;       ///< RegisterAgent sent (both roles)

    // ---- join (central) ----
    std::string device_path;
    std::string rx_char_path;            ///< peer's RX char: we WriteValue here
    std::string tx_char_path;            ///< peer's TX char: we get notified here
    bool services_resolved = false;
    bool resolve_query_in_flight = false;
    bool char_query_in_flight = false;
    bool start_notify_sent = false;
    uint64_t last_resolve_poll_us = 0;
    uint64_t start_ms = 0;               ///< start_join() time (handshake timeout)
    bool link_confirmed = false;         ///< Connect reply ok or Connected=true seen

    std::vector<uint8_t> recv_buffer;

    ~Impl() { shutdown(); }

    void fail() {
        if (state != TransportState::Disconnected && bus && role == Role::Join &&
            !device_path.empty()) {
            // Best-effort teardown of a pending/half-up link (no wait).
            sd_bus_call_method_async(bus, nullptr, kBluez, device_path.c_str(), kDeviceIface,
                                     "Disconnect", nullptr, nullptr, "");
        }
        state = TransportState::Disconnected;
    }

    void shutdown() {
        if (bus) {
            if (role == Role::Join && !device_path.empty()) {
                sd_bus_call_method_async(bus, nullptr, kBluez, device_path.c_str(), kDeviceIface,
                                         "Disconnect", nullptr, nullptr, "");
            }
            if (role == Role::Host && !adapter.empty()) {
                if (adv_registered) {
                    sd_bus_call_method_async(bus, nullptr, kBluez, adapter.c_str(),
                                             kAdvManagerIface, "UnregisterAdvertisement", nullptr,
                                             nullptr, "o", kAdvPath);
                }
                if (app_registered) {
                    sd_bus_call_method_async(bus, nullptr, kBluez, adapter.c_str(),
                                             kGattManagerIface, "UnregisterApplication", nullptr,
                                             nullptr, "o", kAppRoot);
                }
            }
            if (agent_registered) {
                sd_bus_call_method_async(bus, nullptr, kBluez, "/org/bluez", kAgentManagerIface,
                                         "UnregisterAgent", nullptr, nullptr, "o", kAgentPath);
            }
            sd_bus_flush(bus);
            sd_bus_unref(bus);  // floating slots (vtables/matches/async) die with the bus
            bus = nullptr;
        }
        role = Role::None;
        state = TransportState::Disconnected;
        notify_enabled = false;
        app_registered = false;
        adv_registered = false;
        agent_registered = false;
        services_resolved = false;
        resolve_query_in_flight = false;
        char_query_in_flight = false;
        start_notify_sent = false;
        start_ms = 0;
        link_confirmed = false;
        device_path.clear();
        rx_char_path.clear();
        tx_char_path.clear();
        tx_value.clear();
        // recv_buffer intentionally kept: bytes already received stay
        // drainable after close() (poll()/recv() still hand it out).
    }

    // ---------------- host: GATT server vtable handlers ----------------

    static int prop_string_cb(sd_bus_message* reply, const char* value) {
        return sd_bus_message_append(reply, "s", value);
    }

    static int service_uuid_cb(sd_bus*, const char*, const char*, const char*,
                               sd_bus_message* reply, void*, sd_bus_error*) {
        return prop_string_cb(reply, kNusServiceUuid);
    }

    static int service_primary_cb(sd_bus*, const char*, const char*, const char*,
                                  sd_bus_message* reply, void*, sd_bus_error*) {
        return sd_bus_message_append(reply, "b", 1);
    }

    static int rx_uuid_cb(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply,
                          void*, sd_bus_error*) {
        return prop_string_cb(reply, kNusRxCharUuid);
    }

    static int tx_uuid_cb(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply,
                          void*, sd_bus_error*) {
        return prop_string_cb(reply, kNusTxCharUuid);
    }

    static int char_service_cb(sd_bus*, const char*, const char*, const char*,
                               sd_bus_message* reply, void*, sd_bus_error*) {
        return sd_bus_message_append(reply, "o", kServicePath);
    }

    static int rx_flags_cb(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply,
                           void*, sd_bus_error*) {
        // Both write shapes: the central uses write-without-response
        // ("command") for latency; plain write is the interoperable
        // fallback.
        const char* flags[] = {"write", "write-without-response", nullptr};
        return sd_bus_message_append_strv(reply, const_cast<char**>(flags));
    }

    static int tx_flags_cb(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply,
                           void*, sd_bus_error*) {
        const char* flags[] = {"notify", nullptr};
        return sd_bus_message_append_strv(reply, const_cast<char**>(flags));
    }

    static int tx_value_cb(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply,
                           void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        return sd_bus_message_append_array(reply, 'y', self->tx_value.data(),
                                           self->tx_value.size());
    }

    static int tx_notifying_cb(sd_bus*, const char*, const char*, const char*,
                               sd_bus_message* reply, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        return sd_bus_message_append(reply, "b", self->notify_enabled ? 1 : 0);
    }

    /// Central wrote to our RX characteristic: that's one whole game frame.
    static int rx_write_value_cb(sd_bus_message* m, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        read_byte_array(m, self->recv_buffer);
        // (options dict ignored; trailing args need no explicit skip)
        return sd_bus_reply_method_return(m, "");
    }

    static int tx_start_notify_cb(sd_bus_message* m, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        self->notify_enabled = true;
        if (self->state == TransportState::Connecting) {
            self->state = TransportState::Connected;
        }
        return sd_bus_reply_method_return(m, "");
    }

    /// BlueZ calls StopNotify when the subscribed central unsubscribes or
    /// disconnects -- for a 1:1 game link either means the opponent is gone.
    static int tx_stop_notify_cb(sd_bus_message* m, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        self->notify_enabled = false;
        if (self->state == TransportState::Connected) {
            self->fail();
        }
        return sd_bus_reply_method_return(m, "");
    }

    // ---------------- host: advertisement vtable handlers ----------------

    static int adv_type_cb(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply,
                           void*, sd_bus_error*) {
        return prop_string_cb(reply, "peripheral");
    }

    static int adv_local_name_cb(sd_bus*, const char*, const char*, const char*,
                                 sd_bus_message* reply, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        return prop_string_cb(reply, self->local_name.c_str());
    }

    static int adv_service_uuids_cb(sd_bus*, const char*, const char*, const char*,
                                    sd_bus_message* reply, void*, sd_bus_error*) {
        const char* uuids[] = {kNusServiceUuid, nullptr};
        return sd_bus_message_append_strv(reply, const_cast<char**>(uuids));
    }

    // Always false, deliberately. BlueZ (advertising.c set_flags) then
    // writes our own Flags AD element: 0x04 "BR/EDR Not Supported" while
    // the adapter is not classic-discoverable, 0x00 while it is. Either
    // way the joiner's BlueZ never marks us BR/EDR-capable from this
    // advert (adapter.c only does so for non-zero Flags without 0x04), so
    // its Device1.Connect picks LE. Without this property the kernel fills
    // in Flags 0x02 whenever the launcher has made the adapter
    // discoverable, and the joiner's select_conn_bearer() prefers BR/EDR:
    // classic SSP with no agent -> Authentication Failure -> no match.
    // The joiner scans with an LE Transport filter (filtered discovery),
    // which reports adverts without the General Discoverable bit too.
    static int adv_discoverable_cb(sd_bus*, const char*, const char*, const char*,
                                   sd_bus_message* reply, void*, sd_bus_error*) {
        return sd_bus_message_append(reply, "b", 0);
    }

    static int adv_release_cb(sd_bus_message* m, void*, sd_bus_error*) {
        return sd_bus_reply_method_return(m, "");
    }

    // ---------------- host: registration replies ----------------

    static int register_app_reply_cb(sd_bus_message* reply, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        if (sd_bus_message_is_method_error(reply, nullptr)) {
            self->fail();
        } else {
            self->app_registered = true;
        }
        return 0;
    }

    static int register_adv_reply_cb(sd_bus_message* reply, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        if (sd_bus_message_is_method_error(reply, nullptr)) {
            self->fail();
        } else {
            self->adv_registered = true;
        }
        return 0;
    }

    // ---------------- link-loss watch (both roles) ----------------

    /// org.bluez.Device1 PropertiesChanged on the joiner's peer device (the
    /// match is path-filtered): link loss fails the transport.
    static int device_props_changed_cb(sd_bus_message* m, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        const char* iface = nullptr;
        if (sd_bus_message_read_basic(m, 's', &iface) < 0 || !iface ||
            std::strcmp(iface, kDeviceIface) != 0) {
            return 0;
        }
        bool has_connected = false, connected = false, has_resolved = false, resolved = false;
        if (sd_bus_message_enter_container(m, 'a', "{sv}") <= 0) {
            return 0;
        }
        while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
            const char* key = nullptr;
            sd_bus_message_read_basic(m, 's', &key);
            bool* has = nullptr;
            bool* val = nullptr;
            if (key && std::strcmp(key, "Connected") == 0) {
                has = &has_connected;
                val = &connected;
            } else if (key && std::strcmp(key, "ServicesResolved") == 0) {
                has = &has_resolved;
                val = &resolved;
            }
            int b = 0;
            if (has && sd_bus_message_enter_container(m, 'v', "b") > 0) {
                sd_bus_message_read_basic(m, 'b', &b);
                sd_bus_message_exit_container(m);
                *has = true;
                *val = b != 0;
            } else {
                sd_bus_message_skip(m, "v");
            }
            sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);

        if (self->state == TransportState::Disconnected) {
            return 0;
        }
        // Join only. The host ignores device connects/disconnects in the
        // lobby (unrelated devices must never affect it); post-handshake peer
        // loss is covered by StopNotify and the session's silence timeout.
        if (self->role == Role::Join) {
            if (has_connected && connected) {
                self->link_confirmed = true;
            }
            // A Connected=false before our Connect reply / Connected=true may
            // be the tail of a previous link (e.g. a retry): not a loss yet.
            const bool lost_connected = has_connected && !connected && self->link_confirmed;
            if (lost_connected || ble_device_link_lost(false, false, has_resolved, resolved,
                                                       self->services_resolved)) {
                self->fail();
            }
        }
        return 0;
    }

    // ---------------- join: central-side callbacks ----------------

    static int connect_reply_cb(sd_bus_message* reply, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        if (sd_bus_message_is_method_error(reply, nullptr)) {
            self->fail();
            return 0;
        }
        self->link_confirmed = true;
        // Success: keep waiting for ServicesResolved (polled from poll()).
        return 0;
    }

    static int resolved_reply_cb(sd_bus_message* reply, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        self->resolve_query_in_flight = false;
        if (sd_bus_message_is_method_error(reply, nullptr)) {
            return 0;  // device object may not be ready yet; retried
        }
        int resolved = 0;
        if (sd_bus_message_enter_container(reply, 'v', "b") > 0) {
            sd_bus_message_read_basic(reply, 'b', &resolved);
            sd_bus_message_exit_container(reply);
        }
        if (resolved) {
            self->services_resolved = true;
        }
        return 0;
    }

    /// GetManagedObjects reply while joining: locate the NUS RX/TX
    /// characteristic objects beneath our device path.
    static int char_lookup_reply_cb(sd_bus_message* reply, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        self->char_query_in_flight = false;
        if (sd_bus_message_is_method_error(reply, nullptr)) {
            return 0;
        }
        if (sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}") <= 0) {
            return 0;
        }
        while (sd_bus_message_enter_container(reply, 'e', "oa{sa{sv}}") > 0) {
            const char* path = nullptr;
            sd_bus_message_read_basic(reply, 'o', &path);
            const bool under_device = path && starts_with(path, self->device_path.c_str());

            sd_bus_message_enter_container(reply, 'a', "{sa{sv}}");
            while (sd_bus_message_enter_container(reply, 'e', "sa{sv}") > 0) {
                const char* iface = nullptr;
                sd_bus_message_read_basic(reply, 's', &iface);
                if (under_device && iface && std::strcmp(iface, kGattCharIface) == 0) {
                    sd_bus_message_enter_container(reply, 'a', "{sv}");
                    while (sd_bus_message_enter_container(reply, 'e', "sv") > 0) {
                        const char* key = nullptr;
                        sd_bus_message_read_basic(reply, 's', &key);
                        if (key && std::strcmp(key, "UUID") == 0) {
                            const char* uuid = nullptr;
                            if (sd_bus_message_enter_container(reply, 'v', "s") > 0) {
                                sd_bus_message_read_basic(reply, 's', &uuid);
                                sd_bus_message_exit_container(reply);
                            }
                            if (uuid && std::strcmp(uuid, kNusRxCharUuid) == 0) {
                                self->rx_char_path = path;
                            } else if (uuid && std::strcmp(uuid, kNusTxCharUuid) == 0) {
                                self->tx_char_path = path;
                            }
                        } else {
                            sd_bus_message_skip(reply, "v");
                        }
                        sd_bus_message_exit_container(reply);
                    }
                    sd_bus_message_exit_container(reply);
                } else {
                    sd_bus_message_skip(reply, "a{sv}");
                }
                sd_bus_message_exit_container(reply);
            }
            sd_bus_message_exit_container(reply);
            sd_bus_message_exit_container(reply);
        }
        sd_bus_message_exit_container(reply);
        return 0;
    }

    /// PropertiesChanged on the peer's TX characteristic: incoming frame
    /// bytes ride in the changed "Value" property.
    static int tx_props_changed_cb(sd_bus_message* m, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        const char* iface = nullptr;
        if (sd_bus_message_read_basic(m, 's', &iface) < 0 || !iface ||
            std::strcmp(iface, kGattCharIface) != 0) {
            return 0;
        }
        if (sd_bus_message_enter_container(m, 'a', "{sv}") <= 0) {
            return 0;
        }
        while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
            const char* key = nullptr;
            sd_bus_message_read_basic(m, 's', &key);
            if (key && std::strcmp(key, "Value") == 0) {
                if (sd_bus_message_enter_container(m, 'v', "ay") > 0) {
                    read_byte_array(m, self->recv_buffer);
                    sd_bus_message_exit_container(m);
                } else {
                    sd_bus_message_skip(m, "v");
                }
            } else {
                sd_bus_message_skip(m, "v");
            }
            sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
        return 0;
    }

    static int start_notify_reply_cb(sd_bus_message* reply, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        if (sd_bus_message_is_method_error(reply, nullptr)) {
            self->fail();
            return 0;
        }
        if (self->state == TransportState::Connecting) {
            self->state = TransportState::Connected;
        }
        return 0;
    }

    static int write_reply_cb(sd_bus_message* reply, void* userdata, sd_bus_error*) {
        auto* self = static_cast<Impl*>(userdata);
        if (sd_bus_message_is_method_error(reply, nullptr)) {
            // A failed write means the link is gone (device disconnected).
            // The reliability layer would retransmit forever otherwise.
            self->fail();
        }
        return 0;
    }

    // ---------------- pairing agent (both roles) ----------------
    //
    // WHY: the CardputerZero image runs PipeWire's BLE-MIDI GATT server
    // (service 03b80e5a-..., its characteristic requires encryption) on
    // every unit. As soon as a peer's bluetoothd finishes GATT discovery
    // its MIDI profile reads that characteristic -> "Insufficient
    // Encryption" -> the kernel starts SMP pairing (both sides advertise
    // IO capability KeyboardDisplay, SC + MITM -> numeric comparison).
    // With no agent registered bluetoothd answers "User Confirmation
    // Negative Reply" -> Pairing Failed (0x0c) -> the kernel drops the link
    // (Authentication Failure) ~3 s after connect, before our NUS
    // StartNotify. We therefore export an Agent1 that auto-accepts the
    // confirmation. "KeyboardDisplay" is deliberate: it is the capability
    // that produced a working bond on hardware (both sides call
    // RequestConfirmation). Bonds are kept, so a repeat match between the
    // same units skips pairing. Registration failure is ignored (other
    // systems may not need pairing); classic profiles are never authorized.

    static int agent_ok_cb(sd_bus_message* m, void*, sd_bus_error*) {
        return sd_bus_reply_method_return(m, "");
    }

    static int agent_reject_cb(sd_bus_message* m, void*, sd_bus_error*) {
        return sd_bus_reply_method_errorf(m, "org.bluez.Error.Rejected", "Rejected");
    }

    static const sd_bus_vtable kAgentVtable[];

    /// Exports Agent1 and asks BlueZ to use it. Never fails the transport.
    void register_agent() {
        if (!bus) return;
        if (sd_bus_add_object_vtable(bus, nullptr, kAgentPath, kAgentIface, kAgentVtable, this) <
            0) {
            return;
        }
        // Replies are ignored; bluetoothd serves calls in order, so the
        // default-agent request follows the registration.
        if (sd_bus_call_method_async(bus, nullptr, kBluez, "/org/bluez", kAgentManagerIface,
                                     "RegisterAgent", nullptr, nullptr, "os", kAgentPath,
                                     "KeyboardDisplay") < 0) {
            return;
        }
        agent_registered = true;
        sd_bus_call_method_async(bus, nullptr, kBluez, "/org/bluez", kAgentManagerIface,
                                 "RequestDefaultAgent", nullptr, nullptr, "o", kAgentPath);
    }

    // sd-bus vtables for the objects we export in the host role. Static
    // members (not namespace-scope globals) because their entries name the
    // private handler functions above; out-of-class member definitions
    // retain class access.
    static const sd_bus_vtable kServiceVtable[];
    static const sd_bus_vtable kRxCharVtable[];
    static const sd_bus_vtable kTxCharVtable[];
    static const sd_bus_vtable kAdvVtable[];
};

// GattService1 on kServicePath.
const sd_bus_vtable BleTransport::Impl::kServiceVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID", "s", service_uuid_cb, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Primary", "b", service_primary_cb, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};

// GattCharacteristic1 on kRxCharPath (central writes -> our receive path).
const sd_bus_vtable BleTransport::Impl::kRxCharVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID", "s", rx_uuid_cb, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Service", "o", char_service_cb, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Flags", "as", rx_flags_cb, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("WriteValue", "aya{sv}", "", rx_write_value_cb, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

// GattCharacteristic1 on kTxCharPath (our notifications -> central).
const sd_bus_vtable BleTransport::Impl::kTxCharVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID", "s", tx_uuid_cb, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Service", "o", char_service_cb, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Flags", "as", tx_flags_cb, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Notifying", "b", tx_notifying_cb, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Value", "ay", tx_value_cb, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_METHOD("StartNotify", "", "", tx_start_notify_cb, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("StopNotify", "", "", tx_stop_notify_cb, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

// Agent1 on kAgentPath.
const sd_bus_vtable BleTransport::Impl::kAgentVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Release", "", "", agent_ok_cb, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestPinCode", "o", "s", agent_reject_cb, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("DisplayPinCode", "os", "", agent_ok_cb, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestPasskey", "o", "u", agent_reject_cb, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("DisplayPasskey", "ouq", "", agent_ok_cb, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestConfirmation", "ou", "", agent_ok_cb, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestAuthorization", "o", "", agent_ok_cb, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("AuthorizeService", "os", "", agent_reject_cb, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Cancel", "", "", agent_ok_cb, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

// LEAdvertisement1 on kAdvPath.
const sd_bus_vtable BleTransport::Impl::kAdvVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Type", "s", adv_type_cb, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("LocalName", "s", adv_local_name_cb, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ServiceUUIDs", "as", adv_service_uuids_cb, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Discoverable", "b", adv_discoverable_cb, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("Release", "", "", adv_release_cb, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

BleTransport::BleTransport() : impl_(new Impl) {}

BleTransport::~BleTransport() {
    close();
}

bool BleTransport::start_host(const std::string& host_name) {
    close();
    start_error_ = BleStartError::None;
    if (sd_bus_open_system(&impl_->bus) < 0) {
        impl_->bus = nullptr;
        return false;
    }
    impl_->adapter = find_adapter(impl_->bus);
    if (impl_->adapter.empty()) {
        impl_->shutdown();
        start_error_ = BleStartError::Other;
        return false;
    }
    if (!power_on_adapter(impl_->bus, impl_->adapter)) {
        impl_->shutdown();
        start_error_ = BleStartError::AdapterOff;
        return false;
    }

    impl_->role = Impl::Role::Host;
    impl_->register_agent();
    impl_->local_name = std::string(kBleNamePrefix) + host_name;

    // Export the NUS application (ObjectManager root + service + 2 chars)
    // and the advertisement. Slots are left floating: they live exactly as
    // long as the bus connection (released together in shutdown()).
    int rc = sd_bus_add_object_manager(impl_->bus, nullptr, kAppRoot);
    if (rc >= 0) {
        rc = sd_bus_add_object_vtable(impl_->bus, nullptr, kServicePath, kGattServiceIface,
                                      Impl::kServiceVtable, impl_.get());
    }
    if (rc >= 0) {
        rc = sd_bus_add_object_vtable(impl_->bus, nullptr, kRxCharPath, kGattCharIface,
                                      Impl::kRxCharVtable, impl_.get());
    }
    if (rc >= 0) {
        rc = sd_bus_add_object_vtable(impl_->bus, nullptr, kTxCharPath, kGattCharIface,
                                      Impl::kTxCharVtable, impl_.get());
    }
    if (rc >= 0) {
        rc = sd_bus_add_object_vtable(impl_->bus, nullptr, kAdvPath, kAdvIface, Impl::kAdvVtable,
                                      impl_.get());
    }
    if (rc < 0) {
        impl_->shutdown();
        return false;
    }

    rc = sd_bus_call_method_async(impl_->bus, nullptr, kBluez, impl_->adapter.c_str(),
                                  kGattManagerIface, "RegisterApplication",
                                  &Impl::register_app_reply_cb, impl_.get(), "oa{sv}", kAppRoot,
                                  0);
    if (rc >= 0) {
        rc = sd_bus_call_method_async(impl_->bus, nullptr, kBluez, impl_->adapter.c_str(),
                                      kAdvManagerIface, "RegisterAdvertisement",
                                      &Impl::register_adv_reply_cb, impl_.get(), "oa{sv}",
                                      kAdvPath, 0);
    }
    if (rc < 0) {
        impl_->shutdown();
        return false;
    }

    impl_->state = TransportState::Connecting;
    pump(impl_->bus);
    return true;
}

bool BleTransport::start_join(const std::string& device_path) {
    close();
    if (device_path.empty()) {
        return false;
    }
    if (sd_bus_open_system(&impl_->bus) < 0) {
        impl_->bus = nullptr;
        return false;
    }
    impl_->adapter = find_adapter(impl_->bus);
    if (impl_->adapter.empty()) {
        impl_->shutdown();
        return false;
    }

    impl_->role = Impl::Role::Join;
    impl_->register_agent();
    impl_->device_path = device_path;
    impl_->start_ms = monotonic_ms();

    // Watch the peer for link loss before issuing Connect so a drop during
    // GATT discovery cannot slip past.
    {
        const std::string match =
            "type='signal',sender='org.bluez',interface='org.freedesktop.DBus.Properties',"
            "member='PropertiesChanged',path='" +
            device_path + "',arg0='org.bluez.Device1'";
        if (sd_bus_add_match(impl_->bus, nullptr, match.c_str(), &Impl::device_props_changed_cb,
                             impl_.get()) < 0) {
            impl_->shutdown();
            return false;
        }
    }

    const int rc =
        sd_bus_call_method_async(impl_->bus, nullptr, kBluez, device_path.c_str(), kDeviceIface,
                                 "Connect", &Impl::connect_reply_cb, impl_.get(), "");
    if (rc < 0) {
        impl_->shutdown();
        return false;
    }

    impl_->state = TransportState::Connecting;
    pump(impl_->bus);
    return true;
}

void BleTransport::poll() {
    auto& impl = *impl_;
    if (!impl.bus || impl.state == TransportState::Disconnected) {
        return;
    }
    pump(impl.bus);
    if (impl.state == TransportState::Disconnected) {
        return;
    }

    // Joiner overall handshake budget (the host lobby waits unbounded).
    if (impl.role == Impl::Role::Join && impl.state == TransportState::Connecting &&
        ble_handshake_timed_out(impl.start_ms, monotonic_ms())) {
        impl.fail();
        return;
    }

    if (impl.role == Impl::Role::Join && impl.state == TransportState::Connecting) {
        // Drive the connect pipeline: ServicesResolved -> characteristic
        // lookup -> notification subscription. Each stage is async; the
        // in-flight flags keep it at one outstanding query per stage, and
        // the ~100ms UI poll cadence (BaseViewModel::net_poll) is throttle
        // enough between retries of a not-yet-answered stage.
        if (!impl.services_resolved) {
            if (!impl.resolve_query_in_flight) {
                impl.resolve_query_in_flight =
                    sd_bus_call_method_async(impl.bus, nullptr, kBluez, impl.device_path.c_str(),
                                             kPropsIface, "Get", &Impl::resolved_reply_cb,
                                             impl_.get(), "ss", kDeviceIface,
                                             "ServicesResolved") >= 0;
            }
        } else if (impl.rx_char_path.empty() || impl.tx_char_path.empty()) {
            if (!impl.char_query_in_flight) {
                impl.char_query_in_flight =
                    sd_bus_call_method_async(impl.bus, nullptr, kBluez, "/",
                                             "org.freedesktop.DBus.ObjectManager",
                                             "GetManagedObjects", &Impl::char_lookup_reply_cb,
                                             impl_.get(), "") >= 0;
            }
        } else if (!impl.start_notify_sent) {
            impl.start_notify_sent = true;
            // Incoming frames arrive as PropertiesChanged(Value) on the TX
            // characteristic -- subscribe before enabling notifications so
            // no early notification can slip past the match.
            sd_bus_match_signal(impl.bus, nullptr, kBluez, impl.tx_char_path.c_str(), kPropsIface,
                                "PropertiesChanged", &Impl::tx_props_changed_cb, impl_.get());
            sd_bus_call_method_async(impl.bus, nullptr, kBluez, impl.tx_char_path.c_str(),
                                     kGattCharIface, "StartNotify",
                                     &Impl::start_notify_reply_cb, impl_.get(), "");
        }
    }

    pump(impl.bus);
}

TransportState BleTransport::state() const {
    return impl_->state;
}

bool BleTransport::send(const uint8_t* data, size_t len) {
    auto& impl = *impl_;
    if (impl.state != TransportState::Connected || !data || len == 0) {
        return false;
    }
    if (len > kBleSingleWriteLimit) {
        // Protocol invariant (messages.h static_assert) -- should be
        // unreachable; refuse rather than silently truncate.
        return false;
    }

    if (impl.role == Impl::Role::Host) {
        // One send() = one notification. sd_bus_emit_properties_changed
        // snapshots the property value (invokes tx_value_cb) while building
        // the signal, so back-to-back sends each carry their own bytes.
        impl.tx_value.assign(data, data + len);
        const int rc = sd_bus_emit_properties_changed(impl.bus, kTxCharPath, kGattCharIface,
                                                      "Value", nullptr);
        pump(impl.bus);
        return rc >= 0 && impl.state == TransportState::Connected;
    }

    // Join/central: one send() = one WriteValue on the peer's RX. Options:
    // type=command (write-without-response) keeps turn latency at one radio
    // event instead of a full ATT round trip.
    sd_bus_message* call = nullptr;
    int rc = sd_bus_message_new_method_call(impl.bus, &call, kBluez, impl.rx_char_path.c_str(),
                                            kGattCharIface, "WriteValue");
    if (rc >= 0) rc = sd_bus_message_append_array(call, 'y', data, len);
    if (rc >= 0) rc = sd_bus_message_append(call, "a{sv}", 1, "type", "s", "command");
    if (rc >= 0) rc = sd_bus_call_async(impl.bus, nullptr, call, &Impl::write_reply_cb,
                                        impl_.get(), 0);
    sd_bus_message_unref(call);
    pump(impl.bus);
    return rc >= 0 && impl.state == TransportState::Connected;
}

size_t BleTransport::receive(std::vector<uint8_t>& out) {
    auto& buffer = impl_->recv_buffer;
    const size_t n = buffer.size();
    out.insert(out.end(), buffer.begin(), buffer.end());
    buffer.clear();
    return n;
}

void BleTransport::close() {
    impl_->shutdown();
}

}  // namespace battleship::net
