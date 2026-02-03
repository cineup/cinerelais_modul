# CLAUDE.md — AI Assistant Guide for CineRelais Modul

## Project Overview

**Repository**: `cineup/cinerelais_modul`
**Language**: C++ (Arduino framework) + HTML/CSS/JavaScript (Web-Interface)
**Build System**: PlatformIO
**License**: MIT
**Domain**: Cinema relay automation — part of the CineUp ecosystem

Firmware for the **Waveshare ESP32-S3-ETH-8DI-8RO** / **ESP32-S3-POE-ETH-8DI-8RO** module. Controls 8 relay outputs (via TCA9554 I2C expander) and monitors 8 digital inputs. Supports Ethernet (W5500), WiFi (AP/STA), web interface, TCP command protocol, and OTA firmware updates. Used for cinema infrastructure automation (lighting, curtains, projector control, audio routing).

## Repository Structure

```
cinerelais_modul/
├── CLAUDE.md              # This file — AI assistant guide
├── README.md              # Project documentation (German)
├── README_EN.md           # Project documentation (English)
├── LICENSE                # MIT License
├── platformio.ini         # PlatformIO build configuration
├── src/
│   ├── config.h           # Pin definitions, defaults, NetworkConfig struct
│   └── main.cpp           # Main firmware (~940 lines)
└── data/
    └── index.html         # Single-page web interface (LittleFS)
```

## Development Setup

### Prerequisites

- [PlatformIO](https://platformio.org/) CLI or IDE plugin
- USB-C cable for initial flash
- Waveshare ESP32-S3-ETH-8DI-8RO or ESP32-S3-POE-ETH-8DI-8RO board

### Build & Flash

```bash
# Compile firmware
pio run

# Upload firmware via USB
pio run -t upload

# Upload web interface (LittleFS filesystem)
pio run -t uploadfs

# Serial monitor (115200 baud)
pio device monitor
```

### PlatformIO Configuration

- **Platform**: espressif32
- **Board**: esp32-s3-devkitc-1 (240MHz)
- **Framework**: Arduino
- **Filesystem**: LittleFS (16MB flash, `default_16MB.csv` partition table)
- **Build flags**: `CORE_DEBUG_LEVEL=3`, `ARDUINO_USB_CDC_ON_BOOT=1`, `BOARD_HAS_PSRAM`

### Dependencies (lib_deps)

| Library | Version | Purpose |
|---------|---------|---------|
| ArduinoJson | ^7.0.0 | JSON serialization for config and API |
| ElegantOTA | ^3.1.0 | Over-the-air firmware updates |
| AsyncTCP | ^1.1.1 | Async TCP server for command protocol |
| ESPAsyncWebServer | ^1.2.3 | Async HTTP server for web interface and REST API |
| Adafruit NeoPixel | ^1.12.0 | RGB LED (WS2812) status indicator |

## Architecture

### Hardware Abstraction (`src/config.h`)

**Digital Inputs** (DI1-DI8): GPIOs 4, 5, 6, 7, 8, 9, 10, 11 — optocoupler isolated, active LOW with internal pull-up

**Relay Outputs** (RO1-RO8): Controlled via **TCA9554 I2C I/O Expander** at address 0x20
- I2C SDA: GPIO 42
- I2C SCL: GPIO 41
- TCA9554 pins P0-P7 → Relays CH1-CH8

**Ethernet** (W5500 via SPI):
- SCLK: GPIO 15
- MOSI: GPIO 13
- MISO: GPIO 14
- CS: GPIO 16
- INT: GPIO 12
- RST: Not connected (-1)

**Additional Peripherals**:
- RS485: TX=GPIO17, RX=GPIO18 (reserved for Modbus extension)
- RGB LED (WS2812): GPIO 38
- Buzzer: GPIO 46

### Firmware Components (`src/main.cpp`)

The firmware is a single-file monolith with these logical sections:

| Section | Description |
|---------|-------------|
| Includes & globals | Libraries, state arrays, config, LED state machine |
| `setup()` | Init LittleFS, config, pins, I2C, LED, Ethernet, WiFi, NTP, servers |
| `loop()` | Poll inputs, update pulses, update LED, WiFi reconnect, ElegantOTA tick |
| TCA9554 driver | `tca9554Init()`, `tca9554Read()`, `tca9554Write()`, `tca9554WriteAll()` |
| Config management | `loadConfig()` / `saveConfig()` — JSON on LittleFS (`/config.json`) |
| Hardware setup | `setupPins()`, `setupI2C()`, `setupLED()`, `setupEthernet()`, `setupWiFi()`, `setupNTP()` |
| Network events | `WiFiEvent()` — handles ETH and WiFi state changes, updates LED |
| Relay control | `setRelay()`, `setAllRelays()`, `pulseRelay()`, `pulseAllRelays()`, `updatePulses()` |
| LED control | `updateLED()`, `ledFlash()`, `ledCommandReceived()`, `ledRelayActivity()`, `updateLedBackgroundState()` |
| Status JSON | `getStatusJSON()` — combined relay/input/network/WiFi/LED state |
| TCP server | Async TCP on configurable port, text command protocol |
| Web server | AsyncWebServer on port 80, REST API endpoints, ElegantOTA with LED callbacks |

### TCP Command Protocol (default port 5000)

```
r<1-8>_on          → Turn relay on
r<1-8>_off         → Turn relay off
r<1-8>_pulse       → Pulse relay (default duration)
r<1-8>_pulse_<ms>  → Pulse relay for <ms> milliseconds
all_on             → Turn all relays on
all_off            → Turn all relays off
all_pulse          → Pulse all relays (default duration)
all_pulse_<ms>     → Pulse all relays for <ms> milliseconds
status             → JSON status response
help               → Command list
```

Commands are case-insensitive (lowercased on receive). Responses prefixed with `OK:` or `ERROR:`.

### REST API (port 80)

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Full status JSON (relays, inputs, Ethernet, WiFi, TCA9554, uptime) |
| `/api/config` | GET | Current configuration JSON |
| `/api/config` | POST | Save configuration (form data) |
| `/api/relay` | POST | Control single relay (params: `relay`, `state`, `duration`) |
| `/api/relays` | POST | Control all relays (params: `state`, `duration`) |
| `/api/restart` | POST | Restart ESP32 |
| `/api/log` | GET | Command log (last 50 entries, newest first) |
| `/update` | GET | ElegantOTA update page |

### Web Interface (`data/index.html`)

Single-page application with dark theme. German UI labels. Features:
- 4x2 grid of relay toggle buttons (click = toggle, right-click = pulse)
- 4x2 grid of digital input status indicators
- Ethernet configuration (DHCP toggle, static IP fields, hostname)
- WiFi configuration (enable/disable, AP settings, STA settings with DHCP/static IP)
- TCP port and pulse duration settings
- System info (Ethernet status/IP, WiFi STA/AP status, TCA9554 status, uptime)
- Device restart button
- OTA firmware update link

Polls `/api/status` every 2 seconds. Uses `FormData` for POST requests. System fonts (no external dependencies).

### Configuration Persistence

Stored as `/config.json` on LittleFS. Fields:

| Field | Type | Default |
|-------|------|---------|
| `useDHCP` | bool | `true` |
| `staticIP` | string | `192.168.1.100` |
| `gateway` | string | `192.168.1.1` |
| `subnet` | string | `255.255.255.0` |
| `dns` | string | `8.8.8.8` |
| `wifiEnabled` | bool | `true` |
| `wifiAPEnabled` | bool | `true` |
| `wifiSSID` | string | `""` (empty = no STA) |
| `wifiPassword` | string | `""` |
| `wifiAPPassword` | string | `""` (empty = open AP) |
| `wifiDHCP` | bool | `true` |
| `wifiIP` | string | `192.168.4.100` |
| `wifiGateway` | string | `192.168.4.1` |
| `wifiSubnet` | string | `255.255.255.0` |
| `wifiDNS` | string | `8.8.8.8` |
| `hostname` | string | `cinerelais1` |
| `tcpPort` | uint16 | `5000` |
| `pulseDuration` | uint16 | `500` (ms) |
| `ntpEnabled` | bool | `true` |
| `ntpServer` | string | `pool.ntp.org` |
| `ntpTimezone` | string | `CET-1CEST,M3.5.0,M10.5.0/3` |
| `ledEnabled` | bool | `true` |
| `ledBrightness` | uint8 | `51` (20% of 255) |

Network changes require device restart to take effect.

### NTP Time Synchronization

- **Default**: NTP enabled with `pool.ntp.org` server
- **Timezone**: POSIX TZ format (default: Europe/Berlin)
- **Offline mode**: If NTP sync fails, timestamps show uptime instead
- **Status**: Shown in web interface System Info and `/api/status`

### Command Log

- Circular buffer storing last 50 commands
- Records: timestamp, source IP, command
- Commands logged: TCP commands (except `status`/`help`), Web API relay controls
- Available via `/api/log` endpoint
- Displayed in web interface with auto-refresh every 10s

### WiFi Behavior

- **Default**: WiFi enabled, AP enabled, STA disabled (no SSID configured)
- **AP mode**: SSID = hostname, password from `wifiAPPassword` (min 8 chars for WPA2, empty = open)
- **STA mode**: Connects to configured `wifiSSID`, auto-reconnect every 30s if disconnected
- **AP+STA**: Both can run simultaneously
- WiFi can be completely disabled via `wifiEnabled = false`

### RGB LED Status Indicator

The WS2812 RGB LED on GPIO 38 provides visual feedback:

| Color | Pattern | State |
|-------|---------|-------|
| Green | Solid | Ethernet connected |
| Cyan | Solid | WiFi STA connected |
| Blue | Pulsing | WiFi AP only (no other connection) |
| Red | Fast blink | No network connection (error) |
| Orange | Brief flash | Command received (TCP or Web API) |
| Yellow | Brief flash | Relay activity (follows orange) |
| Purple | Pulsing | OTA update in progress |

- Flash sequence: Orange (command) → Yellow (relay) provides visual confirmation
- Brightness configurable (0-255, default 51 = 20%)
- Can be disabled via `ledEnabled = false`

## Conventions

### Language

- Project naming and UI: **German** (Relais, Modul, Eingänge, Ausgänge, etc.)
- Code, TCP commands, variables, functions, comments: **English**
- README: **German** (`README.md`) + **English** (`README_EN.md`)

### Code Style

- Arduino/C++ with standard ESP32 patterns
- Section headers with `// ============` block comments
- Global state arrays for relay/input states
- 1-indexed relay numbering in API/protocol (converted to 0-indexed internally)
- `strlcpy()` for safe string copies into fixed-size buffers
- ArduinoJson `JsonDocument` (v7 API, no explicit size)
- TCA9554 uses read-modify-write for single pins, bulk write for all relays

### Git Workflow

- Commits: clear, descriptive messages in imperative mood
- Branch naming: feature branches with descriptive names

## Key Files

| File | Purpose |
|------|---------|
| `src/main.cpp` | Main firmware — all logic in one file |
| `src/config.h` | Hardware pin mapping, I2C/TCA9554 defines, default values, `NetworkConfig` struct |
| `data/index.html` | Web interface SPA — uploaded to LittleFS |
| `platformio.ini` | Build config, dependencies, board settings |
| `README.md` | User documentation (German) |
| `README_EN.md` | User documentation (English) |

## AI Assistant Guidelines

1. **Read before modifying** — Always read existing files before proposing changes
2. **Minimal changes** — Make only the changes requested; avoid unnecessary refactoring
3. **Preserve conventions** — English code, German UI/docs; keep section comment style
4. **Hardware awareness** — This runs on ESP32-S3 with 16MB flash; consider memory constraints and real-time requirements
5. **TCA9554 relay control** — Relays are NOT direct GPIO; always use `tca9554Write()` or `tca9554WriteAll()`
6. **Safety first** — Relay modules control physical equipment; never bypass safety checks or remove input validation on relay indices (1-8)
7. **Single-file architecture** — `main.cpp` is monolithic by design; don't split into multiple files unless explicitly requested
8. **Async patterns** — Web server and TCP server are async (ESPAsyncWebServer/AsyncTCP); avoid blocking calls in handlers
9. **LittleFS filesystem** — Web files go in `data/`; changes to `data/` require `pio run -t uploadfs`
10. **Pin changes** — Only modify pin assignments in `config.h`; verify against the actual board datasheet
11. **Update this file** — When significant structural changes are made, update CLAUDE.md to reflect the current state

## Hardware Pin Reference (Verified)

| Function | GPIO | Notes |
|----------|------|-------|
| DI1-DI8 | 4, 5, 6, 7, 8, 9, 10, 11 | Optocoupler isolated, active LOW |
| I2C SDA | 42 | TCA9554 for relays |
| I2C SCL | 41 | TCA9554 for relays |
| ETH SCLK | 15 | W5500 SPI |
| ETH MOSI | 13 | W5500 SPI |
| ETH MISO | 14 | W5500 SPI |
| ETH CS | 16 | W5500 SPI |
| ETH INT | 12 | W5500 interrupt |
| RS485 TX | 17 | Reserved for Modbus |
| RS485 RX | 18 | Reserved for Modbus |
| RGB LED | 38 | WS2812 |
| Buzzer | 46 | PWM capable |

## Maintenance Log

| Date | Change |
|------|--------|
| 2026-02-01 | Initial CLAUDE.md created |
| 2026-02-01 | Updated with full project analysis |
| 2026-02-01 | TCP protocol: `ON:1` → `r1_on`, lowercase, underscore-separated |
| 2026-02-01 | Renamed `impuls` → `pulse` for English consistency; added `README_EN.md` |
| 2026-02-03 | **Major rewrite**: Fixed GPIO pins, added TCA9554 I2C relay driver, added WiFi AP/STA support, corrected Ethernet W5500 SPI pins |
| 2026-02-03 | Added NTP time synchronization (configurable server/timezone) and command log (50 entries) |
| 2026-02-03 | Added RGB LED status indicator: network status (green/cyan/blue/red), command flash (orange), relay activity (yellow), OTA (purple pulsing) |
