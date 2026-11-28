/**
 * SPDX-FileCopyrightText: 2026 yutacar
 *
 * SPDX-License-Identifier: MIT
 *
 * Transport-agnostic full-game driver shared by the NetSession (loopback) and
 * LoRaTransport (fake radio) tests.
 */

#pragma once

#include <cstdint>
#include <functional>

#include "net/net_session.h"

namespace battleship::testing {

/// Plays a complete game between two sessions: places fleets, readies up and
/// fires seeded-shuffle shots until both sides are Finished (or a Disconnect /
/// the step budget fails the test). `step(now_ms)` must advance whatever
/// drives the sessions (bus, links, transports) and poll both sessions.
/// Returns the number of steps consumed.
int run_full_game(net::NetSession& host, net::NetSession& client,
                  std::function<void(uint32_t now_ms)> step, uint32_t fire_seed,
                  uint32_t step_ms = 25);

/// gtest expectations that both peers ended in a consistent Finished state.
void expect_consistent_final_state(const net::NetSession& host, const net::NetSession& client);

} // namespace battleship::testing
