/*
 * TEST 2 - WiFi + LittleFS
 */

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>

void setup() {
    Serial.begin(115200);
    delay(3000);

    Serial.println("\n\n========================================");
    Serial.println("TEST 2 - WiFi + LittleFS");
    Serial.println("========================================\n");

    // Test LittleFS
    Serial.println("Testing LittleFS...");
    if (!LittleFS.begin(true)) {
        Serial.println("ERROR: LittleFS mount failed!");
    } else {
        Serial.println("LittleFS OK");
    }

    // Test WiFi AP
    Serial.println("Testing WiFi AP...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("TestAP", "12345678");
    Serial.printf("WiFi AP IP: %s\n", WiFi.softAPIP().toString().c_str());

    Serial.println("\nSetup complete!");
}

void loop() {
    Serial.println("Loop running...");
    delay(2000);
}
