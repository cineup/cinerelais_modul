# CLAUDE.md — AI Assistant Guide for CineRelais Modul

## Project Overview

**Repository**: `cineup/cinerelais_modul`
**Language**: C++ (Arduino framework) + HTML/CSS/JavaScript (Web-Interface)
**Build System**: PlatformIO
**License**: MIT
**Domain**: Cinema relay automation — part of the CineUp ecosystem

Firmware for the **Waveshare ESP32-S3-POE-ETH-8DI-8RO** module. Controls 8 relay outputs and monitors 8 digital inputs via Ethernet (W5500), with a web interface, TCP command protocol, and OTA firmware updates. Used for cinema infrastructure automation (lighting, curtains, projector control, audio routing).

## Repository Structure

```
cinerelais_modul/
├── CLAUDE.md              # This file — AI assistant guide
├── README.md              # Project documentation (German)
├── LICENSE                # MIT License
├── platformio.ini         # PlatformIO build configuration
├── src/
│   ├── config.h           # Pin definitions, defaults, NetworkConfig struct
│   └── main.cpp           # Main firmware (~650 lines)
└── data/
    └── index.html         # Single-page web interface (LittleFS)
```

## Development Setup

### Prerequisites

- [PlatformIO](https://platformio.org/) CLI or IDE plugin
- USB-C cable for initial flash
- Waveshare ESP32-S3-POE-ETH-8DI-8RO board

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

## Architecture

### Hardware Abstraction (`src/config.h`)

- **8 Digital Inputs** (DI1-DI8): GPIOs 4, 5, 6, 7, 15, 16, 17, 18 — optocoupler isolated, active LOW
- **8 Relay Outputs** (RO1-RO8): GPIOs 33-40 — via ULN2803 driver
- **Ethernet**: W5500 via SPI (MISO=11, MOSI=13, SCLK=12, CS=10, INT=14)
- **Status LED**: GPIO 48

### Firmware Components (`src/main.cpp`)

The firmware is a single-file monolith with these logical sections:

| Section | Lines | Description |
|---------|-------|-------------|
| Includes & globals | 1-46 | Libraries, state arrays, config |
| `setup()` | 68-115 | Init LittleFS, config, pins, Ethernet, servers |
| `loop()` | 121-134 | Poll inputs, update pulses, ElegantOTA tick |
| Config management | 140-198 | `loadConfig()` / `saveConfig()` — JSON on LittleFS (`/config.json`) |
| Hardware setup | 204-290 | `setupPins()`, `WiFiEvent()`, `setupEthernet()` |
| Relay control | 296-345 | `setRelay()`, `setAllRelays()`, `pulseRelay()`, `updatePulses()` |
| Status JSON | 351-384 | `getStatusJSON()` — combined relay/input/network state |
| TCP server | 390-509 | Async TCP on configurable port, text command protocol |
| Web server | 515-653 | AsyncWebServer on port 80, REST API endpoints, ElegantOTA |

### TCP Command Protocol (default port 5000)

```
r<1-8>_on          → Turn relay on
r<1-8>_off         → Turn relay off
r<1-8>_impuls      → Pulse relay (default duration)
r<1-8>_impuls_<ms> → Pulse relay for <ms> milliseconds
all_on             → Turn all relays on
all_off            → Turn all relays off
all_impuls         → Pulse all relays (default duration)
all_impuls_<ms>    → Pulse all relays for <ms> milliseconds
status             → JSON status response
help               → Command list
```

Commands are case-insensitive (lowercased on receive). Responses prefixed with `OK:` or `ERROR:`.

### REST API (port 80)

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Full status JSON (relays, inputs, network, uptime) |
| `/api/config` | GET | Current configuration JSON |
| `/api/config` | POST | Save configuration (form data) |
| `/api/relay` | POST | Control single relay (params: `relay`, `state`, `duration`) |
| `/api/relays` | POST | Control all relays (params: `state`, `duration`) |
| `/api/restart` | POST | Restart ESP32 |
| `/update` | GET | ElegantOTA update page |

### Web Interface (`data/index.html`)

Single-page application with dark theme. German UI labels. Features:
- 4x2 grid of relay toggle buttons (click = toggle, right-click = pulse)
- 4x2 grid of digital input status indicators
- Network configuration (DHCP toggle, static IP fields)
- TCP port and pulse duration settings
- System info (IP, MAC, uptime)
- Device restart button
- OTA firmware update link

Polls `/api/status` every 2 seconds. Uses `FormData` for POST requests. Fonts: Space Grotesk + JetBrains Mono (loaded from Google Fonts CDN).

### Configuration Persistence

Stored as `/config.json` on LittleFS. Fields:

| Field | Type | Default |
|-------|------|---------|
| `useDHCP` | bool | `true` |
| `staticIP` | string | `192.168.1.100` |
| `gateway` | string | `192.168.1.1` |
| `subnet` | string | `255.255.255.0` |
| `dns` | string | `8.8.8.8` |
| `hostname` | string | `esp32-relay` |
| `tcpPort` | uint16 | `5000` |
| `pulseDuration` | uint16 | `500` (ms) |

Network changes require device restart to take effect.

## Conventions

### Language

- Project naming and UI: **German** (Relais, Modul, Eingänge, Ausgänge, Impuls, etc.)
- Code (variables, functions, comments): **English**
- README and user-facing docs: **German**

### Code Style

- Arduino/C++ with standard ESP32 patterns
- Section headers with `// ============` block comments
- Global state arrays for relay/input states
- 1-indexed relay numbering in API/protocol (converted to 0-indexed internally)
- `strlcpy()` for safe string copies into fixed-size buffers
- ArduinoJson `JsonDocument` (v7 API, no explicit size)

### Git Workflow

- Commits: clear, descriptive messages in imperative mood
- Branch naming: feature branches with descriptive names

## Key Files

| File | Purpose |
|------|---------|
| `src/main.cpp` | Main firmware — all logic in one file |
| `src/config.h` | Hardware pin mapping, default values, `NetworkConfig` struct |
| `data/index.html` | Web interface SPA — uploaded to LittleFS |
| `platformio.ini` | Build config, dependencies, board settings |
| `README.md` | User documentation (German) |

## AI Assistant Guidelines

1. **Read before modifying** — Always read existing files before proposing changes
2. **Minimal changes** — Make only the changes requested; avoid unnecessary refactoring
3. **Preserve conventions** — English code, German UI/docs; keep section comment style
4. **Hardware awareness** — This runs on ESP32-S3 with 16MB flash; consider memory constraints and real-time requirements
5. **Safety first** — Relay modules control physical equipment; never bypass safety checks or remove input validation on relay indices (1-8)
6. **Single-file architecture** — `main.cpp` is monolithic by design; don't split into multiple files unless explicitly requested
7. **Async patterns** — Web server and TCP server are async (ESPAsyncWebServer/AsyncTCP); avoid blocking calls in handlers
8. **LittleFS filesystem** — Web files go in `data/`; changes to `data/` require `pio run -t uploadfs`
9. **Pin changes** — Only modify pin assignments in `config.h`; verify against the actual board revision
10. **Update this file** — When significant structural changes are made, update CLAUDE.md to reflect the current state

## Maintenance Log

| Date | Change |
|------|--------|
| 2026-02-01 | Initial CLAUDE.md created for empty repository |
| 2026-02-01 | Updated with full project analysis after source code upload |
| 2026-02-01 | TCP protocol changed: `ON:1` → `r1_on`, `PULSE:3:1000` → `r3_impuls_1000`, lowercase, underscore-separated |
