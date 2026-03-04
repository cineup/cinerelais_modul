#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================
// Waveshare ESP32-S3-ETH-8DI-8RO / ESP32-S3-POE-ETH-8DI-8RO
// Pin Configuration (verified from official datasheet)
// ============================================

// Digital Inputs (Active LOW — optocoupler isolated, direct ESP32 GPIO)
const int DI_PINS[8] = {
    4,   // IN1
    5,   // IN2
    6,   // IN3
    7,   // IN4
    8,   // IN5
    9,   // IN6
    10,  // IN7
    11   // IN8
};

// Relay Outputs via TCA9554 I2C I/O Expander
// Relays are NOT on direct GPIO — controlled via I2C at address 0x20
// TCA9554 pins P0-P7 map to CH1-CH8
#define I2C_SDA_PIN         42
#define I2C_SCL_PIN         41
#define TCA9554_ADDR        0x20

// TCA9554 Registers
#define TCA9554_INPUT_REG   0x00
#define TCA9554_OUTPUT_REG  0x01
#define TCA9554_POLARITY_REG 0x02
#define TCA9554_CONFIG_REG  0x03

// Ethernet Configuration (W5500 via SPI - HSPI)
#define ETH_MISO_PIN    14
#define ETH_MOSI_PIN    13
#define ETH_SCLK_PIN    15
#define ETH_CS_PIN      16
#define ETH_INT_PIN     12
#define ETH_RST_PIN     -1      // Not connected on this board

// RS485 (UART1)
#define RS485_TX_PIN    17
#define RS485_RX_PIN    18

// Peripherals
#define RGB_LED_PIN     38      // WS2812 RGB LED
#define BUZZER_PIN      46
#define RTC_INT_PIN     40      // PCF85063 RTC interrupt

// SD Card (optional, directly accessible on header)
// #define SD_CS_PIN    21
// #define SD_MISO_PIN  45
// #define SD_MOSI_PIN  47
// #define SD_SCLK_PIN  48

// ============================================
// Version
// ============================================

#define FIRMWARE_VERSION        "1.1.1"
// Minimum FS (web interface) version required by this firmware.
// Bump this when index.html changes are mandatory alongside a firmware update.
#define REQUIRED_FS_VERSION     "1.2.0"

// ============================================
// Default Configuration Values
// ============================================

#define DEFAULT_HOSTNAME        "CineRelais"
#define DEFAULT_TCP_PORT        5000
#define DEFAULT_PULSE_DURATION  500     // ms
#define CONFIG_FILE             "/config.json"

// WiFi AP defaults
#define DEFAULT_WIFI_AP_PASSWORD ""      // Empty = open AP

// NTP defaults
#define DEFAULT_NTP_SERVER      "pool.ntp.org"
#define DEFAULT_NTP_TIMEZONE    "CET-1CEST,M3.5.0,M10.5.0/3"  // Europe/Berlin

// Command log
#define COMMAND_LOG_SIZE        50

// LED defaults
#define DEFAULT_LED_ENABLED     true
#define DEFAULT_LED_BRIGHTNESS  51      // 20% of 255

// LED timing (ms)
#define LED_FLASH_DURATION      100     // Command/activity flash
#define LED_PULSE_INTERVAL      1000    // Slow pulse period
#define LED_FAST_BLINK_INTERVAL 200     // Fast blink period (error)

// ============================================
// Network Configuration Structure
// ============================================

struct NetworkConfig {
    // Ethernet
    bool useDHCP;
    char staticIP[16];
    char gateway[16];
    char subnet[16];
    char dns[16];

    // WiFi
    bool wifiEnabled;
    bool wifiAPEnabled;
    char wifiSSID[33];
    char wifiPassword[65];
    char wifiAPPassword[65];
    bool wifiDHCP;
    char wifiIP[16];
    char wifiGateway[16];
    char wifiSubnet[16];
    char wifiDNS[16];

    // General
    char hostname[32];
    uint16_t tcpPort;
    uint16_t pulseDuration;

    // NTP
    bool ntpEnabled;
    char ntpServer[64];
    char ntpTimezone[48];

    // LED
    bool ledEnabled;
    uint8_t ledBrightness;      // 0-255

    // Labels (max 12 chars each)
    char relayLabels[8][16];
    char inputLabels[8][16];
};

// Default configuration
const NetworkConfig DEFAULT_CONFIG = {
    // Ethernet
    true,                   // useDHCP
    "192.168.1.100",       // staticIP
    "192.168.1.1",         // gateway
    "255.255.255.0",       // subnet
    "8.8.8.8",             // dns
    // WiFi
    true,                   // wifiEnabled
    true,                   // wifiAPEnabled
    "",                     // wifiSSID (empty = no STA)
    "",                     // wifiPassword
    DEFAULT_WIFI_AP_PASSWORD, // wifiAPPassword
    true,                   // wifiDHCP
    "192.168.4.100",       // wifiIP
    "192.168.4.1",         // wifiGateway
    "255.255.255.0",       // wifiSubnet
    "8.8.8.8",             // wifiDNS
    // General
    DEFAULT_HOSTNAME,       // hostname
    DEFAULT_TCP_PORT,       // tcpPort
    DEFAULT_PULSE_DURATION, // pulseDuration
    // NTP
    true,                   // ntpEnabled
    DEFAULT_NTP_SERVER,     // ntpServer
    DEFAULT_NTP_TIMEZONE,   // ntpTimezone
    // LED
    DEFAULT_LED_ENABLED,    // ledEnabled
    DEFAULT_LED_BRIGHTNESS, // ledBrightness
    // Labels
    {"", "", "", "", "", "", "", ""},  // relayLabels
    {"", "", "", "", "", "", "", ""}   // inputLabels
};

#endif // CONFIG_H
