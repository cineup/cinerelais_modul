#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================
// Waveshare ESP32-S3-POE-ETH-8DI-8RO Pin Configuration
// ============================================

// Digital Inputs (Active LOW - directly read via ESP32 GPIO)
// DI1-DI8: optocoupler isolated inputs
const int DI_PINS[8] = {
    4,   // DI1
    5,   // DI2
    6,   // DI3
    7,   // DI4
    15,  // DI5
    16,  // DI6
    17,  // DI7
    18   // DI8
};

// Relay Outputs (directly controlled via ESP32 GPIO)
// RO1-RO8: relay outputs via ULN2803
const int RO_PINS[8] = {
    33,  // RO1
    34,  // RO2
    35,  // RO3
    36,  // RO4
    37,  // RO5
    38,  // RO6
    39,  // RO7
    40   // RO8
};

// Ethernet Configuration (W5500 via SPI)
#define ETH_SPI_HOST    SPI2_HOST
#define ETH_MISO_PIN    11
#define ETH_MOSI_PIN    13
#define ETH_SCLK_PIN    12
#define ETH_CS_PIN      10
#define ETH_INT_PIN     14
#define ETH_RST_PIN     -1
#define ETH_PHY_TYPE    ETH_PHY_W5500

// Status LED (optional - check your board)
#define STATUS_LED_PIN  48

// ============================================
// Default Configuration Values
// ============================================

#define DEFAULT_HOSTNAME        "esp32-relay"
#define DEFAULT_TCP_PORT        5000
#define DEFAULT_PULSE_DURATION  500     // ms
#define CONFIG_FILE             "/config.json"

// ============================================
// Network Configuration Structure
// ============================================

struct NetworkConfig {
    bool useDHCP;
    char staticIP[16];
    char gateway[16];
    char subnet[16];
    char dns[16];
    char hostname[32];
    uint16_t tcpPort;
    uint16_t pulseDuration;
};

// Default configuration
const NetworkConfig DEFAULT_CONFIG = {
    true,                   // useDHCP
    "192.168.1.100",       // staticIP
    "192.168.1.1",         // gateway
    "255.255.255.0",       // subnet
    "8.8.8.8",             // dns
    DEFAULT_HOSTNAME,       // hostname
    DEFAULT_TCP_PORT,       // tcpPort
    DEFAULT_PULSE_DURATION  // pulseDuration
};

#endif // CONFIG_H
