# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

XiaoZhi ESP32 is an AI voice chatbot firmware (v2.2.4) supporting 95+ hardware board variants. It uses ESP-IDF 5.4+ and implements a streaming ASR+LLM+TTS pipeline with MCP protocol for device/cloud control. Default server: xiaozhi.me.

## Build & Flash

**Prerequisites**: ESP-IDF v5.4+ with `idf.py` and `source $IDF_PATH/export.sh`

```bash
# Build for a specific board (uses scripts/release.py for multi-board builds)
python scripts/release.py <board-name> --name <output-name>

# Direct idf.py build
idf.py set-target esp32s3   # or esp32, esp32c3, esp32p4
idf.py build

# Create merged flashable binary
idf.py merge-bin             # outputs build/merged-binary.bin

# Flash
idf.py flash -p <PORT>
idf.py monitor -p <PORT>
```

**CI**: GitHub Actions (`.github/workflows/build.yml`) uses `espressif/idf:v5.5.2` container and auto-detects affected boards from PR changes.

## Architecture

### Core Event Loop
`main/main.cc` → `Application` singleton (`main/application.cc`) runs on FreeRTOS. The `Application` class owns the entire device lifecycle via an event-group-driven state machine.

**Device States**: Starting → Idle ↔ Listening ↔ Speaking/Thinking → Sleeping/Error

### Audio Pipeline
Three concurrent FreeRTOS tasks communicate via queues:
- **AudioInputTask**: Captures from codec HAL, runs wake word detection (ESP-SR), VAD, AEC
- **OpusCodecTask**: Encodes/decodes Opus frames with dynamic resampling
- **AudioOutputTask**: Decodes and plays to codec HAL

See `main/audio/README.md` for detailed pipeline documentation.

### Communication Protocols
`main/protocols/protocol.h` defines the base interface. Two implementations:
- **WebSocket** (`websocket_protocol.cc`): Single connection, simpler
- **MQTT+UDP** (`mqtt_protocol.cc`): Lower bandwidth, hybrid signaling+audio

Both use JSON for signaling and Opus-encoded audio frames.

### Board Hardware Abstraction
Each board defines:
- `main/boards/<board-name>/config.h` — GPIO pins, hardware features
- `main/boards/<board-name>/config.json` — metadata for firmware releases

`main/boards/common/` contains shared implementations: WiFi, ML307 4G, power management, button handling.

**Adding a new board**: Follow `docs/custom-board.md`. Create a subdirectory under `main/boards/`, implement the board class inheriting from `WifiBoard` or `Ml307Board`, and add to `main/CMakeLists.txt`.

### MCP Server
`main/mcp_server.cc/h` implements the Model Context Protocol for AI tool use. Tools are registered at startup and called by the AI model to control hardware (speaker volume, screen brightness, camera capture, GPIO, etc.). Supports property types: bool, int, string, number, array, object.

### Display System
Supports SPI LCD, I2C OLED, and LVGL-based displays. Theme system with emoji rendering, GIF/JPEG support, battery indicator. Display drivers in `main/display/`.

### OTA Updates
`main/ota.cc/h` handles firmware updates. The v2 partition table (`partitions/v2/`) adds an `assets` partition for dynamic content (wake words, themes, fonts). **v2 is not backward-compatible with v1 partition tables.**

## Partition Tables

Located in `partitions/v2/`: `8m.csv`, `16m.csv` (default), `32m.csv`. Selected via `sdkconfig.defaults` per target. The `assets` partition stores customizable content loaded at runtime.

## Code Style

Google C++ style. A `.clang-format` file is present at the root — run `clang-format` before committing C++ changes.

## Key sdkconfig Flags

- `CONFIG_MINIMAL_BUILD` — reduces binary size by excluding unused board code
- C++ exceptions and RTTI are **enabled** (`CONFIG_COMPILER_CXX_EXCEPTIONS=y`)
- Optimization: size (`CONFIG_COMPILER_OPTIMIZATION_SIZE=y`)
