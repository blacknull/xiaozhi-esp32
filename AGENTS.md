# AGENTS.md - XiaoZhi ESP32 AI Voice Chatbot

This file provides essential information for AI coding agents working with the XiaoZhi ESP32 project.

## Project Overview

**XiaoZhi ESP32** is an AI voice chatbot firmware (v2.2.4) for ESP32 microcontrollers. It implements a streaming ASR+LLM+TTS pipeline with MCP (Model Context Protocol) support for device and cloud control.

- **Repository**: https://github.com/78/xiaozhi-esp32
- **License**: MIT License
- **Default Server**: https://xiaozhi.me
- **Supported Platforms**: ESP32, ESP32-S3, ESP32-C3, ESP32-C5, ESP32-C6, ESP32-P4
- **Supported Hardware**: 95+ board variants from various manufacturers

### Key Features

- Offline voice wake-up using ESP-SR
- Streaming ASR + LLM + TTS voice interaction
- Speaker recognition (3D Speaker)
- Two communication protocols: WebSocket or MQTT+UDP
- OPUS audio codec for efficient transmission
- OLED/LCD display support with emoji rendering
- Battery management and power saving
- Multi-language support (Chinese, English, Japanese, etc.)
- Device-side MCP for hardware control (speaker, LED, GPIO, etc.)
- Cloud-side MCP for extended AI capabilities
- OTA firmware updates

## Technology Stack

- **Framework**: ESP-IDF v5.4+ (Espressif IoT Development Framework)
- **Language**: C++ (with C components)
- **RTOS**: FreeRTOS
- **Build System**: CMake with idf.py
- **Audio**: ESP-SR (Speech Recognition), OPUS codec
- **Display**: LVGL 9.2.2 for GUI rendering
- **Network**: Wi-Fi / ML307 Cat.1 4G
- **Protocol**: WebSocket / MQTT + UDP

## Project Structure

```
├── main/                       # Main application code
│   ├── main.cc                 # Application entry point
│   ├── application.cc/h        # Main Application singleton
│   ├── device_state_machine.cc/h  # Device state management
│   ├── audio/                  # Audio pipeline
│   │   ├── audio_codec.cc/h    # Audio codec HAL
│   │   ├── audio_service.cc/h  # Audio service orchestrator
│   │   ├── codecs/             # Audio codec drivers (ES8311, ES8388, etc.)
│   │   ├── processors/         # Audio processors (AEC, VAD)
│   │   ├── wake_words/         # Wake word detection engines
│   │   └── README.md           # Audio architecture documentation
│   ├── boards/                 # Hardware board implementations
│   │   ├── common/             # Shared board implementations
│   │   │   ├── board.cc/h      # Base Board class
│   │   │   ├── wifi_board.cc/h # WiFi board base class
│   │   │   ├── ml307_board.cc/h # 4G modem board base class
│   │   │   └── ...             # Other common components
│   │   └── <board-name>/       # Individual board implementations
│   │       ├── config.h        # GPIO pin definitions
│   │       ├── config.json     # Build configuration
│   │       └── <board>.cc      # Board implementation
│   ├── protocols/              # Communication protocols
│   │   ├── protocol.cc/h       # Protocol base class
│   │   ├── websocket_protocol.cc/h
│   │   └── mqtt_protocol.cc/h
│   ├── display/                # Display drivers
│   │   ├── display.cc/h        # Base display class
│   │   ├── lcd_display.cc/h    # LCD implementation
│   │   ├── oled_display.cc/h   # OLED implementation
│   │   └── lvgl_display/       # LVGL-based displays
│   ├── led/                    # LED controllers
│   ├── mcp_server.cc/h         # MCP protocol implementation
│   ├── ota.cc/h                # OTA update handling
│   ├── settings.cc/h           # Device settings management
│   └── assets.cc/h             # Asset management
├── partitions/v2/              # Partition tables (v2 format)
├── scripts/                    # Build and utility scripts
│   └── release.py              # Multi-board release builder
├── docs/                       # Documentation
├── managed_components/         # ESP-IDF managed components
├── CMakeLists.txt              # Root CMake configuration
├── main/CMakeLists.txt         # Main component CMake
├── main/Kconfig.projbuild      # Project configuration options
├── sdkconfig.defaults          # Default SDK configuration
└── .clang-format               # Code formatting rules
```

## Build System

### Prerequisites

- ESP-IDF v5.4 or later
- Python 3.7+
- `idf.py` command in PATH
- For Linux: `source $IDF_PATH/export.sh`

### Build Commands

```bash
# Set target chip (esp32, esp32s3, esp32c3, esp32p4, etc.)
idf.py set-target esp32s3

# Build the project
idf.py build

# Create merged flashable binary
idf.py merge-bin

# Flash to device
idf.py flash -p <PORT>

# Monitor serial output
idf.py monitor -p <PORT>

# Build and flash in one command
idf.py build flash monitor -p <PORT>
```

### Multi-Board Release Build

```bash
# Build for a specific board (uses config.json)
python scripts/release.py <board-name>

# Build specific variant
python scripts/release.py <board-name> --name <variant-name>

# List all supported boards
python scripts/release.py --list-boards

# Build all boards
python scripts/release.py all
```

The release script:
1. Reads `main/boards/<board>/config.json` for configuration
2. Sets the target chip automatically
3. Applies `sdkconfig_append` options
4. Builds and creates merged binary
5. Packages into `releases/v{version}_{name}.zip`

## Board Configuration

Each board requires:

1. **Directory**: `main/boards/<board-name>/`
2. **config.h**: GPIO pin mappings and hardware configuration
3. **config.json**: Build configuration (target, sdkconfig_append)
4. **<board>.cc**: Board implementation inheriting from `WifiBoard` or `Ml307Board`

### Board Class Hierarchy

```
Board (base class)
├── WifiBoard          # Wi-Fi connected boards
├── Ml307Board         # 4G ML307 modem boards
├── Nt26Board          # NT26 4G modem boards
└── DualNetworkBoard   # Wi-Fi + 4G dual network
```

### Adding a New Board

1. Create directory: `mkdir main/boards/my-board`
2. Create `config.h` with pin definitions
3. Create `config.json`:
   ```json
   {
       "target": "esp32s3",
       "builds": [
           {
               "name": "my-board",
               "sdkconfig_append": [
                   "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y"
               ]
           }
       ]
   }
   ```
4. Create board implementation file
5. Add entry to `main/Kconfig.projbuild`
6. Add entry to `main/CMakeLists.txt`

See `docs/custom-board.md` for detailed instructions.

## Architecture Overview

### Core Event Loop

The `Application` class (`main/application.cc`) runs on FreeRTOS and manages the device lifecycle via an event-group-driven state machine:

**Device States**:
- `kDeviceStateStarting` → Initial boot
- `kDeviceStateIdle` → Waiting for wake word
- `kDeviceStateListening` → Recording user speech
- `kDeviceStateSpeaking` → Playing AI response
- `kDeviceStateThinking` → Processing
- `kDeviceStateSleeping` → Low power mode

**Main Events**:
- `MAIN_EVENT_WAKE_WORD_DETECTED`
- `MAIN_EVENT_NETWORK_CONNECTED`
- `MAIN_EVENT_TOGGLE_CHAT`
- `MAIN_EVENT_START_LISTENING`
- `MAIN_EVENT_STOP_LISTENING`

### Audio Pipeline

Three concurrent FreeRTOS tasks:

1. **AudioInputTask**: Captures from codec, runs wake word detection, VAD
2. **OpusCodecTask**: Encodes/decodes Opus frames
3. **AudioOutputTask**: Decodes and plays audio

See `main/audio/README.md` for detailed documentation.

### Communication Protocols

Two protocol implementations:

1. **WebSocket Protocol** (`websocket_protocol.cc`)
   - Single connection for signaling and audio
   - Simpler implementation
   - JSON signaling + binary OPUS audio

2. **MQTT+UDP Protocol** (`mqtt_protocol.cc`)
   - MQTT for signaling, UDP for audio
   - Lower bandwidth requirements
   - Better for cellular networks

### MCP Server

The Model Context Protocol implementation (`mcp_server.cc/h`) allows AI models to control hardware:

- Tool registration at startup
- Property types: bool, int, string, number, array, object
- Supports device control (speaker, screen, battery, GPIO)
- Supports cloud-side tools (smart home, PC control)

## Partition Tables

Version 2 partition tables in `partitions/v2/`:

| File | Flash Size | ota_0/ota_1 | assets |
|------|-----------|-------------|--------|
| 4m.csv | 4MB | 1.5MB each | - |
| 8m.csv | 8MB | 3MB each | 2MB |
| 16m.csv | 16MB | 4MB each | 8MB |
| 16m_c3.csv | 16MB (ESP32-C3) | 4MB each | 4MB |
| 32m.csv | 32MB | 4MB each | 16MB |

**Note**: v2 partition tables are NOT backward-compatible with v1. OTA upgrade from v1 to v2 requires manual reflashing.

## Code Style Guidelines

The project uses **Google C++ Style** with customizations:

- **Column limit**: 100 characters
- **Indent**: 4 spaces (no tabs)
- **Pointer alignment**: Left (`int* ptr`)
- **Function definitions**: Short functions allowed on single line
- **Include ordering**: System headers first, then project headers

### Formatting

```bash
# Format all source files
clang-format -i main/**/*.cc main/**/*.h

# Format specific file
clang-format -i main/application.cc
```

The `.clang-format` file is at project root.

## Key Configuration Options

### sdkconfig.defaults

Important settings:
- `CONFIG_COMPILER_CXX_EXCEPTIONS=y` - C++ exceptions enabled
- `CONFIG_COMPILER_CXX_RTTI=y` - RTTI enabled
- `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` - Optimize for size
- `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/v2/16m.csv"`

### Kconfig.projbuild

Project-specific options accessible via `idf.py menuconfig`:

- **OTA_URL**: Default OTA update server
- **Flash Assets**: Select assets to flash
- **Language**: 25+ supported languages
- **Board Type**: Select hardware board
- **Wake Word**: Disabled / ESP / AFE / Custom
- **Display Style**: Default / WeChat / Emote
- **WiFi Provisioning**: Hotspot / Acoustic / BluFi

## Testing and Debugging

### Serial Monitor

```bash
idf.py monitor -p <PORT>
```

Common log tags:
- `Application` - Main application events
- `AudioService` - Audio pipeline
- `Protocol` - Communication protocol
- `Board` - Hardware board

### Audio Debugging

Enable in menuconfig:
- `CONFIG_USE_AUDIO_DEBUGGER=y`
- Set `CONFIG_AUDIO_DEBUG_UDP_SERVER="192.168.1.100:8000"`

### WiFi Provisioning

Hold BOOT button during startup to enter WiFi configuration mode.

## CI/CD

GitHub Actions workflow (`.github/workflows/build.yml`):
- Uses `espressif/idf:v5.5.2` container
- Auto-detects affected boards from PR changes
- Builds all variants on main branch push
- Builds affected boards on PR

## Important Notes for Developers

1. **Board Type Uniqueness**: Each custom board must have a unique identifier. Do not reuse existing board types with different pin configurations.

2. **Partition Compatibility**: v2 partition tables are incompatible with v1. Manual reflash required when upgrading.

3. **Memory Usage**: ESP32-C3 has limited memory; some features are disabled for this target.

4. **Audio Processor**: AFE (Audio Front-End) requires ESP32-S3 or ESP32-P4 with PSRAM.

5. **Custom Wake Words**: Require ESP32-S3 or ESP32-P4 with PSRAM. Use online generator at https://github.com/78/xiaozhi-assets-generator

## Documentation References

- `docs/custom-board.md` - Custom board creation guide
- `docs/mcp-protocol.md` - MCP protocol implementation
- `docs/mcp-usage.md` - MCP usage examples
- `docs/websocket.md` - WebSocket protocol details
- `docs/mqtt-udp.md` - MQTT+UDP protocol details
- `partitions/v2/README.md` - Partition table documentation
- `main/audio/README.md` - Audio pipeline documentation

## Related Projects

- Server implementations: Python, Java, Go variants available
- Client projects: Python, Android, Linux clients
- Assets generator: https://github.com/78/xiaozhi-assets-generator
