# AGENTS.md

This file provides comprehensive guidance to AI coding agents when working with the XiaoZhi ESP32 project.

## Project Overview

**XiaoZhi ESP32** is an open-source AI voice assistant firmware for ESP32 devices, supporting 93+ hardware board variants. It connects to a backend AI service via WebSocket or MQTT+UDP, streams Opus-encoded audio, and provides an MCP (Model Context Protocol) server for LLM-callable device tools.

- **License**: MIT License
- **Language**: Primarily Chinese (comments and documentation)
- **Version**: 2.0.0
- **Author**: 虾哥 (XiaGe)

### Key Features

- Wi-Fi / ML307 Cat.1 4G connectivity
- Offline voice wake-up via ESP-SR
- Dual protocol support (WebSocket or MQTT+UDP)
- OPUS audio codec
- Streaming ASR + LLM + TTS voice interaction
- Voiceprint recognition (speaker identification)
- OLED/LCD display with emoji support
- Battery monitoring and power management
- Multi-language support (18 languages)
- ESP32-C3, ESP32-S3, ESP32-P4 support
- MCP-based device control (volume, lights, motors, GPIO)
- Cloud MCP extensions (smart home, PC control, knowledge search, email)

## Technology Stack

| Component | Technology |
|-----------|------------|
| Framework | ESP-IDF v5.4+ |
| Language | C++17 |
| RTOS | FreeRTOS |
| Build System | CMake |
| GUI | LVGL 9.x |
| Audio | ESP-SR, Opus |
| Network | Wi-Fi / 4G (ML307) |
| Protocols | WebSocket, MQTT, UDP |

## Build Commands

This project uses ESP-IDF with CMake. All commands use `idf.py`:

```bash
# Set chip target (esp32, esp32s3, esp32c3, esp32p4, esp32c6)
idf.py set-target esp32s3

# Configure board type, language, features (interactive menu)
idf.py menuconfig

# Build firmware
idf.py build

# Flash to device
idf.py flash -p /dev/ttyUSB0

# Serial monitor (Ctrl+] to exit)
idf.py monitor

# Flash and monitor in one step
idf.py flash monitor

# Clean all build artifacts
idf.py fullclean

# Release build for specific board
python scripts/release.py [board-directory-name]
```

**Note**: Board selection happens in `menuconfig` under the board-specific Kconfig options. There is no single-test command — testing is done by flashing and monitoring.

## Project Structure

```
xiaozhi-esp32/
├── CMakeLists.txt              # Root CMake configuration
├── sdkconfig.defaults          # Default SDK configuration
├── main/                       # Main application code
│   ├── CMakeLists.txt          # Main component CMake (826 lines, board selection logic)
│   ├── Kconfig.projbuild       # Project Kconfig (580 lines, board/feature selection)
│   ├── idf_component.yml       # Component dependencies (~40 components)
│   ├── main.cc                 # Application entry point
│   ├── application.h/.cc       # Application singleton (state machine)
│   ├── mcp_server.h/.cc        # MCP protocol server
│   ├── ota.h/.cc               # OTA firmware update
│   ├── settings.h/.cc          # Device settings
│   ├── device_manager.cc       # Device management
│   ├── system_info.cc          # System information
│   ├── audio/                  # Audio subsystem
│   │   ├── audio_codec.h/.cc   # Audio codec abstraction
│   │   ├── audio_service.h/.cc # Audio streaming service
│   │   ├── codecs/             # Codec implementations (ES8311, ES8388, etc.)
│   │   ├── processors/         # Audio processors (AEC, AFE, debug)
│   │   └── wake_words/         # Wake word engines
│   ├── protocols/              # Network protocols
│   │   ├── protocol.h/.cc      # Protocol abstraction
│   │   ├── websocket_protocol.h/.cc
│   │   └── mqtt_protocol.h/.cc
│   ├── display/                # Display subsystem
│   │   ├── display.h/.cc       # Display abstraction
│   │   ├── lcd_display.h/.cc
│   │   ├── oled_display.h/.cc
│   │   └── lvgl_display/       # LVGL implementation
│   ├── led/                    # LED control
│   │   ├── led.h/.cc
│   │   ├── single_led.h/.cc
│   │   ├── circular_strip.h/.cc
│   │   └── gpio_led.h/.cc
│   └── boards/                 # Board-specific implementations
│       ├── common/             # Board base classes
│       ├── bread-compact-wifi/ # Example board
│       ├── esp-box-3/          # ESP-BOX-3
│       ├── m5stack-core-s3/    # M5Stack CoreS3
│       └── ... (93+ boards)
├── components/                 # Local components
│   ├── espressif2022__esp_emote_gfx/
│   ├── espressif2022__image_player/
│   └── txp666__otto-emoji-gif-component/
├── partitions/                 # Partition tables
│   ├── v1/                     # Version 1 partitions
│   └── v2/                     # Version 2 partitions (current)
├── docs/                       # Documentation
│   ├── websocket.md            # WebSocket protocol
│   ├── mqtt-udp.md             # MQTT+UDP protocol
│   ├── mcp-protocol.md         # MCP protocol
│   └── mcp-usage.md            # MCP usage guide
├── scripts/                    # Utility scripts
│   ├── release.py              # Release build script
│   ├── gen_lang.py             # Language asset generator
│   ├── audio_debug_server.py   # Audio debugging
│   └── ...
└── managed_components/         # ESP-IDF managed dependencies
```

## Architecture

### Core Abstraction Layers

The firmware is built on five abstract interfaces, each with multiple concrete implementations:

| Interface | Location | Implementations |
|-----------|----------|-----------------|
| `Board` | `main/boards/common/board.h` | 93+ board-specific classes |
| `Protocol` | `main/protocols/protocol.h` | WebSocket, MQTT+UDP |
| `AudioCodec` | `main/audio/audio_codec.h` | ES8311, ES8388, Box, etc. |
| `Display` | `main/display/display.h` | LCD, OLED, LVGL variants |
| `Led` | `main/led/led.h` | Single, circular strip, GPIO |

### Board Abstraction Hierarchy

```
Board (abstract base)
├── WifiBoard (Wi-Fi connectivity)
├── Ml307Board (4G ML307 connectivity)
└── DualNetworkBoard (Wi-Fi + 4G switchable)
    └── [Device-specific classes]
```

Each board lives in `main/boards/<board-name>/` and contains:
- `config.h` — GPIO pin assignments and hardware configuration
- `config.json` — Chip target and build options
- `*_board.cc` — Board class with `DECLARE_BOARD()` macro (creates factory `create_board()`)
- `README.md` — Board documentation (optional)

The `Application` singleton calls `create_board()` at startup to instantiate the correct hardware abstraction.

### Audio Pipeline

Three FreeRTOS tasks handle streaming audio:

1. **AudioInputTask** — microphone → processor → encode queue
2. **OpusCodecTask** — Opus encode/decode
3. **AudioOutputTask** — decode queue → speaker

`AudioService` (`main/audio/audio_service.h`) manages dual queues: TX (mic→network) and RX (network→speaker). It's allocated in PSRAM. The codec processes 16-bit PCM at 16kHz; Opus frames are 24–60ms.

Audio processors (`main/audio/processors/`) optionally apply:
- AEC (Acoustic Echo Cancellation)
- AFE noise reduction (ESP-SR)
- Wake word detection

### MCP Server

`McpServer` (`main/mcp_server.h`) provides device-side tools callable by the LLM:

- `AddTool()` — registers AI-visible tools
- `AddUserOnlyTool()` — registers tools only visible in device UI, not to the AI

Tools return `std::variant<bool, int, std::string, cJSON*, ImageContent*>`. A dedicated thread handles blocking tool calls.

### Application State Machine

`Application` (`main/application.h`) is a singleton orchestrating all subsystems.

**Key States**:
- `kDeviceStateStarting`
- `kDeviceStateReady`
- `kDeviceStateChatting`
- `kDeviceStateSleeping`

Events are dispatched via FreeRTOS `EventGroup` and a deque-based task queue for ISR-safe scheduling.

### Protocol Layer

Both `WebsocketProtocol` and `MqttProtocol` implement the same `Protocol` interface with callback-driven async handling.

Binary audio uses a v2/v3 header format with timestamps; JSON carries session metadata, transcripts, and TTS responses.

### Display

`LvglDisplay` wraps the LVGL 9.x GUI framework with emoji, GIF, and theme support. Multiple SPI panel drivers are supported (ST7789, ILI9341, GC9A01, etc.) with optional touch controllers.

## Configuration

### Kconfig.projbuild

`main/Kconfig.projbuild` (580 lines) drives compile-time feature selection:

- **Board type** (93 options) — Select hardware variant
- **Language/locale** (18 options) — UI and TTS language
- **Wake word engine** (disabled, WakeNet, custom) — Voice activation
- **AEC mode** (device-side, server-side, none) — Echo cancellation
- **Network type** (WiFi, 4G, dual) — Connectivity method

### Component Dependencies

`main/idf_component.yml` manages ~40 component dependencies including:
- `esp-sr` — ESP Speech Recognition
- `lvgl` — Graphics library
- `esp-opus-encoder` — Opus audio codec
- Hardware codec drivers (ES8311, ES8388, etc.)

### Partition Tables

Two partition table versions:
- **v1**: Legacy boards (4MB, 8MB, 16MB, 32MB flash)
- **v2**: Current boards with assets partition support

## Code Style Guidelines

- **Style**: Google C++ Style Guide
- **Standard**: C++17
- **Headers**: `.h` extension
- **Implementation**: `.cc` extension (NOT `.cpp`)
- **Comments**: Primarily in Chinese
- **Naming**: 
  - Classes: `PascalCase`
  - Functions: `PascalCase` for public methods
  - Variables: `snake_case` for locals, `trailing_underscore_` for members
  - Constants: `UPPER_SNAKE_CASE`

### Example

```cpp
#ifndef _MY_CLASS_H_
#define _MY_CLASS_H_

class MyClass {
private:
    int private_member_;
    
public:
    MyClass(int value);
    void PublicMethod();
    int GetValue() const { return private_member_; }
};

#endif // _MY_CLASS_H_
```

## Adding New Features

### New Board

1. Create `main/boards/<name>/` with:
   - `config.h` — Pin definitions and hardware config
   - `config.json` — Target chip and build options
   - `*_board.cc` — Board class using `DECLARE_BOARD()`
2. Add to `main/Kconfig.projbuild` (board type choice)
3. Add to `main/CMakeLists.txt` (board configuration section)
4. Run `python scripts/release.py <name>` to build

**Important**: Never override existing board configs for custom hardware. Always create a new board type to avoid OTA conflicts.

### New Audio Codec

1. Implement `AudioCodec` interface in `main/audio/codecs/`
2. Inherit from `AudioCodec` base class
3. Implement required virtual methods
4. Register in board implementation

### New Display Panel

1. Implement `Display` interface in `main/display/`
2. Or extend existing `LvglDisplay` with new panel driver
3. Add panel initialization to board class

### New MCP Tool

```cpp
// In board initialization or application startup
auto& mcp = McpServer::GetInstance();

// AI-visible tool
mcp.AddTool("tool_name", "Description", properties, [](const PropertyList& props) -> ReturnValue {
    // Tool implementation
    return "Result";
});

// User-only tool (not visible to AI)
mcp.AddUserOnlyTool("user_tool_name", "Description", properties, callback);
```

### New Protocol

1. Implement `Protocol` interface in `main/protocols/`
2. Implement `OpenAudioChannel()`, `CloseAudioChannel()`, `SendAudio()`, etc.
3. Register in `Application` protocol selection

## Testing

There is no unit test framework. Testing is done by:

1. **Flashing and monitoring**: `idf.py flash monitor`
2. **Audio debugging**: Enable `CONFIG_USE_AUDIO_DEBUGGER` in menuconfig
   - Sends audio data via UDP to `scripts/audio_debug_server.py`
3. **Serial logs**: Check ESP-IDF log output at various levels (Error, Warning, Info, Debug, Verbose)

## Release Process

Use the release script to build production firmware:

```bash
python scripts/release.py [board-directory-name]
```

This creates properly versioned firmware packages in the `releases/` directory.

## Communication Protocols

### WebSocket Protocol

- Binary audio streaming (Opus)
- JSON control messages
- Hello handshake with capability negotiation
- Supports MCP protocol extensions

See `docs/websocket.md` for detailed specification.

### MQTT + UDP Protocol

- MQTT for control and signaling
- UDP for audio streaming
- Lower latency alternative to WebSocket

See `docs/mqtt-udp.md` for detailed specification.

### MCP Protocol

Model Context Protocol for LLM tool calling:
- Device capability advertisement
- Tool registration and invocation
- Support for text, numeric, boolean, JSON, and image return types

See `docs/mcp-protocol.md` and `docs/mcp-usage.md`.

## Security Considerations

- **OTA updates**: Firmware signed and verified via ESP-IDF secure boot
- **Network**: TLS/SSL for WebSocket and MQTT connections
- **Authentication**: Bearer token via `Authorization` header
- **MCP tools**: User-only tools restrict sensitive operations from AI access

## Common Issues

1. **Display issues**: Check SPI config, mirror settings, color inversion
2. **No audio output**: Check I2S config, PA enable pin, codec address
3. **Network connection**: Verify Wi-Fi credentials and network config
4. **Server communication**: Check MQTT or WebSocket configuration
5. **Memory issues**: Enable PSRAM for audio processing features

## Resources

- **ESP-IDF**: https://docs.espressif.com/projects/esp-idf/
- **LVGL**: https://docs.lvgl.io/
- **ESP-SR**: https://github.com/espressif/esp-sr
- **Project Docs**: `docs/` directory
- **Board Guide**: `main/boards/README.md`

## Contributing

1. Follow Google C++ style guide
2. Ensure code compiles for all supported targets
3. Test on actual hardware before submitting
4. Update relevant documentation
5. Comments should be in Chinese (project convention)
