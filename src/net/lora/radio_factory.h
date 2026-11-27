/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <memory>

#include "net/lora/cap_power.h"
#include "net/lora/radio_iface.h"

namespace battleship::net::lora {

enum class RadioBackend { Hardware, UdpSim };

// Members are declared power first so destruction runs radio -> power: the
// radio is torn down before the Cap 5V output may be switched off.
struct RadioHandle {
    std::unique_ptr<CapPower> power;
    std::unique_ptr<IRadio> radio;
    RadioInitError error = RadioInitError::None;
};

// Hardware when built with BATTLESHIP_LORA_HW and BATTLESHIP_LORA_SIM != "1",
// otherwise UdpSim.
RadioBackend default_radio_backend();

// On failure the returned handle has no radio and error set; any power this
// process turned on has already been released.
RadioHandle open_radio(RadioBackend backend);

} // namespace battleship::net::lora
