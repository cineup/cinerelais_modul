/*
 * MINIMAL TEST - Just to see if the board boots
 */

#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    delay(3000);

    Serial.println("\n\n========================================");
    Serial.println("MINIMAL TEST - ESP32-S3");
    Serial.println("========================================\n");

    pinMode(LED_BUILTIN, OUTPUT);

    Serial.println("Setup complete!");
}

void loop() {
    Serial.println("Loop running...");
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
}
