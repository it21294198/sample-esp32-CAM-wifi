#include <Wire.h>
#include <ArduinoJson.h>
#include <vector>

#define I2C_SLAVE_ADDR 0x08
#define JSON_CAPACITY 512
#define BUTTON_PIN 2

// Function prototypes
void receiveEvent(int numBytes);
void requestEvent();
void startRoverOperation(const std::vector<Coordinates> &coordinatesArray);

// Coordinate structure
struct Coordinates
{
    float x;
    float y;

    // Constructor to initialize coordinates
    Coordinates(float xCoord = 0, float yCoord = 0) : x(xCoord), y(yCoord) {}
};

// Global variables
StaticJsonDocument<JSON_CAPACITY> doc;
char jsonBuffer[JSON_CAPACITY];
volatile size_t expectedLength = 0;
volatile size_t currentIndex = 0;
volatile bool receivingLength = true;
volatile bool newData = false;
volatile byte buttonState = 0;

// Use a vector to store coordinates
std::vector<Coordinates> coordinatesArray;

void setup()
{
    // Initialize I2C communication
    Wire.begin(I2C_SLAVE_ADDR);
    Wire.onReceive(receiveEvent);
    Wire.onRequest(requestEvent);

    // Setup button pin with internal pullup
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Initialize serial communication for debugging
    Serial.begin(115200);
    Serial.println("Arduino I2C Slave initialized");
}

void loop()
{
    // Read button state (invert because of pullup)
    buttonState = !digitalRead(BUTTON_PIN);

    // Clear previous coordinates
    coordinatesArray.clear();

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

                    coordinatesArray.push_back(Coordinates(x, y));
                }

                startRoverOperation(coordinatesArray);
                Serial.println("Rover Task is done");
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
        memset(jsonBuffer, 0, sizeof(jsonBuffer));
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

                // Reset if expected length is too large
                if (expectedLength > JSON_CAPACITY - 1)
                {
                    expectedLength = JSON_CAPACITY - 1;
                }
            }
        }
        else
        {
            // Receive JSON data
            if (currentIndex < expectedLength)
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

void startRoverOperation(const std::vector<Coordinates> &coordinatesArray)
{
    // Process the received coordinates
    for (size_t i = 0; i < coordinatesArray.size(); i++)
    {
        Serial.print("Processing Point ");
        Serial.print(i + 1);
        Serial.print(": X = ");
        Serial.print(coordinatesArray[i].x);
        Serial.print(", Y = ");
        Serial.println(coordinatesArray[i].y);

        // Add your rover control logic here
        // For example:
        // moveRoverToCoordinate(coordinatesArray[i].x, coordinatesArray[i].y);
    }
}