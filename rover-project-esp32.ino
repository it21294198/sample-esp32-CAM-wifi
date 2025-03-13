/*
ESP32 (Master)     Arduino (Slave)
SDA (PIN 15) ----- SDA (A4/SDA)
SCL (PIN 14) ----- SCL (A5/SCL)
GND -------------- GND
*/

// Comment for test to production version
#define SERIAL_DEBUG 0

// Server libs
#include <WiFi.h>
#include <WebServer.h>

// Camera and network libs
#include "esp_camera.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "base64.h"
#include "soc/soc.h"          // Disable brownout problems
#include "soc/rtc_cntl_reg.h" // Disable brownout problems

// Communication libs
#include <Wire.h>

// Select camera model
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// Flash Memory
#include <EEPROM.h>

// Temp and Humidity capture
#include "DHT.h"

// Pin where the DHT sensor is connected
#define DHTPIN 2
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);

// EEPROM configuration
#define EEPROM_SIZE 128
#define SSID_ADDR 0
#define PASSWORD_ADDR 32
#define UID_ADDR 96

// I2C settings
#define I2C_SLAVE_ADDR 0x08
#define SDA_PIN 15
#define SCL_PIN 14
#define JSON_CAPACITY 512

// ESP32 Access Point Settings
const char* ap_ssid = "ESP32-Config-AP";
// No password for open access point
const char* ap_password = "";  

WebServer server(80);
HTTPClient http;

// Flag to indicate Wi-Fi connection status
bool isConnected = false; 

// defalt or from user
String ssid = "";
String url = "";
String password = "";
int uid = 1;

// from EEPROM
String client_ssid;
String client_password;
int client_uid = 1;

// whole rover status
bool running = false;

// I2C connected board status
int roverCurrentState = 0;

String clientURL = "https://rusiii.com/api/Rover";
// String baseURL = "https://axum-jwt-static-page-template-4gs7.shuttle.app";
String baseURL = "http://192.168.1.22:8000";

// Camera capture and upload function
bool captureAndUploadImage() {
    // Capture photo
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        #ifdef SERIAL_DEBUG
          Serial.println("Camera capture failed");
        #endif
        return false;
    }

    // Convert to base64
    String base64Image = base64::encode(fb->buf, fb->len);

    // Return the frame buffer to the driver
    esp_camera_fb_return(fb);

    // Prepare JSON payload
    StaticJsonDocument<1024> doc;

    // Get Sensor data
    float temp = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (isnan(temp) || isnan(humidity))
    {
      temp = 0;
      humidity = 0;
    }

    doc["roverId"] = uid;
    doc["randomId"] = 1;
    doc["batteryStatus"] = 1;
    doc["temp"] = temp;
    doc["humidity"] = humidity;
    doc["imageData"] = base64Image;

    // Serial.println(base64Image); // --for testing
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // Make POST request
    // baseURL = "https://axum-jwt-static-page-template-4gs7.shuttle.app";
    // baseURL = "http://192.168.1.22:8000";
    baseURL = url;
    String roverURL = baseURL + "/rover";
    // String roverURL = baseURL + "/test/rover";
    http.begin(roverURL);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.POST(jsonPayload);
    if (httpResponseCode > 0) {
        String response = http.getString();
        #ifdef SERIAL_DEBUG
          Serial.println("HTTP Response code: " + String(httpResponseCode));
          Serial.println("Response: " + response);
        #endif

        // Parse JSON response
        StaticJsonDocument<512> responseDoc;
        DeserializationError error = deserializeJson(responseDoc, response);
        
        if (error) {
            #ifdef SERIAL_DEBUG
              Serial.print("JSON parsing failed: ");
              Serial.println(error.c_str());
            #endif

            http.end();
            return false;
        }

        // Check rover status
        if (responseDoc.containsKey("roverState")) {
            int result = responseDoc["roverState"].as<int>();
            if(result != 1){
              running = false;
              http.end();
              return false;
            }
        }

        // Check if imageResult exists and is an array
        if (responseDoc.containsKey("imageResult") && responseDoc["imageResult"].is<JsonArray>()) {
            JsonArray imageArray = responseDoc["imageResult"].as<JsonArray>();
            
            // Check if the array is not empty
            if (!imageArray.isNull() && imageArray.size() > 0) {
                sendResponseToArduino(imageArray);
            } else {
                // Handle the empty array case
                #ifdef SERIAL_DEBUG
                  Serial.println("imageResult array is empty.");
                #endif

                Wire.beginTransmission(I2C_SLAVE_ADDR);
                Wire.write((const uint8_t*)"mn", 2); // Send "reset" as a byte array
                // End the transmission and check for errors
                byte error = Wire.endTransmission();

                if (error != 0) {
                  #ifdef SERIAL_DEBUG
                    Serial.print("Error sending command. Error code: ");
                    Serial.println(error);
                  #endif
                }
                // You can also send a default response or take other actions here
            }
        } else {
            // Handle the case where imageResult is missing or not an array
            #ifdef SERIAL_DEBUG
              Serial.println("imageResult is missing or not a valid array.");
            #endif
        }

        http.end();
        return true;
    } else {
        #ifdef SERIAL_DEBUG
          Serial.println("Error on HTTP request");
          Serial.println("Error code: " + String(httpResponseCode));
        #endif

        http.end();
        return false;
    }
}

// Function to send coordinates
void sendCoordinatesToArduino(const int16_t* xValues, const int16_t* yValues, size_t count) {
    // Start I2C transmission
    Wire.beginTransmission(I2C_SLAVE_ADDR);

    // Send the count of coordinate pairs as a 16-bit integer
    Wire.write((byte)(count & 0xFF));   // Lower byte of count
    Wire.write((byte)(count >> 8));     // Upper byte of count

    // Send the coordinates as 16-bit integers
    for (size_t i = 0; i < count; i++) {
        Wire.write((byte)(xValues[i] & 0xFF));  // Lower byte of X
        Wire.write((byte)(xValues[i] >> 8));    // Upper byte of X
        Wire.write((byte)(yValues[i] & 0xFF));  // Lower byte of Y
        Wire.write((byte)(yValues[i] >> 8));    // Upper byte of Y
    }

    // End the transmission and check for errors
    byte error = Wire.endTransmission();
    #ifdef SERIAL_DEBUG
      if (error == 0) {
          Serial.println("Coordinates sent successfully.");
      } else {
          Serial.print("Error sending coordinates. Error code: ");
          Serial.println(error);
      }
    #endif
}

void sendResponseToArduino(JsonArray imageResult) {
    #ifdef SERIAL_DEBUG
      Serial.println("Processing imageResult array");
    #endif

    // Get the count of points from the JSON array
    size_t count = imageResult.size();

    // Allocate arrays dynamically based on the count
    int16_t* xValues = new int16_t[count];
    int16_t* yValues = new int16_t[count];

    // factorer
    // int xFactor = 10000;
    int xFactor = 1;
    // int yFactor = 100;
    int yFactor = 1;

    // Extract x and y values from the JSON array
    size_t index = 0;
    for (JsonVariant point : imageResult) {
        xValues[index] = static_cast<int16_t>(point["x"].as<float>()*xFactor);
        yValues[index] = static_cast<int16_t>(point["y"].as<float>()*yFactor);
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

void handleRoot() {
String response = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">
    <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <title>Login</title>
      <style>
        body {
          font-family: Arial, sans-serif;
          margin: 0;
          padding: 0;
          display: flex;
          justify-content: center;
          align-items: center;
          height: 100vh;
          background-color: #f5f5f5;
          color: #333;
        }
        .form-container {
          background: #ffffff;
          padding: 20px;
          border-radius: 10px;
          box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
          max-width: 400px;
          width: 90%;
        }
        h1 {
          text-align: center;
          margin-bottom: 20px;
          font-size: 24px;
          color: #007BFF;
        }
        label {
          display: block;
          margin-bottom: 5px;
          font-weight: bold;
        }
        input[type="text"], input[type="password"], input[type="submit"] {
          width: 100%;
          padding: 10px;
          margin: 10px 0;
          border: 1px solid #ccc;
          border-radius: 5px;
          font-size: 16px;
        }
        input[type="submit"] {
          background-color: #007BFF;
          color: white;
          border: none;
          cursor: pointer;
          transition: background-color 0.3s;
        }
        input[type="submit"]:hover {
          background-color: #0056b3;
        }
      </style>
    </head>
    <body>
      <div class="form-container">
        <h1>Login</h1>
        <form action="/submit" method="POST">

          <label for="username">User ID</label>
          <input type="text" id="uid" name="uid" value="[[uid]]" placeholder="Enter User Id" required>

          <label for="username">User ID</label>
          <input type="text" id="url" name="url" value="[[url]]" placeholder="Enter URL" required>

          <label for="username">Username</label>
          <input type="text" id="ssid" name="ssid" value="[[ssid]]" placeholder="Enter WIFI Router Name" required>

          <label for="password">Password</label>
          <input type="text" id="password" name="password" value="[[password]]" placeholder="Enter WIFI Router Password" required>

          <input type="submit" value="Submit">
        </form>
      </div>
    </body>
    </html>
  )rawliteral";

  response.replace("[[ssid]]", ssid);
  response.replace("[[url]]", url);
  response.replace("[[password]]", password);
  response.replace("[[uid]]", String(uid));
  server.send(200, "text/html", response);
}

void handleSubmit() {
  ssid = server.arg("ssid");
  url = server.arg("url");
  password = server.arg("password");
  uid = server.arg("uid").toInt();

String response = R"rawliteral(
  <!DOCTYPE html>
  <html lang="en">
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Connection Details</title>
    <style>
      body {
        font-family: Arial, sans-serif;
        margin: 0;
        padding: 0;
        background-color: #f5f5f5;
        display: flex;
        justify-content: center;
        align-items: center;
        height: 100vh;
        color: #333;
      }
      .container {
        background: #ffffff;
        padding: 20px;
        border-radius: 10px;
        box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
        max-width: 400px;
        width: 90%;
        text-align: center;
      }
      h1 {
        font-size: 24px;
        margin-bottom: 20px;
      }
      p {
        font-size: 16px;
        margin: 10px 0;
        line-height: 1.5;
      }
      .highlight {
        font-weight: bold;
        color: #007BFF;
      }
    </style>
  </head>
  <body>
    <div class="container">
      <h1>Connection Details</h1>
      <p><strong>SSID:</strong> <span class="highlight">[[SSID]]</span></p>
      <p><strong>SSID:</strong> <span class="highlight">User ID: [[UID]]</span></p>
      <p><a href="http://192.168.4.1">If not Connected Re-Connect to The Rover and Click on This</a></p>
      <p>Connecting to the server. Please wait...</p>
    </div>
  </body>
  </html>
  )rawliteral";

    response.replace("[[SSID]]", ssid);
    response.replace("[[UID]]", String(uid));
    server.send(200, "text/html", response);

    // Try to connect to the provided Wi-Fi network
    WiFi.softAPdisconnect(true); // Disconnect from the access point
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
        delay(500);
        #ifdef SERIAL_DEBUG
          Serial.print(".");
        #endif
    }

    if (WiFi.status() == WL_CONNECTED) {
        // Update the flag to indicate connection success
        isConnected = true; 
        #ifdef SERIAL_DEBUG
          Serial.println("Connected!");
          Serial.print("IP address: ");
          Serial.println(WiFi.localIP());
        #endif

    } else {
        #ifdef SERIAL_DEBUG
          Serial.println("Failed to connect.");
        #endif

        // Revert to AP mode if connection fails
        WiFi.softAP(ap_ssid, ap_password);
    }
}

// Function to write a string to EEPROM
void writeStringToEEPROM(int address, const String& data) {
    for (int i = 0; i < data.length(); ++i) {
        EEPROM.write(address + i, data[i]);
    }
    EEPROM.write(address + data.length(), '\0'); // Null-terminate the string
    EEPROM.commit();
}

// Function to read a string from EEPROM
String readStringFromEEPROM(int address) {
    String data = "";
    char ch;
    for (int i = 0; i < 64; ++i) { // Read up to 64 characters
        ch = EEPROM.read(address + i);
        if (ch == '\0') break; // Stop at null terminator
        data += ch;
    }
    return data;
}

void EEPROM_Config_Begin(){
  #ifdef SERIAL_DEBUG
    Serial.println("\nEEPROM Data RW stated.");
  #endif

  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);

  // Retrieve stored credentials
  client_ssid = readStringFromEEPROM(SSID_ADDR);
  client_password = readStringFromEEPROM(PASSWORD_ADDR);
  client_uid = EEPROM.read(UID_ADDR);

  #ifdef SERIAL_DEBUG
    Serial.print("Stored UID: ");
    Serial.println(client_uid);
    Serial.print("Stored SSID: ");
    Serial.println(client_ssid);
    Serial.print("Stored Password: ");
    Serial.println(client_password);
  #endif

  ssid = client_ssid;
  password = client_password;
  uid = client_uid;
}

void EEPROM_Config_End(){
  // Save credentials to EEPROM
  if(ssid != client_ssid){
    writeStringToEEPROM(SSID_ADDR, ssid);
  }
  if(password != client_password){
    writeStringToEEPROM(PASSWORD_ADDR, password);
  }

  // change EEPROM data in middle if needed
  if(uid != client_uid){
    EEPROM.write(UID_ADDR, uid);
    EEPROM.commit();
  }

    #ifdef SERIAL_DEBUG
      Serial.println("EEPROM setup is done");
    #endif
}

void WIFI_Config() {
    // Set up the access point
    WiFi.softAP(ap_ssid, ap_password);

    #ifdef SERIAL_DEBUG
      Serial.println("Access Point started");
      Serial.print("IP address: ");
      Serial.println(WiFi.softAPIP());
    #endif
    
    server.on("/", handleRoot);
    server.on("/submit", HTTP_POST, handleSubmit);

    server.begin();

    // Handle incoming client requests in a while loop
    while (!isConnected) {
        server.handleClient();
        delay(10); // Add a small delay to prevent watchdog timer issues
    }
}

void Camara_Config() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.frame_size = FRAMESIZE_VGA;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 4; // before 12
    config.fb_count = 2;

    if (psramFound()) {
        config.jpeg_quality = 4; // before 10
        config.fb_count = 2;
        config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.fb_location = CAMERA_FB_IN_DRAM;
    }

    // Initialize camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        #ifdef SERIAL_DEBUG
          Serial.printf("Camera init failed with error 0x%x\n", err);
        #endif
        return;
    }

    // Optimize camera settings
    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, FRAMESIZE_VGA);
    s->set_quality(s, 10);
}

void I2c_Config() {
    if (!Wire.begin(SDA_PIN, SCL_PIN, 100000)) {
        #ifdef SERIAL_DEBUG
          Serial.println("I2C initialization failed!");
        #endif
        while(1);
    }
    #ifdef SERIAL_DEBUG
      Serial.println("ESP32 I2C Master initialized");
    #endif
}

void SetBaseURL() {
    http.begin(clientURL); // Initialize the HTTP request with the client URL

    int httpResponseCode = http.GET(); // Perform the GET request
    if (httpResponseCode > 0) {
        String response = http.getString();

        #if defined(SERIAL_DEBUG)
          Serial.println("HTTP Response code: " + String(httpResponseCode));
          Serial.println("Response: " + response);
        #endif

        // Parse the JSON response
        StaticJsonDocument<512> responseDoc;
        DeserializationError error = deserializeJson(responseDoc, response);

        if (error) {
            #if defined(SERIAL_DEBUG)
              Serial.print("JSON parsing failed: ");
              Serial.println(error.c_str());
            #endif

            http.end(); // End the HTTP connection
            return; // Exit the function
        }

        // Check if the JSON contains the expected key and update clientURL
        if (responseDoc.containsKey("url")) {
            String result = responseDoc["url"].as<String>();
            if (!result.isEmpty()) {
                clientURL = result;

                #if defined(SERIAL_DEBUG)
                  Serial.println("Updated clientURL: " + clientURL);
                #endif
            }
        }
    } else {
        #if defined(SERIAL_DEBUG)
          Serial.println("HTTP GET request failed with response code: " + String(httpResponseCode));
        #endif
    }

    http.end(); // End the HTTP connection
}

// void hardCodeWiFiConnection(){
//     WiFi.begin("SLT_FIBRE","aa8888aa");
//     unsigned long startAttemptTime = millis();
//     while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
//         delay(500);
//         #ifdef SERIAL_DEBUG
//           Serial.print(".");
//         #endif
//     }

//     if (WiFi.status() == WL_CONNECTED) {
//         // Update the flag to indicate connection success
//         isConnected = true;
//         #ifdef SERIAL_DEBUG
//           Serial.println("Connected!");
//           Serial.print("IP address: ");
//           Serial.println(WiFi.localIP());
//         #endif
//     }
// }

void setup() {
    #ifdef SERIAL_DEBUG
      Serial.begin(9600);
      Serial.setDebugOutput(true);
      Serial.println("\nESP32 Master Started");
    #endif

    dht.begin();
    EEPROM_Config_Begin();
    WIFI_Config();
    // hardCodeWiFiConnection();
    Camara_Config();
    I2c_Config();
    EEPROM_Config_End();
    // SetBaseURL();
}

void loop() {
  Wire.requestFrom(I2C_SLAVE_ADDR, 1);
  roverCurrentState = Wire.read();
  #ifdef SERIAL_DEBUG
    Serial.println(roverCurrentState);
  #endif
  // Normal rover operation state
  if (running == true) {
      if (WiFi.status() == WL_CONNECTED) {
        if(roverCurrentState == 1){
          if (captureAndUploadImage()) {
            #ifdef SERIAL_DEBUG
              Serial.println("Image captured and uploaded successfully");
            #endif
          } else {
            #ifdef SERIAL_DEBUG
              Serial.println("Failed to capture or upload image");
            #endif
          }
        }else{
          #ifdef SERIAL_DEBUG
            Serial.println("roverCurrentState is not 1");
          #endif
        }
      } else {
        #ifdef SERIAL_DEBUG
          Serial.println("WiFi not connected");
        #endif
        // Attempt to reconnect
        WiFi.reconnect();
    }       
    // Add a delay to prevent multiple captures
    delay(2000);

  }else{
    delay(5 * 1000);
    #ifdef SERIAL_DEBUG
      Serial.println("Get rover status");
    #endif
    // baseURL = "https://axum-jwt-static-page-template-4gs7.shuttle.app";
    // baseURL = "http://192.168.1.22:8000";
    baseURL = url;
    String fullURL = baseURL + "/api/user/" + String(uid);
    http.begin(fullURL);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.GET();

    if (httpResponseCode == 200){
      String response = http.getString();
      response.trim(); // Remove leading and trailing whitespace

      // SERIAL_DEBUG raw response
      #ifdef SERIAL_DEBUG
      Serial.println("Raw Response with Characters:");
      for (int i = 0; i < response.length(); i++) {
          Serial.print("Character ");
          Serial.print(i);
          Serial.print(": ");
          Serial.print(response[i]);
          Serial.print(" (ASCII: ");
          Serial.print((int)response[i]); // Print ASCII value
          Serial.println(")");
      }
      #endif

      // Clean the response string
      response.replace("[", "");
      response.replace("]", "");
      response.replace("\"", ""); // Remove quotation marks
      response.trim();

      #ifdef SERIAL_DEBUG
      Serial.println("Cleaned Response: [" + response + "]");
      #endif

      // Convert to an integer
      int result = response.toInt();
      #ifdef SERIAL_DEBUG
      Serial.println("Converted Result: " + String(result));
      #endif
      http.end();

      if(result == 0) {
          #ifdef SERIAL_DEBUG
          Serial.println("Rover in pause state");
          #endif
          delay(5 * 1000);
      } else if(result == 1) {
          #ifdef SERIAL_DEBUG
          Serial.println("Move to main loop");
          #endif
          running = true;
      } else if(result == 2) {
          #ifdef SERIAL_DEBUG
          Serial.println("Move to Deep sleep");
          #endif
          delay(60 * 1 * 1000);
      } else if(result == 3) {
          #ifdef SERIAL_DEBUG
          Serial.println("Reset the rover for maintenance");
          #endif
          // Start communication with the I2C slave device
          Wire.beginTransmission(I2C_SLAVE_ADDR);
          Wire.write((const uint8_t*)"rs", 2); // Send "reset" as a byte array
          // End the transmission and check for errors
          byte error = Wire.endTransmission();
          if (error != 0) {
            #ifdef SERIAL_DEBUG
            Serial.print("Error sending command. Error code: ");
            Serial.println(error);
            #endif
          } else {
            #ifdef SERIAL_DEBUG
            Serial.println("Command sent successfully.");
            #endif
          }
          delay(5);// Add a small delay before continuing
      }else {
          #ifdef SERIAL_DEBUG
          Serial.println("Undefine state");
          #endif
          delay(5 * 1000);
      } 
    }
  }
}
