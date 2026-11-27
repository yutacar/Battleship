/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * BleTransport + BleScanner: BLE implementation of ITransport for Linux/BlueZ
 * (Phase 6, PLAN.md §2.2 "BleTransport" row). Uses the CardputerZero's
 * built-in BT 4.2 radio -- no access point, no extra hardware -- making BLE
 * the product's default 2-player connection method.
 *
 * Design (Nordic UART Service compatible GATT, over the BlueZ D-Bus API via
 * sd-bus/libsystemd):
 *
 *   Host  = GATT peripheral: registers a NUS-compatible application
 *           (service 6E400001-..., RX characteristic = write /
 *           write-without-response, TX characteristic = notify) with
 *           org.bluez.GattManager1 and advertises via
 *           org.bluez.LEAdvertisingManager1 with local name "BSHIP-<host>".
 *   Join  = GATT central: BleScanner runs Adapter1.StartDiscovery with a
 *           NUS-service-UUID + name-prefix filter and exposes the result
 *           list; BleTransport then Device1.Connect()s the chosen device,
 *           resolves the NUS characteristics, subscribes to TX notifications
 *           and writes outgoing bytes to RX.
 *
 * Byte-stream mapping: one encoded game frame (<= net::kMaxGameFrameLen =
 * 13 bytes, statically asserted <= kBleSingleWriteLimit = 20 in messages.h)
 * per WriteValue / notification, so a single ATT PDU at the BT 4.2 default
 * MTU always suffices -- no fragmentation layer, no MTU negotiation. The
 * higher layers (FrameDecoder / ReliableLink / NetSession) treat this as an
 * ordinary byte stream, and the frame decoder's
 * resynchronization tolerates any chunking anyway.
 *
 * Security scope (deliberate, PLAN.md Phase 6): Just Works connections only.
 * No pairing/bonding is requested and link encryption is out of scope for a
 * casual game -- the protocol carries only coordinates/outcomes (never board
 * contents, see messages.h), so a sniffed link reveals nothing the opposing
 * player doesn't already see on their own screen.
 *
 * Platform scope: Linux/BlueZ only. This header is safe to include (and this
 * class safe to name) on any platform -- the implementation is pimpl'd -- but
 * ble_transport.cpp is only compiled when the CMake option BATTLESHIP_BLE is
 * ON (Linux builds only; the macOS desktop build has no BlueZ and gets
 * BATTLESHIP_BLE=0, see CMakeLists.txt). UI code must guard construction
 * with `#if BATTLESHIP_BLE`.
 *
 * Threading: single-threaded poll-driven like every other ITransport. poll()
 * pumps the sd-bus connection non-blockingly (sd_bus_process with no wait);
 * all BlueZ method calls are issued asynchronously except a small number of
 * one-shot setup lookups performed inside start_host()/start_join()/
 * BleScanner::start() themselves (adapter enumeration -- a handful of local
 * IPC round-trips to bluetoothd at user-action time, not in the poll loop).
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "net/transport.h"

namespace battleship::net {

/// Nordic UART Service UUIDs (lowercase, as BlueZ reports them).
inline constexpr const char* kNusServiceUuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
inline constexpr const char* kNusRxCharUuid = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";  ///< central -> peripheral (Write)
inline constexpr const char* kNusTxCharUuid = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";  ///< peripheral -> central (Notify)

/// Prefix of the advertised LE local name, so joiners can filter scan
/// results by name as well as by service UUID.
inline constexpr const char* kBleNamePrefix = "BSHIP-";

/// One advertising Battleship host as seen by a scanning joiner.
struct BleScanResult {
    std::string device_path;  ///< BlueZ D-Bus object path (org.bluez.Device1)
    std::string address;      ///< BD_ADDR "AA:BB:CC:DD:EE:FF" (diagnostics/UI)
    std::string name;         ///< advertised local name ("BSHIP-<host>")
};

/// Join side: LE discovery with a NUS/name filter, poll-driven host list
/// (the BLE counterpart of the LoRa beacon scan).
class BleScanner {
public:
    BleScanner();
    ~BleScanner();
    BleScanner(const BleScanner&) = delete;
    BleScanner& operator=(const BleScanner&) = delete;

    /// Connects to the system bus, powers the first BlueZ adapter and starts
    /// filtered LE discovery. Returns false if there is no usable adapter /
    /// bluetoothd (UI then shows "Bluetooth unavailable").
    bool start();

    /// Re-enumerates discovered devices at a modest interval and services
    /// D-Bus traffic. Call from the LVGL-side net poll timer.
    void poll(uint32_t now_ms);

    [[nodiscard]] const std::vector<BleScanResult>& results() const;

    /// Stops discovery and releases the bus connection. Idempotent.
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// BLE ITransport. One instance handles either role:
///   start_host()  -> peripheral (advertise + GATT server), state Connecting
///                    until a central connects AND subscribes to TX.
///   start_join()  -> central (connect + resolve + subscribe), state
///                    Connecting until the TX subscription is live.
class BleTransport final : public ITransport {
public:
    BleTransport();
    ~BleTransport() override;
    BleTransport(const BleTransport&) = delete;
    BleTransport& operator=(const BleTransport&) = delete;

    /// Host side: register the NUS GATT application and start advertising as
    /// "BSHIP-<host_name>". Returns false on immediate setup failure (no
    /// adapter, no bluetoothd, D-Bus permission denied -- see NOTES.md Phase
    /// 6 for the bluetooth-group requirement). On success state() ==
    /// Connecting until a central subscribes to the TX characteristic.
    bool start_host(const std::string& host_name);

    /// Join side: connect to a device previously found by BleScanner.
    /// `device_path` is BleScanResult::device_path. Returns false on
    /// immediate failure; on success state() == Connecting until the NUS
    /// characteristics are resolved and TX notifications are subscribed.
    bool start_join(const std::string& device_path);

    void poll() override;
    [[nodiscard]] TransportState state() const override;
    using ITransport::send;  // keep the std::vector convenience overload visible
    bool send(const uint8_t* data, size_t len) override;
    size_t receive(std::vector<uint8_t>& out) override;
    void close() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace battleship::net
