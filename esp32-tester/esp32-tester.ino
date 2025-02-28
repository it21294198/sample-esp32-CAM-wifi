/*
ESP32 (Master)     Arduino (Slave)
SDA (PIN 15) ----- SDA (A4/SDA)
SCL (PIN 14) ----- SCL (A5/SCL)
GND -------------- GND
*/

// Comment for test to production version
#define SERIAL_DEBUG 1

// Communication libs
#include <Wire.h>
// Add ArduinoJson library
#include <ArduinoJson.h>

// Temp and Humidity capture
#include "DHT.h"

// Pin where the DHT sensor is connected
#define DHTPIN 2
// Define the DHT sensor type (DHT11, DHT22, etc.)
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);

// I2C settings
#define I2C_SLAVE_ADDR 0x08
#define SDA_PIN 15
#define SCL_PIN 14
#define JSON_CAPACITY 512

// whole rover status
bool running = false;

// I2C connected board status
int roverCurrentState = 0;

// Camera capture and upload function
bool getArrayData()
{
    // Create a JSON document for parsing
    StaticJsonDocument<JSON_CAPACITY> doc;

    // Sample JSON string that would come from API response
    const char *jsonString = "{\
        \"status\": 200,\
        \"imageResult\": [\
            {\
            \"x\": 0.02,\
            \"y\": 0.1000,\
            \"confidence\": 0.7\
            },\
            {\
            \"x\": 0.03,\
            \"y\": 0.2000,\
            \"confidence\": 0.59\
            },\
            {\
            \"x\": 0.05,\
            \"y\": 0.3000,\
            \"confidence\": 0.87\
            },\
            {\
            \"x\": 0.07,\
            \"y\": 0.3500,\
            \"confidence\": 0.71\
            }\
        ]\
    }";

    // Parse the JSON string
    DeserializationError error = deserializeJson(doc, jsonString);

    // Check for parsing errors
    if (error)
    {
#ifdef SERIAL_DEBUG
        Serial.print("JSON parsing failed: ");
        Serial.println(error.c_str());
#endif
        return false;
    }

    // Get the image results array
    JsonArray imageArray = doc["imageResult"].as<JsonArray>();

    if (imageArray)
    {
// Process image data if array is not empty
#ifdef SERIAL_DEBUG
        Serial.println("Processing image data...");
#endif
        sendResponseToArduino(imageArray);
        return true;
    }
    else
    {
// Handle the empty array case
#ifdef SERIAL_DEBUG
        Serial.println("imageResult array is empty.");
#endif

        Wire.beginTransmission(I2C_SLAVE_ADDR);
        Wire.write((const uint8_t *)"mn", 2); // Send "mn" as a byte array for reset
        // End the transmission and check for errors
        byte error = Wire.endTransmission();

        if (error != 0)
        {
#ifdef SERIAL_DEBUG
            Serial.print("Error sending command. Error code: ");
            Serial.println(error);
#endif
        }
        return false;
    }
}

// Function to send coordinates
void sendCoordinatesToArduino(const int16_t *xValues, const int16_t *yValues, size_t count)
{
    // Start I2C transmission
    Wire.beginTransmission(I2C_SLAVE_ADDR);

    // Send the count of coordinate pairs as a 16-bit integer
    Wire.write((byte)(count & 0xFF)); // Lower byte of count
    Wire.write((byte)(count >> 8));   // Upper byte of count

    // Send the coordinates as 16-bit integers
    for (size_t i = 0; i < count; i++)
    {
        Wire.write((byte)(xValues[i] & 0xFF)); // Lower byte of X
        Wire.write((byte)(xValues[i] >> 8));   // Upper byte of X
        Wire.write((byte)(yValues[i] & 0xFF)); // Lower byte of Y
        Wire.write((byte)(yValues[i] >> 8));   // Upper byte of Y
    }

    // End the transmission and check for errors
    byte error = Wire.endTransmission();
#ifdef SERIAL_DEBUG
    if (error == 0)
    {
        Serial.println("Coordinates sent successfully.");
    }
    else
    {
        Serial.print("Error sending coordinates. Error code: ");
        Serial.println(error);
    }
#endif
}

void sendResponseToArduino(JsonArray imageResult)
{
#ifdef SERIAL_DEBUG
    Serial.println("Processing imageResult array");
#endif

    // Get the count of points from the JSON array
    size_t count = imageResult.size();

    // Allocate arrays dynamically based on the count
    int16_t *xValues = new int16_t[count];
    int16_t *yValues = new int16_t[count];

    // Scaling factors for coordinates
    int xFactor = 100;
    int yFactor = 1000;

    // Extract x and y values from the JSON array
    size_t index = 0;
    for (JsonVariant point : imageResult)
    {
        xValues[index] = static_cast<int16_t>(point["x"].as<float>() * xFactor);
        yValues[index] = static_cast<int16_t>(point["y"].as<float>() * yFactor);
        index++;
    }

    // Send the coordinates to Arduino
    sendCoordinatesToArduino(xValues, yValues, count);

    // Clean up dynamically allocated memory
    delete[] xValues;
    delete[] yValues;

#ifdef SERIAL_DEBUG
    Serial.println("Response sent successfully");
#endif
}

// Function to read DHT sensor data
void readDHTSensor()
{
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    // Check if any reads failed
    if (isnan(humidity) || isnan(temperature))
    {
#ifdef SERIAL_DEBUG
        Serial.println("Failed to read from DHT sensor!");
#endif
        return;
    }

#ifdef SERIAL_DEBUG
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.print(" °C, Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");
#endif

    // You can send this data to Arduino if needed
    // sendTemperatureHumidityToArduino(temperature, humidity);
}

void I2c_Config()
{
    if (!Wire.begin(SDA_PIN, SCL_PIN, 100000))
    {
#ifdef SERIAL_DEBUG
        Serial.println("I2C initialization failed!");
#endif
        while (1)
            ;
    }
#ifdef SERIAL_DEBUG
    Serial.println("ESP32 I2C Master initialized");
#endif
}

void setup()
{
#ifdef SERIAL_DEBUG
    Serial.begin(9600);
    Serial.setDebugOutput(true);
    Serial.println("\nESP32 Master Started");
#endif

    dht.begin();
    I2c_Config();
}

void loop()
{
    // Request the current state from the Arduino slave
    Wire.requestFrom(I2C_SLAVE_ADDR, 1);
    if (Wire.available())
    {
        roverCurrentState = Wire.read();
#ifdef SERIAL_DEBUG
        Serial.print("Rover state: ");
        Serial.println(roverCurrentState);
#endif
    }

    if (roverCurrentState == 1)
    {
        running = true;
    }
    else
    {
        running = false;
    }

    // Normal rover operation state
    if (running)
    {
        // Read sensor data
        readDHTSensor();

        // Process and send image data
        if (getArrayData())
        {
#ifdef SERIAL_DEBUG
            Serial.println("Data sent to Arduino successfully");
#endif
        }
    }

    // Add a delay to prevent excessive polling
    delay(2000);
}