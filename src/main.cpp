/*
 * TEST 5 - All components except Ethernet
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

// Create as pointers (not global objects!)
AsyncWebServer* webServer = nullptr;
Adafruit_NeoPixel* rgbLed = nullptr;

void setup() {
    Serial.begin(115200);
    delay(3000);

    Serial.println("\n\n========================================");
    Serial.println("TEST 5 - All components except Ethernet");
    Serial.println("========================================\n");

    // Test LittleFS
    Serial.println("Testing LittleFS...");
    if (!LittleFS.begin(true)) {
        Serial.println("ERROR: LittleFS mount failed!");
    } else {
        Serial.println("LittleFS OK");
    }

    // Test I2C
    Serial.println("Testing I2C...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.beginTransmission(TCA9554_ADDR);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
        Serial.printf("TCA9554 found at 0x%02X\n", TCA9554_ADDR);
    } else {
        Serial.printf("TCA9554 NOT found (error %d)\n", err);
    }

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
        doc["test"] = 5;
        doc["message"] = "All components working!";
        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    });

    // Test ElegantOTA
    ElegantOTA.begin(webServer);
    Serial.println("ElegantOTA OK - available at /update");

    webServer->begin();
    Serial.println("Web server started on port 80");

    Serial.println("\n========================================");
    Serial.println("Setup complete!");
    Serial.println("Try http://192.168.4.1/ for JSON");
    Serial.println("Try http://192.168.4.1/update for OTA");
    Serial.println("========================================\n");
}

void loop() {
    ElegantOTA.loop();
    delay(100);
}
