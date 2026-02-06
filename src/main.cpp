/*
 * TEST 6c - I2C init only, NO TCA9554 communication
 * Test if Wire.begin() or TCA9554 commands cause crash
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

// Hardcoded pins
#define I2C_SDA_PIN         42
#define I2C_SCL_PIN         41
#define RGB_LED_PIN         38

// Global variables - CRITICAL: Use pointers!
AsyncWebServer* webServer = nullptr;
Adafruit_NeoPixel* rgbLed = nullptr;
bool relayStates[8] = {false};

// Forward declarations
void setRelay(int relay, bool state);

void setup() {
    Serial.begin(115200);
    delay(3000);

    Serial.println("\n\n========================================");
    Serial.println("TEST 6c - I2C init only (no TCA9554)");
    Serial.println("========================================\n");

    // Test LittleFS
    Serial.println("1. Testing LittleFS...");
    if (!LittleFS.begin(true)) {
        Serial.println("   ERROR: LittleFS mount failed!");
    } else {
        Serial.println("   LittleFS OK");
    }

    // Test I2C init ONLY (no communication)
    Serial.println("2. Testing I2C init...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Serial.println("   Wire.begin() OK");

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
        doc["test"] = "6c";
        doc["note"] = "TCA9554 disabled - dummy relays only";
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
            request->send(200, "text/plain", "OK (dummy - no real relay)");
        } else {
            request->send(400, "text/plain", "Use /relay?n=1&s=1 or /relay?n=1&s=0");
        }
    });

    ElegantOTA.begin(webServer);
    webServer->begin();
    Serial.println("   Web server OK");

    Serial.println("\n========================================");
    Serial.println("Setup complete!");
    Serial.println("TCA9554 DISABLED - relays are dummy only");
    Serial.println("Try http://192.168.4.1/");
    Serial.println("========================================\n");
}

void loop() {
    ElegantOTA.loop();
    delay(100);
}

// DUMMY relay function - no I2C communication
void setRelay(int relay, bool state) {
    if (relay < 1 || relay > 8) return;
    int index = relay - 1;
    relayStates[index] = state;
    Serial.printf("DUMMY Relay %d: %s (no real output)\n", relay, state ? "ON" : "OFF");
}
