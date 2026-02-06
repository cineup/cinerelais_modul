/*
 * TEST 6b - I2C + TCA9554 Relays (ohne config.h)
 * Basiert auf Test 5 der funktioniert hat
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

// Hardcoded pins (statt config.h)
#define I2C_SDA_PIN         42
#define I2C_SCL_PIN         41
#define TCA9554_ADDR        0x20
#define TCA9554_OUTPUT_REG  0x01
#define TCA9554_CONFIG_REG  0x03
#define RGB_LED_PIN         38

// Global variables - CRITICAL: Use pointers!
AsyncWebServer* webServer = nullptr;
Adafruit_NeoPixel* rgbLed = nullptr;
bool tca9554Found = false;
bool relayStates[8] = {false};

// Forward declarations
void tca9554Init();
void tca9554Write(uint8_t pin, bool state);
void setRelay(int relay, bool state);

void setup() {
    Serial.begin(115200);
    delay(3000);

    Serial.println("\n\n========================================");
    Serial.println("TEST 6b - I2C + TCA9554 Relays");
    Serial.println("========================================\n");

    // Test LittleFS
    Serial.println("1. Testing LittleFS...");
    if (!LittleFS.begin(true)) {
        Serial.println("   ERROR: LittleFS mount failed!");
    } else {
        Serial.println("   LittleFS OK");
    }

    // Test I2C / TCA9554
    Serial.println("2. Testing I2C...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    tca9554Init();

    // Test NeoPixel
    Serial.println("3. Testing NeoPixel...");
    rgbLed = new Adafruit_NeoPixel(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
    rgbLed->begin();
    rgbLed->setPixelColor(0, rgbLed->Color(0, 0, 255));  // Blue
    rgbLed->show();
    Serial.println("   NeoPixel OK (should be BLUE)");

    // Test WiFi AP
    Serial.println("4. Testing WiFi AP...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("cinerelais1", "12345678");
    Serial.printf("   WiFi AP IP: %s\n", WiFi.softAPIP().toString().c_str());

    // Test AsyncWebServer
    Serial.println("5. Creating AsyncWebServer...");
    webServer = new AsyncWebServer(80);

    webServer->on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        JsonDocument doc;
        doc["status"] = "ok";
        doc["test"] = "6b";
        doc["tca9554"] = tca9554Found;
        for (int i = 0; i < 8; i++) {
            doc["relays"][i] = relayStates[i];
        }
        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    });

    webServer->on("/relay", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("n") && request->hasParam("s")) {
            int n = request->getParam("n")->value().toInt();
            int s = request->getParam("s")->value().toInt();
            setRelay(n, s == 1);
            request->send(200, "text/plain", "OK");
        } else {
            request->send(400, "text/plain", "Use /relay?n=1&s=1 or /relay?n=1&s=0");
        }
    });

    ElegantOTA.begin(webServer);
    webServer->begin();
    Serial.println("   Web server OK");

    Serial.println("\n========================================");
    Serial.println("Setup complete!");
    Serial.println("Try http://192.168.4.1/");
    Serial.println("Try http://192.168.4.1/relay?n=1&s=1");
    Serial.println("========================================\n");
}

void loop() {
    ElegantOTA.loop();
    delay(100);
}

// ============================================
// TCA9554 I2C Relay Control
// ============================================

void tca9554Init() {
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(TCA9554_CONFIG_REG);
    Wire.write(0x00);  // All outputs
    uint8_t err = Wire.endTransmission();

    if (err != 0) {
        Serial.printf("   ERROR: TCA9554 not found (error %d)\n", err);
        tca9554Found = false;
        return;
    }

    tca9554Found = true;
    Serial.printf("   TCA9554 found at 0x%02X\n", TCA9554_ADDR);

    // All relays off
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(TCA9554_OUTPUT_REG);
    Wire.write(0x00);
    Wire.endTransmission();
}

void tca9554Write(uint8_t pin, bool state) {
    if (!tca9554Found || pin > 7) return;

    // Read current state
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(TCA9554_OUTPUT_REG);
    Wire.endTransmission();
    Wire.requestFrom((uint8_t)TCA9554_ADDR, (uint8_t)1);
    uint8_t current = Wire.available() ? Wire.read() : 0;

    // Modify
    if (state) {
        current |= (1 << pin);
    } else {
        current &= ~(1 << pin);
    }

    // Write
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(TCA9554_OUTPUT_REG);
    Wire.write(current);
    Wire.endTransmission();
}

void setRelay(int relay, bool state) {
    if (relay < 1 || relay > 8) return;
    int index = relay - 1;
    tca9554Write(index, state);
    relayStates[index] = state;
    Serial.printf("Relay %d: %s\n", relay, state ? "ON" : "OFF");
}
