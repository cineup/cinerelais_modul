/*
 * TEST 4 - WiFi + LittleFS + AsyncWebServer + NeoPixel
 */

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_NeoPixel.h>

#define RGB_LED_PIN 38

// Create as pointers (not global objects!)
AsyncWebServer* webServer = nullptr;
Adafruit_NeoPixel* rgbLed = nullptr;

void setup() {
    Serial.begin(115200);
    delay(3000);

    Serial.println("\n\n========================================");
    Serial.println("TEST 4 - WiFi + LittleFS + AsyncWebServer + NeoPixel");
    Serial.println("========================================\n");

    // Test LittleFS
    Serial.println("Testing LittleFS...");
    if (!LittleFS.begin(true)) {
        Serial.println("ERROR: LittleFS mount failed!");
    } else {
        Serial.println("LittleFS OK");
    }

    // Test NeoPixel
    Serial.println("Testing NeoPixel...");
    rgbLed = new Adafruit_NeoPixel(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
    rgbLed->begin();
    rgbLed->setPixelColor(0, rgbLed->Color(0, 255, 0));  // Green
    rgbLed->show();
    Serial.println("NeoPixel OK (should be GREEN)");

    // Test WiFi AP
    Serial.println("Testing WiFi AP...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("TestAP", "12345678");
    Serial.printf("WiFi AP IP: %s\n", WiFi.softAPIP().toString().c_str());

    // Test AsyncWebServer
    Serial.println("Creating AsyncWebServer...");
    webServer = new AsyncWebServer(80);

    webServer->on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "Hello from ESP32-S3!");
    });

    webServer->begin();
    Serial.println("Web server started on port 80");

    Serial.println("\nSetup complete!");
}

void loop() {
    delay(1000);
}
