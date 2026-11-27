/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 *
 * Ported from LoraMessenger src/adapters/radio/linux_cap_lora_1262_radio.cpp
 * (MIT, same author). Changes: sustained carrier sense, packet RSSI via
 * GetPacketStatus, RadioInitError reporting, no antenna_confirmed flag,
 * stale RX-in-progress IRQ recovery, TX timeout returns to RX.
 */

#include "net/lora/sx1262_radio.h"

#include "net/lora/sx1262_math.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace battleship::net::lora {
namespace {

constexpr std::uint8_t kPi4ioAddress = 0x43U;
constexpr std::uint8_t kPi4ioDirection = 0x03U;
constexpr std::uint8_t kPi4ioOutput = 0x05U;
constexpr std::uint8_t kPi4ioHighImpedance = 0x07U;
constexpr std::uint8_t kPi4ioDeviceId = 0x01U;
constexpr std::uint8_t kAntennaSwitchBit = 0x01U;

constexpr std::uint8_t kSetStandby = 0x80U;
constexpr std::uint8_t kSetRx = 0x82U;
constexpr std::uint8_t kSetTx = 0x83U;
constexpr std::uint8_t kSetSleep = 0x84U;
constexpr std::uint8_t kSetRfFrequency = 0x86U;
constexpr std::uint8_t kCalibrate = 0x89U;
constexpr std::uint8_t kSetPacketType = 0x8AU;
constexpr std::uint8_t kSetModulationParams = 0x8BU;
constexpr std::uint8_t kSetPacketParams = 0x8CU;
constexpr std::uint8_t kSetTxParams = 0x8EU;
constexpr std::uint8_t kSetBufferBaseAddress = 0x8FU;
constexpr std::uint8_t kSetPaConfig = 0x95U;
constexpr std::uint8_t kSetRegulatorMode = 0x96U;
constexpr std::uint8_t kSetDio3TcxoCtrl = 0x97U;
constexpr std::uint8_t kCalibrateImage = 0x98U;
constexpr std::uint8_t kSetDioIrqParams = 0x08U;
constexpr std::uint8_t kClearIrqStatus = 0x02U;
constexpr std::uint8_t kReadRegister = 0x1DU;
constexpr std::uint8_t kWriteRegister = 0x0DU;
constexpr std::uint8_t kWriteBuffer = 0x0EU;
constexpr std::uint8_t kGetIrqStatus = 0x12U;
constexpr std::uint8_t kGetRxBufferStatus = 0x13U;
constexpr std::uint8_t kGetPacketStatus = 0x14U;
constexpr std::uint8_t kGetRssiInst = 0x15U;
constexpr std::uint8_t kReadBuffer = 0x1EU;

constexpr std::uint16_t kIrqTxDone = 0x0001U;
constexpr std::uint16_t kIrqRxDone = 0x0002U;
constexpr std::uint16_t kIrqPreambleDetected = 0x0004U;
constexpr std::uint16_t kIrqHeaderValid = 0x0010U;
constexpr std::uint16_t kIrqHeaderError = 0x0020U;
constexpr std::uint16_t kIrqCrcError = 0x0040U;
constexpr std::uint16_t kIrqTimeout = 0x0200U;
constexpr std::uint16_t kRxInProgressIrqs = kIrqPreambleDetected | kIrqHeaderValid;
constexpr std::uint16_t kReceiveIrqs =
    kIrqRxDone | kRxInProgressIrqs | kIrqHeaderError | kIrqCrcError | kIrqTimeout;
constexpr std::uint16_t kTransmitIrqs = kIrqTxDone | kIrqTimeout;

constexpr std::uint16_t kLoRaSyncWordRegister = 0x0740U;
constexpr std::uint16_t kOcpRegister = 0x08E7U;
constexpr std::uint8_t kLoRaPacketType = 0x01U;
constexpr std::uint8_t kBandwidth125Khz = 0x04U;
constexpr std::uint8_t kExplicitHeader = 0x00U;
constexpr std::uint8_t kCrcEnabled = 0x01U;
constexpr std::uint8_t kStandardIq = 0x00U;
constexpr std::uint8_t kStandbyRc = 0x00U;
constexpr std::uint8_t kStandbyXosc = 0x01U;
constexpr std::uint8_t kSetRxTxFallbackMode = 0x93U;
constexpr std::uint8_t kFallbackStandbyXosc = 0x30U;
constexpr std::uint8_t kRegulatorDcdc = 0x01U;
constexpr std::uint8_t kTcxo3Volts = 0x06U;
constexpr std::uint32_t kTcxoDelayUnits = 320U;     // 5 ms / 15.625 us
constexpr std::uint32_t kTxTimeoutUnits = 320'000U; // 5 seconds
constexpr std::uint32_t kBusyTimeoutMs = 100U;
constexpr std::uint8_t kMaxTxPowerDbm = 13U;
constexpr auto kCarrierSenseInterval = std::chrono::microseconds(200);
// Longest on-air time of a Battleship packet is well below this; a preamble /
// header-valid flag older than this without RxDone is stale and gets cleared.
constexpr auto kRxInProgressMax = std::chrono::milliseconds(500);

constexpr std::size_t kSpiBufferSize = kMaxRadioPacket + 6U;

int open_flags(int base) noexcept {
#ifdef O_CLOEXEC
    return base | O_CLOEXEC;
#else
    return base;
#endif
}

bool close_descriptor(int& descriptor) noexcept {
    if (descriptor < 0) {
        return true;
    }
    const int closing = descriptor;
    descriptor = -1;
    while (::close(closing) != 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    return true;
}

bool gpio_get(int descriptor, bool& high) noexcept {
    gpiohandle_data data{};
    if (::ioctl(descriptor, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) != 0) {
        return false;
    }
    high = data.values[0] != 0U;
    return true;
}

bool gpio_set(int descriptor, bool high) noexcept {
    gpiohandle_data data{};
    data.values[0] = high ? 1U : 0U;
    return ::ioctl(descriptor, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data) == 0;
}

int request_gpio(int chip_descriptor, std::uint32_t offset, bool output, bool initial_high,
                 const char* consumer) noexcept {
    gpiohandle_request request{};
    request.lineoffsets[0] = offset;
    request.flags = output ? GPIOHANDLE_REQUEST_OUTPUT : GPIOHANDLE_REQUEST_INPUT;
    request.default_values[0] = initial_high ? 1U : 0U;
    request.lines = 1U;
    std::strncpy(request.consumer_label, consumer, sizeof(request.consumer_label) - 1U);
    if (::ioctl(chip_descriptor, GPIO_GET_LINEHANDLE_IOCTL, &request) != 0) {
        return -1;
    }
    return request.fd;
}

} // namespace

bool Sx1262Config::valid() const noexcept {
    return !spi_device.empty() && !gpio_chip.empty() && !i2c_device.empty() &&
           spi_speed_hz > 0U && spi_speed_hz <= 10'000'000U && reset_gpio != irq_gpio &&
           reset_gpio != busy_gpio && irq_gpio != busy_gpio && lbt_threshold_dbm >= -127 &&
           lbt_threshold_dbm <= -20 && carrier_sense_ms >= 1U && carrier_sense_ms <= 100U &&
           profile.valid() && profile.transmit_power_dbm <= kMaxTxPowerDbm;
}

struct Sx1262Radio::Impl {
    using Clock = std::chrono::steady_clock;

    explicit Impl(Sx1262Config value) noexcept : config(std::move(value)) { initialize(); }

    ~Impl() { shutdown(); }

    // Latched on any SPI/GPIO failure after init: ready() then stays false so
    // LoRaLink drops the radio instead of polling a dead device forever.
    bool ready() const noexcept { return initialized && !closed && !faulted; }

    RadioStart fault_tx() noexcept {
        faulted = true;
        return RadioStart::Failed;
    }

    // Busy-wait on the BUSY pin; bounded, <= 100 us sleeps.
    bool wait_while_busy() noexcept {
        const auto deadline = Clock::now() + std::chrono::milliseconds(kBusyTimeoutMs);
        for (;;) {
            bool busy = false;
            if (!gpio_get(busy_descriptor, busy)) {
                return false;
            }
            if (!busy) {
                return true;
            }
            if (Clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }

    bool spi_transfer(const std::uint8_t* transmit, std::uint8_t* receive,
                      std::size_t size) noexcept {
        if (spi_descriptor < 0 || !transmit || size == 0U ||
            size > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        spi_ioc_transfer transfer{};
        transfer.tx_buf = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(transmit));
        transfer.rx_buf = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(receive));
        transfer.len = static_cast<std::uint32_t>(size);
        transfer.speed_hz = config.spi_speed_hz;
        transfer.bits_per_word = 8U;
        return ::ioctl(spi_descriptor, SPI_IOC_MESSAGE(1), &transfer) >= 1;
    }

    bool write_command(std::uint8_t opcode, const std::uint8_t* data, std::size_t size) noexcept {
        if (!wait_while_busy() || size > kSpiBufferSize - 1U) {
            return false;
        }
        std::array<std::uint8_t, kSpiBufferSize> transmit{};
        transmit[0] = opcode;
        if (size > 0U) {
            if (!data) {
                return false;
            }
            std::copy_n(data, size, transmit.begin() + 1);
        }
        if (!spi_transfer(transmit.data(), nullptr, size + 1U)) {
            return false;
        }
        return opcode == kSetSleep || wait_while_busy();
    }

    bool read_command(std::uint8_t opcode, const std::uint8_t* arguments, std::size_t argument_size,
                      std::uint8_t* destination, std::size_t destination_size) noexcept {
        if (!wait_while_busy() || !destination || destination_size == 0U ||
            argument_size + destination_size + 2U > kSpiBufferSize) {
            return false;
        }
        std::array<std::uint8_t, kSpiBufferSize> transmit{};
        std::array<std::uint8_t, kSpiBufferSize> receive{};
        transmit[0] = opcode;
        if (argument_size > 0U) {
            if (!arguments) {
                return false;
            }
            std::copy_n(arguments, argument_size, transmit.begin() + 1);
        }
        const auto data_offset = argument_size + 2U;
        const auto transfer_size = data_offset + destination_size;
        if (!spi_transfer(transmit.data(), receive.data(), transfer_size) || !wait_while_busy()) {
            return false;
        }
        std::copy_n(receive.begin() + static_cast<std::ptrdiff_t>(data_offset), destination_size,
                    destination);
        return true;
    }

    bool write_register(std::uint16_t address, const std::uint8_t* data,
                        std::size_t size) noexcept {
        if (!data || size == 0U || size > kMaxRadioPacket) {
            return false;
        }
        std::array<std::uint8_t, kMaxRadioPacket + 2U> arguments{};
        arguments[0] = static_cast<std::uint8_t>(address >> 8U);
        arguments[1] = static_cast<std::uint8_t>(address & 0xffU);
        std::copy_n(data, size, arguments.begin() + 2);
        return write_command(kWriteRegister, arguments.data(), size + 2U);
    }

    bool read_register(std::uint16_t address, std::uint8_t* destination,
                       std::size_t size) noexcept {
        const std::uint8_t arguments[]{
            static_cast<std::uint8_t>(address >> 8U),
            static_cast<std::uint8_t>(address & 0xffU),
        };
        return read_command(kReadRegister, arguments, sizeof(arguments), destination, size);
    }

    bool i2c_read_register(std::uint8_t address, std::uint8_t& value) noexcept {
        i2c_msg messages[2]{};
        messages[0].addr = kPi4ioAddress;
        messages[0].flags = 0U;
        messages[0].len = 1U;
        messages[0].buf = &address;
        messages[1].addr = kPi4ioAddress;
        messages[1].flags = I2C_M_RD;
        messages[1].len = 1U;
        messages[1].buf = &value;
        i2c_rdwr_ioctl_data transfer{};
        transfer.msgs = messages;
        transfer.nmsgs = 2U;
        return ::ioctl(i2c_descriptor, I2C_RDWR, &transfer) == 2;
    }

    bool i2c_write_register(std::uint8_t address, std::uint8_t value) noexcept {
        std::uint8_t data[2]{address, value};
        i2c_msg message{};
        message.addr = kPi4ioAddress;
        message.flags = 0U;
        message.len = 2U;
        message.buf = data;
        i2c_rdwr_ioctl_data transfer{};
        transfer.msgs = &message;
        transfer.nmsgs = 1U;
        return ::ioctl(i2c_descriptor, I2C_RDWR, &transfer) == 1;
    }

    bool update_i2c_register(std::uint8_t address, std::uint8_t set_mask,
                             std::uint8_t clear_mask) noexcept {
        std::uint8_t value = 0U;
        if (!i2c_read_register(address, value)) {
            return false;
        }
        value = static_cast<std::uint8_t>((value | set_mask) &
                                          static_cast<std::uint8_t>(~clear_mask));
        return i2c_write_register(address, value);
    }

    bool set_antenna_switch(bool enabled) noexcept {
        if (i2c_descriptor < 0) {
            return false;
        }
        if (enabled) {
            std::uint8_t device_id = 0U;
            if (!i2c_read_register(kPi4ioDeviceId, device_id) || device_id == 0U ||
                !update_i2c_register(kPi4ioDirection, kAntennaSwitchBit, 0U) ||
                !update_i2c_register(kPi4ioHighImpedance, 0U, kAntennaSwitchBit) ||
                !update_i2c_register(kPi4ioOutput, kAntennaSwitchBit, 0U)) {
                return false;
            }
            antenna_switch_enabled = true;
            return true;
        }
        const bool result = update_i2c_register(kPi4ioOutput, 0U, kAntennaSwitchBit);
        antenna_switch_enabled = false;
        return result;
    }

    bool reset_radio() noexcept {
        if (!gpio_set(reset_descriptor, false)) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (!gpio_set(reset_descriptor, true)) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return wait_while_busy();
    }

    bool set_irq_mask(std::uint16_t mask) noexcept {
        const std::uint8_t arguments[]{
            static_cast<std::uint8_t>(mask >> 8U), static_cast<std::uint8_t>(mask & 0xffU),
            static_cast<std::uint8_t>(mask >> 8U), static_cast<std::uint8_t>(mask & 0xffU),
            0U, 0U, 0U, 0U,
        };
        return write_command(kSetDioIrqParams, arguments, sizeof(arguments));
    }

    bool clear_irq(std::uint16_t mask = 0xffffU) noexcept {
        const std::uint8_t arguments[]{
            static_cast<std::uint8_t>(mask >> 8U),
            static_cast<std::uint8_t>(mask & 0xffU),
        };
        return write_command(kClearIrqStatus, arguments, sizeof(arguments));
    }

    bool read_irq(std::uint16_t& mask) noexcept {
        std::uint8_t data[2]{};
        if (!read_command(kGetIrqStatus, nullptr, 0U, data, sizeof(data))) {
            return false;
        }
        mask = static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[0]) << 8U |
                                          static_cast<std::uint16_t>(data[1]));
        return true;
    }

    bool set_packet_params(std::size_t payload_size) noexcept {
        if (payload_size == 0U || payload_size > kMaxRadioPacket) {
            return false;
        }
        const std::uint8_t arguments[]{
            static_cast<std::uint8_t>(config.profile.preamble_symbols >> 8U),
            static_cast<std::uint8_t>(config.profile.preamble_symbols & 0xffU),
            kExplicitHeader,
            static_cast<std::uint8_t>(payload_size),
            kCrcEnabled,
            kStandardIq,
        };
        return write_command(kSetPacketParams, arguments, sizeof(arguments));
    }

    static std::uint32_t elapsed_us(Clock::time_point from) noexcept {
        return static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - from).count());
    }

    bool start_receive() noexcept {
        const auto t0 = Clock::now();
        const bool ok = start_receive_untimed();
        last_rx_rearm_us = elapsed_us(t0);
        return ok;
    }

    bool start_receive_untimed() noexcept {
        if (!set_packet_params(kMaxRadioPacket) || !clear_irq() || !set_irq_mask(kReceiveIrqs)) {
            return false;
        }
        const std::uint8_t continuous[]{0xffU, 0xffU, 0xffU};
        if (!write_command(kSetRx, continuous, sizeof(continuous))) {
            return false;
        }
        rx_in_progress_since_valid = false;
        transmitting = false;
        return true;
    }

    // Returns true when a packet is being received. Preamble / header-valid
    // flags that stay set longer than kRxInProgressMax without RxDone (signal
    // vanished mid-packet) are cleared so the line cannot read busy forever.
    bool rx_in_progress(std::uint16_t irq) noexcept {
        if ((irq & kRxInProgressIrqs) == 0U) {
            rx_in_progress_since_valid = false;
            return false;
        }
        const auto now = Clock::now();
        if (!rx_in_progress_since_valid) {
            rx_in_progress_since = now;
            rx_in_progress_since_valid = true;
            return true;
        }
        if (now - rx_in_progress_since > kRxInProgressMax) {
            static_cast<void>(clear_irq(kRxInProgressIrqs));
            rx_in_progress_since_valid = false;
            return false;
        }
        return true;
    }

    bool configure_radio() noexcept {
        const std::uint8_t standby[]{kStandbyRc};
        const std::uint8_t regulator[]{kRegulatorDcdc};
        const std::uint8_t tcxo[]{
            kTcxo3Volts,
            static_cast<std::uint8_t>(kTcxoDelayUnits >> 16U),
            static_cast<std::uint8_t>(kTcxoDelayUnits >> 8U),
            static_cast<std::uint8_t>(kTcxoDelayUnits),
        };
        const std::uint8_t calibrate[]{0x7fU};
        const std::uint8_t image[]{0xe1U, 0xe9U};
        const std::uint8_t packet_type[]{kLoRaPacketType};
        const std::uint8_t buffer_base[]{0U, 0U};
        const std::uint8_t fallback[]{kFallbackStandbyXosc};
        if (!write_command(kSetStandby, standby, sizeof(standby))) {
            init_error = RadioInitError::RadioNoResponse;
            return false;
        }
        if (!write_command(kSetRegulatorMode, regulator, sizeof(regulator)) ||
            !write_command(kSetDio3TcxoCtrl, tcxo, sizeof(tcxo)) ||
            !write_command(kCalibrate, calibrate, sizeof(calibrate)) ||
            !write_command(kCalibrateImage, image, sizeof(image)) ||
            !write_command(kSetPacketType, packet_type, sizeof(packet_type)) ||
            !write_command(kSetBufferBaseAddress, buffer_base, sizeof(buffer_base)) ||
            // After TX/RX return to STDBY_XOSC so the DIO3 TCXO stays running
            // and SetTx/SetRx do not wait for the 5 ms TCXO start-up again.
            !write_command(kSetRxTxFallbackMode, fallback, sizeof(fallback))) {
            init_error = RadioInitError::ConfigFailed;
            return false;
        }

        const std::uint32_t frequency_word = sx1262_frequency_word(config.profile.frequency_hz);
        const std::uint8_t frequency[]{
            static_cast<std::uint8_t>(frequency_word >> 24U),
            static_cast<std::uint8_t>(frequency_word >> 16U),
            static_cast<std::uint8_t>(frequency_word >> 8U),
            static_cast<std::uint8_t>(frequency_word),
        };
        const std::uint8_t modulation[]{
            config.profile.spreading_factor,
            kBandwidth125Khz,
            static_cast<std::uint8_t>(config.profile.coding_rate_denominator - 4U),
            static_cast<std::uint8_t>(config.profile.spreading_factor >= 11U ? 1U : 0U),
        };
        // Optimal +22 dBm PA settings (as in LoraMessenger); the output power
        // itself is the clamped TxParams value (<= 13 dBm). To verify on the
        // bench in Task 7.
        const std::uint8_t pa_config[]{0x04U, 0x07U, 0x00U, 0x01U};
        const std::uint8_t tx_params[]{
            std::min(config.profile.transmit_power_dbm, kMaxTxPowerDbm), 0x04U};
        const std::uint8_t ocp[]{0x38U};
        const auto sync_word = sx1262_sync_word_bytes(config.profile.sync_word);
        if (!write_command(kSetRfFrequency, frequency, sizeof(frequency)) ||
            !write_command(kSetModulationParams, modulation, sizeof(modulation)) ||
            !write_command(kSetPaConfig, pa_config, sizeof(pa_config)) ||
            !write_command(kSetTxParams, tx_params, sizeof(tx_params)) ||
            !write_register(kOcpRegister, ocp, sizeof(ocp)) ||
            !write_register(kLoRaSyncWordRegister, sync_word.data(), sync_word.size())) {
            init_error = RadioInitError::ConfigFailed;
            return false;
        }
        std::uint8_t readback[2]{};
        if (!read_register(kLoRaSyncWordRegister, readback, sizeof(readback)) ||
            readback[0] != sync_word[0] || readback[1] != sync_word[1]) {
            init_error = RadioInitError::RadioNoResponse;
            return false;
        }
        if (!start_receive()) {
            init_error = RadioInitError::ConfigFailed;
            return false;
        }
        return true;
    }

    void fail_init(RadioInitError error) noexcept {
        init_error = error;
        shutdown();
    }

    void initialize() noexcept {
        if (!config.valid()) {
            fail_init(RadioInitError::ConfigFailed);
            return;
        }
        spi_descriptor = ::open(config.spi_device.c_str(), open_flags(O_RDWR));
        if (spi_descriptor < 0) {
            fail_init(RadioInitError::SpiOpenFailed);
            return;
        }
        i2c_descriptor = ::open(config.i2c_device.c_str(), open_flags(O_RDWR));
        if (i2c_descriptor < 0) {
            fail_init(RadioInitError::ExpanderMissing);
            return;
        }
        int gpio_chip_descriptor = ::open(config.gpio_chip.c_str(), open_flags(O_RDONLY));
        if (gpio_chip_descriptor < 0) {
            fail_init(RadioInitError::GpioFailed);
            return;
        }
        reset_descriptor =
            request_gpio(gpio_chip_descriptor, config.reset_gpio, true, true, "lora-reset");
        irq_descriptor =
            request_gpio(gpio_chip_descriptor, config.irq_gpio, false, false, "lora-irq");
        busy_descriptor =
            request_gpio(gpio_chip_descriptor, config.busy_gpio, false, false, "lora-busy");
        static_cast<void>(close_descriptor(gpio_chip_descriptor));
        if (reset_descriptor < 0 || irq_descriptor < 0 || busy_descriptor < 0) {
            fail_init(RadioInitError::GpioFailed);
            return;
        }

        std::uint8_t mode = SPI_MODE_0;
        std::uint8_t bits = 8U;
        std::uint32_t speed = config.spi_speed_hz;
        if (::ioctl(spi_descriptor, SPI_IOC_WR_MODE, &mode) != 0 ||
            ::ioctl(spi_descriptor, SPI_IOC_WR_BITS_PER_WORD, &bits) != 0 ||
            ::ioctl(spi_descriptor, SPI_IOC_WR_MAX_SPEED_HZ, &speed) != 0) {
            fail_init(RadioInitError::SpiOpenFailed);
            return;
        }
        if (!set_antenna_switch(true)) {
            fail_init(RadioInitError::ExpanderMissing);
            return;
        }
        if (!reset_radio()) {
            fail_init(RadioInitError::RadioNoResponse);
            return;
        }
        if (!configure_radio()) {
            fail_init(init_error == RadioInitError::None ? RadioInitError::ConfigFailed
                                                         : init_error);
            return;
        }
        init_error = RadioInitError::None;
        initialized = true;
        closed = false;
    }

    // Sustained carrier sense: sample GetRssiInst every ~200 us for
    // carrier_sense_ms. Any sample at or above the threshold, or any sign of a
    // packet in progress, means Busy. The radio is in continuous RX here.
    RadioStart carrier_sense() noexcept {
        const auto end = Clock::now() + std::chrono::milliseconds(config.carrier_sense_ms);
        for (;;) {
            bool irq_high = false;
            std::uint8_t raw_rssi = 0U;
            if (!gpio_get(irq_descriptor, irq_high) ||
                !read_command(kGetRssiInst, nullptr, 0U, &raw_rssi, 1U)) {
                return fault_tx();
            }
            if (irq_high || sx1262_carrier_busy(raw_rssi, config.lbt_threshold_dbm)) {
                return RadioStart::Busy;
            }
            if (Clock::now() >= end) {
                return RadioStart::Started;
            }
            std::this_thread::sleep_for(kCarrierSenseInterval);
        }
    }

    RadioStart try_start_transmit(const std::uint8_t* data, std::size_t size) noexcept {
        const auto t0 = Clock::now();
        const RadioStart result = try_start_transmit_untimed(data, size);
        last_tx_start_us = elapsed_us(t0);
        return result;
    }

    RadioStart try_start_transmit_untimed(const std::uint8_t* data, std::size_t size) noexcept {
        if (!ready() || !data || size == 0U || size > kMaxRadioPacket) {
            return RadioStart::Failed; // caller error / already faulted: no new latch
        }
        if (transmitting) {
            return RadioStart::Busy;
        }

        bool irq_high = false;
        std::uint16_t irq = 0U;
        if (!gpio_get(irq_descriptor, irq_high) || !read_irq(irq)) {
            return fault_tx();
        }
        if (rx_in_progress(irq) || (irq & kIrqRxDone) != 0U) {
            return RadioStart::Busy;
        }
        if (irq_high && (irq & kReceiveIrqs & ~kRxInProgressIrqs) != 0U) {
            return RadioStart::Busy; // pending event; poll() will service it
        }
        const RadioStart sensed = carrier_sense();
        if (sensed != RadioStart::Started) {
            return sensed;
        }

        const std::uint8_t standby[]{kStandbyXosc};
        if (!write_command(kSetStandby, standby, sizeof(standby)) || !set_packet_params(size)) {
            return fault_tx();
        }
        std::array<std::uint8_t, kMaxRadioPacket + 1U> buffer{};
        buffer[0] = 0U;
        std::copy_n(data, size, buffer.begin() + 1);
        if (!write_command(kWriteBuffer, buffer.data(), size + 1U) || !clear_irq() ||
            !set_irq_mask(kTransmitIrqs)) {
            return fault_tx();
        }
        const std::uint8_t timeout[]{
            static_cast<std::uint8_t>(kTxTimeoutUnits >> 16U),
            static_cast<std::uint8_t>(kTxTimeoutUnits >> 8U),
            static_cast<std::uint8_t>(kTxTimeoutUnits),
        };
        if (!write_command(kSetTx, timeout, sizeof(timeout))) {
            return fault_tx();
        }
        transmitting = true;
        rx_in_progress_since_valid = false;
        return RadioStart::Started;
    }

    RadioPoll failed() noexcept {
        faulted = true;
        return {RadioEvent::Failed, 0U, 0};
    }

    RadioPoll poll(std::uint8_t* destination, std::size_t destination_capacity) noexcept {
        if (!ready() || !destination || destination_capacity == 0U) {
            return failed();
        }
        bool irq_high = false;
        if (!gpio_get(irq_descriptor, irq_high)) {
            return failed();
        }
        if (!irq_high) {
            return {};
        }

        std::uint16_t irq = 0U;
        if (!read_irq(irq)) {
            return failed();
        }
        if ((irq & kIrqTimeout) != 0U && transmitting) {
            // TX timed out: report it and go back to receive so the radio
            // does not stay wedged in "transmitting".
            if (!clear_irq(irq) || !start_receive()) {
                return failed(); // cannot re-arm: latch fault
            }
            return {RadioEvent::Failed, 0U, 0};
        }
        if ((irq & kIrqTimeout) != 0U) {
            if (!clear_irq(irq) || !start_receive()) {
                return failed();
            }
            return {};
        }
        if ((irq & kIrqTxDone) != 0U) {
            if (!clear_irq(irq) || !start_receive()) {
                return failed();
            }
            return {RadioEvent::TxDone, 0U, 0};
        }
        if ((irq & (kIrqHeaderError | kIrqCrcError)) != 0U) {
            if (!clear_irq(irq) || !start_receive()) {
                return failed();
            }
            return {};
        }
        if ((irq & kIrqRxDone) == 0U) {
            static_cast<void>(rx_in_progress(irq)); // clears stale preamble flags
            return {};
        }

        std::uint8_t status[2]{};
        std::uint8_t packet_status[3]{};
        if (!read_command(kGetRxBufferStatus, nullptr, 0U, status, sizeof(status)) ||
            !read_command(kGetPacketStatus, nullptr, 0U, packet_status, sizeof(packet_status))) {
            return failed();
        }
        const auto payload_size = static_cast<std::size_t>(status[0]);
        const std::int16_t rssi = sx1262_rssi_dbm(packet_status[0]);
        if (payload_size == 0U || payload_size > destination_capacity ||
            payload_size > kMaxRadioPacket) {
            // Unusable packet: drop it and keep listening.
            if (!clear_irq(irq) || !start_receive()) {
                return failed();
            }
            return {};
        }
        const std::uint8_t offset[]{status[1]};
        if (!read_command(kReadBuffer, offset, sizeof(offset), destination, payload_size) ||
            !clear_irq(irq) || !start_receive()) {
            return failed();
        }
        return {RadioEvent::Received, payload_size, rssi};
    }

    void shutdown() noexcept {
        if (closed) {
            return;
        }
        if (spi_descriptor >= 0 && busy_descriptor >= 0 && initialized) {
            const std::uint8_t sleep[]{0x00U};
            static_cast<void>(write_command(kSetSleep, sleep, sizeof(sleep)));
        }
        if (antenna_switch_enabled) {
            static_cast<void>(set_antenna_switch(false));
        }
        initialized = false;
        transmitting = false;
        closed = true;
        static_cast<void>(close_descriptor(reset_descriptor));
        static_cast<void>(close_descriptor(irq_descriptor));
        static_cast<void>(close_descriptor(busy_descriptor));
        static_cast<void>(close_descriptor(spi_descriptor));
        static_cast<void>(close_descriptor(i2c_descriptor));
    }

    Sx1262Config config;
    RadioInitError init_error{RadioInitError::None};
    int spi_descriptor{-1};
    int i2c_descriptor{-1};
    int reset_descriptor{-1};
    int irq_descriptor{-1};
    int busy_descriptor{-1};
    bool antenna_switch_enabled{false};
    bool transmitting{false};
    bool initialized{false};
    bool faulted{false};
    std::uint32_t last_tx_start_us{0};
    std::uint32_t last_rx_rearm_us{0};
    bool closed{false};
    bool rx_in_progress_since_valid{false};
    Clock::time_point rx_in_progress_since{};
};

Sx1262Radio::Sx1262Radio(Sx1262Config config) noexcept
    : impl_(std::make_unique<Impl>(std::move(config))) {}

Sx1262Radio::~Sx1262Radio() = default;

RadioInitError Sx1262Radio::init_error() const noexcept {
    return impl_ ? impl_->init_error : RadioInitError::ConfigFailed;
}

std::uint32_t Sx1262Radio::last_tx_start_us() const noexcept {
    return impl_ ? impl_->last_tx_start_us : 0U;
}

std::uint32_t Sx1262Radio::last_rx_rearm_us() const noexcept {
    return impl_ ? impl_->last_rx_rearm_us : 0U;
}

bool Sx1262Radio::ready() const { return impl_ && impl_->ready(); }

RadioStart Sx1262Radio::try_start_transmit(const std::uint8_t* data, std::size_t size) {
    return impl_ ? impl_->try_start_transmit(data, size) : RadioStart::Failed;
}

RadioPoll Sx1262Radio::poll(std::uint8_t* dst, std::size_t cap) {
    return impl_ ? impl_->poll(dst, cap) : RadioPoll{RadioEvent::Failed, 0U, 0};
}

void Sx1262Radio::shutdown() {
    if (impl_) {
        impl_->shutdown();
    }
}

} // namespace battleship::net::lora
