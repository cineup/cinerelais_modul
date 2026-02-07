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
#include <vector>

// ============================================
// Hardware Pins
// ============================================
#define RGB_LED_PIN 38
#define I2C_SDA_PIN 42
#define I2C_SCL_PIN 41
#define TCA9554_ADDR 0x20
#define TCA9554_OUTPUT_REG 0x01
#define TCA9554_CONFIG_REG 0x03
#define CONFIG_FILE "/config.json"

// W5500 Ethernet (SPI)
#define ETH_MISO_PIN 14
#define ETH_MOSI_PIN 13
#define ETH_SCLK_PIN 15
#define ETH_CS_PIN   16
#define ETH_INT_PIN  12
#define ETH_RST_PIN  -1

const int DI_PINS[8] = {4, 5, 6, 7, 8, 9, 10, 11};

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
};

Config config = {
    "cinerelais1",  // hostname
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
    "CET-1CEST,M3.5.0,M10.5.0/3"  // ntpTimezone (Europe/Berlin)
};

// ============================================
// Global State
// ============================================
AsyncWebServer* webServer = nullptr;
AsyncServer* tcpServer = nullptr;
Adafruit_NeoPixel* rgbLed = nullptr;
std::vector<AsyncClient*> tcpClients;

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
    rgbLed = new Adafruit_NeoPixel(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
    rgbLed->begin();
    rgbLed->setPixelColor(0, rgbLed->Color(0, 0, 50));  // Dim blue during startup
    rgbLed->show();

    // Ethernet
    setupEthernet();

    // WiFi
    setupWiFi();

    // Web Server
    Serial.println("Starting Web Server...");
    webServer = new AsyncWebServer(80);

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

    // API: Restart
    webServer->on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "Restarting...");
        delay(500);
        ESP.restart();
    });

    // API: Log (stub - returns empty for now)
    webServer->on("/api/log", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "[]");
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
    checkWiFiConnection();

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
            break;
        case ARDUINO_EVENT_ETH_LOST_IP:
            ethConnected = false;
            Serial.println("ETH: Lost IP");
            ledUpdateNeeded = true;
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            ethConnected = false;
            Serial.println("ETH: Link Down");
            ledUpdateNeeded = true;
            break;
        case ARDUINO_EVENT_ETH_STOP:
            ethConnected = false;
            Serial.println("ETH: Stopped");
            ledUpdateNeeded = true;
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
    }

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
    } else if (!wifiSTAConnected) {
        wifiSTAConnected = true;
        Serial.printf("WiFi STA reconnected: %s\n", WiFi.localIP().toString().c_str());
        ledUpdateNeeded = true;
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
    doc["ethIP"] = ethConnected ? ETH.localIP().toString() : "";
    doc["ethMAC"] = ETH.linkUp() ? ETH.macAddress() : "";
#else
    doc["ethIP"] = "";
    doc["ethMAC"] = "";
#endif

    // WiFi status
    doc["wifiEnabled"] = config.wifiEnabled;
    doc["wifiSSID"] = config.wifiSSID;
    doc["wifiSTAConnected"] = wifiSTAConnected;
    doc["wifiSTAIP"] = wifiSTAConnected ? WiFi.localIP().toString() : "";
    doc["wifiAPActive"] = wifiAPActive;
    doc["wifiAPIP"] = wifiAPActive ? WiFi.softAPIP().toString() : "";

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

    strlcpy(config.hostname, doc["hostname"] | "cinerelais1", sizeof(config.hostname));
    config.tcpPort = doc["tcpPort"] | 5000;
    config.pulseDuration = doc["pulseDuration"] | 500;

    // Ethernet - use explicit key check for boolean
    config.ethEnabled = doc["ethEnabled"] | true;
    if (doc.containsKey("ethDHCP")) {
        config.ethDHCP = doc["ethDHCP"].as<bool>();
    } else if (doc.containsKey("useDHCP")) {
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

    Serial.printf("Config loaded: hostname=%s, tcpPort=%d, ethDHCP=%d, ledBrightness=%d\n",
                  config.hostname, config.tcpPort, config.ethDHCP, config.ledBrightness);
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
    for (int i = 0; i < 8; i++) {
        if (pulseActive[i] && now >= pulseEndTime[i]) {
            setRelay(i + 1, false);
            pulseActive[i] = false;
        }
    }
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

    if (cmd == "status") {
        return getStatusJSON();
    }

    if (cmd == "help") {
        return "Commands: r<1-8>_on, r<1-8>_off, r<1-8>_pulse[_ms], all_on, all_off, all_pulse[_ms], status, help";
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
        Serial.printf("LED: Green (ethConnected=%d)\n", ethConnected);
    } else if (wifiSTAConnected) {
        // Cyan = WiFi STA connected
        g = brightness;
        b = brightness;
        Serial.println("LED: Cyan (wifiSTAConnected)");
    } else if (wifiAPActive) {
        // Blue = WiFi AP only
        b = brightness;
        Serial.println("LED: Blue (wifiAPActive)");
    } else {
        // Red = no connection
        r = brightness;
        Serial.printf("LED: Red (eth=%d, sta=%d, ap=%d)\n", ethConnected, wifiSTAConnected, wifiAPActive);
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
