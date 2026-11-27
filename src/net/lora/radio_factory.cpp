/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#include "net/lora/radio_factory.h"

#include <cstdlib>
#include <cstring>

#include "net/lora/udp_sim_radio.h"
#if BATTLESHIP_LORA_HW
#include "net/lora/sx1262_radio.h"
#endif

namespace battleship::net::lora {

RadioBackend default_radio_backend() {
#if BATTLESHIP_LORA_HW
    const char* sim = std::getenv("BATTLESHIP_LORA_SIM");
    if (sim != nullptr && std::strcmp(sim, "1") == 0) return RadioBackend::UdpSim;
    return RadioBackend::Hardware;
#else
    return RadioBackend::UdpSim;
#endif
}

RadioHandle open_radio(RadioBackend backend) {
    RadioHandle h;
    if (backend == RadioBackend::UdpSim) {
        h.radio = std::make_unique<UdpSimRadio>();
        return h;
    }
#if BATTLESHIP_LORA_HW
    h.power = std::make_unique<CapPower>();
    if (!h.power->acquire()) {
        h.power.reset();
        h.error = RadioInitError::PowerFailed;
        return h;
    }
    auto radio = std::make_unique<Sx1262Radio>(Sx1262Config{});
    if (radio->init_error() != RadioInitError::None) {
        h.error = radio->init_error();
        radio.reset(); // radio first, then power (ownership rule)
        h.power->release();
        h.power.reset();
        return h;
    }
    h.radio = std::move(radio);
    return h;
#else
    h.error = RadioInitError::SpiOpenFailed;
    return h;
#endif
}

} // namespace battleship::net::lora
