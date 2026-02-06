/*
 * TEST 7 - Add TCP Command Server
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

#define RGB_LED_PIN 38
#define I2C_SDA_PIN 42
#define I2C_SCL_PIN 41
#define TCA9554_ADDR 0x20
#define TCA9554_OUTPUT_REG 0x01
#define TCA9554_CONFIG_REG 0x03
#define TCP_PORT 5000

// Create as pointers (not global objects!)
AsyncWebServer* webServer = nullptr;
AsyncServer* tcpServer = nullptr;
Adafruit_NeoPixel* rgbLed = nullptr;

// TCP clients
std::vector<AsyncClient*> tcpClients;

// Relay state
bool tca9554Found = false;
bool relayStates[8] = {false};
uint8_t relayRegister = 0x00;

// Forward declarations
void tca9554Init();
void setRelay(int relay, bool state);
void setAllRelays(bool state);
void setupTcpServer();
String processCommand(const String& cmd);

void setup() {
    Serial.begin(115200);
    delay(3000);

    Serial.println("\n\n========================================");
    Serial.println("TEST 7 - TCP Command Server");
    Serial.println("========================================\n");

    // Test LittleFS
    Serial.println("Testing LittleFS...");
    if (!LittleFS.begin(true)) {
        Serial.println("ERROR: LittleFS mount failed!");
    } else {
        Serial.println("LittleFS OK");
    }

    // Test I2C + TCA9554
    Serial.println("Testing I2C + TCA9554...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    tca9554Init();

    // Test NeoPixel
    Serial.println("Testing NeoPixel...");
    rgbLed = new Adafruit_NeoPixel(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
    rgbLed->begin();
    rgbLed->setPixelColor(0, rgbLed->Color(0, 255, 0));  // Green
    rgbLed->show();
    Serial.println("NeoPixel OK");

    // Test WiFi AP
    Serial.println("Testing WiFi AP...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("TestAP", "12345678");
    Serial.printf("WiFi AP IP: %s\n", WiFi.softAPIP().toString().c_str());

    // Test AsyncWebServer + ArduinoJson
    Serial.println("Creating AsyncWebServer...");
    webServer = new AsyncWebServer(80);

    webServer->on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        JsonDocument doc;
        doc["status"] = "ok";
        doc["test"] = 6;
        doc["tca9554"] = tca9554Found;
        for (int i = 0; i < 8; i++) {
            doc["relays"][i] = relayStates[i];
        }
        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    });

    // Relay control: /relay?n=1&s=1 (on) or /relay?n=1&s=0 (off)
    webServer->on("/relay", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("n") && request->hasParam("s")) {
            int n = request->getParam("n")->value().toInt();
            int s = request->getParam("s")->value().toInt();
            if (n >= 1 && n <= 8) {
                setRelay(n, s == 1);
                request->send(200, "text/plain", "OK: Relay " + String(n) + (s ? " ON" : " OFF"));
            } else {
                request->send(400, "text/plain", "ERROR: Relay must be 1-8");
            }
        } else {
            request->send(400, "text/plain", "Usage: /relay?n=1&s=1 or /relay?n=1&s=0");
        }
    });

    // All relays: /relays?s=1 (all on) or /relays?s=0 (all off)
    webServer->on("/relays", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("s")) {
            int s = request->getParam("s")->value().toInt();
            setAllRelays(s == 1);
            request->send(200, "text/plain", s ? "OK: All relays ON" : "OK: All relays OFF");
        } else {
            request->send(400, "text/plain", "Usage: /relays?s=1 or /relays?s=0");
        }
    });

    // Test ElegantOTA
    ElegantOTA.begin(webServer);
    Serial.println("ElegantOTA OK - available at /update");

    webServer->begin();
    Serial.println("Web server started on port 80");

    // TCP Command Server
    Serial.println("Starting TCP server...");
    setupTcpServer();
    Serial.printf("TCP server started on port %d\n", TCP_PORT);

    Serial.println("\n========================================");
    Serial.println("Setup complete!");
    Serial.println("HTTP: http://192.168.4.1/");
    Serial.printf("TCP:  nc 192.168.4.1 %d\n", TCP_PORT);
    Serial.println("Commands: r1_on, r1_off, all_on, all_off, status, help");
    Serial.println("========================================\n");
}

void loop() {
    ElegantOTA.loop();
    delay(100);
}

// ============================================
// TCA9554 Relay Control
// ============================================

void tca9554Init() {
    // Configure all pins as outputs
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(TCA9554_CONFIG_REG);
    Wire.write(0x00);  // All outputs
    uint8_t err = Wire.endTransmission();

    if (err != 0) {
        Serial.printf("ERROR: TCA9554 not found (error %d)\n", err);
        tca9554Found = false;
        return;
    }

    tca9554Found = true;
    Serial.printf("TCA9554 found at 0x%02X\n", TCA9554_ADDR);

    // Turn all relays off
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

// ============================================
// TCP Command Server
// ============================================

void setupTcpServer() {
    tcpServer = new AsyncServer(TCP_PORT);

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
            Serial.printf("TCP client disconnected\n");
            for (auto it = tcpClients.begin(); it != tcpClients.end(); ++it) {
                if (*it == c) {
                    tcpClients.erase(it);
                    break;
                }
            }
        }, nullptr);

    }, nullptr);

    tcpServer->begin();
}

String processCommand(const String& cmd) {
    // r<1-8>_on / r<1-8>_off
    if (cmd.startsWith("r") && cmd.length() >= 4) {
        int relay = cmd.substring(1, 2).toInt();
        if (relay >= 1 && relay <= 8) {
            if (cmd.indexOf("_on") > 0) {
                setRelay(relay, true);
                return "OK: Relay " + String(relay) + " ON";
            } else if (cmd.indexOf("_off") > 0) {
                setRelay(relay, false);
                return "OK: Relay " + String(relay) + " OFF";
            }
        }
    }

    // all_on / all_off
    if (cmd == "all_on") {
        setAllRelays(true);
        return "OK: All relays ON";
    }
    if (cmd == "all_off") {
        setAllRelays(false);
        return "OK: All relays OFF";
    }

    // status
    if (cmd == "status") {
        JsonDocument doc;
        doc["tca9554"] = tca9554Found;
        for (int i = 0; i < 8; i++) {
            doc["relays"][i] = relayStates[i];
        }
        String output;
        serializeJson(doc, output);
        return output;
    }

    // help
    if (cmd == "help") {
        return "Commands: r<1-8>_on, r<1-8>_off, all_on, all_off, status, help";
    }

    return "ERROR: Unknown command. Type 'help'";
}
