# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> See `AGENTS.md` for comprehensive architecture, project structure, and feature addition guides.

## Project

XiaoZhi ESP32 — open-source AI voice assistant firmware for 93+ ESP32 hardware variants. Streams Opus-encoded audio to a backend AI service via WebSocket or MQTT+UDP; exposes device control via an MCP (Model Context Protocol) server.

- Framework: ESP-IDF v5.4+, C++17, FreeRTOS
- Build: CMake via `idf.py`
- GUI: LVGL 9.x

## Build Commands

```bash
idf.py set-target esp32s3       # set chip (esp32, esp32s3, esp32c3, esp32p4)
idf.py menuconfig               # select board type, language, features
idf.py build
idf.py flash -p /dev/ttyUSB0
idf.py monitor                  # Ctrl+] to exit
idf.py flash monitor
idf.py fullclean
python scripts/release.py <board-dir-name>   # production build
```

Board type is **compile-time only** — set it in `menuconfig`, not at runtime.
There is no unit test framework; testing means flash + monitor.

## Code Style

- Google C++ Style Guide, C++17
- Files: `.h` / `.cc` (never `.cpp`)
- Comments: Chinese (project convention)
- Classes/public methods: `PascalCase`; locals: `snake_case`; members: `trailing_underscore_`

## Key Architecture Points

**Five core abstractions** (each has multiple concrete implementations):
- `Board` (`main/boards/common/board.h`) — 93+ hardware variants via `DECLARE_BOARD()` / `create_board()`
- `Protocol` (`main/protocols/protocol.h`) — WebSocket or MQTT+UDP
- `AudioCodec` (`main/audio/audio_codec.h`) — ES8311, ES8388, etc.
- `Display` (`main/display/display.h`) — SPI LCD, OLED, LVGL
- `Led` (`main/led/led.h`) — single LED, circular strip, GPIO

**Application singleton** (`main/application.h/.cc`) owns the state machine, FreeRTOS EventGroup, and ISR-safe deque task queue. It calls `create_board()` at startup.

**Audio pipeline** — three FreeRTOS tasks: mic → encode queue → Opus codec → network TX; network RX → decode queue → speaker. Audio service allocated in PSRAM.

**MCP server** (`main/mcp_server.h/.cc`) — `AddTool()` exposes tools to the LLM; `AddUserOnlyTool()` exposes tools only to the device UI.

## Adding a New Board

1. Create `main/boards/<name>/` with `config.h`, `config.json`, `*_board.cc` (using `DECLARE_BOARD()`)
2. Add entry to `main/Kconfig.projbuild`
3. Add entry to `main/CMakeLists.txt`
4. **Never modify an existing board config** — always create a new board type to avoid OTA conflicts.

See `main/boards/README.md` for a step-by-step template.
