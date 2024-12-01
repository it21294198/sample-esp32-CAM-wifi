#include <Wire.h>
#include <ArduinoJson.h>

#define I2C_SLAVE_ADDR 0x08
#define JSON_CAPACITY 512
#define BUTTON_PIN 2

StaticJsonDocument<JSON_CAPACITY> doc;
char jsonBuffer[JSON_CAPACITY];
volatile size_t expectedLength = 0;
volatile size_t currentIndex = 0;
volatile bool receivingLength = true;
volatile bool newData = false;
volatile byte buttonState = 0;

void setup()
{
    Wire.begin(I2C_SLAVE_ADDR);
    Wire.onReceive(receiveEvent);
    Wire.onRequest(requestEvent);

    // Setup button pin with internal pullup
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    Serial.begin(115200);
    Serial.println("Arduino I2C Slave initialized");
}

void loop()
{
    // Read button state (invert because of pullup)
    buttonState = !digitalRead(BUTTON_PIN);

    if (newData)
    {
        // Try to parse JSON
        DeserializationError error = deserializeJson(doc, jsonBuffer);

        if (!error)
        {
            Serial.println("Detected Points:");

            // Check if "points" key exists and is an array
            if (doc.containsKey("points") && doc["points"].is<JsonArray>())
            {
                JsonArray points = doc["points"].as<JsonArray>();

                // Iterate through points
                int pointCount = 0;
                for (JsonVariant pointVar : points)
                {
                    pointCount++;
                    float x = pointVar["x"].as<float>();
                    float y = pointVar["y"].as<float>();

                    Serial.print("Point ");
                    Serial.print(pointCount);
                    Serial.print(": x = ");
                    Serial.print(x);
                    Serial.print(", y = ");
                    Serial.println(y);
                }
            }
        }
        else
        {
            Serial.print("JSON parsing failed: ");
            Serial.println(error.c_str());
        }

        // Reset for next reception
        currentIndex = 0;
        receivingLength = true;
        newData = false;
        memset(jsonBuffer, 0, JSON_CAPACITY);
    }

    delay(50);
}

void receiveEvent(int numBytes)
{
    while (Wire.available())
    {
        if (receivingLength)
        {
            // First receive the expected length (2 bytes)
            byte lowByte = Wire.read();
            if (Wire.available())
            {
                byte highByte = Wire.read();
                expectedLength = (highByte << 8) | lowByte;
                receivingLength = false;
            }
        }
        else
        {
            // Receive JSON data
            if (currentIndex < JSON_CAPACITY - 1)
            {
                jsonBuffer[currentIndex] = Wire.read();
                currentIndex++;

                if (currentIndex >= expectedLength)
                {
                    jsonBuffer[currentIndex] = '\0'; // Null terminate
                    newData = true;
                }
            }
        }
    }
}

void requestEvent()
{
    // Send the button state when master requests it
    Wire.write(buttonState);
}