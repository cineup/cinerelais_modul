# ESP32-S3-POE-ETH-8DI-8RO Relay Controller

**[Deutsch](README.md)** | English

Firmware for the Waveshare ESP32-S3-POE-ETH-8DI-8RO module with web interface, TCP control and OTA updates.

## Features

- **DHCP (default)** or static IP configuration
- **Web interface** for configuration and control
- **TCP command interface** for relay control
- **OTA firmware updates** via web interface
- **8 Digital inputs** (optocoupler-isolated)
- **8 Relay outputs** with on/off/pulse commands

## Installation

### Prerequisites

- [PlatformIO](https://platformio.org/) (recommended) or Arduino IDE
- USB cable for initial programming

### With PlatformIO

```bash
# Clone/copy project
cd esp32-relay-controller

# Compile
pio run

# Upload firmware
pio run -t upload

# Upload filesystem (web interface)
pio run -t uploadfs

# Serial monitor
pio device monitor
```

### With Arduino IDE

1. Install ESP32 Board Support
2. Install required libraries:
   - ArduinoJson
   - ElegantOTA
   - AsyncTCP
   - ESPAsyncWebServer
3. Select board: "ESP32S3 Dev Module"
4. Open `src/main.cpp` as `.ino` file
5. Upload

## Hardware Pin Mapping

### Digital Inputs (DI1-DI8)
| Input | GPIO |
|-------|------|
| DI1   | 4    |
| DI2   | 5    |
| DI3   | 6    |
| DI4   | 7    |
| DI5   | 15   |
| DI6   | 16   |
| DI7   | 17   |
| DI8   | 18   |

### Relay Outputs (RO1-RO8)
| Relay | GPIO |
|-------|------|
| RO1   | 33   |
| RO2   | 34   |
| RO3   | 35   |
| RO4   | 36   |
| RO5   | 37   |
| RO6   | 38   |
| RO7   | 39   |
| RO8   | 40   |

### Ethernet (W5500)
| Signal | GPIO |
|--------|------|
| MISO   | 11   |
| MOSI   | 13   |
| SCLK   | 12   |
| CS     | 10   |
| INT    | 14   |

> **Note:** Pin mapping may vary depending on board revision. Please check the documentation for your specific board.

## Usage

### Web Interface

After startup, the web interface is available at the device's IP address:
- With DHCP: Check the IP from your router/DHCP server or serial monitor
- Default: `http://[IP-ADDRESS]/`

**Features:**
- Toggle relays on/off (click)
- Pulse relays (right-click)
- Control all relays at once
- Change network configuration
- Set TCP port and pulse duration
- View system information
- OTA firmware update

### TCP Commands

Connect with a TCP client (e.g. `nc`, `telnet`, or custom software) to the configured port (default: 5000).

| Command | Description |
|---------|-------------|
| `r1_on` | Turn relay 1 on (r1-r8) |
| `r1_off` | Turn relay 1 off (r1-r8) |
| `r1_pulse` | Pulse relay 1 with default duration (r1-r8) |
| `r1_pulse_1000` | Pulse relay 1 for 1000ms (r1-r8) |
| `all_on` | Turn all relays on |
| `all_off` | Turn all relays off |
| `all_pulse` | Pulse all relays with default duration |
| `all_pulse_500` | Pulse all relays for 500ms |
| `status` | JSON status of all I/O |
| `help` | Show help |

**Examples:**

```bash
# Connect with netcat
nc 192.168.1.100 5000

# Turn relay 1 on
r1_on

# Pulse relay 3 for 1000ms
r3_pulse_1000

# Turn all relays off
all_off

# Query status
status
```

### OTA Updates

1. Open web interface
2. Click "OTA Firmware Update"
3. Select `.bin` file and upload
4. Wait for upload to complete
5. Device restarts automatically

## Configuration

### Default Values

| Parameter | Default |
|-----------|---------|
| DHCP | Enabled |
| Hostname | esp32-relay |
| TCP Port | 5000 |
| Pulse Duration | 500 ms |
| Static IP | 192.168.1.100 |
| Gateway | 192.168.1.1 |
| Subnet | 255.255.255.0 |
| DNS | 8.8.8.8 |

### Changing Configuration

All settings can be changed via the web interface. Configuration is stored in flash memory (LittleFS) and persists across restarts.

**Important:** A restart is required after changing network settings!

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Current status (JSON) |
| `/api/config` | GET | Current configuration (JSON) |
| `/api/config` | POST | Save configuration |
| `/api/relay` | POST | Control single relay |
| `/api/relays` | POST | Control all relays |
| `/api/restart` | POST | Restart device |
| `/update` | GET | OTA update page |

### API Examples

```bash
# Query status
curl http://192.168.1.100/api/status

# Turn relay 1 on
curl -X POST -d "relay=1&state=on" http://192.168.1.100/api/relay

# Pulse relay 2 for 2000ms
curl -X POST -d "relay=2&state=pulse&duration=2000" http://192.168.1.100/api/relay

# Turn all relays off
curl -X POST -d "state=off" http://192.168.1.100/api/relays

# Change TCP port
curl -X POST -d "tcpPort=5001" http://192.168.1.100/api/config
```

## Troubleshooting

### No Ethernet Connection
- Check the Ethernet cable
- Check PoE power supply
- Check serial monitor for error messages

### Web Interface Not Reachable
- Check IP address in serial monitor
- Check firewall settings
- Clear browser cache

### Relays Not Responding
- Check pin mapping (may vary by board revision)
- Check relay power supply

## License

MIT License

## Custom Pin Mapping

If your board has a different pin mapping, adjust the arrays in `src/config.h`:

```cpp
// Digital Inputs
const int DI_PINS[8] = { 4, 5, 6, 7, 15, 16, 17, 18 };

// Relay Outputs
const int RO_PINS[8] = { 33, 34, 35, 36, 37, 38, 39, 40 };
```
