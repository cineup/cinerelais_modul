/*
 * TEST 6d - NO I2C at all
 * Remove Wire completely to confirm it's the problem
 */

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <Adafruit_NeoPixel.h>

// Hardcoded pins
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
    Serial.println("TEST 6d - NO I2C at all");
    Serial.println("========================================\n");

    // Test LittleFS
    Serial.println("1. Testing LittleFS...");
    if (!LittleFS.begin(true)) {
        Serial.println("   ERROR: LittleFS mount failed!");
    } else {
        Serial.println("   LittleFS OK");
    }

    // NO I2C - skipped
    Serial.println("2. I2C SKIPPED");

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
        doc["test"] = "6d";
        doc["note"] = "NO I2C - dummy relays only";
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
            request->send(200, "text/plain", "OK (dummy)");
        } else {
            request->send(400, "text/plain", "Use /relay?n=1&s=1");
        }
    });

    ElegantOTA.begin(webServer);
    webServer->begin();
    Serial.println("   Web server OK");

    Serial.println("\n========================================");
    Serial.println("Setup complete! NO I2C in this test.");
    Serial.println("Try http://192.168.4.1/");
    Serial.println("========================================\n");
}

void loop() {
    ElegantOTA.loop();
    delay(100);
}

// DUMMY relay function
void setRelay(int relay, bool state) {
    if (relay < 1 || relay > 8) return;
    relayStates[relay - 1] = state;
    Serial.printf("DUMMY Relay %d: %s\n", relay, state ? "ON" : "OFF");
}
