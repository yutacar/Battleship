/**
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "core/types.h"

namespace battleship::core {

std::string to_string(ShipType type) {
    switch (type) {
        case ShipType::Carrier:
            return "Carrier";
        case ShipType::Battleship:
            return "Battleship";
        case ShipType::Cruiser:
            return "Cruiser";
        case ShipType::Submarine:
            return "Submarine";
        case ShipType::Destroyer:
            return "Destroyer";
    }
    return "Unknown";
}

}  // namespace battleship::core
