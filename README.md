# Battleship for CardputerZero

A classic naval-combat game built for the [M5Stack CardputerZero](https://cardputerzero.github.io/) (320×170, LVGL v9.5 + C++/CMake).

- **1-player**: fight a local, on-device probability-density AI (no cloud, no LLM) across three difficulty levels, with live template battle commentary you can toggle off in Settings.
- **2-player**: play another CardputerZero over **BLE** (default, no access point needed), **WiFi** (LAN), or pass-and-play **hotseat** on a single unit.
- **Languages**: English, 日本語 (Japanese), 简体中文 (Simplified Chinese) — switchable anytime from Settings, no restart required.

日本語版の README は [README.ja.md](README.ja.md) を参照してください。

## Screenshots

| Menu | Difficulty | Placement |
| --- | --- | --- |
| ![Menu](docs/screenshots/i18n_en_01_menu.png) | ![Difficulty](docs/screenshots/difficulty_select.png) | ![Placement](docs/screenshots/placement_dense_en.png) |

| Battle | Result | Network play |
| --- | --- | --- |
| ![Battle](docs/screenshots/battle_dense_en.png) | ![Result](docs/screenshots/i18n_en_08_result.png) | ![Network](docs/screenshots/i18n_en_12_net_host_lobby.png) |

More screenshots, including the Japanese and Simplified Chinese UI, are in [`docs/screenshots/`](docs/screenshots/) (see the `i18n_ja_*` and `i18n_zh_*` files).

## How to Play

Classic Battleship rules: each player secretly places a fleet of 5 ships (Carrier·5, Battleship·4, Cruiser·3, Submarine·3, Destroyer·2) on a 10×10 grid, ships placed in a straight line without overlapping. Players then take turns calling a cell on the opponent's grid; a hit is marked, a sunk ship is announced, and the first player to sink the entire enemy fleet wins.

### Key Controls

| Key | Action |
| --- | --- |
| Arrow keys | Move the cursor / change focus |
| Enter | Confirm — place a ship, fire at the selected cell, confirm handover/ready |
| `R` | Rotate ship orientation (Placement screen) |
| `X` | Randomize the current fleet's placement (Placement screen) |
| Backspace | Undo the last placed ship (Placement screen) |
| Esc (short press) | Go back one screen / dismiss the current dialog (asks for confirmation during an active Battle/Placement/Handover) |
| Esc (long press, ~1s) or Home | Quit the app safely from anywhere (any active network match is ended gracefully) |

## Installation

**From the CardputerZero Hub AppStore (recommended)**: search for "Battleship" in the Hub app and install it directly on your device.

**Manual install** (if you have a `.deb` built or downloaded separately):

```shell
sudo dpkg -i battleship_1.0.0_arm64.deb
# or, to also pull in any missing dependencies:
sudo apt install --no-install-recommends ./battleship_1.0.0_arm64.deb
```

The app appears in APPLaunch after installation.

### Notes for playing on real hardware

- **BLE (default 2-player mode)** requires the device's Bluetooth stack (BlueZ) to be running and the app's user account to be in the `bluetooth` group (`sudo usermod -aG bluetooth <user>`, then re-login). Without this, BLE features report "Bluetooth unavailable" instead of crashing. If a device doesn't show up when scanning, also check `rfkill unblock bluetooth`. BLE play works best with Wi-Fi turned off (the Bluetooth and Wi-Fi radios share one antenna); the app retries the connection automatically.
- **WiFi 2-player mode** uses LAN-only discovery (UDP broadcast on port 35353, TCP game traffic on port 35354) — both devices must be on the same access point/subnet. No data ever leaves the LAN.
- The game runs entirely offline; no telemetry or external network calls are made outside of an explicit WiFi match.
- Battle commentary is a fixed template-phrase system (no local LLM is used or required) — see [Architecture](#architecture) below.

## Development

### Build (macOS desktop)

Install dependencies with Homebrew:

```shell
brew install cmake ninja libpng jpeg fmt sdl2 freetype dpkg zlib
```

Configure, build, and test (Apple Silicon):

```shell
cmake --preset darwin-arm64
cmake --build --preset darwin-arm64-dbg
ctest --preset darwin-arm64-dbg
```

Run the desktop simulator:

```shell
./build/darwin-arm64/Debug/battleship
```

For Intel Macs, use the `darwin-x86-64` presets instead. See [`NOTES.md`](NOTES.md) for Linux desktop builds, the CardputerZero cross-build/device build (including the Docker `debian:trixie` device-build runbook), CMake options (`BATTLESHIP_BLE`, `BATTLESHIP_LLM`, `BATTLESHIP_WINDOW_ZOOM`, etc.), and the full phase-by-phase development history.

### Architecture

The codebase separates game logic, networking, AI, and UI so the whole game can be developed and tested on desktop before ever touching real hardware:

```
view/        LVGL v9 screens, widgets, ship-pattern rendering, key input handling
i18n/        String catalog (EN/JA/ZH) + CJK font loading, no LVGL/game-logic dependency
net/         Transport-agnostic protocol (framing, ACK/retry) with TcpTransport (WiFi)
             and BleTransport (BlueZ/D-Bus) implementations behind one ITransport interface
core/        Pure C++ game logic — board, ship placement/validation, shot resolution,
             the probability-density AI opponent. No I/O, no LVGL, no threads.
commentary/  Template-based battle commentary (TemplateCommentator); an LLM-backed
             commentator exists in the tree behind a default-OFF build flag but is unused
```

`core/` and `net/` have no LVGL dependency and are covered by an extensive `ctest` suite (headless, including AI-vs-AI statistical tests and two-process network match simulations). See [`PLAN.md`](PLAN.md) for the original design rationale and [`NOTES.md`](NOTES.md) for what actually got built, including known limitations.

## License

This project is released under the [MIT License](LICENSE). Third-party components (LVGL, fonts, fmt, etc.) are under their own licenses — see [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md) for the full list and what ships in the `.deb`.
