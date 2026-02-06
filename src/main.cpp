/*
 * TEST 6e - Minimal web server, NO JSON in endpoints
 */

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <Adafruit_NeoPixel.h>

#define RGB_LED_PIN 38

// Global variables - CRITICAL: Use pointers!
AsyncWebServer* webServer = nullptr;
Adafruit_NeoPixel* rgbLed = nullptr;

void setup() {
    Serial.begin(115200);
    delay(3000);

    Serial.println("\n\n========================================");
    Serial.println("TEST 6e - Minimal (no JSON endpoints)");
    Serial.println("========================================\n");

    // LittleFS
    Serial.println("1. LittleFS...");
    LittleFS.begin(true);
    Serial.println("   OK");

    // NeoPixel
    Serial.println("2. NeoPixel...");
    rgbLed = new Adafruit_NeoPixel(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
    rgbLed->begin();
    rgbLed->setPixelColor(0, rgbLed->Color(0, 0, 255));
    rgbLed->show();
    Serial.println("   OK (BLUE)");

    // WiFi AP
    Serial.println("3. WiFi AP...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("cinerelais1", "12345678");
    Serial.printf("   IP: %s\n", WiFi.softAPIP().toString().c_str());

    // Web Server - SIMPLE endpoint only
    Serial.println("4. WebServer...");
    webServer = new AsyncWebServer(80);

    webServer->on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "TEST 6e OK");
    });

    ElegantOTA.begin(webServer);
    webServer->begin();
    Serial.println("   OK");

    Serial.println("\n========================================");
    Serial.println("Setup complete!");
    Serial.println("http://192.168.4.1/");
    Serial.println("========================================\n");
}

void loop() {
    ElegantOTA.loop();
    delay(100);
}
