# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

XiaoZhi ESP32 is a voice-based AI chatbot firmware for ESP32 microcontrollers (S3/C3/P4) that communicates with LLM backends (Qwen, DeepSeek, etc.) via WebSocket or MQTT+UDP. Current version: 2.2.4. Requires ESP-IDF >=5.4.0.

## Build Commands

This is an ESP-IDF project. All builds require the IDF environment to be sourced.

```bash
# Configure for a specific board (required before first build)
idf.py set-target esp32s3
idf.py menuconfig   # Select board under "XiaoZhi Configuration"

# Build
idf.py build

# Flash and monitor
idf.py flash monitor

# Flash assets partition (wake words, fonts, audio, images)
idf.py -p PORT flash -f assets

# Build with specific board target
idf.py -DBOARD=<board_name> build
```

## Code Style

Uses clang-format with Google C++ style (customized):
- 4-space indentation, 100-character line limit
- Attach brace style, auto header sorting
- Run `clang-format` before committing (pre-commit formatting required)

Config is in `.clang-format` at repo root.

## Architecture

### Core Application Flow

`main.cc` → `Application` singleton (event-driven, FreeRTOS-based) → `DeviceStateMachine`

Device states: idle → listening → processing → speaking → error

The `Application` class in `main/application.cc` is the central orchestrator. It owns the audio pipeline, protocol connection, MCP server, and display.

### Key Subsystems

**Protocols** (`main/protocols/`): Two implementations behind a common `Protocol` interface:
- `WebSocketProtocol` — WebSocket streaming (default for most boards)
- `MqttProtocol` — MQTT+UDP hybrid for bandwidth optimization

Both carry a JSON-RPC 2.0 MCP protocol layer for IoT tool/resource control.

**Audio pipeline** (`main/audio/`):
- Three concurrent FreeRTOS tasks: input capture, codec processing, output playback
- Opus codec for compression; ESP-SR for wake word detection + VAD + AEC
- Hardware abstracted via `AudioCodec` base class; concrete implementations for ES8311/ES8374/ES8388/ES8389

**Display** (`main/display/`):
- Base `Display` interface with `OledDisplay`, `LcdDisplay`, and `EmoteDisplay` implementations
- LVGL 9.x used for LCD panels; emoji/GIF/JPEG support included

**Board HAL** (`main/boards/`):
- 90+ board configs, each inheriting from `Board` (WiFi), `Ml307Board` (4G), or `DualNetworkBoard`
- Board selection done at compile-time via `menuconfig`
- Common utilities (audio codecs, power management, camera, input handling) in `main/boards/common/`

**MCP Server** (`main/mcp_server.cc`):
- Implements device-side MCP allowing cloud or local tools to control the device
- Tools registered at startup; invoked via JSON-RPC over the active protocol channel

### Partition Layout (v2, current)

| Flash | App | Assets |
|-------|-----|--------|
| 8MB   | 3MB | 2MB    |
| 16MB  | 4MB | 8MB    |
| 32MB  | 4MB | 16MB   |

Assets partition stores wake word models, fonts, audio effects, background images, emoji packs. **v2 is incompatible with v1** — OTA upgrade between versions is not supported; manual flash required.

### Adding a New Board

Follow `docs/custom-board.md`. Create a directory under `main/boards/`, implement the `Board` subclass, and register it in `main/CMakeLists.txt`.

## Key Docs

- `docs/websocket.md` — WebSocket protocol spec
- `docs/mqtt-udp.md` — MQTT+UDP protocol spec
- `docs/mcp-protocol.md` — MCP JSON-RPC 2.0 interaction flow
- `docs/custom-board.md` — Adding new hardware support
- `docs/code_style.md` — Full formatting rules
