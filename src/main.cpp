/*
 * CineRelais Controller - Full Version
 * WiFi AP/STA, Web Interface, TCP Server, Relay Control
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
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

const int DI_PINS[8] = {4, 5, 6, 7, 8, 9, 10, 11};

// ============================================
// Configuration
// ============================================
struct Config {
    char hostname[32];
    uint16_t tcpPort;
    uint16_t pulseDuration;

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
};

Config config = {
    "cinerelais1",  // hostname
    5000,           // tcpPort
    500,            // pulseDuration
    true,           // wifiEnabled
    true,           // wifiAPEnabled
    "",             // wifiSSID
    "",             // wifiPassword
    "",             // wifiAPPassword (empty = open)
    true,           // wifiDHCP
    "192.168.4.100",// wifiIP
    "192.168.4.1",  // wifiGateway
    "255.255.255.0",// wifiSubnet
    "8.8.8.8"       // wifiDNS
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

// WiFi state
bool wifiSTAConnected = false;
bool wifiAPActive = false;
unsigned long lastWiFiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 30000;

// ============================================
// Forward Declarations
// ============================================
void loadConfig();
void saveConfig();
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

    // Set LED based on connection status
    if (wifiSTAConnected) {
        rgbLed->setPixelColor(0, rgbLed->Color(0, 255, 255));  // Cyan = STA connected
    } else if (wifiAPActive) {
        rgbLed->setPixelColor(0, rgbLed->Color(0, 0, 255));    // Blue = AP only
    } else {
        rgbLed->setPixelColor(0, rgbLed->Color(255, 0, 0));    // Red = no connection
    }
    rgbLed->show();

    Serial.println("\n========================================");
    Serial.println("Setup complete!");
    Serial.printf("Hostname: %s\n", config.hostname);
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
    delay(10);
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
        rgbLed->setPixelColor(0, rgbLed->Color(0, 0, 255));  // Blue during reconnect
        rgbLed->show();
    }

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
    } else if (!wifiSTAConnected) {
        wifiSTAConnected = true;
        Serial.printf("WiFi STA reconnected: %s\n", WiFi.localIP().toString().c_str());
        rgbLed->setPixelColor(0, rgbLed->Color(0, 255, 255));  // Cyan
        rgbLed->show();
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

    // WiFi status
    doc["wifiEnabled"] = config.wifiEnabled;
    doc["wifiSSID"] = config.wifiSSID;
    doc["wifiSTAConnected"] = wifiSTAConnected;
    doc["wifiSTAIP"] = wifiSTAConnected ? WiFi.localIP().toString() : "";
    doc["wifiAPActive"] = wifiAPActive;
    doc["wifiAPIP"] = wifiAPActive ? WiFi.softAPIP().toString() : "";

    // IP for header
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

    Serial.printf("Config loaded: hostname=%s, tcpPort=%d\n", config.hostname, config.tcpPort);
}

void saveConfig() {
    JsonDocument doc;
    doc["hostname"] = config.hostname;
    doc["tcpPort"] = config.tcpPort;
    doc["pulseDuration"] = config.pulseDuration;
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
