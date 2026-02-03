# CineRelais Module — ESP32-S3 Relay Controller

**[Deutsch](README.md)** | English

Firmware for the Waveshare ESP32-S3-ETH-8DI-8RO / ESP32-S3-POE-ETH-8DI-8RO module with web interface, TCP control, WiFi support and OTA updates.

## Features

- **Ethernet (W5500)** with DHCP or static IP
- **WiFi** — AP mode for initial setup, optional STA mode
- **Web interface** for configuration and control
- **TCP command interface** for relay control
- **OTA firmware updates** via web interface
- **8 Digital inputs** (optocoupler-isolated)
- **8 Relay outputs** via TCA9554 I2C expander

## Installation

### Prerequisites

- [PlatformIO](https://platformio.org/) (recommended) or Arduino IDE
- USB-C cable for initial programming

### With PlatformIO

```bash
# Compile
pio run

# Upload firmware
pio run -t upload

# Upload filesystem (web interface)
pio run -t uploadfs

# Serial monitor (115200 baud)
pio device monitor
```

### With Arduino IDE

1. Install ESP32 Board Support (v3.x)
2. Install required libraries:
   - ArduinoJson (^7.0.0)
   - ElegantOTA (^3.1.0)
   - AsyncTCP (^1.1.1)
   - ESPAsyncWebServer (^1.2.3)
3. Select board: "ESP32S3 Dev Module"
4. Open `src/main.cpp` as `.ino` file
5. Upload

## Hardware

### Pin Mapping

#### Digital Inputs (DI1-DI8)
Optocoupler-isolated, active LOW with internal pull-up.

| Input | GPIO |
|-------|------|
| DI1   | 4    |
| DI2   | 5    |
| DI3   | 6    |
| DI4   | 7    |
| DI5   | 8    |
| DI6   | 9    |
| DI7   | 10   |
| DI8   | 11   |

#### Relay Outputs (RO1-RO8)
Controlled via **TCA9554 I2C I/O Expander** (address 0x20).

| I2C | GPIO |
|-----|------|
| SDA | 42   |
| SCL | 41   |

The 8 relays (RO1-RO8) are controlled via TCA9554 pins P0-P7.

#### Ethernet (W5500 SPI)
| Signal | GPIO |
|--------|------|
| SCLK   | 15   |
| MOSI   | 13   |
| MISO   | 14   |
| CS     | 16   |
| INT    | 12   |

#### Additional Peripherals
| Function   | GPIO |
|------------|------|
| RS485 TX   | 17   |
| RS485 RX   | 18   |
| RGB LED    | 38   |
| Buzzer     | 46   |

## Usage

### Initial Setup

1. Upload firmware and filesystem
2. The module starts a WiFi Access Point:
   - SSID: `cinerelais1` (= hostname)
   - Password: open (no password)
3. Connect to the AP and open `http://192.168.4.1`
4. Configure Ethernet/WiFi in the web interface
5. Restart the device

### Web Interface

After startup, the web interface is available at:
- **Ethernet**: IP from DHCP or configured static IP
- **WiFi AP**: `http://192.168.4.1`
- **WiFi STA**: IP from DHCP or configured static IP

**Features:**
- Toggle relays on/off (click)
- Pulse relays (right-click)
- Control all relays at once
- Ethernet configuration (DHCP/static IP)
- WiFi configuration (AP/STA, DHCP/static IP)
- Set TCP port and pulse duration
- System information (Ethernet/WiFi status, TCA9554)
- OTA firmware update

### TCP Commands

Connect with a TCP client (e.g. `nc`, `telnet`) to the configured port (default: 5000).

| Command | Description |
|---------|-------------|
| `r1_on` ... `r8_on` | Turn relay on |
| `r1_off` ... `r8_off` | Turn relay off |
| `r1_pulse` | Pulse relay (default duration) |
| `r1_pulse_1000` | Pulse relay for 1000ms |
| `all_on` | Turn all relays on |
| `all_off` | Turn all relays off |
| `all_pulse` | Pulse all relays |
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
4. Device restarts automatically

## Configuration

### Default Values

| Parameter | Default |
|-----------|---------|
| Hostname | cinerelais1 |
| DHCP (Ethernet) | Enabled |
| WiFi | Enabled |
| WiFi AP | Enabled |
| WiFi AP Password | (open) |
| TCP Port | 5000 |
| Pulse Duration | 500 ms |
| Static IP | 192.168.1.100 |

### Changing Configuration

All settings can be changed via the web interface. Configuration is stored in flash memory (LittleFS) as `/config.json`.

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

# Configure WiFi SSID
curl -X POST -d "wifiSSID=MyNetwork&wifiPassword=secret" http://192.168.1.100/api/config
```

## Troubleshooting

### TCA9554 Not Found
- Check I2C connection (SDA=GPIO42, SCL=GPIO41)
- Check serial monitor for "TCA9554 found at 0x20"
- If not found: check board hardware

### No Ethernet Connection
- Check Ethernet cable
- Check PoE power supply (if POE version)
- Check serial monitor for "ETH Got IP"

### WiFi AP Not Visible
- Check if WiFi is enabled in configuration
- Check if WiFi AP is enabled
- Hostname = SSID of the Access Point

### Web Interface Not Reachable
- Check IP address in serial monitor
- For WiFi AP: use `192.168.4.1`
- Clear browser cache

## License

MIT License
