/*
 * CineRelais Controller - Full Version
 * Ethernet, WiFi AP/STA, Web Interface, TCP Server, Relay Control
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <ETH.h>
#include <WiFi.h>

// Ethernet enabled - requires Arduino Core 3.x (pioarduino platform)
// #define ETHERNET_DISABLED 1
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <Adafruit_NeoPixel.h>
#include <ModbusMaster.h>
#include <vector>
#include <esp_netif.h>
#include "driver/uart.h"
#include "config.h"

// RS485 Modbus baud rate (pin defines are in config.h)
#define RS485_BAUD   9600

// ============================================
// TCP Device Presets (for Input→TCP Actions)
// ============================================

// Command format: ASCII string or HEX bytes (prefixed with "HEX:")
struct DeviceCommand {
    const char* name;
    const char* command;  // ASCII text or "HEX:060e2b34..." for binary
};

struct DevicePreset {
    const char* name;
    uint16_t port;
    const DeviceCommand* commands;
    uint8_t commandCount;
};

// Dolby IMS3000 Commands (Port 11730, binary SMPTE protocol)
const DeviceCommand IMS3000_COMMANDS[] = {
    {"PlaySPL",      "HEX:060e2b340205010a0e10010101030b008300000401020304"},
    {"PauseSPL",     "HEX:060e2b340205010a0e10010101030d008300000401020304"},
    {"EjectSPL",     "HEX:060e2b340205010a0e10010101030f008300000401020304"},
    {"SkipForward",  "HEX:060e2b340205010a0e100101010311008300000401020304"},
    {"SkipBackward", "HEX:060e2b340205010a0e100101010313008300000401020304"},
    {"JumpForward",  "HEX:060e2b340205010a0e100101010315008300000401020304"},
    {"JumpBackward", "HEX:060e2b340205010a0e100101010317008300000401020304"}
};

// AP20 Audio Processor Commands (Port 14500, ASCII protocol)
const DeviceCommand AP20_COMMANDS[] = {
    {"Mute",       "@MUTED 1\r"},
    {"Unmute",     "@MUTED 0\r"},
    {"Volume +1",  "@VOLUME +1\r"},
    {"Volume -1",  "@VOLUME -1\r"},
    {"Volume +5",  "@VOLUME +5\r"},
    {"Volume -5",  "@VOLUME -5\r"}
};

// Device Presets Array (add new devices here)
const DevicePreset DEVICE_PRESETS[] = {
    {"Dolby IMS3000", 11730, IMS3000_COMMANDS, sizeof(IMS3000_COMMANDS) / sizeof(DeviceCommand)},
    {"AP20",          14500, AP20_COMMANDS,    sizeof(AP20_COMMANDS) / sizeof(DeviceCommand)}
};
const uint8_t DEVICE_PRESET_COUNT = sizeof(DEVICE_PRESETS) / sizeof(DevicePreset);

// ============================================
// Configuration
// ============================================
struct Config {
    char hostname[32];
    uint16_t tcpPort;
    uint16_t pulseDuration;

    // Ethernet
    bool ethEnabled;
    bool ethDHCP;
    char ethIP[16];
    char ethGateway[16];
    char ethSubnet[16];
    char ethDNS[16];

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

    // LED
    bool ledEnabled;
    uint8_t ledBrightness;

    // NTP
    bool ntpEnabled;
    char ntpServer[64];
    char ntpTimezone[48];

    // Labels (max 12 chars each)
    char relayLabels[8][16];
    char inputLabels[8][16];

    // Modbus RS485
    bool modbusEnabled;
    uint8_t modbusAddress;      // 1-247
    uint8_t modbusRelayCount;   // 6 or 8
    char modbusLabels[8][16];   // Labels for Modbus relays

    // Input-to-Relay Mapping (Bitmask: Bit n = Relay n+1)
    uint8_t inputRelayMap[8];   // Which relays to activate for each input

    // TCP Command per Input
    // Mode: 0=disabled, 1=manual (IP/Port/Command), 2=preset (Device/Function)
    uint8_t inputTcpMode[8];
    char inputTcpHost[8][16];      // IP address (used in both modes)
    uint16_t inputTcpPort[8];      // Port (manual mode only)
    char inputTcpCommand[8][64];   // Command string (manual mode, larger for HEX)
    uint8_t inputTcpDevice[8];     // Device preset index (preset mode)
    uint8_t inputTcpFunction[8];   // Function index within device (preset mode)
};

Config config = {
    "CineRelais",   // hostname
    5000,           // tcpPort
    500,            // pulseDuration
    // Ethernet
    true,           // ethEnabled
    true,           // ethDHCP
    "192.168.1.100",// ethIP
    "192.168.1.1",  // ethGateway
    "255.255.255.0",// ethSubnet
    "8.8.8.8",      // ethDNS
    // WiFi
    true,           // wifiEnabled
    true,           // wifiAPEnabled
    "",             // wifiSSID
    "",             // wifiPassword
    "",             // wifiAPPassword (empty = open)
    true,           // wifiDHCP
    "192.168.4.100",// wifiIP
    "192.168.4.1",  // wifiGateway
    "255.255.255.0",// wifiSubnet
    "8.8.8.8",      // wifiDNS
    // LED
    true,           // ledEnabled
    51,             // ledBrightness (20%)
    // NTP
    true,           // ntpEnabled
    "pool.ntp.org", // ntpServer
    "CET-1CEST,M3.5.0,M10.5.0/3",  // ntpTimezone (Europe/Berlin)
    // Labels
    {"", "", "", "", "", "", "", ""},  // relayLabels
    {"", "", "", "", "", "", "", ""},  // inputLabels
    // Modbus
    false,          // modbusEnabled
    1,              // modbusAddress (default 0x01)
    8,              // modbusRelayCount
    {"", "", "", "", "", "", "", ""},  // modbusLabels
    // Input-to-Relay Mapping
    {0, 0, 0, 0, 0, 0, 0, 0},          // inputRelayMap (all disabled)
    // TCP per Input
    {0, 0, 0, 0, 0, 0, 0, 0},          // inputTcpMode (0=disabled)
    {"", "", "", "", "", "", "", ""},  // inputTcpHost
    {0, 0, 0, 0, 0, 0, 0, 0},          // inputTcpPort
    {"", "", "", "", "", "", "", ""},  // inputTcpCommand
    {0, 0, 0, 0, 0, 0, 0, 0},          // inputTcpDevice
    {0, 0, 0, 0, 0, 0, 0, 0}           // inputTcpFunction
};

// ============================================
// Global State
// ============================================
AsyncWebServer* webServer = nullptr;
AsyncServer* tcpServer = nullptr;
Adafruit_NeoPixel* rgbLed = nullptr;
std::vector<AsyncClient*> tcpClients;
AsyncWebSocket* ws = nullptr;
bool wsBroadcastNeeded = false;  // Thread-safe flag for WebSocket broadcast in loop()

bool tca9554Found = false;
bool relayStates[8] = {false};
uint8_t relayRegister = 0x00;
bool inputStates[8] = {false};

// Pulse timing
unsigned long pulseEndTime[8] = {0};
bool pulseActive[8] = {false};

// Network state
bool ethConnected = false;
bool wifiSTAConnected = false;
bool wifiAPActive = false;
unsigned long lastWiFiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 30000;

// LED state
unsigned long ledEventEndTime = 0;
bool ledEventActive = false;
volatile bool ledUpdateNeeded = false;  // Flag for thread-safe LED updates

// Modbus RS485
ModbusMaster modbusNode;
bool modbusInitialized = false;  // Set true only after setupModbus() completes
bool modbusConnected = false;
bool modbusRelayStates[8] = {false};
unsigned long modbusLastPoll = 0;
const unsigned long MODBUS_POLL_INTERVAL = 60000;  // Heartbeat every 60s
bool modbusAllPulseActive = false;
unsigned long modbusAllPulseEndTime = 0;

// NTP
bool ntpSynced = false;

// Command Log (circular buffer, newest first)
struct LogEntry {
    char timestamp[20];   // "DD.MM.YYYY HH:MM:SS" or uptime
    char source[32];      // IP address or "Modbus"
    char command[64];     // Command text
    char direction[4];    // "IN" or "OUT"
};
const int MAX_LOG_ENTRIES = 50;
LogEntry commandLog[MAX_LOG_ENTRIES];
int logIndex = 0;
int logCount = 0;

void addLogEntry(const char* source, const char* command, const char* direction) {
    LogEntry& entry = commandLog[logIndex];

    // Timestamp
    if (ntpSynced) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0)) {
            strftime(entry.timestamp, sizeof(entry.timestamp), "%d.%m.%Y %H:%M:%S", &timeinfo);
        } else {
            snprintf(entry.timestamp, sizeof(entry.timestamp), "Uptime: %lus", millis() / 1000);
        }
    } else {
        snprintf(entry.timestamp, sizeof(entry.timestamp), "Uptime: %lus", millis() / 1000);
    }

    strlcpy(entry.source, source, sizeof(entry.source));
    strlcpy(entry.command, command, sizeof(entry.command));
    strlcpy(entry.direction, direction, sizeof(entry.direction));

    logIndex = (logIndex + 1) % MAX_LOG_ENTRIES;
    if (logCount < MAX_LOG_ENTRIES) logCount++;
}

// Input-to-Relay Mapping
uint8_t inputControlledRelays = 0;  // Bitmask of relays controlled by inputs
bool inputDebouncedStates[8] = {false};  // Debounced input states
unsigned long inputDebounceTime[8] = {0};  // Timestamp when input changed
const unsigned long INPUT_DEBOUNCE_MS = 50;  // 50ms debounce delay

// Input TCP command state (track if command was sent for current input state)
bool inputTcpSent[8] = {false};  // True if TCP command was sent for active input

// ============================================
// Forward Declarations
// ============================================
void loadConfig();
void saveConfig();
void setupEthernet();
#ifndef ETHERNET_DISABLED
void onEthEvent(arduino_event_id_t event, arduino_event_info_t info);
#endif
void tca9554Init();
void setRelay(int relay, bool state);
void setAllRelays(bool state);
void pulseRelay(int relay, uint16_t duration);
void pulseAllRelays(uint16_t duration);
void updatePulses();
void setupTcpServer();
void setupDigitalInputs();
void readDigitalInputs();
void setupWiFi();
void checkWiFiConnection();
String processCommand(const String& cmd);
String getStatusJSON();
String getConfigJSON();
void setStatusLED();
void flashEventLED();
void flashInputTcpLED();
void setupModbus();
bool modbusSetRelay(int relay, bool state);
bool modbusSetAllRelays(bool state);
bool modbusFlashNative(int relay);
void modbusPulseRelay(int relay, uint16_t duration);
void modbusPulseAllRelays(uint16_t duration);
void updateModbusPulses();
void modbusReadRelays();
int modbusScanAddress();
void setupNTP();
void processInputMappings();
void sendInputTcpCommand(int inputIndex);
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len);
void wsBroadcast();
void wsNotify();

// ============================================
// Setup
// ============================================
void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n\n========================================");
    Serial.println("CineRelais Controller");
    Serial.println("========================================\n");

    // LittleFS
    Serial.println("Mounting LittleFS...");
    if (!LittleFS.begin(true)) {
        Serial.println("ERROR: LittleFS mount failed!");
    } else {
        Serial.println("LittleFS OK");
        loadConfig();
    }

    // I2C + TCA9554
    Serial.println("Initializing I2C...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    tca9554Init();

    // Digital Inputs
    Serial.println("Setting up Digital Inputs...");
    setupDigitalInputs();

    // NeoPixel
    Serial.println("Initializing NeoPixel...");
    rgbLed = new Adafruit_NeoPixel(1, RGB_LED_PIN, NEO_RGB + NEO_KHZ800);
    rgbLed->begin();
    rgbLed->setPixelColor(0, rgbLed->Color(0, 0, 50));  // Dim blue during startup
    rgbLed->show();

    // Ethernet
    setupEthernet();

    // WiFi
    setupWiFi();

    // NTP Time Sync
    setupNTP();

    // Modbus RS485
    setupModbus();

    // Web Server
    Serial.println("Starting Web Server...");
    webServer = new AsyncWebServer(80);

    // WebSocket
    ws = new AsyncWebSocket("/ws");
    ws->onEvent(onWsEvent);
    webServer->addHandler(ws);
    Serial.println("WebSocket handler registered on /ws");

    // Serve index.html
    webServer->on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/index.html", "text/html");
    });

    // API: Status
    webServer->on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", getStatusJSON());
    });

    // API: Config GET
    webServer->on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", getConfigJSON());
    });

    // API: Config POST
    webServer->on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request){
        bool changed = false;

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

        // Ethernet config - handle checkbox booleans properly
        // HTML checkboxes send "true"/"on" when checked, nothing when unchecked
        // So we check for explicit "true"/"false" or presence of related fields
        if (request->hasParam("useDHCP", true)) {
            String val = request->getParam("useDHCP", true)->value();
            config.ethDHCP = (val == "true" || val == "on" || val == "1");
            Serial.printf("Config POST: useDHCP='%s' -> ethDHCP=%d\n", val.c_str(), config.ethDHCP);
            changed = true;
        } else if (request->hasParam("staticIP", true)) {
            // If staticIP is being set but useDHCP not sent, assume DHCP disabled
            config.ethDHCP = false;
            Serial.println("Config POST: no useDHCP param, staticIP present -> ethDHCP=0");
            changed = true;
        }
        if (request->hasParam("staticIP", true)) {
            strlcpy(config.ethIP, request->getParam("staticIP", true)->value().c_str(), sizeof(config.ethIP));
            changed = true;
        }
        if (request->hasParam("gateway", true)) {
            strlcpy(config.ethGateway, request->getParam("gateway", true)->value().c_str(), sizeof(config.ethGateway));
            changed = true;
        }
        if (request->hasParam("subnet", true)) {
            strlcpy(config.ethSubnet, request->getParam("subnet", true)->value().c_str(), sizeof(config.ethSubnet));
            changed = true;
        }
        if (request->hasParam("dns", true)) {
            strlcpy(config.ethDNS, request->getParam("dns", true)->value().c_str(), sizeof(config.ethDNS));
            changed = true;
        }

        // WiFi config - same checkbox handling
        if (request->hasParam("wifiEnabled", true)) {
            String val = request->getParam("wifiEnabled", true)->value();
            config.wifiEnabled = (val == "true" || val == "on" || val == "1");
            changed = true;
        }
        if (request->hasParam("wifiAPEnabled", true)) {
            String val = request->getParam("wifiAPEnabled", true)->value();
            config.wifiAPEnabled = (val == "true" || val == "on" || val == "1");
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
            String val = request->getParam("wifiDHCP", true)->value();
            config.wifiDHCP = (val == "true" || val == "on" || val == "1");
            changed = true;
        } else if (request->hasParam("wifiIP", true)) {
            // If wifiIP is being set but wifiDHCP not sent, assume DHCP disabled
            config.wifiDHCP = false;
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

        // LED settings
        if (request->hasParam("ledEnabled", true)) {
            String val = request->getParam("ledEnabled", true)->value();
            config.ledEnabled = (val == "true" || val == "on" || val == "1");
            changed = true;
        }
        if (request->hasParam("ledBrightness", true)) {
            config.ledBrightness = request->getParam("ledBrightness", true)->value().toInt();
            changed = true;
        }

        // NTP settings
        if (request->hasParam("ntpEnabled", true)) {
            String val = request->getParam("ntpEnabled", true)->value();
            config.ntpEnabled = (val == "true" || val == "on" || val == "1");
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

        // Labels (sent as JSON arrays)
        if (request->hasParam("relayLabels", true)) {
            String labelsJson = request->getParam("relayLabels", true)->value();
            JsonDocument labelsDoc;
            if (deserializeJson(labelsDoc, labelsJson) == DeserializationError::Ok) {
                JsonArray arr = labelsDoc.as<JsonArray>();
                for (int i = 0; i < 8 && i < arr.size(); i++) {
                    strlcpy(config.relayLabels[i], arr[i] | "", sizeof(config.relayLabels[i]));
                }
                changed = true;
            }
        }
        if (request->hasParam("inputLabels", true)) {
            String labelsJson = request->getParam("inputLabels", true)->value();
            JsonDocument labelsDoc;
            if (deserializeJson(labelsDoc, labelsJson) == DeserializationError::Ok) {
                JsonArray arr = labelsDoc.as<JsonArray>();
                for (int i = 0; i < 8 && i < arr.size(); i++) {
                    strlcpy(config.inputLabels[i], arr[i] | "", sizeof(config.inputLabels[i]));
                }
                changed = true;
            }
        }

        // Modbus settings
        if (request->hasParam("modbusEnabled", true)) {
            String val = request->getParam("modbusEnabled", true)->value();
            config.modbusEnabled = (val == "true" || val == "on" || val == "1");
            changed = true;
        }
        if (request->hasParam("modbusAddress", true)) {
            config.modbusAddress = request->getParam("modbusAddress", true)->value().toInt();
            if (config.modbusAddress < 1) config.modbusAddress = 1;
            if (config.modbusAddress > 247) config.modbusAddress = 247;
            changed = true;
        }
        if (request->hasParam("modbusRelayCount", true)) {
            config.modbusRelayCount = request->getParam("modbusRelayCount", true)->value().toInt();
            if (config.modbusRelayCount < 1) config.modbusRelayCount = 1;
            if (config.modbusRelayCount > 8) config.modbusRelayCount = 8;
            changed = true;
        }
        if (request->hasParam("modbusLabels", true)) {
            String labelsJson = request->getParam("modbusLabels", true)->value();
            JsonDocument labelsDoc;
            if (deserializeJson(labelsDoc, labelsJson) == DeserializationError::Ok) {
                JsonArray arr = labelsDoc.as<JsonArray>();
                for (int i = 0; i < 8 && i < arr.size(); i++) {
                    strlcpy(config.modbusLabels[i], arr[i] | "", sizeof(config.modbusLabels[i]));
                }
                changed = true;
            }
        }

        // Input-to-Relay Mapping
        if (request->hasParam("inputRelayMap", true)) {
            String mapJson = request->getParam("inputRelayMap", true)->value();
            JsonDocument mapDoc;
            if (deserializeJson(mapDoc, mapJson) == DeserializationError::Ok) {
                JsonArray arr = mapDoc.as<JsonArray>();
                for (int i = 0; i < 8 && i < arr.size(); i++) {
                    config.inputRelayMap[i] = arr[i] | 0;
                }
                changed = true;
            }
        }

        // TCP per Input
        if (request->hasParam("inputTcpMode", true)) {
            String tcpJson = request->getParam("inputTcpMode", true)->value();
            JsonDocument tcpDoc;
            if (deserializeJson(tcpDoc, tcpJson) == DeserializationError::Ok) {
                JsonArray arr = tcpDoc.as<JsonArray>();
                for (int i = 0; i < 8 && i < arr.size(); i++) {
                    config.inputTcpMode[i] = arr[i] | 0;
                }
                changed = true;
            }
        }
        if (request->hasParam("inputTcpHost", true)) {
            String tcpJson = request->getParam("inputTcpHost", true)->value();
            JsonDocument tcpDoc;
            if (deserializeJson(tcpDoc, tcpJson) == DeserializationError::Ok) {
                JsonArray arr = tcpDoc.as<JsonArray>();
                for (int i = 0; i < 8 && i < arr.size(); i++) {
                    strlcpy(config.inputTcpHost[i], arr[i] | "", sizeof(config.inputTcpHost[i]));
                }
                changed = true;
            }
        }
        if (request->hasParam("inputTcpPort", true)) {
            String tcpJson = request->getParam("inputTcpPort", true)->value();
            JsonDocument tcpDoc;
            if (deserializeJson(tcpDoc, tcpJson) == DeserializationError::Ok) {
                JsonArray arr = tcpDoc.as<JsonArray>();
                for (int i = 0; i < 8 && i < arr.size(); i++) {
                    config.inputTcpPort[i] = arr[i] | 0;
                }
                changed = true;
            }
        }
        if (request->hasParam("inputTcpCommand", true)) {
            String tcpJson = request->getParam("inputTcpCommand", true)->value();
            JsonDocument tcpDoc;
            if (deserializeJson(tcpDoc, tcpJson) == DeserializationError::Ok) {
                JsonArray arr = tcpDoc.as<JsonArray>();
                for (int i = 0; i < 8 && i < arr.size(); i++) {
                    strlcpy(config.inputTcpCommand[i], arr[i] | "", sizeof(config.inputTcpCommand[i]));
                }
                changed = true;
            }
        }
        if (request->hasParam("inputTcpDevice", true)) {
            String tcpJson = request->getParam("inputTcpDevice", true)->value();
            JsonDocument tcpDoc;
            if (deserializeJson(tcpDoc, tcpJson) == DeserializationError::Ok) {
                JsonArray arr = tcpDoc.as<JsonArray>();
                for (int i = 0; i < 8 && i < arr.size(); i++) {
                    config.inputTcpDevice[i] = arr[i] | 0;
                }
                changed = true;
            }
        }
        if (request->hasParam("inputTcpFunction", true)) {
            String tcpJson = request->getParam("inputTcpFunction", true)->value();
            JsonDocument tcpDoc;
            if (deserializeJson(tcpDoc, tcpJson) == DeserializationError::Ok) {
                JsonArray arr = tcpDoc.as<JsonArray>();
                for (int i = 0; i < 8 && i < arr.size(); i++) {
                    config.inputTcpFunction[i] = arr[i] | 0;
                }
                changed = true;
            }
        }

        if (changed) {
            saveConfig();
        }

        JsonDocument doc;
        doc["success"] = true;
        doc["message"] = "Konfiguration gespeichert. Neustart fuer Aenderungen.";
        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    });

    // API: Single Relay
    webServer->on("/api/relay", HTTP_POST, [](AsyncWebServerRequest *request){
        if (!request->hasParam("relay", true) || !request->hasParam("state", true)) {
            request->send(400, "text/plain", "Missing relay or state parameter");
            return;
        }

        int relay = request->getParam("relay", true)->value().toInt();
        String state = request->getParam("state", true)->value();

        if (relay < 1 || relay > 8) {
            request->send(400, "text/plain", "Relay must be 1-8");
            return;
        }

        // Log Web API command
        char logCmd[32];
        if (state == "pulse") {
            uint16_t duration = config.pulseDuration;
            if (request->hasParam("duration", true)) {
                duration = request->getParam("duration", true)->value().toInt();
            }
            snprintf(logCmd, sizeof(logCmd), "r%d_pulse_%d", relay, duration);
        } else {
            snprintf(logCmd, sizeof(logCmd), "r%d_%s", relay, state.c_str());
        }
        addLogEntry(request->client()->remoteIP().toString().c_str(), logCmd, "IN");

        if (state == "on") {
            setRelay(relay, true);
        } else if (state == "off") {
            setRelay(relay, false);
        } else if (state == "pulse") {
            uint16_t duration = config.pulseDuration;
            if (request->hasParam("duration", true)) {
                duration = request->getParam("duration", true)->value().toInt();
            }
            pulseRelay(relay, duration);
        }

        flashEventLED();
        request->send(200, "text/plain", "OK");
    });

    // API: All Relays
    webServer->on("/api/relays", HTTP_POST, [](AsyncWebServerRequest *request){
        if (!request->hasParam("state", true)) {
            request->send(400, "text/plain", "Missing state parameter");
            return;
        }

        String state = request->getParam("state", true)->value();

        // Log Web API command
        char logCmd[32];
        if (state == "pulse") {
            uint16_t duration = config.pulseDuration;
            if (request->hasParam("duration", true)) {
                duration = request->getParam("duration", true)->value().toInt();
            }
            snprintf(logCmd, sizeof(logCmd), "all_pulse_%d", duration);
        } else {
            snprintf(logCmd, sizeof(logCmd), "all_%s", state.c_str());
        }
        addLogEntry(request->client()->remoteIP().toString().c_str(), logCmd, "IN");

        if (state == "on") {
            setAllRelays(true);
        } else if (state == "off") {
            setAllRelays(false);
        } else if (state == "pulse") {
            uint16_t duration = config.pulseDuration;
            if (request->hasParam("duration", true)) {
                duration = request->getParam("duration", true)->value().toInt();
            }
            pulseAllRelays(duration);
        }

        flashEventLED();
        request->send(200, "text/plain", "OK");
    });

    // API: Single Modbus Relay
    webServer->on("/api/modbus/relay", HTTP_POST, [](AsyncWebServerRequest *request){
        Serial.println("API: /api/modbus/relay called");

        if (!config.modbusEnabled) {
            Serial.println("API: Modbus disabled");
            request->send(400, "application/json", "{\"error\":\"Modbus disabled\"}");
            return;
        }

        if (!modbusInitialized) {
            Serial.println("API: Modbus not initialized");
            request->send(400, "application/json", "{\"error\":\"Modbus not initialized - restart required\"}");
            return;
        }

        if (!request->hasParam("relay", true) || !request->hasParam("state", true)) {
            request->send(400, "application/json", "{\"error\":\"Missing relay or state\"}");
            return;
        }

        int relay = request->getParam("relay", true)->value().toInt();
        String state = request->getParam("state", true)->value();
        Serial.printf("API: Modbus relay %d state=%s\n", relay, state.c_str());

        if (relay < 1 || relay > config.modbusRelayCount) {
            Serial.printf("API: Invalid relay %d (max %d)\n", relay, config.modbusRelayCount);
            request->send(400, "application/json", "{\"error\":\"Invalid relay number\"}");
            return;
        }

        bool success = false;
        if (state == "on") {
            success = modbusSetRelay(relay, true);
        } else if (state == "off") {
            success = modbusSetRelay(relay, false);
        } else if (state == "pulse") {
            uint16_t duration = config.pulseDuration;
            if (request->hasParam("duration", true)) {
                duration = request->getParam("duration", true)->value().toInt();
            }
            modbusPulseRelay(relay, duration);
            success = true;
        }

        if (success) {
            flashEventLED();
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            Serial.println("API: Modbus communication failed");
            request->send(500, "application/json", "{\"error\":\"Modbus communication failed\"}");
        }
    });

    // API: All Modbus Relays
    webServer->on("/api/modbus/relays", HTTP_POST, [](AsyncWebServerRequest *request){
        Serial.println("API: /api/modbus/relays called");

        if (!config.modbusEnabled) {
            Serial.println("API: Modbus disabled (all)");
            request->send(400, "application/json", "{\"error\":\"Modbus disabled\"}");
            return;
        }

        if (!modbusInitialized) {
            Serial.println("API: Modbus not initialized (all)");
            request->send(400, "application/json", "{\"error\":\"Modbus not initialized - restart required\"}");
            return;
        }

        if (!request->hasParam("state", true)) {
            request->send(400, "application/json", "{\"error\":\"Missing state\"}");
            return;
        }

        String state = request->getParam("state", true)->value();
        Serial.printf("API: Modbus all relays state=%s\n", state.c_str());
        bool success = false;

        if (state == "on") {
            success = modbusSetAllRelays(true);
        } else if (state == "off") {
            success = modbusSetAllRelays(false);
        } else if (state == "pulse") {
            uint16_t duration = config.pulseDuration;
            if (request->hasParam("duration", true)) {
                duration = request->getParam("duration", true)->value().toInt();
            }
            modbusPulseAllRelays(duration);
            success = true;
        }

        if (success) {
            flashEventLED();
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            Serial.println("API: Modbus all relays failed");
            request->send(500, "application/json", "{\"error\":\"Modbus communication failed\"}");
        }
    });

    // API: Modbus Scan
    webServer->on("/api/modbus/scan", HTTP_POST, [](AsyncWebServerRequest *request){
        int addr = modbusScanAddress();
        JsonDocument doc;
        if (addr > 0) {
            doc["success"] = true;
            doc["address"] = addr;
        } else {
            doc["success"] = false;
            doc["error"] = "No device found";
        }
        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    });

    // API: Restart
    webServer->on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "Restarting...");
        delay(500);
        ESP.restart();
    });

    // API: Command Log (newest first)
    webServer->on("/api/log", HTTP_GET, [](AsyncWebServerRequest *request){
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        // Return entries newest first
        for (int i = 0; i < logCount; i++) {
            int idx = (logIndex - 1 - i + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;
            JsonObject entry = arr.add<JsonObject>();
            entry["time"] = commandLog[idx].timestamp;
            entry["source"] = commandLog[idx].source;
            entry["command"] = commandLog[idx].command;
            entry["direction"] = commandLog[idx].direction;
        }

        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    });

    // Serve static files
    webServer->serveStatic("/", LittleFS, "/");

    ElegantOTA.begin(webServer);
    webServer->begin();
    Serial.println("Web Server OK");

    // TCP Server
    Serial.println("Starting TCP Server...");
    setupTcpServer();

    // Set LED based on connection status (20% brightness)
    setStatusLED();

    Serial.println("\n========================================");
    Serial.println("Setup complete!");
    Serial.printf("Hostname: %s\n", config.hostname);
#ifndef ETHERNET_DISABLED
    if (ethConnected) {
        Serial.printf("Ethernet: %s\n", ETH.localIP().toString().c_str());
    }
#endif
    if (wifiAPActive) {
        Serial.printf("WiFi AP: %s @ %s\n", config.hostname, WiFi.softAPIP().toString().c_str());
    }
    if (wifiSTAConnected) {
        Serial.printf("WiFi STA: %s\n", WiFi.localIP().toString().c_str());
    }
    Serial.printf("TCP Port: %d\n", config.tcpPort);
    Serial.println("========================================\n");
}

// ============================================
// Loop
// ============================================
void loop() {
    ElegantOTA.loop();
    updatePulses();
    updateModbusPulses();
    modbusReadRelays();
    processInputMappings();  // Handle input-to-relay mappings
    checkWiFiConnection();

    // Check NTP sync status periodically
    static unsigned long lastNtpCheck = 0;
    if (config.ntpEnabled && millis() - lastNtpCheck > 60000) {  // Every minute
        lastNtpCheck = millis();
        struct tm timeinfo;
        ntpSynced = getLocalTime(&timeinfo, 0);
    }

    // Handle LED updates (thread-safe: flag set by event handlers)
    if (ledUpdateNeeded) {
        ledUpdateNeeded = false;
        setStatusLED();
    }

    // Handle LED event timeout
    if (ledEventActive && millis() >= ledEventEndTime) {
        ledEventActive = false;
        setStatusLED();
    }

    // WebSocket: broadcast status to all connected clients when state changed
    if (wsBroadcastNeeded) {
        wsBroadcastNeeded = false;
        wsBroadcast();
    }

    delay(10);
}

// ============================================
// Ethernet Setup (W5500)
// ============================================
#ifndef ETHERNET_DISABLED
void onEthEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            Serial.println("ETH: Started");
            ETH.setHostname(config.hostname);
            break;
        case ARDUINO_EVENT_ETH_CONNECTED:
            Serial.println("ETH: Link Up");
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            ethConnected = true;
            Serial.printf("ETH: Got IP %s\n", ETH.localIP().toString().c_str());
            ledUpdateNeeded = true;  // Thread-safe: set flag, update in loop()
            wsBroadcastNeeded = true;
            break;
        case ARDUINO_EVENT_ETH_LOST_IP:
            ethConnected = false;
            Serial.println("ETH: Lost IP");
            ledUpdateNeeded = true;
            wsBroadcastNeeded = true;
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            ethConnected = false;
            Serial.println("ETH: Link Down");
            ledUpdateNeeded = true;
            wsBroadcastNeeded = true;
            break;
        case ARDUINO_EVENT_ETH_STOP:
            ethConnected = false;
            Serial.println("ETH: Stopped");
            ledUpdateNeeded = true;
            wsBroadcastNeeded = true;
            break;
        default:
            break;
    }
}
#endif

void setupEthernet() {
#ifdef ETHERNET_DISABLED
    Serial.println("Ethernet support not available (compile-time disabled)");
    ethConnected = false;
#else
    if (!config.ethEnabled) {
        Serial.println("Ethernet disabled");
        return;
    }

    Serial.println("Initializing Ethernet (W5500)...");

    // Register event handler
    Network.onEvent(onEthEvent);

    // Initialize W5500 first
    SPI.begin(ETH_SCLK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN, ETH_CS_PIN);

    if (!ETH.begin(ETH_PHY_W5500, 1, ETH_CS_PIN, ETH_INT_PIN, ETH_RST_PIN, SPI)) {
        Serial.println("ETH: Failed to initialize W5500");
        return;
    }

    // Configure static IP AFTER ETH.begin() if not DHCP
    if (!config.ethDHCP) {
        IPAddress ip, gateway, subnet, dns;
        ip.fromString(config.ethIP);
        gateway.fromString(config.ethGateway);
        subnet.fromString(config.ethSubnet);
        dns.fromString(config.ethDNS);

        Serial.printf("ETH: Using static IP %s\n", config.ethIP);
        ETH.config(ip, gateway, subnet, dns);
    } else {
        Serial.println("ETH: Using DHCP");
    }

    Serial.println("ETH: W5500 initialized, waiting for link...");

    // Wait a bit for link
    delay(1000);
#endif
}

// ============================================
// WiFi Setup
// ============================================
void setupWiFi() {
    if (!config.wifiEnabled) {
        Serial.println("WiFi disabled");
        WiFi.mode(WIFI_OFF);
        return;
    }

    bool hasSSID = strlen(config.wifiSSID) > 0;

    // Determine WiFi mode
    if (hasSSID && config.wifiAPEnabled) {
        WiFi.mode(WIFI_AP_STA);
        Serial.println("WiFi Mode: AP + STA");
    } else if (hasSSID) {
        WiFi.mode(WIFI_STA);
        Serial.println("WiFi Mode: STA only");
    } else {
        WiFi.mode(WIFI_AP);
        Serial.println("WiFi Mode: AP only");
    }

    // Start AP if enabled
    if (config.wifiAPEnabled) {
        if (strlen(config.wifiAPPassword) >= 8) {
            WiFi.softAP(config.hostname, config.wifiAPPassword);
        } else {
            WiFi.softAP(config.hostname);  // Open AP
        }
        wifiAPActive = true;
        Serial.printf("WiFi AP started: %s @ %s\n", config.hostname, WiFi.softAPIP().toString().c_str());
    }

    // Connect to STA if SSID configured
    if (hasSSID) {
        Serial.printf("Connecting to WiFi: %s\n", config.wifiSSID);

        if (!config.wifiDHCP) {
            IPAddress ip, gateway, subnet, dns;
            ip.fromString(config.wifiIP);
            gateway.fromString(config.wifiGateway);
            subnet.fromString(config.wifiSubnet);
            dns.fromString(config.wifiDNS);
            WiFi.config(ip, gateway, subnet, dns);
        }

        WiFi.begin(config.wifiSSID, config.wifiPassword);

        // Wait for connection (max 10 seconds)
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            wifiSTAConnected = true;
            Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println("WiFi STA connection failed - AP remains active");
            // Make sure AP is enabled as fallback
            if (!wifiAPActive) {
                WiFi.mode(WIFI_AP);
                WiFi.softAP(config.hostname);
                wifiAPActive = true;
                Serial.printf("Fallback AP started: %s\n", config.hostname);
            }
        }
    }
}

void checkWiFiConnection() {
    if (!config.wifiEnabled || strlen(config.wifiSSID) == 0) return;

    unsigned long now = millis();
    if (now - lastWiFiCheck < WIFI_CHECK_INTERVAL) return;
    lastWiFiCheck = now;

    if (WiFi.status() != WL_CONNECTED && wifiSTAConnected) {
        wifiSTAConnected = false;
        Serial.println("WiFi STA disconnected, attempting reconnect...");
        ledUpdateNeeded = true;
        wsBroadcastNeeded = true;
    }

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
    } else if (!wifiSTAConnected) {
        wifiSTAConnected = true;
        Serial.printf("WiFi STA reconnected: %s\n", WiFi.localIP().toString().c_str());
        ledUpdateNeeded = true;
        wsBroadcastNeeded = true;
    }
}

// ============================================
// Status & Config JSON
// ============================================
String getStatusJSON() {
    readDigitalInputs();

    JsonDocument doc;
    doc["hostname"] = config.hostname;
    doc["tca9554"] = tca9554Found;
    doc["tcpPort"] = config.tcpPort;
    doc["uptime"] = millis() / 1000;

    // Relays
    JsonArray relays = doc["relays"].to<JsonArray>();
    for (int i = 0; i < 8; i++) {
        relays.add(relayStates[i]);
    }

    // Inputs
    JsonArray inputs = doc["inputs"].to<JsonArray>();
    for (int i = 0; i < 8; i++) {
        inputs.add(inputStates[i]);
    }

    // Ethernet status
    doc["ethEnabled"] = config.ethEnabled;
    doc["ethConnected"] = ethConnected;
#ifndef ETHERNET_DISABLED
    doc["ethMAC"] = ETH.linkUp() ? ETH.macAddress() : "";
    if (ethConnected) {
        // Use ESP-IDF netif API to get correct IP info (fixes DHCP gateway/subnet issue)
        esp_netif_ip_info_t ip_info;
        esp_netif_t* eth_netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
        if (eth_netif && esp_netif_get_ip_info(eth_netif, &ip_info) == ESP_OK) {
            doc["ethIP"] = IPAddress(ip_info.ip.addr).toString();
            doc["ethGateway"] = IPAddress(ip_info.gw.addr).toString();
            doc["ethSubnet"] = IPAddress(ip_info.netmask.addr).toString();
        } else {
            // Fallback to ETH class methods
            doc["ethIP"] = ETH.localIP().toString();
            doc["ethGateway"] = ETH.gatewayIP().toString();
            doc["ethSubnet"] = ETH.subnetMask().toString();
        }
    } else {
        doc["ethIP"] = "";
        doc["ethGateway"] = "";
        doc["ethSubnet"] = "";
    }
#else
    doc["ethIP"] = "";
    doc["ethGateway"] = "";
    doc["ethSubnet"] = "";
    doc["ethMAC"] = "";
#endif

    // WiFi status
    doc["wifiEnabled"] = config.wifiEnabled;
    doc["wifiSSID"] = config.wifiSSID;
    doc["wifiSTAConnected"] = wifiSTAConnected;
    if (wifiSTAConnected) {
        // Use ESP-IDF netif API for WiFi STA (consistent with Ethernet fix)
        esp_netif_ip_info_t wifi_ip_info;
        esp_netif_t* wifi_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (wifi_netif && esp_netif_get_ip_info(wifi_netif, &wifi_ip_info) == ESP_OK) {
            doc["wifiSTAIP"] = IPAddress(wifi_ip_info.ip.addr).toString();
            doc["wifiGateway"] = IPAddress(wifi_ip_info.gw.addr).toString();
            doc["wifiSubnet"] = IPAddress(wifi_ip_info.netmask.addr).toString();
        } else {
            // Fallback to WiFi class methods
            doc["wifiSTAIP"] = WiFi.localIP().toString();
            doc["wifiGateway"] = WiFi.gatewayIP().toString();
            doc["wifiSubnet"] = WiFi.subnetMask().toString();
        }
    } else {
        doc["wifiSTAIP"] = "";
        doc["wifiGateway"] = "";
        doc["wifiSubnet"] = "";
    }
    doc["wifiRSSI"] = wifiSTAConnected ? WiFi.RSSI() : 0;
    doc["wifiAPActive"] = wifiAPActive;
    doc["wifiAPIP"] = wifiAPActive ? WiFi.softAPIP().toString() : "";

    // Firmware version
    doc["version"] = FIRMWARE_VERSION;
    doc["requiredFsVersion"] = REQUIRED_FS_VERSION;

    // IP for header (priority: Ethernet > WiFi STA > WiFi AP)
#ifndef ETHERNET_DISABLED
    if (ethConnected) {
        doc["ip"] = ETH.localIP().toString();
    } else
#endif
    if (wifiSTAConnected) {
        doc["ip"] = WiFi.localIP().toString();
    } else if (wifiAPActive) {
        doc["ip"] = WiFi.softAPIP().toString();
    } else {
        doc["ip"] = "";
    }

    // NTP status
    doc["ntpEnabled"] = config.ntpEnabled;
    doc["ntpSynced"] = ntpSynced;
    if (ntpSynced) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0)) {
            char timeStr[32];
            strftime(timeStr, sizeof(timeStr), "%d.%m.%Y %H:%M:%S", &timeinfo);
            doc["currentTime"] = timeStr;
        } else {
            doc["currentTime"] = "";
        }
    } else {
        doc["currentTime"] = "";
    }

    // Hardware info
    doc["chipModel"] = ESP.getChipModel();
    doc["chipCores"] = ESP.getChipCores();
    doc["flashSize"] = ESP.getFlashChipSize() / 1024 / 1024;  // MB
    doc["freeHeap"] = ESP.getFreeHeap() / 1024;  // KB

    // Modbus status
    doc["modbusEnabled"] = config.modbusEnabled;
    doc["modbusInitialized"] = modbusInitialized;
    doc["modbusConnected"] = modbusConnected;
    doc["modbusAddress"] = config.modbusAddress;
    doc["modbusRelayCount"] = config.modbusRelayCount;
    JsonArray modbusRelays = doc["modbusRelays"].to<JsonArray>();
    for (int i = 0; i < config.modbusRelayCount && i < 8; i++) {
        modbusRelays.add(modbusRelayStates[i]);
    }

    String output;
    serializeJson(doc, output);
    return output;
}

String getConfigJSON() {
    JsonDocument doc;
    doc["hostname"] = config.hostname;
    doc["tcpPort"] = config.tcpPort;
    doc["pulseDuration"] = config.pulseDuration;

    // Ethernet
    doc["ethEnabled"] = config.ethEnabled;
    doc["useDHCP"] = config.ethDHCP;  // For compatibility with HTML
    doc["ethDHCP"] = config.ethDHCP;
    doc["staticIP"] = config.ethIP;
    doc["ethIP"] = config.ethIP;
    doc["gateway"] = config.ethGateway;
    doc["ethGateway"] = config.ethGateway;
    doc["subnet"] = config.ethSubnet;
    doc["ethSubnet"] = config.ethSubnet;
    doc["dns"] = config.ethDNS;
    doc["ethDNS"] = config.ethDNS;

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

    // Don't send password
    doc["wifiPassword"] = "";

    // LED
    doc["ledEnabled"] = config.ledEnabled;
    doc["ledBrightness"] = config.ledBrightness;

    // NTP
    doc["ntpEnabled"] = config.ntpEnabled;
    doc["ntpServer"] = config.ntpServer;
    doc["ntpTimezone"] = config.ntpTimezone;

    // Labels
    JsonArray relayLabelsArr = doc["relayLabels"].to<JsonArray>();
    for (int i = 0; i < 8; i++) {
        relayLabelsArr.add(config.relayLabels[i]);
    }
    JsonArray inputLabelsArr = doc["inputLabels"].to<JsonArray>();
    for (int i = 0; i < 8; i++) {
        inputLabelsArr.add(config.inputLabels[i]);
    }

    // Modbus
    doc["modbusEnabled"] = config.modbusEnabled;
    doc["modbusAddress"] = config.modbusAddress;
    doc["modbusRelayCount"] = config.modbusRelayCount;
    JsonArray modbusLabelsArr = doc["modbusLabels"].to<JsonArray>();
    for (int i = 0; i < 8; i++) {
        modbusLabelsArr.add(config.modbusLabels[i]);
    }

    // Input-to-Relay Mapping
    JsonArray inputRelayMapArr = doc["inputRelayMap"].to<JsonArray>();
    for (int i = 0; i < 8; i++) {
        inputRelayMapArr.add(config.inputRelayMap[i]);
    }

    // TCP per Input
    JsonArray inputTcpModeArr = doc["inputTcpMode"].to<JsonArray>();
    JsonArray inputTcpHostArr = doc["inputTcpHost"].to<JsonArray>();
    JsonArray inputTcpPortArr = doc["inputTcpPort"].to<JsonArray>();
    JsonArray inputTcpCommandArr = doc["inputTcpCommand"].to<JsonArray>();
    JsonArray inputTcpDeviceArr = doc["inputTcpDevice"].to<JsonArray>();
    JsonArray inputTcpFunctionArr = doc["inputTcpFunction"].to<JsonArray>();
    for (int i = 0; i < 8; i++) {
        inputTcpModeArr.add(config.inputTcpMode[i]);
        inputTcpHostArr.add(config.inputTcpHost[i]);
        inputTcpPortArr.add(config.inputTcpPort[i]);
        inputTcpCommandArr.add(config.inputTcpCommand[i]);
        inputTcpDeviceArr.add(config.inputTcpDevice[i]);
        inputTcpFunctionArr.add(config.inputTcpFunction[i]);
    }

    // Device presets info for UI
    JsonArray devicesArr = doc["tcpDevices"].to<JsonArray>();
    for (int d = 0; d < DEVICE_PRESET_COUNT; d++) {
        JsonObject dev = devicesArr.add<JsonObject>();
        dev["name"] = DEVICE_PRESETS[d].name;
        dev["port"] = DEVICE_PRESETS[d].port;
        JsonArray funcs = dev["functions"].to<JsonArray>();
        for (int f = 0; f < DEVICE_PRESETS[d].commandCount; f++) {
            funcs.add(DEVICE_PRESETS[d].commands[f].name);
        }
    }

    String output;
    serializeJson(doc, output);
    return output;
}

// ============================================
// Config Load/Save
// ============================================
void loadConfig() {
    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file) {
        Serial.println("No config file, using defaults");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("Config parse error: %s\n", error.c_str());
        return;
    }

    strlcpy(config.hostname, doc["hostname"] | "CineRelais", sizeof(config.hostname));
    config.tcpPort = doc["tcpPort"] | 5000;
    config.pulseDuration = doc["pulseDuration"] | 500;

    // Ethernet - use explicit key check for boolean (ArduinoJson v7 API)
    config.ethEnabled = doc["ethEnabled"] | true;
    if (doc["ethDHCP"].is<bool>()) {
        config.ethDHCP = doc["ethDHCP"].as<bool>();
    } else if (doc["useDHCP"].is<bool>()) {
        config.ethDHCP = doc["useDHCP"].as<bool>();
    } else {
        config.ethDHCP = true;
    }
    strlcpy(config.ethIP, doc["ethIP"] | doc["staticIP"] | "192.168.1.100", sizeof(config.ethIP));
    strlcpy(config.ethGateway, doc["ethGateway"] | doc["gateway"] | "192.168.1.1", sizeof(config.ethGateway));
    strlcpy(config.ethSubnet, doc["ethSubnet"] | doc["subnet"] | "255.255.255.0", sizeof(config.ethSubnet));
    strlcpy(config.ethDNS, doc["ethDNS"] | doc["dns"] | "8.8.8.8", sizeof(config.ethDNS));

    // WiFi
    config.wifiEnabled = doc["wifiEnabled"] | true;
    config.wifiAPEnabled = doc["wifiAPEnabled"] | true;
    strlcpy(config.wifiSSID, doc["wifiSSID"] | "", sizeof(config.wifiSSID));
    strlcpy(config.wifiPassword, doc["wifiPassword"] | "", sizeof(config.wifiPassword));
    strlcpy(config.wifiAPPassword, doc["wifiAPPassword"] | "", sizeof(config.wifiAPPassword));
    config.wifiDHCP = doc["wifiDHCP"] | true;
    strlcpy(config.wifiIP, doc["wifiIP"] | "192.168.4.100", sizeof(config.wifiIP));
    strlcpy(config.wifiGateway, doc["wifiGateway"] | "192.168.4.1", sizeof(config.wifiGateway));
    strlcpy(config.wifiSubnet, doc["wifiSubnet"] | "255.255.255.0", sizeof(config.wifiSubnet));
    strlcpy(config.wifiDNS, doc["wifiDNS"] | "8.8.8.8", sizeof(config.wifiDNS));

    // LED
    config.ledEnabled = doc["ledEnabled"] | true;
    config.ledBrightness = doc["ledBrightness"] | 51;

    // NTP
    config.ntpEnabled = doc["ntpEnabled"] | true;
    strlcpy(config.ntpServer, doc["ntpServer"] | "pool.ntp.org", sizeof(config.ntpServer));
    strlcpy(config.ntpTimezone, doc["ntpTimezone"] | "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(config.ntpTimezone));

    // Labels
    if (doc["relayLabels"].is<JsonArray>()) {
        JsonArray arr = doc["relayLabels"].as<JsonArray>();
        for (int i = 0; i < 8 && i < arr.size(); i++) {
            strlcpy(config.relayLabels[i], arr[i] | "", sizeof(config.relayLabels[i]));
        }
    }
    if (doc["inputLabels"].is<JsonArray>()) {
        JsonArray arr = doc["inputLabels"].as<JsonArray>();
        for (int i = 0; i < 8 && i < arr.size(); i++) {
            strlcpy(config.inputLabels[i], arr[i] | "", sizeof(config.inputLabels[i]));
        }
    }

    // Modbus
    config.modbusEnabled = doc["modbusEnabled"] | false;
    config.modbusAddress = doc["modbusAddress"] | 1;
    config.modbusRelayCount = doc["modbusRelayCount"] | 8;
    if (doc["modbusLabels"].is<JsonArray>()) {
        JsonArray arr = doc["modbusLabels"].as<JsonArray>();
        for (int i = 0; i < 8 && i < arr.size(); i++) {
            strlcpy(config.modbusLabels[i], arr[i] | "", sizeof(config.modbusLabels[i]));
        }
    }

    // Input-to-Relay Mapping
    if (doc["inputRelayMap"].is<JsonArray>()) {
        JsonArray arr = doc["inputRelayMap"].as<JsonArray>();
        for (int i = 0; i < 8 && i < arr.size(); i++) {
            config.inputRelayMap[i] = arr[i] | 0;
        }
    }

    // TCP per Input
    if (doc["inputTcpMode"].is<JsonArray>()) {
        JsonArray arr = doc["inputTcpMode"].as<JsonArray>();
        for (int i = 0; i < 8 && i < arr.size(); i++) {
            config.inputTcpMode[i] = arr[i] | 0;
        }
    }
    if (doc["inputTcpHost"].is<JsonArray>()) {
        JsonArray arr = doc["inputTcpHost"].as<JsonArray>();
        for (int i = 0; i < 8 && i < arr.size(); i++) {
            strlcpy(config.inputTcpHost[i], arr[i] | "", sizeof(config.inputTcpHost[i]));
        }
    }
    if (doc["inputTcpPort"].is<JsonArray>()) {
        JsonArray arr = doc["inputTcpPort"].as<JsonArray>();
        for (int i = 0; i < 8 && i < arr.size(); i++) {
            config.inputTcpPort[i] = arr[i] | 0;
        }
    }
    if (doc["inputTcpCommand"].is<JsonArray>()) {
        JsonArray arr = doc["inputTcpCommand"].as<JsonArray>();
        for (int i = 0; i < 8 && i < arr.size(); i++) {
            strlcpy(config.inputTcpCommand[i], arr[i] | "", sizeof(config.inputTcpCommand[i]));
        }
    }
    if (doc["inputTcpDevice"].is<JsonArray>()) {
        JsonArray arr = doc["inputTcpDevice"].as<JsonArray>();
        for (int i = 0; i < 8 && i < arr.size(); i++) {
            config.inputTcpDevice[i] = arr[i] | 0;
        }
    }
    if (doc["inputTcpFunction"].is<JsonArray>()) {
        JsonArray arr = doc["inputTcpFunction"].as<JsonArray>();
        for (int i = 0; i < 8 && i < arr.size(); i++) {
            config.inputTcpFunction[i] = arr[i] | 0;
        }
    }

    Serial.printf("Config loaded: hostname=%s, tcpPort=%d, ethDHCP=%d, modbus=%d\n",
                  config.hostname, config.tcpPort, config.ethDHCP, config.modbusEnabled);
}

void saveConfig() {
    JsonDocument doc;
    doc["hostname"] = config.hostname;
    doc["tcpPort"] = config.tcpPort;
    doc["pulseDuration"] = config.pulseDuration;

    // Ethernet
    doc["ethEnabled"] = config.ethEnabled;
    doc["ethDHCP"] = config.ethDHCP;
    doc["ethIP"] = config.ethIP;
    doc["ethGateway"] = config.ethGateway;
    doc["ethSubnet"] = config.ethSubnet;
    doc["ethDNS"] = config.ethDNS;

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

    // LED
    doc["ledEnabled"] = config.ledEnabled;
    doc["ledBrightness"] = config.ledBrightness;

    // NTP
    doc["ntpEnabled"] = config.ntpEnabled;
    doc["ntpServer"] = config.ntpServer;
    doc["ntpTimezone"] = config.ntpTimezone;

    // Labels
    JsonArray relayLabelsArr = doc["relayLabels"].to<JsonArray>();
    for (int i = 0; i < 8; i++) {
        relayLabelsArr.add(config.relayLabels[i]);
    }
    JsonArray inputLabelsArr = doc["inputLabels"].to<JsonArray>();
    for (int i = 0; i < 8; i++) {
        inputLabelsArr.add(config.inputLabels[i]);
    }

    // Modbus
    doc["modbusEnabled"] = config.modbusEnabled;
    doc["modbusAddress"] = config.modbusAddress;
    doc["modbusRelayCount"] = config.modbusRelayCount;
    JsonArray modbusLabelsArr = doc["modbusLabels"].to<JsonArray>();
    for (int i = 0; i < 8; i++) {
        modbusLabelsArr.add(config.modbusLabels[i]);
    }

    // Input-to-Relay Mapping
    JsonArray inputRelayMapArr = doc["inputRelayMap"].to<JsonArray>();
    for (int i = 0; i < 8; i++) {
        inputRelayMapArr.add(config.inputRelayMap[i]);
    }

    // TCP per Input
    JsonArray inputTcpModeArr = doc["inputTcpMode"].to<JsonArray>();
    JsonArray inputTcpHostArr = doc["inputTcpHost"].to<JsonArray>();
    JsonArray inputTcpPortArr = doc["inputTcpPort"].to<JsonArray>();
    JsonArray inputTcpCommandArr = doc["inputTcpCommand"].to<JsonArray>();
    JsonArray inputTcpDeviceArr = doc["inputTcpDevice"].to<JsonArray>();
    JsonArray inputTcpFunctionArr = doc["inputTcpFunction"].to<JsonArray>();
    for (int i = 0; i < 8; i++) {
        inputTcpModeArr.add(config.inputTcpMode[i]);
        inputTcpHostArr.add(config.inputTcpHost[i]);
        inputTcpPortArr.add(config.inputTcpPort[i]);
        inputTcpCommandArr.add(config.inputTcpCommand[i]);
        inputTcpDeviceArr.add(config.inputTcpDevice[i]);
        inputTcpFunctionArr.add(config.inputTcpFunction[i]);
    }

    File file = LittleFS.open(CONFIG_FILE, "w");
    if (!file) {
        Serial.println("ERROR: Cannot open config file for writing");
        return;
    }

    serializeJson(doc, file);
    file.close();
    Serial.println("Config saved");
}

// ============================================
// TCA9554 Relay Control
// ============================================
void tca9554Init() {
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(TCA9554_CONFIG_REG);
    Wire.write(0x00);
    uint8_t err = Wire.endTransmission();

    if (err != 0) {
        Serial.printf("ERROR: TCA9554 not found (error %d)\n", err);
        tca9554Found = false;
        return;
    }

    tca9554Found = true;
    Serial.printf("TCA9554 found at 0x%02X\n", TCA9554_ADDR);

    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(TCA9554_OUTPUT_REG);
    Wire.write(0x00);
    Wire.endTransmission();
    relayRegister = 0x00;
}

void setRelay(int relay, bool state) {
    if (!tca9554Found || relay < 1 || relay > 8) return;

    int bit = relay - 1;
    if (state) {
        relayRegister |= (1 << bit);
    } else {
        relayRegister &= ~(1 << bit);
    }

    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(TCA9554_OUTPUT_REG);
    Wire.write(relayRegister);
    Wire.endTransmission();

    relayStates[bit] = state;
    Serial.printf("Relay %d: %s\n", relay, state ? "ON" : "OFF");
    wsNotify();
}

void setAllRelays(bool state) {
    if (!tca9554Found) return;

    relayRegister = state ? 0xFF : 0x00;

    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(TCA9554_OUTPUT_REG);
    Wire.write(relayRegister);
    Wire.endTransmission();

    for (int i = 0; i < 8; i++) {
        relayStates[i] = state;
    }
    Serial.printf("All relays: %s\n", state ? "ON" : "OFF");
    wsNotify();
}

void pulseRelay(int relay, uint16_t duration) {
    if (relay < 1 || relay > 8) return;
    int idx = relay - 1;
    setRelay(relay, true);
    pulseActive[idx] = true;
    pulseEndTime[idx] = millis() + duration;
}

void pulseAllRelays(uint16_t duration) {
    setAllRelays(true);
    unsigned long endTime = millis() + duration;
    for (int i = 0; i < 8; i++) {
        pulseActive[i] = true;
        pulseEndTime[i] = endTime;
    }
}

void updatePulses() {
    unsigned long now = millis();
    bool anyChanged = false;
    for (int i = 0; i < 8; i++) {
        if (pulseActive[i] && now >= pulseEndTime[i]) {
            pulseActive[i] = false;
            relayStates[i] = false;
            relayRegister &= ~(1 << i);
            anyChanged = true;
        }
    }
    if (anyChanged && tca9554Found) {
        Wire.beginTransmission(TCA9554_ADDR);
        Wire.write(TCA9554_OUTPUT_REG);
        Wire.write(relayRegister);
        Wire.endTransmission();
        wsNotify();
    }
}

// ============================================
// NTP Time Synchronization
// ============================================
void setupNTP() {
    if (!config.ntpEnabled) {
        Serial.println("NTP: Disabled");
        return;
    }

    Serial.printf("NTP: Configuring with server=%s, timezone=%s\n",
                  config.ntpServer, config.ntpTimezone);

    // Configure NTP
    configTzTime(config.ntpTimezone, config.ntpServer);

    // Wait briefly for initial sync attempt
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5000)) {  // 5 second timeout
        ntpSynced = true;
        char timeStr[32];
        strftime(timeStr, sizeof(timeStr), "%d.%m.%Y %H:%M:%S", &timeinfo);
        Serial.printf("NTP: Synced - %s\n", timeStr);
    } else {
        ntpSynced = false;
        Serial.println("NTP: Initial sync failed, will retry in background");
    }
}

// ============================================
// Input-to-Relay Mapping and TCP Commands
// ============================================
void processInputMappings() {
    readDigitalInputs();
    unsigned long now = millis();

    // Debounce each input
    for (int i = 0; i < 8; i++) {
        if (inputStates[i] != inputDebouncedStates[i]) {
            // Input changed - reset debounce timer
            if (inputDebounceTime[i] == 0) {
                inputDebounceTime[i] = now;
            } else if (now - inputDebounceTime[i] >= INPUT_DEBOUNCE_MS) {
                // Stable for debounce period - accept new state
                inputDebouncedStates[i] = inputStates[i];
                inputDebounceTime[i] = 0;
                wsNotify();  // Input state changed — push to WS clients

                // Handle TCP command on rising edge (input became active)
                if (inputDebouncedStates[i] && config.inputTcpMode[i] != 0) {
                    sendInputTcpCommand(i);
                }
            }
        } else {
            // Input matches debounced state - reset timer
            inputDebounceTime[i] = 0;
        }
    }

    uint8_t newInputControlled = 0;

    // Check each input (using debounced states) and accumulate relay bits
    for (int i = 0; i < 8; i++) {
        if (inputDebouncedStates[i] && config.inputRelayMap[i] != 0) {
            newInputControlled |= config.inputRelayMap[i];
        }
    }

    // Only update relays if the controlled set changed
    if (newInputControlled != inputControlledRelays) {
        for (int r = 0; r < 8; r++) {
            bool shouldBeOn = (newInputControlled & (1 << r)) != 0;
            bool wasOn = (inputControlledRelays & (1 << r)) != 0;

            if (shouldBeOn != wasOn) {
                setRelay(r + 1, shouldBeOn);
            }
        }
        inputControlledRelays = newInputControlled;
    }
}

// Helper: Convert HEX string to byte array
int hexStringToBytes(const char* hexStr, uint8_t* outBytes, int maxLen) {
    int len = strlen(hexStr);
    int byteCount = 0;

    for (int i = 0; i < len && byteCount < maxLen; i += 2) {
        char hexByte[3] = {hexStr[i], hexStr[i + 1], 0};
        outBytes[byteCount++] = (uint8_t)strtol(hexByte, nullptr, 16);
    }
    return byteCount;
}

// Send TCP command for input (async, fire-and-forget)
void sendInputTcpCommand(int inputIndex) {
    if (inputIndex < 0 || inputIndex >= 8) return;

    const char* host = config.inputTcpHost[inputIndex];
    if (strlen(host) == 0) {
        Serial.printf("Input %d TCP: No host configured\n", inputIndex + 1);
        return;
    }

    uint16_t port;
    const char* command;
    bool isHex = false;

    if (config.inputTcpMode[inputIndex] == 1) {
        // Manual mode
        port = config.inputTcpPort[inputIndex];
        command = config.inputTcpCommand[inputIndex];
        isHex = (strncmp(command, "HEX:", 4) == 0);
        if (isHex) command += 4;  // Skip "HEX:" prefix
    } else if (config.inputTcpMode[inputIndex] == 2) {
        // Preset mode
        uint8_t deviceIdx = config.inputTcpDevice[inputIndex];
        uint8_t funcIdx = config.inputTcpFunction[inputIndex];

        if (deviceIdx >= DEVICE_PRESET_COUNT) {
            Serial.printf("Input %d TCP: Invalid device index\n", inputIndex + 1);
            return;
        }

        const DevicePreset& device = DEVICE_PRESETS[deviceIdx];
        if (funcIdx >= device.commandCount) {
            Serial.printf("Input %d TCP: Invalid function index\n", inputIndex + 1);
            return;
        }

        port = device.port;
        command = device.commands[funcIdx].command;
        isHex = (strncmp(command, "HEX:", 4) == 0);
        if (isHex) command += 4;
    } else {
        return;  // Mode 0 = disabled
    }

    if (port == 0 || strlen(command) == 0) {
        Serial.printf("Input %d TCP: Invalid port or command\n", inputIndex + 1);
        return;
    }

    Serial.printf("Input %d TCP: Sending to %s:%d\n", inputIndex + 1, host, port);
    flashInputTcpLED();

    // Log outgoing TCP command
    static char lastLogTarget[48];
    char logCmd[64];
    snprintf(lastLogTarget, sizeof(lastLogTarget), "%s:%d", host, port);
    if (config.inputTcpMode[inputIndex] == 2) {
        // Preset mode - show device and function name
        uint8_t deviceIdx = config.inputTcpDevice[inputIndex];
        uint8_t funcIdx = config.inputTcpFunction[inputIndex];
        snprintf(logCmd, sizeof(logCmd), "%s: %s",
                 DEVICE_PRESETS[deviceIdx].name,
                 DEVICE_PRESETS[deviceIdx].commands[funcIdx].name);
    } else if (isHex) {
        snprintf(logCmd, sizeof(logCmd), "HEX (%d bytes)", (int)strlen(command) / 2);
    } else {
        strlcpy(logCmd, command, sizeof(logCmd));
    }
    addLogEntry(lastLogTarget, logCmd, "OUT");

    // Create async client for fire-and-forget
    AsyncClient* client = new AsyncClient();

    // Prepare command data
    static uint8_t cmdBuffer[128];
    int cmdLen;

    if (isHex) {
        cmdLen = hexStringToBytes(command, cmdBuffer, sizeof(cmdBuffer));
    } else {
        cmdLen = strlen(command);
        memcpy(cmdBuffer, command, cmdLen);
    }

    // Store command info for callback (simple approach using static for last command)
    static uint8_t lastCmdBuffer[128];
    static int lastCmdLen;
    memcpy(lastCmdBuffer, cmdBuffer, cmdLen);
    lastCmdLen = cmdLen;

    client->onConnect([](void* arg, AsyncClient* c) {
        Serial.println("TCP Input: Connected, sending command");
        c->write((char*)lastCmdBuffer, lastCmdLen);
        // Don't close immediately - wait for response
    }, nullptr);

    client->onData([](void* arg, AsyncClient* c, void* data, size_t len) {
        // Log incoming response
        char response[65];
        size_t copyLen = len < 64 ? len : 64;
        memcpy(response, data, copyLen);
        response[copyLen] = '\0';
        // Clean up non-printable characters for log
        for (size_t i = 0; i < copyLen; i++) {
            if (response[i] < 32 || response[i] > 126) response[i] = '.';
        }
        Serial.printf("TCP Input: Response: %s\n", response);
        addLogEntry(lastLogTarget, response, "IN");
        c->close(true);
    }, nullptr);

    client->onDisconnect([](void* arg, AsyncClient* c) {
        Serial.println("TCP Input: Disconnected");
        delete c;
    }, nullptr);

    client->onError([](void* arg, AsyncClient* c, int8_t error) {
        Serial.printf("TCP Input: Error %d\n", error);
        delete c;
    }, nullptr);

    client->onTimeout([](void* arg, AsyncClient* c, uint32_t time) {
        Serial.println("TCP Input: Timeout (no response)");
        c->close(true);
    }, nullptr);

    client->setRxTimeout(2);  // 2 second timeout for response

    // Connect (async)
    if (!client->connect(host, port)) {
        Serial.printf("TCP Input: Connect to %s:%d failed\n", host, port);
        delete client;
    }
}

// ============================================
// Modbus RS485 Relay Control
// ============================================

// Helper to decode Modbus error codes
const char* modbusErrorString(uint8_t error) {
    switch (error) {
        case 0x00: return "Success";
        case 0x01: return "Illegal function";
        case 0x02: return "Illegal data address";
        case 0x03: return "Illegal data value";
        case 0x04: return "Slave device failure";
        case 0xE0: return "Invalid slave ID";
        case 0xE1: return "Invalid function";
        case 0xE2: return "Response timed out";
        case 0xE3: return "Invalid CRC";
        default: return "Unknown error";
    }
}

void setupModbus() {
    if (!config.modbusEnabled) {
        Serial.println("Modbus: Disabled");
        modbusInitialized = false;
        return;
    }

    Serial.println("Initializing Modbus RS485...");

    // Initialize Serial1 for RS485 (TX=17, RX=18)
    Serial1.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

    // Enable RS485 Half-Duplex mode - prevents reading own TX data
    uart_set_mode(UART_NUM_1, UART_MODE_RS485_HALF_DUPLEX);

    // Initialize ModbusMaster with device address
    modbusNode.begin(config.modbusAddress, Serial1);

    // Mark as initialized so other functions can safely use it
    modbusInitialized = true;

    Serial.printf("Modbus: Enabled, Address=%d, Relays=%d\n",
                  config.modbusAddress, config.modbusRelayCount);

    // Try to read relay states to verify connection
    // Waveshare uses Function Code 0x01 (Read Coils) at address 0x0000
    delay(100);
    uint8_t result = modbusNode.readCoils(0x0000, config.modbusRelayCount);
    if (result == modbusNode.ku8MBSuccess) {
        modbusConnected = true;
        // Read initial states from bitmap
        uint16_t data = modbusNode.getResponseBuffer(0);
        for (int i = 0; i < config.modbusRelayCount && i < 8; i++) {
            modbusRelayStates[i] = (data >> i) & 0x01;
        }
        Serial.printf("Modbus: Connected (relay status: 0x%02X)\n", data & 0xFF);
    } else {
        modbusConnected = false;
        Serial.printf("Modbus: Connection failed - %s (0x%02X)\n", modbusErrorString(result), result);
    }
}

bool modbusSetRelay(int relay, bool state) {
    if (!config.modbusEnabled) {
        Serial.println("Modbus: Disabled");
        return false;
    }
    if (!modbusInitialized) {
        Serial.println("Modbus: Not initialized");
        return false;
    }
    if (relay < 1 || relay > config.modbusRelayCount) {
        Serial.printf("Modbus: Invalid relay %d (max %d)\n", relay, config.modbusRelayCount);
        return false;
    }

    // Waveshare Modbus RTU Relay Protocol:
    // Function Code 0x05 (Write Single Coil)
    // Coil Address: 0x0000-0x0007 for relays 1-8 (0-based, consistent with readCoils)
    // ModbusMaster writeSingleCoil(addr, u8State): u8State is uint8_t (0=OFF, non-zero=ON)
    // IMPORTANT: Do NOT pass 0xFF00 here — it is truncated to 0x00 (uint8_t), which always sends OFF!
    uint16_t coilAddr = relay - 1;  // 0-based: relay 1 = coil 0x0000
    uint8_t  coilState = state ? 1 : 0;  // ModbusMaster converts 1→0xFF00, 0→0x0000 in frame

    Serial.printf("Modbus: FC05 Coil 0x%04X = %s\n", coilAddr, state ? "ON (0xFF00)" : "OFF (0x0000)");

    // Delays for RS485 timing
    delay(10);
    uint8_t result = modbusNode.writeSingleCoil(coilAddr, coilState);
    delay(20);  // Give relay module time to process

    if (result == modbusNode.ku8MBSuccess) {
        modbusRelayStates[relay - 1] = state;
        modbusConnected = true;
        Serial.printf("Modbus Relay %d: %s\n", relay, state ? "ON" : "OFF");
        wsNotify();

        // Log Modbus command
        char logCmd[32];
        snprintf(logCmd, sizeof(logCmd), "mr%d_%s", relay, state ? "on" : "off");
        char logSource[32];
        snprintf(logSource, sizeof(logSource), "Modbus @%d", config.modbusAddress);
        addLogEntry(logSource, logCmd, "OUT");

        return true;
    } else {
        modbusConnected = false;
        Serial.printf("Modbus Relay %d: FAILED - %s (0x%02X)\n", relay, modbusErrorString(result), result);
        wsNotify();
        return false;
    }
}

bool modbusSetAllRelays(bool state) {
    if (!config.modbusEnabled) {
        Serial.println("Modbus: Disabled (all relays)");
        return false;
    }
    if (!modbusInitialized) {
        Serial.println("Modbus: Not initialized (all relays)");
        return false;
    }

    // FC15 (Write Multiple Coils) — single frame for all relays at once
    uint16_t coilBitmap = state ? ((1u << config.modbusRelayCount) - 1u) : 0u;
    Serial.printf("Modbus: FC15 Write %d coils = %s (bitmap=0x%04X)\n",
                  config.modbusRelayCount, state ? "ON" : "OFF", coilBitmap);

    delay(10);
    modbusNode.setTransmitBuffer(0, coilBitmap);
    uint8_t result = modbusNode.writeMultipleCoils(0x0000, config.modbusRelayCount);
    delay(20);

    if (result == modbusNode.ku8MBSuccess) {
        for (int i = 0; i < config.modbusRelayCount; i++) {
            modbusRelayStates[i] = state;
        }
        modbusConnected = true;
        Serial.printf("Modbus All relays: %s\n", state ? "ON" : "OFF");
        wsNotify();

        char logCmd[32];
        snprintf(logCmd, sizeof(logCmd), "m1_all_%s", state ? "on" : "off");
        char logSource[32];
        snprintf(logSource, sizeof(logSource), "Modbus @%d", config.modbusAddress);
        addLogEntry(logSource, logCmd, "OUT");

        return true;
    } else {
        modbusConnected = false;
        Serial.printf("Modbus All relays FAILED: %s (0x%02X)\n", modbusErrorString(result), result);
        wsNotify();
        return false;
    }
}

// CRC16 Modbus (polynomial 0xA001, initial value 0xFFFF, low byte first in frame)
static uint16_t crc16Modbus(const uint8_t *data, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else              crc >>= 1;
        }
    }
    return crc;
}

// Waveshare native flash command:
//   FC05, coil address = 0x02XX (XX = relay 0-indexed), data = delay in 100ms units
//   Frame: [addr] 05 02 [relay] [delay_hi] [delay_lo] [CRC_lo] [CRC_hi]
//   Example: 01 05 02 00 00 07 8D B0  → relay 1, 700ms
//   Delay is taken from config.pulseDuration (ms).
bool modbusFlashNative(int relay) {
    if (!config.modbusEnabled || !modbusInitialized) return false;
    if (relay < 1 || relay > config.modbusRelayCount) return false;

    uint16_t delay100 = config.pulseDuration / 100;
    if (delay100 < 1)      delay100 = 1;
    if (delay100 > 0x7FFF) delay100 = 0x7FFF;

    uint8_t frame[8];
    frame[0] = config.modbusAddress;
    frame[1] = 0x05;
    frame[2] = 0x02;                        // flash-on command
    frame[3] = (uint8_t)(relay - 1);        // relay index 0-7
    frame[4] = (uint8_t)(delay100 >> 8);    // delay high byte
    frame[5] = (uint8_t)(delay100 & 0xFF);  // delay low byte
    uint16_t crc = crc16Modbus(frame, 6);
    frame[6] = (uint8_t)(crc & 0xFF);       // CRC low byte first
    frame[7] = (uint8_t)(crc >> 8);         // CRC high byte

    Serial.printf("Modbus: FC05 Flash relay %d for %dms (%d * 100ms)\n", relay, config.pulseDuration, delay100);
    Serial.printf("Modbus Frame: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  frame[0], frame[1], frame[2], frame[3], frame[4], frame[5], frame[6], frame[7]);

    delay(10);
    Serial1.write(frame, 8);
    delay(50);  // Wait for device to process and respond

    // Drain response echo (device mirrors the request frame)
    while (Serial1.available()) Serial1.read();

    modbusConnected = true;

    char logCmd[40];
    snprintf(logCmd, sizeof(logCmd), "mr%d_flash_%d", relay, config.pulseDuration);
    char logSource[32];
    snprintf(logSource, sizeof(logSource), "Modbus @%d", config.modbusAddress);
    addLogEntry(logSource, logCmd, "OUT");

    return true;
}

void modbusPulseRelay(int relay, uint16_t duration) {
    (void)duration;  // pulseDuration is read from config inside modbusFlashNative
    if (!config.modbusEnabled || !modbusInitialized || relay < 1 || relay > config.modbusRelayCount) return;
    modbusFlashNative(relay);
}

void modbusPulseAllRelays(uint16_t duration) {
    if (!config.modbusEnabled || !modbusInitialized) return;
    if (duration < 10) duration = config.pulseDuration;
    if (modbusSetAllRelays(true)) {
        modbusAllPulseActive = true;
        modbusAllPulseEndTime = millis() + duration;
    }
}

void updateModbusPulses() {
    if (!modbusAllPulseActive) return;
    if (millis() >= modbusAllPulseEndTime) {
        modbusAllPulseActive = false;
        modbusSetAllRelays(false);
    }
}

void modbusReadRelays() {
    if (!config.modbusEnabled || !modbusInitialized) return;

    unsigned long now = millis();
    if (now - modbusLastPoll < MODBUS_POLL_INTERVAL) return;
    modbusLastPoll = now;

    // Waveshare: Function Code 0x01 (Read Coils) at address 0x0000
    uint8_t result = modbusNode.readCoils(0x0000, config.modbusRelayCount);
    if (result == modbusNode.ku8MBSuccess) {
        bool wasConnected = modbusConnected;
        modbusConnected = true;
        uint16_t data = modbusNode.getResponseBuffer(0);
        bool stateChanged = !wasConnected;
        for (int i = 0; i < config.modbusRelayCount && i < 8; i++) {
            bool newState = (data >> i) & 0x01;
            if (modbusRelayStates[i] != newState) stateChanged = true;
            modbusRelayStates[i] = newState;
        }
        if (stateChanged) wsNotify();
    } else {
        if (modbusConnected) wsNotify();  // Connection lost
        modbusConnected = false;
    }
}

// Scan for Modbus device address (1-247)
int modbusScanAddress() {
    Serial.println("Modbus: Scanning for device...");

    // Initialize Serial1 if not already done
    Serial1.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    delay(100);

    for (uint8_t addr = 1; addr <= 32; addr++) {  // Scan first 32 addresses
        yield();  // Feed watchdog
        modbusNode.begin(addr, Serial1);
        delay(20);

        // Try reading coils (Function Code 0x01) at address 0x0000
        uint8_t result = modbusNode.readCoils(0x0000, 1);
        if (result == modbusNode.ku8MBSuccess) {
            Serial.printf("Modbus: Found device at address %d\n", addr);
            return addr;
        }

        // Print progress every 8 addresses
        if (addr % 8 == 0) {
            Serial.printf("Modbus: Scanned 1-%d, no device yet...\n", addr);
        }
    }

    Serial.println("Modbus: No device found");
    return -1;
}

// ============================================
// Digital Inputs
// ============================================
void setupDigitalInputs() {
    for (int i = 0; i < 8; i++) {
        pinMode(DI_PINS[i], INPUT_PULLUP);
    }
}

void readDigitalInputs() {
    for (int i = 0; i < 8; i++) {
        inputStates[i] = (digitalRead(DI_PINS[i]) == LOW);
    }
}

// ============================================
// TCP Command Server
// ============================================
void setupTcpServer() {
    tcpServer = new AsyncServer(config.tcpPort);

    tcpServer->onClient([](void* arg, AsyncClient* client) {
        Serial.printf("TCP client connected: %s\n", client->remoteIP().toString().c_str());
        tcpClients.push_back(client);

        client->onData([](void* arg, AsyncClient* c, void* data, size_t len) {
            String cmd = String((char*)data).substring(0, len);
            cmd.trim();
            cmd.toLowerCase();
            if (cmd.length() > 0) {
                Serial.printf("TCP cmd: %s\n", cmd.c_str());
                String response = processCommand(cmd);
                if (cmd != "status" && cmd != "help") {
                    flashEventLED();
                    // Log incoming TCP command
                    addLogEntry(c->remoteIP().toString().c_str(), cmd.c_str(), "IN");
                }
                if (c->connected()) {
                    c->write((response + "\n").c_str());
                }
            }
        }, nullptr);

        client->onDisconnect([](void* arg, AsyncClient* c) {
            Serial.println("TCP client disconnected");
            for (auto it = tcpClients.begin(); it != tcpClients.end(); ++it) {
                if (*it == c) {
                    tcpClients.erase(it);
                    break;
                }
            }
        }, nullptr);

    }, nullptr);

    tcpServer->begin();
    Serial.printf("TCP server started on port %d\n", config.tcpPort);
}

// ============================================
// WebSocket
// ============================================
void wsNotify() {
    wsBroadcastNeeded = true;
}

void wsBroadcast() {
    if (ws && ws->count() > 0) {
        String status = getStatusJSON();
        ws->textAll(status);
        ws->cleanupClients(4);  // Limit to 4 concurrent WS clients
    }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WS client #%u connected from %s\n",
                          client->id(), client->remoteIP().toString().c_str());
            // Send full status to newly connected client
            client->text(getStatusJSON());
            break;

        case WS_EVT_DISCONNECT:
            Serial.printf("WS client #%u disconnected\n", client->id());
            break;

        case WS_EVT_DATA: {
            AwsFrameInfo *info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                String cmd = String((char*)data).substring(0, len);
                cmd.trim();
                cmd.toLowerCase();
                if (cmd.length() > 0) {
                    Serial.printf("WS cmd from #%u: %s\n", client->id(), cmd.c_str());
                    String response = processCommand(cmd);
                    if (cmd != "status" && cmd != "help") {
                        flashEventLED();
                        addLogEntry(client->remoteIP().toString().c_str(), cmd.c_str(), "IN");
                    }
                    client->text(response);
                }
            }
            break;
        }

        case WS_EVT_ERROR:
            Serial.printf("WS client #%u error\n", client->id());
            break;

        case WS_EVT_PONG:
            break;
    }
}

String processCommand(const String& cmd) {
    // r<1-8>_on / r<1-8>_off / r<1-8>_pulse / r<1-8>_pulse_<ms>
    if (cmd.startsWith("r") && cmd.length() >= 4) {
        int relay = cmd.substring(1, 2).toInt();
        if (relay >= 1 && relay <= 8) {
            if (cmd.indexOf("_on") > 0) {
                setRelay(relay, true);
                return "OK: Relay " + String(relay) + " ON";
            } else if (cmd.indexOf("_off") > 0) {
                setRelay(relay, false);
                return "OK: Relay " + String(relay) + " OFF";
            } else if (cmd.indexOf("_pulse") > 0) {
                uint16_t duration = config.pulseDuration;
                int underscorePos = cmd.lastIndexOf('_');
                if (underscorePos > 7) {
                    duration = cmd.substring(underscorePos + 1).toInt();
                    if (duration < 10) duration = config.pulseDuration;
                }
                pulseRelay(relay, duration);
                return "OK: Relay " + String(relay) + " PULSE " + String(duration) + "ms";
            }
        }
    }

    if (cmd == "all_on") {
        setAllRelays(true);
        return "OK: All relays ON";
    }
    if (cmd == "all_off") {
        setAllRelays(false);
        return "OK: All relays OFF";
    }
    if (cmd.startsWith("all_pulse")) {
        uint16_t duration = config.pulseDuration;
        int underscorePos = cmd.lastIndexOf('_');
        if (underscorePos > 4) {
            duration = cmd.substring(underscorePos + 1).toInt();
            if (duration < 10) duration = config.pulseDuration;
        }
        pulseAllRelays(duration);
        return "OK: All relays PULSE " + String(duration) + "ms";
    }

    // Modbus commands: m1_r<1-8>_on / m1_r<1-8>_off / m1_r<1-8>_pulse / m1_all_on / m1_all_off
    if (cmd.startsWith("m1_")) {
        if (!config.modbusEnabled) {
            return "ERROR: Modbus disabled";
        }

        String subCmd = cmd.substring(3);  // Remove "m1_"

        // m1_r<1-8>_on / m1_r<1-8>_off / m1_r<1-8>_pulse
        if (subCmd.startsWith("r") && subCmd.length() >= 4) {
            int relay = subCmd.substring(1, 2).toInt();
            if (relay >= 1 && relay <= config.modbusRelayCount) {
                if (subCmd.indexOf("_on") > 0) {
                    if (modbusSetRelay(relay, true)) {
                        return "OK: Modbus Relay " + String(relay) + " ON";
                    } else {
                        return "ERROR: Modbus communication failed";
                    }
                } else if (subCmd.indexOf("_off") > 0) {
                    if (modbusSetRelay(relay, false)) {
                        return "OK: Modbus Relay " + String(relay) + " OFF";
                    } else {
                        return "ERROR: Modbus communication failed";
                    }
                } else if (subCmd.indexOf("_pulse") > 0) {
                    uint16_t duration = config.pulseDuration;
                    int underscorePos = subCmd.lastIndexOf('_');
                    if (underscorePos > 7) {
                        duration = subCmd.substring(underscorePos + 1).toInt();
                        if (duration < 10) duration = config.pulseDuration;
                    }
                    modbusPulseRelay(relay, duration);
                    return "OK: Modbus Relay " + String(relay) + " PULSE " + String(duration) + "ms";
                }
            }
        }

        if (subCmd == "all_on") {
            if (modbusSetAllRelays(true)) {
                return "OK: Modbus all relays ON";
            } else {
                return "ERROR: Modbus communication failed";
            }
        }
        if (subCmd == "all_off") {
            if (modbusSetAllRelays(false)) {
                return "OK: Modbus all relays OFF";
            } else {
                return "ERROR: Modbus communication failed";
            }
        }
        if (subCmd.startsWith("all_pulse")) {
            uint16_t duration = config.pulseDuration;
            int underscorePos = subCmd.lastIndexOf('_');
            if (underscorePos > 4) {
                duration = subCmd.substring(underscorePos + 1).toInt();
                if (duration < 10) duration = config.pulseDuration;
            }
            modbusPulseAllRelays(duration);
            return "OK: Modbus all relays PULSE " + String(duration) + "ms";
        }

        return "ERROR: Invalid Modbus command";
    }

    // Modbus scan command
    if (cmd == "modbus_scan") {
        int addr = modbusScanAddress();
        if (addr > 0) {
            return "OK: Modbus device found at address " + String(addr);
        } else {
            return "ERROR: No Modbus device found";
        }
    }

    if (cmd == "status") {
        return getStatusJSON();
    }

    if (cmd == "help") {
        return "Commands: r<1-8>_on/off/pulse, all_on/off/pulse, m1_r<1-8>_on/off/pulse, m1_all_on/off/pulse, modbus_scan, status, help";
    }

    return "ERROR: Unknown command. Type 'help'";
}

// ============================================
// LED Control
// ============================================
void setStatusLED() {
    if (!config.ledEnabled || rgbLed == nullptr) {
        if (rgbLed != nullptr) {
            rgbLed->setPixelColor(0, 0);
            rgbLed->show();
        }
        return;
    }

    uint8_t brightness = config.ledBrightness;
    uint8_t r = 0, g = 0, b = 0;

    if (ethConnected) {
        // Green = Ethernet connected (highest priority)
        g = brightness;
    } else if (wifiSTAConnected) {
        // Cyan = WiFi STA connected
        g = brightness;
        b = brightness;
    } else if (wifiAPActive) {
        // Blue = WiFi AP only
        b = brightness;
    } else {
        // Red = no connection
        r = brightness;
    }

    rgbLed->setPixelColor(0, rgbLed->Color(r, g, b));
    rgbLed->show();
}

void flashEventLED() {
    if (!config.ledEnabled || rgbLed == nullptr) return;

    // Orange flash at ~2.5x status brightness (capped at 255)
    uint8_t eventBrightness = min(255, (int)config.ledBrightness * 5 / 2);
    rgbLed->setPixelColor(0, rgbLed->Color(eventBrightness, eventBrightness / 2, 0));
    rgbLed->show();
    ledEventActive = true;
    ledEventEndTime = millis() + 150;  // 150ms flash
}

void flashInputTcpLED() {
    if (!config.ledEnabled || rgbLed == nullptr) return;

    // Magenta flash — outgoing TCP command triggered by digital input
    uint8_t eventBrightness = min(255, (int)config.ledBrightness * 5 / 2);
    rgbLed->setPixelColor(0, rgbLed->Color(eventBrightness, 0, eventBrightness));
    rgbLed->show();
    ledEventActive = true;
    ledEventEndTime = millis() + 150;  // 150ms flash
}
