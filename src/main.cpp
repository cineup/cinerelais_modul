/*
 * CineRelais Modul — ESP32-S3 Relay Controller
 * For Waveshare ESP32-S3-ETH-8DI-8RO / ESP32-S3-POE-ETH-8DI-8RO
 *
 * Features:
 * - 8 Relay outputs via TCA9554 I2C I/O expander
 * - 8 Digital inputs (optocoupler isolated)
 * - Ethernet (W5500) with DHCP or static IP
 * - WiFi AP mode for initial configuration
 * - WiFi STA mode (optional, connect to existing network)
 * - Web interface for configuration and control
 * - TCP command interface for relay control
 * - OTA firmware updates via ElegantOTA
 *
 * TCP Commands (Port configurable, default 5000):
 *   r1_on ... r8_on      - Turn relay on
 *   r1_off ... r8_off    - Turn relay off
 *   r1_pulse             - Pulse relay (default duration)
 *   r1_pulse_1000        - Pulse relay for 1000ms
 *   all_on / all_off     - All relays on/off
 *   all_pulse            - Pulse all relays
 *   all_pulse_500        - Pulse all relays for 500ms
 *   status               - JSON status
 *   help                 - Command list
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

// Ethernet_Generic for W5500 support on ESP32
// TEMPORARILY DISABLED for debugging
// #define USING_W5500       true
// #define USING_CUSTOM_SPI  true
// #include <Ethernet_Generic.h>

#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <time.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"

// ============================================
// Global Variables
// ============================================

NetworkConfig config;
AsyncWebServer webServer(80);
AsyncServer* tcpServer = nullptr;
std::vector<AsyncClient*> tcpClients;

bool relayStates[8] = {false};
bool inputStates[8] = {false};
bool ethConnected = false;
bool wifiSTAConnected = false;
bool wifiAPActive = false;
bool tca9554Found = false;

unsigned long pulseEndTime[8] = {0};
bool pulseActive[8] = {false};

unsigned long lastWifiReconnect = 0;

// NTP state
bool ntpSynced = false;

// Command log (circular buffer)
struct LogEntry {
    time_t timestamp;
    char source[16];    // IP address or "Web"
    char command[48];
};
LogEntry commandLog[COMMAND_LOG_SIZE];
int logIndex = 0;
int logCount = 0;

// ============================================
// RGB LED (WS2812) - TEMPORARILY USE POINTER
// ============================================

Adafruit_NeoPixel* rgbLed = nullptr;  // Initialize in setup() instead

// LED state machine
enum LedState {
    LED_OFF,
    LED_SOLID,
    LED_PULSE,          // Slow pulse (fade in/out)
    LED_FAST_BLINK,     // Fast on/off blink
    LED_FLASH           // Brief flash, then return to background
};

// LED colors (R, G, B)
struct LedColor {
    uint8_t r, g, b;
};

const LedColor COLOR_OFF     = {0, 0, 0};
const LedColor COLOR_RED     = {255, 0, 0};
const LedColor COLOR_GREEN   = {0, 255, 0};
const LedColor COLOR_BLUE    = {0, 0, 255};
const LedColor COLOR_CYAN    = {0, 255, 255};
const LedColor COLOR_YELLOW  = {255, 255, 0};
const LedColor COLOR_ORANGE  = {255, 128, 0};
const LedColor COLOR_PURPLE  = {128, 0, 255};

// Current LED state
LedState ledCurrentState = LED_OFF;
LedColor ledBackgroundColor = COLOR_OFF;
LedColor ledCurrentColor = COLOR_OFF;
unsigned long ledStateStartTime = 0;
unsigned long ledLastUpdate = 0;
bool ledOtaActive = false;

// Flash queue (command received -> relay activity)
bool ledFlashPending = false;
LedColor ledFlashColor = COLOR_OFF;
unsigned long ledFlashEndTime = 0;

// ============================================
// Forward Declarations
// ============================================

void loadConfig();
void saveConfig();
void setupPins();
void setupI2C();
void setupEthernet();
void checkEthernetLink();
void setupWiFi();
void setupWebServer();
void setupTCPServer();
void handleTCPCommand(AsyncClient* client, String command);
String getStatusJSON();
void setRelay(int relay, bool state);
void setAllRelays(bool state);
void pulseRelay(int relay, unsigned long duration);
void pulseAllRelays(unsigned long duration);
void updatePulses();
void WiFiEvent(WiFiEvent_t event);
void tca9554Init();
void tca9554Write(uint8_t pin, bool state);
void tca9554WriteAll(uint8_t value);
uint8_t tca9554Read();
void setupNTP();
void addLogEntry(const char* source, const char* command);
String getLogJSON();
String getTimeString(time_t t);
void setupLED();
void updateLED();
void setLedColor(LedColor color, uint8_t brightness);
void ledFlash(LedColor color);
void ledCommandReceived();
void ledRelayActivity();
void updateLedBackgroundState();

// ============================================
// Setup
// ============================================

void setup() {
    // Early serial init with longer delay for USB CDC
    Serial.begin(115200);
    delay(3000);  // Wait for USB CDC to connect

    Serial.println("\n\n========================================");
    Serial.println("CineRelais Modul — ESP32-S3 Relay Controller");
    Serial.println("========================================\n");
    Serial.flush();

    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("ERROR: LittleFS mount failed!");
    } else {
        Serial.println("LittleFS mounted successfully");
    }

    // Load configuration
    loadConfig();

    // Setup hardware
    setupPins();
    setupI2C();
    setupLED();

    // Setup networking
    WiFi.onEvent(WiFiEvent);
    // setupEthernet();  // TEMPORARILY DISABLED - debugging boot loop
    setupWiFi();

    // Wait for any network connection
    Serial.println("Waiting for network connection...");
    unsigned long startTime = millis();
    while (!ethConnected && !wifiSTAConnected && !wifiAPActive &&
           (millis() - startTime < 10000)) {
        delay(100);
    }

    // Setup NTP (if enabled and network is available)
    setupNTP();

    // Start servers
    setupWebServer();
    setupTCPServer();

    Serial.println("\n========================================");
    Serial.println("System Ready!");
    // TEMP DISABLED: if (ethConnected) {
    //     Serial.printf("Ethernet IP: %s\n", Ethernet.localIP().toString().c_str());
    // }
    if (wifiSTAConnected) {
        Serial.printf("WiFi STA IP: %s\n", WiFi.localIP().toString().c_str());
    }
    if (wifiAPActive) {
        Serial.printf("WiFi AP IP: %s (SSID: %s)\n",
                       WiFi.softAPIP().toString().c_str(), config.hostname);
    }
    Serial.printf("TCP Port: %d\n", config.tcpPort);
    Serial.println("========================================\n");
}

// ============================================
// Main Loop
// ============================================

void loop() {
    // Update pulse timers
    updatePulses();

    // Update LED state
    updateLED();

    // Check Ethernet link status
    // checkEthernetLink();  // TEMPORARILY DISABLED

    // Read input states
    for (int i = 0; i < 8; i++) {
        inputStates[i] = !digitalRead(DI_PINS[i]); // Active LOW
    }

    // WiFi STA reconnect (every 30s if disconnected)
    if (config.wifiEnabled && strlen(config.wifiSSID) > 0 &&
        !wifiSTAConnected && millis() - lastWifiReconnect > 30000) {
        lastWifiReconnect = millis();
        Serial.println("WiFi STA: Reconnecting...");
        WiFi.begin(config.wifiSSID, config.wifiPassword);
    }

    // ElegantOTA loop
    ElegantOTA.loop();

    delay(10);
}

// ============================================
// TCA9554 I2C I/O Expander (Relay Control)
// ============================================

void tca9554Init() {
    // Set all pins as outputs
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(TCA9554_CONFIG_REG);
    Wire.write(0x00); // All outputs
    uint8_t err = Wire.endTransmission();

    if (err != 0) {
        Serial.printf("ERROR: TCA9554 not found at 0x%02X (I2C error %d)\n",
                       TCA9554_ADDR, err);
        tca9554Found = false;
        return;
    }

    tca9554Found = true;
    Serial.printf("TCA9554 found at 0x%02X\n", TCA9554_ADDR);

    // Set all outputs LOW (all relays off)
    tca9554WriteAll(0x00);
}

uint8_t tca9554Read() {
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(TCA9554_OUTPUT_REG);
    Wire.endTransmission();
    Wire.requestFrom((uint8_t)TCA9554_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

void tca9554Write(uint8_t pin, bool state) {
    if (!tca9554Found || pin > 7) return;

    uint8_t current = tca9554Read();
    if (state) {
        current |= (1 << pin);
    } else {
        current &= ~(1 << pin);
    }

    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(TCA9554_OUTPUT_REG);
    Wire.write(current);
    Wire.endTransmission();
}

void tca9554WriteAll(uint8_t value) {
    if (!tca9554Found) return;

    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(TCA9554_OUTPUT_REG);
    Wire.write(value);
    Wire.endTransmission();
}

// ============================================
// Configuration Management
// ============================================

void loadConfig() {
    Serial.println("Loading configuration...");

    // Set defaults first
    config = DEFAULT_CONFIG;

    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file) {
        Serial.println("No config file found, using defaults");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("Config parse error: %s\n", error.c_str());
        return;
    }

    // Ethernet
    config.useDHCP = doc["useDHCP"] | DEFAULT_CONFIG.useDHCP;
    strlcpy(config.staticIP, doc["staticIP"] | DEFAULT_CONFIG.staticIP, sizeof(config.staticIP));
    strlcpy(config.gateway, doc["gateway"] | DEFAULT_CONFIG.gateway, sizeof(config.gateway));
    strlcpy(config.subnet, doc["subnet"] | DEFAULT_CONFIG.subnet, sizeof(config.subnet));
    strlcpy(config.dns, doc["dns"] | DEFAULT_CONFIG.dns, sizeof(config.dns));

    // WiFi
    config.wifiEnabled = doc["wifiEnabled"] | DEFAULT_CONFIG.wifiEnabled;
    config.wifiAPEnabled = doc["wifiAPEnabled"] | DEFAULT_CONFIG.wifiAPEnabled;
    strlcpy(config.wifiSSID, doc["wifiSSID"] | DEFAULT_CONFIG.wifiSSID, sizeof(config.wifiSSID));
    strlcpy(config.wifiPassword, doc["wifiPassword"] | DEFAULT_CONFIG.wifiPassword, sizeof(config.wifiPassword));
    strlcpy(config.wifiAPPassword, doc["wifiAPPassword"] | DEFAULT_CONFIG.wifiAPPassword, sizeof(config.wifiAPPassword));
    config.wifiDHCP = doc["wifiDHCP"] | DEFAULT_CONFIG.wifiDHCP;
    strlcpy(config.wifiIP, doc["wifiIP"] | DEFAULT_CONFIG.wifiIP, sizeof(config.wifiIP));
    strlcpy(config.wifiGateway, doc["wifiGateway"] | DEFAULT_CONFIG.wifiGateway, sizeof(config.wifiGateway));
    strlcpy(config.wifiSubnet, doc["wifiSubnet"] | DEFAULT_CONFIG.wifiSubnet, sizeof(config.wifiSubnet));
    strlcpy(config.wifiDNS, doc["wifiDNS"] | DEFAULT_CONFIG.wifiDNS, sizeof(config.wifiDNS));

    // General
    strlcpy(config.hostname, doc["hostname"] | DEFAULT_CONFIG.hostname, sizeof(config.hostname));
    config.tcpPort = doc["tcpPort"] | DEFAULT_CONFIG.tcpPort;
    config.pulseDuration = doc["pulseDuration"] | DEFAULT_CONFIG.pulseDuration;

    // NTP
    config.ntpEnabled = doc["ntpEnabled"] | DEFAULT_CONFIG.ntpEnabled;
    strlcpy(config.ntpServer, doc["ntpServer"] | DEFAULT_CONFIG.ntpServer, sizeof(config.ntpServer));
    strlcpy(config.ntpTimezone, doc["ntpTimezone"] | DEFAULT_CONFIG.ntpTimezone, sizeof(config.ntpTimezone));

    // LED
    config.ledEnabled = doc["ledEnabled"] | DEFAULT_CONFIG.ledEnabled;
    config.ledBrightness = doc["ledBrightness"] | DEFAULT_CONFIG.ledBrightness;

    Serial.println("Configuration loaded:");
    Serial.printf("  DHCP: %s\n", config.useDHCP ? "enabled" : "disabled");
    Serial.printf("  Hostname: %s\n", config.hostname);
    Serial.printf("  WiFi: %s\n", config.wifiEnabled ? "enabled" : "disabled");
    Serial.printf("  WiFi SSID: %s\n", strlen(config.wifiSSID) > 0 ? config.wifiSSID : "(none)");
    Serial.printf("  TCP Port: %d\n", config.tcpPort);
}

void saveConfig() {
    Serial.println("Saving configuration...");

    JsonDocument doc;

    // Ethernet
    doc["useDHCP"] = config.useDHCP;
    doc["staticIP"] = config.staticIP;
    doc["gateway"] = config.gateway;
    doc["subnet"] = config.subnet;
    doc["dns"] = config.dns;

    // WiFi
    doc["wifiEnabled"] = config.wifiEnabled;
    doc["wifiAPEnabled"] = config.wifiAPEnabled;
    doc["wifiSSID"] = config.wifiSSID;
    doc["wifiPassword"] = config.wifiPassword;
    doc["wifiAPPassword"] = config.wifiAPPassword;
    doc["wifiDHCP"] = config.wifiDHCP;
    doc["wifiIP"] = config.wifiIP;
    doc["wifiGateway"] = config.wifiGateway;
    doc["wifiSubnet"] = config.wifiSubnet;
    doc["wifiDNS"] = config.wifiDNS;

    // General
    doc["hostname"] = config.hostname;
    doc["tcpPort"] = config.tcpPort;
    doc["pulseDuration"] = config.pulseDuration;

    // NTP
    doc["ntpEnabled"] = config.ntpEnabled;
    doc["ntpServer"] = config.ntpServer;
    doc["ntpTimezone"] = config.ntpTimezone;

    // LED
    doc["ledEnabled"] = config.ledEnabled;
    doc["ledBrightness"] = config.ledBrightness;

    File file = LittleFS.open(CONFIG_FILE, "w");
    if (!file) {
        Serial.println("ERROR: Failed to open config file for writing");
        return;
    }

    serializeJson(doc, file);
    file.close();
    Serial.println("Configuration saved");
}

// ============================================
// Hardware Setup
// ============================================

void setupPins() {
    Serial.println("Setting up GPIO pins...");

    // Setup Digital Inputs (active LOW with internal pull-up)
    for (int i = 0; i < 8; i++) {
        pinMode(DI_PINS[i], INPUT_PULLUP);
    }

    Serial.println("GPIO pins configured");
}

void setupI2C() {
    Serial.println("Setting up I2C bus...");

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);
    tca9554Init();
}

// ============================================
// Network Events (WiFi only - Ethernet uses polling)
// ============================================

void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("WiFi STA Got IP: %s\n", WiFi.localIP().toString().c_str());
            wifiSTAConnected = true;
            updateLedBackgroundState();
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("WiFi STA Disconnected");
            wifiSTAConnected = false;
            updateLedBackgroundState();
            break;
        case ARDUINO_EVENT_WIFI_AP_START:
            Serial.printf("WiFi AP Started (IP: %s)\n", WiFi.softAPIP().toString().c_str());
            wifiAPActive = true;
            updateLedBackgroundState();
            break;
        case ARDUINO_EVENT_WIFI_AP_STOP:
            Serial.println("WiFi AP Stopped");
            wifiAPActive = false;
            updateLedBackgroundState();
            break;
        default:
            break;
    }
}

// ============================================
// Ethernet Setup (W5500 via SPI) - TEMPORARILY DISABLED
// ============================================

// MAC address for W5500 (unique per device)
byte ethMac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

void setupEthernet() {
    Serial.println("Ethernet DISABLED for debugging");
}

void checkEthernetLink() {
    // DISABLED
}

// ============================================
// WiFi Setup
// ============================================

void setupWiFi() {
    if (!config.wifiEnabled) {
        Serial.println("WiFi disabled in configuration");
        WiFi.mode(WIFI_OFF);
        return;
    }

    Serial.println("Setting up WiFi...");

    bool hasSSID = strlen(config.wifiSSID) > 0;

    // Determine WiFi mode
    if (config.wifiAPEnabled && hasSSID) {
        WiFi.mode(WIFI_AP_STA);
    } else if (config.wifiAPEnabled) {
        WiFi.mode(WIFI_AP);
    } else if (hasSSID) {
        WiFi.mode(WIFI_STA);
    } else {
        Serial.println("WiFi enabled but no AP or STA configured");
        WiFi.mode(WIFI_OFF);
        return;
    }

    // Start AP if enabled
    if (config.wifiAPEnabled) {
        const char* apPassword = strlen(config.wifiAPPassword) >= 8 ?
                                 config.wifiAPPassword : NULL;
        WiFi.softAP(config.hostname, apPassword);
        Serial.printf("WiFi AP started: SSID=%s, Password=%s\n",
                       config.hostname, apPassword ? "****" : "(open)");
    }

    // Connect STA if SSID is configured
    if (hasSSID) {
        WiFi.setHostname(config.hostname);

        if (!config.wifiDHCP) {
            IPAddress ip, gateway, subnet, dns;
            ip.fromString(config.wifiIP);
            gateway.fromString(config.wifiGateway);
            subnet.fromString(config.wifiSubnet);
            dns.fromString(config.wifiDNS);
            WiFi.config(ip, gateway, subnet, dns);
        }

        WiFi.begin(config.wifiSSID, config.wifiPassword);
        Serial.printf("WiFi STA connecting to: %s\n", config.wifiSSID);
    }
}

// ============================================
// NTP Setup
// ============================================

void setupNTP() {
    if (!config.ntpEnabled) {
        Serial.println("NTP disabled");
        return;
    }

    if (!ethConnected && !wifiSTAConnected) {
        Serial.println("NTP: No network connection available");
        return;
    }

    Serial.printf("Configuring NTP: server=%s, timezone=%s\n",
                   config.ntpServer, config.ntpTimezone);

    configTzTime(config.ntpTimezone, config.ntpServer);

    // Wait briefly for time sync
    Serial.print("NTP: Waiting for time sync");
    int attempts = 0;
    while (time(nullptr) < 100000 && attempts < 50) {
        delay(100);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    time_t now = time(nullptr);
    if (now > 100000) {
        ntpSynced = true;
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        Serial.printf("NTP synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                       timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                       timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        Serial.println("NTP: Time sync failed (will retry in background)");
    }
}

// ============================================
// RGB LED Control
// ============================================

void setupLED() {
    if (!config.ledEnabled) {
        Serial.println("LED disabled");
        return;
    }

    Serial.println("Setting up RGB LED...");
    rgbLed = new Adafruit_NeoPixel(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
    rgbLed->begin();
    rgbLed->clear();
    rgbLed->show();

    // Initial state: no network = red
    updateLedBackgroundState();
    Serial.println("RGB LED initialized");
}

void setLedColor(LedColor color, uint8_t brightness) {
    if (!config.ledEnabled || !rgbLed) return;

    // Scale color by brightness (0-255)
    uint8_t r = (color.r * brightness) / 255;
    uint8_t g = (color.g * brightness) / 255;
    uint8_t b = (color.b * brightness) / 255;

    rgbLed->setPixelColor(0, rgbLed->Color(r, g, b));
    rgbLed->show();
}

void updateLedBackgroundState() {
    // Determine background color based on network status
    // Priority: Ethernet (green) > WiFi STA (cyan) > WiFi AP only (blue) > No network (red)
    if (ethConnected) {
        ledBackgroundColor = COLOR_GREEN;
        ledCurrentState = LED_SOLID;
    } else if (wifiSTAConnected) {
        ledBackgroundColor = COLOR_CYAN;
        ledCurrentState = LED_SOLID;
    } else if (wifiAPActive) {
        ledBackgroundColor = COLOR_BLUE;
        ledCurrentState = LED_PULSE;  // Slow pulse for AP-only mode
    } else {
        ledBackgroundColor = COLOR_RED;
        ledCurrentState = LED_FAST_BLINK;  // Error state
    }
}

void ledFlash(LedColor color) {
    if (!config.ledEnabled) return;

    ledFlashPending = true;
    ledFlashColor = color;
    ledFlashEndTime = millis() + LED_FLASH_DURATION;

    // Immediately show the flash color
    setLedColor(color, config.ledBrightness);
}

void ledCommandReceived() {
    // Orange flash when command is received
    ledFlash(COLOR_ORANGE);
}

void ledRelayActivity() {
    // Yellow flash when relay state changes
    // If currently flashing orange (command received), queue the yellow flash
    if (ledFlashPending && ledFlashColor.r == COLOR_ORANGE.r) {
        // Will be handled after orange flash ends
        return;
    }
    ledFlash(COLOR_YELLOW);
}

void updateLED() {
    if (!config.ledEnabled) return;

    unsigned long now = millis();

    // OTA mode: purple pulsing (highest priority)
    if (ledOtaActive) {
        unsigned long elapsed = (now - ledStateStartTime) % LED_PULSE_INTERVAL;
        float phase = (float)elapsed / LED_PULSE_INTERVAL;
        // Sine wave pulse: 0 -> 1 -> 0
        float brightness = sin(phase * PI);
        uint8_t scaledBrightness = (uint8_t)(brightness * config.ledBrightness);
        setLedColor(COLOR_PURPLE, scaledBrightness);
        return;
    }

    // Handle flash sequence
    if (ledFlashPending) {
        if (now < ledFlashEndTime) {
            // Still flashing
            return;
        }

        // Flash ended
        ledFlashPending = false;

        // If this was an orange flash (command), now do yellow (relay activity)
        if (ledFlashColor.r == COLOR_ORANGE.r && ledFlashColor.g == COLOR_ORANGE.g) {
            ledFlash(COLOR_YELLOW);
            return;
        }

        // Return to background state
        updateLedBackgroundState();
    }

    // Handle background states
    switch (ledCurrentState) {
        case LED_OFF:
            setLedColor(COLOR_OFF, 0);
            break;

        case LED_SOLID:
            setLedColor(ledBackgroundColor, config.ledBrightness);
            break;

        case LED_PULSE: {
            // Slow pulse (WiFi AP only mode)
            unsigned long elapsed = (now - ledStateStartTime) % LED_PULSE_INTERVAL;
            float phase = (float)elapsed / LED_PULSE_INTERVAL;
            float brightness = (sin(phase * 2 * PI) + 1) / 2;  // 0.5 -> 1 -> 0.5
            uint8_t scaledBrightness = (uint8_t)(brightness * config.ledBrightness);
            setLedColor(ledBackgroundColor, scaledBrightness);
            break;
        }

        case LED_FAST_BLINK: {
            // Fast blink (error/no network)
            unsigned long elapsed = now % LED_FAST_BLINK_INTERVAL;
            bool on = elapsed < (LED_FAST_BLINK_INTERVAL / 2);
            setLedColor(ledBackgroundColor, on ? config.ledBrightness : 0);
            break;
        }

        case LED_FLASH:
            // Handled above
            break;
    }
}

// ============================================
// Command Log
// ============================================

String getTimeString(time_t t) {
    if (t < 100000) {
        // No valid time, use uptime
        unsigned long uptime = millis() / 1000;
        char buf[16];
        snprintf(buf, sizeof(buf), "+%lus", uptime);
        return String(buf);
    }
    struct tm timeinfo;
    localtime_r(&t, &timeinfo);
    char buf[24];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return String(buf);
}

void addLogEntry(const char* source, const char* command) {
    LogEntry& entry = commandLog[logIndex];
    entry.timestamp = time(nullptr);
    strlcpy(entry.source, source, sizeof(entry.source));
    strlcpy(entry.command, command, sizeof(entry.command));

    logIndex = (logIndex + 1) % COMMAND_LOG_SIZE;
    if (logCount < COMMAND_LOG_SIZE) {
        logCount++;
    }

    Serial.printf("[LOG] %s | %s | %s\n",
                   getTimeString(entry.timestamp).c_str(), source, command);
}

String getLogJSON() {
    JsonDocument doc;
    JsonArray logArray = doc.to<JsonArray>();

    // Output in reverse chronological order (newest first)
    for (int i = 0; i < logCount; i++) {
        int idx = (logIndex - 1 - i + COMMAND_LOG_SIZE) % COMMAND_LOG_SIZE;
        JsonObject entry = logArray.add<JsonObject>();
        entry["time"] = getTimeString(commandLog[idx].timestamp);
        entry["timestamp"] = (long)commandLog[idx].timestamp;
        entry["source"] = commandLog[idx].source;
        entry["command"] = commandLog[idx].command;
    }

    String output;
    serializeJson(doc, output);
    return output;
}

// ============================================
// Relay Control (via TCA9554)
// ============================================

void setRelay(int relay, bool state) {
    if (relay < 1 || relay > 8) return;

    int index = relay - 1;
    tca9554Write(index, state);
    relayStates[index] = state;

    // Cancel any active pulse
    pulseActive[index] = false;
    pulseEndTime[index] = 0;

    // LED feedback
    ledRelayActivity();

    Serial.printf("Relay %d: %s\n", relay, state ? "ON" : "OFF");
}

void setAllRelays(bool state) {
    tca9554WriteAll(state ? 0xFF : 0x00);
    for (int i = 0; i < 8; i++) {
        relayStates[i] = state;
        pulseActive[i] = false;
        pulseEndTime[i] = 0;
    }

    // LED feedback
    ledRelayActivity();

    Serial.printf("All relays: %s\n", state ? "ON" : "OFF");
}

void pulseRelay(int relay, unsigned long duration) {
    if (relay < 1 || relay > 8) return;

    int index = relay - 1;
    tca9554Write(index, true);
    relayStates[index] = true;
    pulseActive[index] = true;
    pulseEndTime[index] = millis() + duration;

    // LED feedback
    ledRelayActivity();

    Serial.printf("Relay %d: PULSE (%lu ms)\n", relay, duration);
}

void pulseAllRelays(unsigned long duration) {
    tca9554WriteAll(0xFF);
    unsigned long endTime = millis() + duration;
    for (int i = 0; i < 8; i++) {
        relayStates[i] = true;
        pulseActive[i] = true;
        pulseEndTime[i] = endTime;
    }

    // LED feedback
    ledRelayActivity();

    Serial.printf("All relays: PULSE (%lu ms)\n", duration);
}

void updatePulses() {
    unsigned long now = millis();
    bool needsWrite = false;
    uint8_t outputState = tca9554Found ? tca9554Read() : 0;

    for (int i = 0; i < 8; i++) {
        if (pulseActive[i] && now >= pulseEndTime[i]) {
            outputState &= ~(1 << i);
            relayStates[i] = false;
            pulseActive[i] = false;
            pulseEndTime[i] = 0;
            needsWrite = true;
            Serial.printf("Relay %d: PULSE ended\n", i + 1);
        }
    }

    if (needsWrite) {
        tca9554WriteAll(outputState);
    }
}

// ============================================
// Status JSON
// ============================================

String getStatusJSON() {
    JsonDocument doc;

    // Network info
    doc["hostname"] = config.hostname;
    doc["tcpPort"] = config.tcpPort;
    doc["pulseDuration"] = config.pulseDuration;
    doc["uptime"] = millis() / 1000;

    // Ethernet (TEMPORARILY DISABLED)
    doc["ethConnected"] = false;
    doc["ethIP"] = "";
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             ethMac[0], ethMac[1], ethMac[2], ethMac[3], ethMac[4], ethMac[5]);
    doc["ethMAC"] = macStr;
    doc["dhcp"] = config.useDHCP;
    doc["staticIP"] = config.staticIP;
    doc["gateway"] = config.gateway;
    doc["subnet"] = config.subnet;
    doc["dns"] = config.dns;

    // WiFi
    doc["wifiEnabled"] = config.wifiEnabled;
    doc["wifiAPEnabled"] = config.wifiAPEnabled;
    doc["wifiAPActive"] = wifiAPActive;
    doc["wifiAPIP"] = wifiAPActive ? WiFi.softAPIP().toString() : "";
    doc["wifiSTAConnected"] = wifiSTAConnected;
    doc["wifiSTAIP"] = wifiSTAConnected ? WiFi.localIP().toString() : "";
    doc["wifiSSID"] = config.wifiSSID;
    doc["wifiDHCP"] = config.wifiDHCP;
    doc["wifiIP"] = config.wifiIP;
    doc["wifiGateway"] = config.wifiGateway;
    doc["wifiSubnet"] = config.wifiSubnet;
    doc["wifiDNS"] = config.wifiDNS;

    // For backwards compatibility: "ip" returns first available IP
    // ethConnected check disabled temporarily
    if (wifiSTAConnected) {
        doc["ip"] = WiFi.localIP().toString();
        doc["mac"] = WiFi.macAddress();
    } else if (wifiAPActive) {
        doc["ip"] = WiFi.softAPIP().toString();
        doc["mac"] = WiFi.softAPmacAddress();
    } else {
        doc["ip"] = "";
        doc["mac"] = "";
    }

    // Hardware
    doc["tca9554"] = tca9554Found;

    // LED
    doc["ledEnabled"] = config.ledEnabled;
    doc["ledBrightness"] = config.ledBrightness;

    // NTP
    doc["ntpEnabled"] = config.ntpEnabled;
    doc["ntpSynced"] = ntpSynced;
    time_t now = time(nullptr);
    if (now > 100000) {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char timeBuf[32];
        snprintf(timeBuf, sizeof(timeBuf), "%04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        doc["currentTime"] = timeBuf;
    } else {
        doc["currentTime"] = "";
    }

    // Relay states
    JsonArray relays = doc["relays"].to<JsonArray>();
    for (int i = 0; i < 8; i++) {
        relays.add(relayStates[i]);
    }

    // Input states
    JsonArray inputs = doc["inputs"].to<JsonArray>();
    for (int i = 0; i < 8; i++) {
        inputs.add(inputStates[i]);
    }

    String output;
    serializeJson(doc, output);
    return output;
}

// ============================================
// TCP Server
// ============================================

void handleTCPData(void* arg, AsyncClient* client, void* data, size_t len) {
    String command = String((char*)data).substring(0, len);
    command.trim();
    command.toLowerCase();

    String clientIP = client->remoteIP().toString();
    Serial.printf("TCP [%s]: %s\n", clientIP.c_str(), command.c_str());

    // Log the command (except status and help which are informational)
    if (command != "status" && command != "help") {
        addLogEntry(clientIP.c_str(), command.c_str());
        // LED flash for command received
        ledCommandReceived();
    }

    handleTCPCommand(client, command);
}

void handleTCPClient(void* arg, AsyncClient* client) {
    Serial.printf("TCP Client connected: %s\n", client->remoteIP().toString().c_str());

    tcpClients.push_back(client);

    client->onData(handleTCPData);

    client->onDisconnect([](void* arg, AsyncClient* client) {
        Serial.printf("TCP Client disconnected: %s\n", client->remoteIP().toString().c_str());
        tcpClients.erase(std::remove(tcpClients.begin(), tcpClients.end(), client), tcpClients.end());
    });

    client->onError([](void* arg, AsyncClient* client, int8_t error) {
        Serial.printf("TCP Error: %d\n", error);
    });

    // Send welcome message
    client->write("CineRelais Modul — ESP32-S3 Relay Controller\r\nType help for commands\r\n");
}

void handleTCPCommand(AsyncClient* client, String command) {
    String response = "";

    if (command.startsWith("r") && command.length() >= 4) {
        // Parse relay number: r1_on, r8_off, r3_pulse, r3_pulse_1000
        char relayChar = command.charAt(1);
        int relay = relayChar - '0';

        if (relay >= 1 && relay <= 8 && command.charAt(2) == '_') {
            String action = command.substring(3);

            if (action == "on") {
                setRelay(relay, true);
                response = "OK: r" + String(relay) + " on\r\n";
            } else if (action == "off") {
                setRelay(relay, false);
                response = "OK: r" + String(relay) + " off\r\n";
            } else if (action == "pulse") {
                pulseRelay(relay, config.pulseDuration);
                response = "OK: r" + String(relay) + " pulse (" + String(config.pulseDuration) + "ms)\r\n";
            } else if (action.startsWith("pulse_")) {
                unsigned long duration = action.substring(6).toInt();
                if (duration == 0) duration = config.pulseDuration;
                pulseRelay(relay, duration);
                response = "OK: r" + String(relay) + " pulse (" + String(duration) + "ms)\r\n";
            } else {
                response = "ERROR: Unknown action. Use on, off, pulse, pulse_<ms>\r\n";
            }
        } else {
            response = "ERROR: Invalid relay number (r1-r8)\r\n";
        }
    }
    else if (command.startsWith("all_")) {
        String action = command.substring(4);

        if (action == "on") {
            setAllRelays(true);
            response = "OK: all on\r\n";
        } else if (action == "off") {
            setAllRelays(false);
            response = "OK: all off\r\n";
        } else if (action == "pulse") {
            pulseAllRelays(config.pulseDuration);
            response = "OK: all pulse (" + String(config.pulseDuration) + "ms)\r\n";
        } else if (action.startsWith("pulse_")) {
            unsigned long duration = action.substring(6).toInt();
            if (duration == 0) duration = config.pulseDuration;
            pulseAllRelays(duration);
            response = "OK: all pulse (" + String(duration) + "ms)\r\n";
        } else {
            response = "ERROR: Unknown action. Use on, off, pulse, pulse_<ms>\r\n";
        }
    }
    else if (command == "status") {
        response = getStatusJSON() + "\r\n";
    }
    else if (command == "help") {
        response = "Available commands:\r\n";
        response += "  r<1-8>_on          - Turn relay on\r\n";
        response += "  r<1-8>_off         - Turn relay off\r\n";
        response += "  r<1-8>_pulse       - Pulse relay (default duration)\r\n";
        response += "  r<1-8>_pulse_<ms>  - Pulse relay for <ms> milliseconds\r\n";
        response += "  all_on             - Turn all relays on\r\n";
        response += "  all_off            - Turn all relays off\r\n";
        response += "  all_pulse          - Pulse all relays (default duration)\r\n";
        response += "  all_pulse_<ms>     - Pulse all relays for <ms> milliseconds\r\n";
        response += "  status             - Get JSON status of all I/O\r\n";
        response += "  help               - Show this help\r\n";
    }
    else {
        response = "ERROR: Unknown command. Type help for available commands.\r\n";
    }

    if (client && client->connected()) {
        client->write(response.c_str());
    }
}

void setupTCPServer() {
    Serial.printf("Starting TCP server on port %d...\n", config.tcpPort);

    tcpServer = new AsyncServer(config.tcpPort);
    tcpServer->onClient(handleTCPClient, NULL);
    tcpServer->begin();

    Serial.println("TCP server started");
}

// ============================================
// Web Server
// ============================================

void setupWebServer() {
    Serial.println("Setting up Web Server...");

    // Serve static files from LittleFS
    webServer.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // API: Get status
    webServer.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", getStatusJSON());
    });

    // API: Set relay
    webServer.on("/api/relay", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("relay", true) && request->hasParam("state", true)) {
            int relay = request->getParam("relay", true)->value().toInt();
            String state = request->getParam("state", true)->value();
            String clientIP = request->client()->remoteIP().toString();

            if (relay >= 1 && relay <= 8) {
                // LED flash for command received
                ledCommandReceived();

                char logCmd[32];
                if (state == "on") {
                    setRelay(relay, true);
                    snprintf(logCmd, sizeof(logCmd), "r%d_on", relay);
                } else if (state == "off") {
                    setRelay(relay, false);
                    snprintf(logCmd, sizeof(logCmd), "r%d_off", relay);
                } else if (state == "pulse") {
                    unsigned long duration = config.pulseDuration;
                    if (request->hasParam("duration", true)) {
                        duration = request->getParam("duration", true)->value().toInt();
                    }
                    pulseRelay(relay, duration);
                    snprintf(logCmd, sizeof(logCmd), "r%d_pulse_%lu", relay, duration);
                } else {
                    logCmd[0] = '\0';
                }
                if (logCmd[0]) {
                    addLogEntry(clientIP.c_str(), logCmd);
                }
                request->send(200, "application/json", "{\"success\":true}");
            } else {
                request->send(400, "application/json", "{\"error\":\"Invalid relay number\"}");
            }
        } else {
            request->send(400, "application/json", "{\"error\":\"Missing parameters\"}");
        }
    });

    // API: Set all relays
    webServer.on("/api/relays", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("state", true)) {
            String state = request->getParam("state", true)->value();
            String clientIP = request->client()->remoteIP().toString();
            char logCmd[32];

            // LED flash for command received
            ledCommandReceived();

            if (state == "on") {
                setAllRelays(true);
                strlcpy(logCmd, "all_on", sizeof(logCmd));
            } else if (state == "off") {
                setAllRelays(false);
                strlcpy(logCmd, "all_off", sizeof(logCmd));
            } else if (state == "pulse") {
                unsigned long duration = config.pulseDuration;
                if (request->hasParam("duration", true)) {
                    duration = request->getParam("duration", true)->value().toInt();
                }
                pulseAllRelays(duration);
                snprintf(logCmd, sizeof(logCmd), "all_pulse_%lu", duration);
            } else {
                logCmd[0] = '\0';
            }
            if (logCmd[0]) {
                addLogEntry(clientIP.c_str(), logCmd);
            }
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(400, "application/json", "{\"error\":\"Missing state parameter\"}");
        }
    });

    // API: Get config
    webServer.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        // Ethernet
        doc["useDHCP"] = config.useDHCP;
        doc["staticIP"] = config.staticIP;
        doc["gateway"] = config.gateway;
        doc["subnet"] = config.subnet;
        doc["dns"] = config.dns;
        // WiFi
        doc["wifiEnabled"] = config.wifiEnabled;
        doc["wifiAPEnabled"] = config.wifiAPEnabled;
        doc["wifiSSID"] = config.wifiSSID;
        doc["wifiAPPassword"] = config.wifiAPPassword;
        doc["wifiDHCP"] = config.wifiDHCP;
        doc["wifiIP"] = config.wifiIP;
        doc["wifiGateway"] = config.wifiGateway;
        doc["wifiSubnet"] = config.wifiSubnet;
        doc["wifiDNS"] = config.wifiDNS;
        // General
        doc["hostname"] = config.hostname;
        doc["tcpPort"] = config.tcpPort;
        doc["pulseDuration"] = config.pulseDuration;
        // NTP
        doc["ntpEnabled"] = config.ntpEnabled;
        doc["ntpServer"] = config.ntpServer;
        doc["ntpTimezone"] = config.ntpTimezone;

        // LED
        doc["ledEnabled"] = config.ledEnabled;
        doc["ledBrightness"] = config.ledBrightness;

        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    });

    // API: Save config
    webServer.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool changed = false;

        // Ethernet
        if (request->hasParam("useDHCP", true)) {
            config.useDHCP = request->getParam("useDHCP", true)->value() == "true";
            changed = true;
        }
        if (request->hasParam("staticIP", true)) {
            strlcpy(config.staticIP, request->getParam("staticIP", true)->value().c_str(), sizeof(config.staticIP));
            changed = true;
        }
        if (request->hasParam("gateway", true)) {
            strlcpy(config.gateway, request->getParam("gateway", true)->value().c_str(), sizeof(config.gateway));
            changed = true;
        }
        if (request->hasParam("subnet", true)) {
            strlcpy(config.subnet, request->getParam("subnet", true)->value().c_str(), sizeof(config.subnet));
            changed = true;
        }
        if (request->hasParam("dns", true)) {
            strlcpy(config.dns, request->getParam("dns", true)->value().c_str(), sizeof(config.dns));
            changed = true;
        }
        // WiFi
        if (request->hasParam("wifiEnabled", true)) {
            config.wifiEnabled = request->getParam("wifiEnabled", true)->value() == "true";
            changed = true;
        }
        if (request->hasParam("wifiAPEnabled", true)) {
            config.wifiAPEnabled = request->getParam("wifiAPEnabled", true)->value() == "true";
            changed = true;
        }
        if (request->hasParam("wifiSSID", true)) {
            strlcpy(config.wifiSSID, request->getParam("wifiSSID", true)->value().c_str(), sizeof(config.wifiSSID));
            changed = true;
        }
        if (request->hasParam("wifiPassword", true)) {
            strlcpy(config.wifiPassword, request->getParam("wifiPassword", true)->value().c_str(), sizeof(config.wifiPassword));
            changed = true;
        }
        if (request->hasParam("wifiAPPassword", true)) {
            strlcpy(config.wifiAPPassword, request->getParam("wifiAPPassword", true)->value().c_str(), sizeof(config.wifiAPPassword));
            changed = true;
        }
        if (request->hasParam("wifiDHCP", true)) {
            config.wifiDHCP = request->getParam("wifiDHCP", true)->value() == "true";
            changed = true;
        }
        if (request->hasParam("wifiIP", true)) {
            strlcpy(config.wifiIP, request->getParam("wifiIP", true)->value().c_str(), sizeof(config.wifiIP));
            changed = true;
        }
        if (request->hasParam("wifiGateway", true)) {
            strlcpy(config.wifiGateway, request->getParam("wifiGateway", true)->value().c_str(), sizeof(config.wifiGateway));
            changed = true;
        }
        if (request->hasParam("wifiSubnet", true)) {
            strlcpy(config.wifiSubnet, request->getParam("wifiSubnet", true)->value().c_str(), sizeof(config.wifiSubnet));
            changed = true;
        }
        if (request->hasParam("wifiDNS", true)) {
            strlcpy(config.wifiDNS, request->getParam("wifiDNS", true)->value().c_str(), sizeof(config.wifiDNS));
            changed = true;
        }
        // General
        if (request->hasParam("hostname", true)) {
            strlcpy(config.hostname, request->getParam("hostname", true)->value().c_str(), sizeof(config.hostname));
            changed = true;
        }
        if (request->hasParam("tcpPort", true)) {
            config.tcpPort = request->getParam("tcpPort", true)->value().toInt();
            changed = true;
        }
        if (request->hasParam("pulseDuration", true)) {
            config.pulseDuration = request->getParam("pulseDuration", true)->value().toInt();
            changed = true;
        }
        // NTP
        if (request->hasParam("ntpEnabled", true)) {
            config.ntpEnabled = request->getParam("ntpEnabled", true)->value() == "true";
            changed = true;
        }
        if (request->hasParam("ntpServer", true)) {
            strlcpy(config.ntpServer, request->getParam("ntpServer", true)->value().c_str(), sizeof(config.ntpServer));
            changed = true;
        }
        if (request->hasParam("ntpTimezone", true)) {
            strlcpy(config.ntpTimezone, request->getParam("ntpTimezone", true)->value().c_str(), sizeof(config.ntpTimezone));
            changed = true;
        }
        // LED
        if (request->hasParam("ledEnabled", true)) {
            config.ledEnabled = request->getParam("ledEnabled", true)->value() == "true";
            changed = true;
        }
        if (request->hasParam("ledBrightness", true)) {
            config.ledBrightness = request->getParam("ledBrightness", true)->value().toInt();
            changed = true;
        }

        if (changed) {
            saveConfig();
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Configuration saved. Restart required for network changes.\"}");
        } else {
            request->send(400, "application/json", "{\"error\":\"No parameters provided\"}");
        }
    });

    // API: Restart
    webServer.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Restarting...\"}");
        delay(500);
        ESP.restart();
    });

    // API: Get command log
    webServer.on("/api/log", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", getLogJSON());
    });

    // Setup ElegantOTA with LED callbacks
    ElegantOTA.begin(&webServer);
    ElegantOTA.onStart([]() {
        Serial.println("OTA Update started");
        ledOtaActive = true;
        ledStateStartTime = millis();
    });
    ElegantOTA.onEnd([](bool success) {
        Serial.printf("OTA Update %s\n", success ? "successful" : "failed");
        ledOtaActive = false;
        updateLedBackgroundState();
    });

    // Handle 404
    webServer.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not Found");
    });

    webServer.begin();
    Serial.println("Web server started on port 80");
}
