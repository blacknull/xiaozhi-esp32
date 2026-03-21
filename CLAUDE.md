# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

XiaoZhi ESP32 is an open-source AI voice assistant firmware for ESP32 devices, supporting 93+ hardware board variants. It connects to a backend AI service via WebSocket or MQTT+UDP, streams Opus-encoded audio, and provides an MCP (Model Context Protocol) server for LLM-callable device tools.

## Build Commands

This project uses ESP-IDF (v5.4+) with CMake. All commands use `idf.py`.

```bash
idf.py set-target esp32s3      # Set chip target (esp32, esp32s3, esp32c3, esp32p4)
idf.py menuconfig              # Configure board type, language, features
idf.py build                   # Build firmware
idf.py flash -p /dev/ttyUSB0   # Flash to device
idf.py monitor                 # Serial monitor (Ctrl+] to exit)
idf.py flash monitor           # Flash and monitor in one step
idf.py fullclean               # Clean all build artifacts
```

Board selection happens in `menuconfig` under the board-specific Kconfig options. There is no single-test command — testing is done by flashing and monitoring.

## Architecture

### Core Abstraction Layers

The firmware is built on five abstract interfaces, each with multiple concrete implementations:

| Interface | Location | Implementations |
|-----------|----------|-----------------|
| `Board` | `main/boards/common/board.h` | 93 board-specific classes |
| `Protocol` | `main/protocols/protocol.h` | WebSocket, MQTT+UDP |
| `AudioCodec` | `main/audio/audio_codec.h` | ES8311, ES8388, Box, etc. |
| `Display` | `main/display/display.h` | LCD, OLED, LVGL variants |
| `Led` | `main/led/led.h` | Single, circular strip, GPIO |

### Board Abstraction

**Hierarchy:** `Board` → `WifiBoard`/`Ml307Board`/`DualNetworkBoard` → device-specific class

Each board lives in `main/boards/<board-name>/` and contains:
- `config.h` — GPIO pin assignments
- `config.json` — chip target and build options
- `*_board.cc` — board class with `DECLARE_BOARD()` macro (creates factory `create_board()`)

The `Application` singleton calls `create_board()` at startup to instantiate the correct hardware abstraction.

### Audio Pipeline

Three FreeRTOS tasks handle streaming audio:
- **AudioInputTask** — microphone → processor → encode queue
- **OpusCodecTask** — Opus encode/decode
- **AudioOutputTask** — decode queue → speaker

`AudioService` (`main/audio/audio_service.h`) manages dual queues: TX (mic→network) and RX (network→speaker). It's allocated in PSRAM. The codec processes 16-bit PCM at 16kHz; Opus frames are 24–60ms.

Audio processors (`main/audio/processors/`) optionally apply AEC, AFE noise reduction (ESP-SR), and wake word detection.

### MCP Server

`McpServer` (`main/mcp_server.h`) provides device-side tools callable by the LLM:
- `AddTool()` — registers AI-visible tools
- `AddUserOnlyTool()` — registers tools only visible in device UI, not to the AI

Tools return `std::variant<bool, int, std::string, cJSON*, ImageContent*>`. A dedicated thread handles blocking tool calls.

### Application State Machine

`Application` (`main/application.h`) is a singleton orchestrating all subsystems. Key states: `kDeviceStateStarting`, `kDeviceStateReady`, `kDeviceStateChatting`, `kDeviceStateSleeping`. Events are dispatched via FreeRTOS `EventGroup` and a deque-based task queue for ISR-safe scheduling.

### Protocol Layer

Both `WebsocketProtocol` and `MqttProtocol` implement the same `Protocol` interface with callback-driven async handling. Binary audio uses a v2/v3 header format with timestamps; JSON carries session metadata, transcripts, and TTS responses.

### Display

`LvglDisplay` wraps the LVGL 9.x GUI framework with emoji, GIF, and theme support. Multiple SPI panel drivers are supported (ST7789, ILI9341, GC9A01, etc.) with optional touch controllers.

## Key Configuration

`main/Kconfig.projbuild` (580 lines) drives compile-time feature selection:
- Board type (93 options)
- Language/locale (18 options)
- Wake word engine (disabled, WakeNet, custom)
- AEC mode (device-side, server-side, none)
- Network type (WiFi, 4G, dual)

`main/idf_component.yml` manages ~40 component dependencies including `esp-sr`, `lvgl`, `esp-opus-encoder`, and hardware codec drivers.

## Adding New Features

- **New board:** Create `main/boards/<name>/` with `config.h`, `config.json`, and `*_board.cc` using `DECLARE_BOARD()`. Add to `Kconfig.projbuild` and `CMakeLists.txt`.
- **New audio codec:** Implement `AudioCodec` in `main/audio/codecs/`.
- **New display panel:** Implement `Display` in `main/display/`.
- **New MCP tool:** Call `McpServer::GetInstance().AddTool()` or `AddUserOnlyTool()` in board or application init.
- **New protocol:** Implement `Protocol` interface in `main/protocols/`.

## Code Style

Google C++ style guide. C++17. Headers use `.h`, implementation files use `.cc`.
