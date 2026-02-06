/*
 * TEST 6 - Add TCA9554 Relay Control
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

#define RGB_LED_PIN 38
#define I2C_SDA_PIN 42
#define I2C_SCL_PIN 41
#define TCA9554_ADDR 0x20
#define TCA9554_OUTPUT_REG 0x01
#define TCA9554_CONFIG_REG 0x03

// Create as pointers (not global objects!)
AsyncWebServer* webServer = nullptr;
Adafruit_NeoPixel* rgbLed = nullptr;

// Relay state
bool tca9554Found = false;
bool relayStates[8] = {false};
uint8_t relayRegister = 0x00;

// Forward declarations
void tca9554Init();
void setRelay(int relay, bool state);
void setAllRelays(bool state);

void setup() {
    Serial.begin(115200);
    delay(3000);

    Serial.println("\n\n========================================");
    Serial.println("TEST 6 - TCA9554 Relay Control");
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

    Serial.println("\n========================================");
    Serial.println("Setup complete!");
    Serial.println("http://192.168.4.1/          - Status JSON");
    Serial.println("http://192.168.4.1/relay?n=1&s=1 - Relay 1 ON");
    Serial.println("http://192.168.4.1/relay?n=1&s=0 - Relay 1 OFF");
    Serial.println("http://192.168.4.1/relays?s=1    - All ON");
    Serial.println("http://192.168.4.1/relays?s=0    - All OFF");
    Serial.println("http://192.168.4.1/update    - OTA Update");
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
