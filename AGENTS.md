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
- **Network**: Wi-Fi / ML307 Cat.1 4G / NT26 4G
- **Protocol**: WebSocket / MQTT + UDP
- **MCP**: Model Context Protocol for AI tool use

## Project Structure

```
├── main/                       # Main application code
│   ├── main.cc                 # Application entry point
│   ├── application.cc/h        # Main Application singleton
│   ├── device_state_machine.cc/h  # Device state management
│   ├── assets.cc/h             # Asset management (fonts, themes)
│   ├── ota.cc/h                # OTA update handling
│   ├── settings.cc/h           # Device settings management
│   ├── system_info.cc/h        # System information utilities
│   ├── timer_manager.cc/h      # Timer management
│   ├── mcp_server.cc/h         # MCP protocol implementation
│   ├── audio/                  # Audio pipeline
│   │   ├── audio_codec.cc/h    # Audio codec HAL
│   │   ├── audio_service.cc/h  # Audio service orchestrator
│   │   ├── codecs/             # Audio codec drivers
│   │   │   ├── es8311_audio_codec.cc/h
│   │   │   ├── es8388_audio_codec.cc/h
│   │   │   ├── es8389_audio_codec.cc/h
│   │   │   ├── es8374_audio_codec.cc/h
│   │   │   ├── box_audio_codec.cc/h
│   │   │   ├── dummy_audio_codec.cc/h
│   │   │   └── no_audio_codec.cc/h
│   │   ├── processors/         # Audio processors (AEC, VAD)
│   │   │   ├── afe_audio_processor.cc/h
│   │   │   ├── no_audio_processor.cc/h
│   │   │   └── audio_debugger.cc/h
│   │   ├── wake_words/         # Wake word detection engines
│   │   │   ├── afe_wake_word.cc/h
│   │   │   ├── esp_wake_word.cc/h
│   │   │   └── custom_wake_word.cc/h
│   │   └── demuxer/            # Audio demuxers
│   │       └── ogg_demuxer.cc/h
│   ├── boards/                 # Hardware board implementations
│   │   ├── common/             # Shared board implementations
│   │   │   ├── board.cc/h      # Base Board class
│   │   │   ├── wifi_board.cc/h # WiFi board base class
│   │   │   ├── ml307_board.cc/h # 4G ML307 modem base class
│   │   │   ├── nt26_board.cc/h  # 4G NT26 modem base class
│   │   │   ├── dual_network_board.cc/h
│   │   │   ├── button.cc/h
│   │   │   ├── backlight.cc/h
│   │   │   ├── adc_battery_monitor.cc/h
│   │   │   ├── power_save_timer.cc/h
│   │   │   ├── sleep_timer.cc/h
│   │   │   ├── esp32_camera.cc/h
│   │   │   ├── esp_video.cc/h
│   │   │   └── ...
│   │   ├── <board-name>/       # Individual board implementations
│   │   │   ├── config.h        # GPIO pin definitions
│   │   │   ├── config.json     # Build configuration
│   │   │   └── <board>.cc      # Board implementation
│   │   └── waveshare/          # Manufacturer subdirectories
│   ├── protocols/              # Communication protocols
│   │   ├── protocol.cc/h       # Protocol base class
│   │   ├── websocket_protocol.cc/h
│   │   └── mqtt_protocol.cc/h
│   ├── display/                # Display drivers
│   │   ├── display.cc/h        # Base display class
│   │   ├── lcd_display.cc/h    # LCD implementation
│   │   ├── oled_display.cc/h   # OLED implementation
│   │   ├── emote_display.cc/h  # Emote animation display
│   │   └── lvgl_display/       # LVGL-based displays
│   │       ├── lvgl_display.cc/h
│   │       ├── lvgl_theme.cc/h
│   │       ├── lvgl_font.cc/h
│   │       ├── emoji_collection.cc/h
│   │       ├── gif/
│   │       └── jpg/
│   └── led/                    # LED controllers
│       ├── led.h
│       ├── single_led.cc/h
│       ├── circular_strip.cc/h
│       └── gpio_led.cc/h
├── partitions/v2/              # Partition tables (v2 format)
│   ├── 4m.csv
│   ├── 8m.csv
│   ├── 16m.csv
│   ├── 16m_c3.csv
│   └── 32m.csv
├── scripts/                    # Build and utility scripts
│   ├── release.py              # Multi-board release builder
│   └── gen_lang.py             # Language header generator
├── docs/                       # Documentation
├── managed_components/         # ESP-IDF managed components
├── CMakeLists.txt              # Root CMake configuration
├── main/CMakeLists.txt         # Main component CMake
├── main/Kconfig.projbuild      # Project configuration options
├── sdkconfig.defaults          # Default SDK configuration
├── sdkconfig.defaults.*        # Target-specific defaults
├── .clang-format               # Code formatting rules
└── dependencies.lock           # Component dependencies lock file
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
├── RndisBoard         # RNDIS USB network boards
└── DualNetworkBoard   # Wi-Fi + 4G dual network
```

### Board Configuration Files

**config.h** example:
```c
#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_INPUT_REFERENCE    true

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_40
#define AUDIO_I2S_GPIO_WS GPIO_NUM_47
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_38
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_39
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_48

#define AUDIO_CODEC_PA_PIN       GPIO_NUM_9
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_42
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_41

#define BUILTIN_LED_GPIO        GPIO_NUM_3
#define BOOT_BUTTON_GPIO        GPIO_NUM_0

#endif
```

**config.json** example:
```json
{
    "target": "esp32s3",
    "builds": [
        {
            "name": "my-board",
            "sdkconfig_append": [
                "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y",
                "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions/v2/8m.csv\""
            ]
        }
    ]
}
```

### Adding a New Board

1. Create directory: `mkdir main/boards/my-board`
2. Create `config.h` with pin definitions
3. Create `config.json` with target and build options
4. Create board implementation file
5. Add entry to `main/Kconfig.projbuild` (choice BOARD_TYPE)
6. Add entry to `main/CMakeLists.txt` (elseif chain)

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

**Main Events** (defined in `application.h`):
- `MAIN_EVENT_SCHEDULE` - Schedule callback execution
- `MAIN_EVENT_SEND_AUDIO` - Send audio data
- `MAIN_EVENT_WAKE_WORD_DETECTED` - Wake word detected
- `MAIN_EVENT_VAD_CHANGE` - Voice activity detection change
- `MAIN_EVENT_ERROR` - Error occurred
- `MAIN_EVENT_NETWORK_CONNECTED` - Network connected
- `MAIN_EVENT_NETWORK_DISCONNECTED` - Network disconnected
- `MAIN_EVENT_TOGGLE_CHAT` - Toggle chat state
- `MAIN_EVENT_START_LISTENING` - Start listening
- `MAIN_EVENT_STOP_LISTENING` - Stop listening
- `MAIN_EVENT_STATE_CHANGED` - Device state changed

### Audio Pipeline

Three concurrent FreeRTOS tasks communicate via queues:

1. **AudioInputTask**: Captures from codec HAL, runs wake word detection (ESP-SR), VAD, AEC
2. **OpusCodecTask**: Encodes/decodes Opus frames with dynamic resampling
3. **AudioOutputTask**: Decodes and plays to codec HAL

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
- JSON-RPC 2.0 based protocol

See `docs/mcp-protocol.md` and `docs/mcp-usage.md` for details.

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
- **Naming**: snake_case for functions/variables, PascalCase for classes

### Formatting

```bash
# Format all source files
clang-format -i main/**/*.cc main/**/*.h

# Format specific file
clang-format -i main/application.cc
```

The `.clang-format` file is at project root based on Google style.

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
- **Language**: 40+ supported languages (zh-CN, en-US, ja-JP, ko-KR, etc.)
- **Board Type**: 95+ supported hardware boards
- **Wake Word**: Disabled / ESP / AFE / Custom
- **Display Style**: Default / WeChat / Emote
- **WiFi Provisioning**: Hotspot / Acoustic / BluFi
- **Audio Processor**: Enable AEC, VAD, noise reduction
- **Camera**: JPEG encoding, rotation options

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
- `MCP` - MCP server messages

### Audio Debugging

Enable in menuconfig:
- `CONFIG_USE_AUDIO_DEBUGGER=y`
- Set `CONFIG_AUDIO_DEBUG_UDP_SERVER="192.168.1.100:8000"`

### WiFi Provisioning

Hold BOOT button during startup to enter WiFi configuration mode.

### Testing Strategy

**Note**: This project does not include automated unit tests. Testing is done through:

1. **Manual testing** on physical hardware devices
2. **CI/CD build verification** (GitHub Actions builds all board variants)
3. **Serial monitor logging** for debugging runtime issues
4. **Audio debugging** via UDP for audio pipeline verification

When making changes:
- Test on at least one ESP32-S3 and one ESP32-C3 device if possible
- Verify audio input/output functionality
- Check display rendering if display code is modified
- Test network connectivity and protocol communication

## CI/CD

GitHub Actions workflow (`.github/workflows/build.yml`):
- Uses `espressif/idf:v5.5.2` container
- Auto-detects affected boards from PR changes
- Builds all variants on main branch push
- Builds affected boards on PR only
- Uploads build artifacts

## Security Considerations

1. **OTA Updates**: Firmware is signed and verified before flashing
2. **WiFi Credentials**: Stored in NVS flash, encrypted
3. **WebSocket/MQTT**: Uses TLS/SSL for encrypted connections
4. **MCP Tools**: Device-side tools are sandboxed, input validated
5. **Custom Wake Words**: User-defined wake words processed locally

**Important Security Notes**:
- Never commit hardcoded credentials or API keys
- Use `idf.py menuconfig` for sensitive configuration
- The project uses mbedTLS for TLS/SSL connections
- Camera data is processed locally, only transmitted when explicitly requested

## Important Notes for Developers

1. **Board Type Uniqueness**: Each custom board must have a unique identifier. Do not reuse existing board types with different pin configurations.

2. **Partition Compatibility**: v2 partition tables are incompatible with v1. Manual reflash required when upgrading.

3. **Memory Usage**: ESP32-C3 has limited memory; some features are disabled for this target.

4. **Audio Processor**: AFE (Audio Front-End) requires ESP32-S3 or ESP32-P4 with PSRAM.

5. **Custom Wake Words**: Require ESP32-S3 or ESP32-P4 with PSRAM. Use online generator at https://github.com/78/xiaozhi-assets-generator

6. **Language Support**: Default language is Chinese (zh-CN). Audio files are embedded at build time based on language selection.

7. **Build Dependencies**: The build system auto-downloads required components via ESP-IDF's component manager.

## Documentation References

- `docs/custom-board.md` - Custom board creation guide (Chinese)
- `docs/mcp-protocol.md` - MCP protocol implementation (Chinese)
- `docs/mcp-usage.md` - MCP usage examples (Chinese)
- `docs/websocket.md` - WebSocket protocol details
- `docs/mqtt-udp.md` - MQTT+UDP protocol details
- `docs/code_style.md` - Code style guidelines
- `partitions/v2/README.md` - Partition table documentation
- `main/audio/README.md` - Audio pipeline documentation

## Related Projects

- Server implementations: Python, Java, Go variants available
- Client projects: Python, Android, Linux clients
- Assets generator: https://github.com/78/xiaozhi-assets-generator
