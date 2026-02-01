/*
 * ESP32-S3-POE-ETH-8DI-8RO Relay Controller
 * 
 * Features:
 * - DHCP (default) or Static IP configuration
 * - Web Interface for configuration and control
 * - TCP command interface for relay control
 * - OTA firmware updates via web
 * - 8 Digital Inputs monitoring
 * - 8 Relay Outputs with ON/OFF/PULSE commands
 * 
 * TCP Commands (Port configurable, default 5000):
 *   r1_on          - Turn relay 1 on (r1-r8)
 *   r1_off         - Turn relay 1 off (r1-r8)
 *   r1_impuls      - Pulse relay 1 with default duration (r1-r8)
 *   r1_impuls_1000 - Pulse relay 1 for 1000ms (r1-r8)
 *   all_on         - Turn all relays on
 *   all_off        - Turn all relays off
 *   all_impuls     - Pulse all relays with default duration
 *   all_impuls_500 - Pulse all relays for 500ms
 *   status         - Get status of all relays and inputs
 *   help           - Show available commands
 */

#include <Arduino.h>
#include <SPI.h>
#include <ETH.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
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

unsigned long pulseEndTime[8] = {0};
bool pulseActive[8] = {false};

// ============================================
// Forward Declarations
// ============================================

void loadConfig();
void saveConfig();
void setupPins();
void setupEthernet();
void setupWebServer();
void setupTCPServer();
void handleTCPCommand(AsyncClient* client, String command);
String getStatusJSON();
void setRelay(int relay, bool state);
void pulseRelay(int relay, unsigned long duration);
void updatePulses();
void WiFiEvent(WiFiEvent_t event);

// ============================================
// Setup
// ============================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n========================================");
    Serial.println("ESP32-S3-POE-ETH-8DI-8RO Relay Controller");
    Serial.println("========================================\n");
    
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
    
    // Setup Ethernet
    setupEthernet();
    
    // Wait for Ethernet connection
    Serial.println("Waiting for Ethernet connection...");
    unsigned long startTime = millis();
    while (!ethConnected && (millis() - startTime < 10000)) {
        delay(100);
    }
    
    if (ethConnected) {
        // Setup Web Server
        setupWebServer();
        
        // Setup TCP Server
        setupTCPServer();
        
        Serial.println("\n========================================");
        Serial.println("System Ready!");
        Serial.printf("IP Address: %s\n", ETH.localIP().toString().c_str());
        Serial.printf("Web Interface: http://%s\n", ETH.localIP().toString().c_str());
        Serial.printf("TCP Port: %d\n", config.tcpPort);
        Serial.println("========================================\n");
    } else {
        Serial.println("ERROR: No Ethernet connection!");
    }
}

// ============================================
// Main Loop
// ============================================

void loop() {
    // Update pulse timers
    updatePulses();
    
    // Read input states
    for (int i = 0; i < 8; i++) {
        inputStates[i] = !digitalRead(DI_PINS[i]); // Active LOW
    }
    
    // ElegantOTA loop
    ElegantOTA.loop();
    
    delay(10);
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
    
    config.useDHCP = doc["useDHCP"] | DEFAULT_CONFIG.useDHCP;
    strlcpy(config.staticIP, doc["staticIP"] | DEFAULT_CONFIG.staticIP, sizeof(config.staticIP));
    strlcpy(config.gateway, doc["gateway"] | DEFAULT_CONFIG.gateway, sizeof(config.gateway));
    strlcpy(config.subnet, doc["subnet"] | DEFAULT_CONFIG.subnet, sizeof(config.subnet));
    strlcpy(config.dns, doc["dns"] | DEFAULT_CONFIG.dns, sizeof(config.dns));
    strlcpy(config.hostname, doc["hostname"] | DEFAULT_CONFIG.hostname, sizeof(config.hostname));
    config.tcpPort = doc["tcpPort"] | DEFAULT_CONFIG.tcpPort;
    config.pulseDuration = doc["pulseDuration"] | DEFAULT_CONFIG.pulseDuration;
    
    Serial.println("Configuration loaded:");
    Serial.printf("  DHCP: %s\n", config.useDHCP ? "enabled" : "disabled");
    Serial.printf("  Hostname: %s\n", config.hostname);
    Serial.printf("  TCP Port: %d\n", config.tcpPort);
}

void saveConfig() {
    Serial.println("Saving configuration...");
    
    JsonDocument doc;
    doc["useDHCP"] = config.useDHCP;
    doc["staticIP"] = config.staticIP;
    doc["gateway"] = config.gateway;
    doc["subnet"] = config.subnet;
    doc["dns"] = config.dns;
    doc["hostname"] = config.hostname;
    doc["tcpPort"] = config.tcpPort;
    doc["pulseDuration"] = config.pulseDuration;
    
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
    
    // Setup Digital Inputs
    for (int i = 0; i < 8; i++) {
        pinMode(DI_PINS[i], INPUT);
    }
    
    // Setup Relay Outputs
    for (int i = 0; i < 8; i++) {
        pinMode(RO_PINS[i], OUTPUT);
        digitalWrite(RO_PINS[i], LOW);
        relayStates[i] = false;
    }
    
    // Status LED
    #ifdef STATUS_LED_PIN
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    #endif
    
    Serial.println("GPIO pins configured");
}

// ============================================
// Ethernet Setup
// ============================================

void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            Serial.println("ETH Started");
            ETH.setHostname(config.hostname);
            break;
        case ARDUINO_EVENT_ETH_CONNECTED:
            Serial.println("ETH Connected");
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.printf("ETH Got IP: %s\n", ETH.localIP().toString().c_str());
            Serial.printf("  Gateway: %s\n", ETH.gatewayIP().toString().c_str());
            Serial.printf("  Subnet: %s\n", ETH.subnetMask().toString().c_str());
            ethConnected = true;
            #ifdef STATUS_LED_PIN
            digitalWrite(STATUS_LED_PIN, HIGH);
            #endif
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("ETH Disconnected");
            ethConnected = false;
            #ifdef STATUS_LED_PIN
            digitalWrite(STATUS_LED_PIN, LOW);
            #endif
            break;
        case ARDUINO_EVENT_ETH_STOP:
            Serial.println("ETH Stopped");
            ethConnected = false;
            break;
        default:
            break;
    }
}

void setupEthernet() {
    Serial.println("Setting up Ethernet...");
    
    WiFi.onEvent(WiFiEvent);
    
    // Initialize Ethernet with W5500
    SPI.begin(ETH_SCLK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN, ETH_CS_PIN);
    
    if (!ETH.begin(ETH_PHY_W5500, 1, ETH_CS_PIN, ETH_INT_PIN, ETH_RST_PIN, ETH_SPI_HOST)) {
        Serial.println("ERROR: ETH.begin() failed!");
        return;
    }
    
    if (!config.useDHCP) {
        Serial.println("Configuring static IP...");
        IPAddress ip, gateway, subnet, dns;
        ip.fromString(config.staticIP);
        gateway.fromString(config.gateway);
        subnet.fromString(config.subnet);
        dns.fromString(config.dns);
        ETH.config(ip, gateway, subnet, dns);
    }
    
    Serial.println("Ethernet initialized");
}

// ============================================
// Relay Control
// ============================================

void setRelay(int relay, bool state) {
    if (relay < 1 || relay > 8) return;
    
    int index = relay - 1;
    digitalWrite(RO_PINS[index], state ? HIGH : LOW);
    relayStates[index] = state;
    
    // Cancel any active pulse
    pulseActive[index] = false;
    pulseEndTime[index] = 0;
    
    Serial.printf("Relay %d: %s\n", relay, state ? "ON" : "OFF");
}

void setAllRelays(bool state) {
    for (int i = 1; i <= 8; i++) {
        setRelay(i, state);
    }
}

void pulseRelay(int relay, unsigned long duration) {
    if (relay < 1 || relay > 8) return;
    
    int index = relay - 1;
    digitalWrite(RO_PINS[index], HIGH);
    relayStates[index] = true;
    pulseActive[index] = true;
    pulseEndTime[index] = millis() + duration;
    
    Serial.printf("Relay %d: PULSE (%lu ms)\n", relay, duration);
}

void pulseAllRelays(unsigned long duration) {
    for (int i = 1; i <= 8; i++) {
        pulseRelay(i, duration);
    }
}

void updatePulses() {
    unsigned long now = millis();
    for (int i = 0; i < 8; i++) {
        if (pulseActive[i] && now >= pulseEndTime[i]) {
            digitalWrite(RO_PINS[i], LOW);
            relayStates[i] = false;
            pulseActive[i] = false;
            pulseEndTime[i] = 0;
            Serial.printf("Relay %d: PULSE ended\n", i + 1);
        }
    }
}

// ============================================
// Status JSON
// ============================================

String getStatusJSON() {
    JsonDocument doc;
    
    // Network info
    doc["ip"] = ETH.localIP().toString();
    doc["mac"] = ETH.macAddress();
    doc["hostname"] = config.hostname;
    doc["dhcp"] = config.useDHCP;
    doc["tcpPort"] = config.tcpPort;
    doc["pulseDuration"] = config.pulseDuration;
    doc["uptime"] = millis() / 1000;
    
    // Static IP config
    doc["staticIP"] = config.staticIP;
    doc["gateway"] = config.gateway;
    doc["subnet"] = config.subnet;
    doc["dns"] = config.dns;
    
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
    
    Serial.printf("TCP [%s]: %s\n", client->remoteIP().toString().c_str(), command.c_str());
    
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
    client->write("ESP32-S3 Relay Controller\r\nType help for commands\r\n");
}

void handleTCPCommand(AsyncClient* client, String command) {
    String response = "";

    if (command.startsWith("r") && command.length() >= 4) {
        // Parse relay number: r1_on, r8_off, r3_impuls, r3_impuls_1000
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
            } else if (action == "impuls") {
                pulseRelay(relay, config.pulseDuration);
                response = "OK: r" + String(relay) + " impuls (" + String(config.pulseDuration) + "ms)\r\n";
            } else if (action.startsWith("impuls_")) {
                unsigned long duration = action.substring(7).toInt();
                if (duration == 0) duration = config.pulseDuration;
                pulseRelay(relay, duration);
                response = "OK: r" + String(relay) + " impuls (" + String(duration) + "ms)\r\n";
            } else {
                response = "ERROR: Unknown action. Use on, off, impuls, impuls_<ms>\r\n";
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
        } else if (action == "impuls") {
            pulseAllRelays(config.pulseDuration);
            response = "OK: all impuls (" + String(config.pulseDuration) + "ms)\r\n";
        } else if (action.startsWith("impuls_")) {
            unsigned long duration = action.substring(7).toInt();
            if (duration == 0) duration = config.pulseDuration;
            pulseAllRelays(duration);
            response = "OK: all impuls (" + String(duration) + "ms)\r\n";
        } else {
            response = "ERROR: Unknown action. Use on, off, impuls, impuls_<ms>\r\n";
        }
    }
    else if (command == "status") {
        response = getStatusJSON() + "\r\n";
    }
    else if (command == "help") {
        response = "Available commands:\r\n";
        response += "  r<1-8>_on          - Turn relay on\r\n";
        response += "  r<1-8>_off         - Turn relay off\r\n";
        response += "  r<1-8>_impuls      - Pulse relay (default duration)\r\n";
        response += "  r<1-8>_impuls_<ms> - Pulse relay for <ms> milliseconds\r\n";
        response += "  all_on             - Turn all relays on\r\n";
        response += "  all_off            - Turn all relays off\r\n";
        response += "  all_impuls         - Pulse all relays (default duration)\r\n";
        response += "  all_impuls_<ms>    - Pulse all relays for <ms> milliseconds\r\n";
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
            
            if (relay >= 1 && relay <= 8) {
                if (state == "on") {
                    setRelay(relay, true);
                } else if (state == "off") {
                    setRelay(relay, false);
                } else if (state == "pulse") {
                    unsigned long duration = config.pulseDuration;
                    if (request->hasParam("duration", true)) {
                        duration = request->getParam("duration", true)->value().toInt();
                    }
                    pulseRelay(relay, duration);
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
            if (state == "on") {
                setAllRelays(true);
            } else if (state == "off") {
                setAllRelays(false);
            } else if (state == "pulse") {
                unsigned long duration = config.pulseDuration;
                if (request->hasParam("duration", true)) {
                    duration = request->getParam("duration", true)->value().toInt();
                }
                pulseAllRelays(duration);
            }
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(400, "application/json", "{\"error\":\"Missing state parameter\"}");
        }
    });
    
    // API: Get config
    webServer.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["useDHCP"] = config.useDHCP;
        doc["staticIP"] = config.staticIP;
        doc["gateway"] = config.gateway;
        doc["subnet"] = config.subnet;
        doc["dns"] = config.dns;
        doc["hostname"] = config.hostname;
        doc["tcpPort"] = config.tcpPort;
        doc["pulseDuration"] = config.pulseDuration;
        
        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    });
    
    // API: Save config
    webServer.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool changed = false;
        
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
    
    // Setup ElegantOTA
    ElegantOTA.begin(&webServer);
    
    // Handle 404
    webServer.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not Found");
    });
    
    webServer.begin();
    Serial.println("Web server started on port 80");
}
