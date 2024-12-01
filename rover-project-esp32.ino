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

// I2C settings
#define I2C_SLAVE_ADDR 0x08
#define SDA_PIN 15
#define SCL_PIN 14
#define JSON_CAPACITY 512

// ESP32 Access Point Settings
const char* ap_ssid = "ESP32-Config-AP";
const char* ap_password = "";  // No password for open access point

WebServer server(80);

String ssid = "";
String password = "";
bool isConnected = false; // Flag to indicate Wi-Fi connection status

// Camera capture and upload function
bool captureAndUploadImage() {
    // Capture photo
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return false;
    }

    // Convert to base64
    String base64Image = base64::encode(fb->buf, fb->len);

    // Return the frame buffer to the driver
    esp_camera_fb_return(fb);

    // Prepare JSON payload
    StaticJsonDocument<1024> doc;

    doc["roverId"] = 1;
    doc["randomId"] = 1234;
    doc["batteryStatus"] = 12.3;
    doc["temp"] = 12.3;
    doc["humidity"] = 12.3;
    doc["imageData"] = base64Image;

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // Make POST request
    HTTPClient http;
    http.begin("https://axum-jwt-static-page-template-4gs7.shuttle.app/test/rover"); 
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.POST(jsonPayload);
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("HTTP Response code: " + String(httpResponseCode));
        Serial.println("Response: " + response);

        // Parse JSON response
        StaticJsonDocument<512> responseDoc;
        DeserializationError error = deserializeJson(responseDoc, response);
        
        if (error) {
            Serial.print("JSON parsing failed: ");
            Serial.println(error.c_str());
            http.end();
            return false;
        }

        // Check if imageResult exists and is an array
        if (responseDoc.containsKey("imageResult") && responseDoc["imageResult"].is<JsonArray>()) {
            sendResponseToArduino(responseDoc["imageResult"].as<JsonArray>());
        }

        http.end();
        return true;
    } else {
        Serial.println("Error on HTTP request");
        Serial.println("Error code: " + String(httpResponseCode));
        http.end();
        return false;
    }
}

void sendResponseToArduino(JsonArray imageResult) {
    // Clear previous document
    StaticJsonDocument<JSON_CAPACITY> responseDoc;
    
    // Create nested array for points
    JsonArray array = responseDoc.createNestedArray("points");
    
    // Correctly iterate through the image result array
    for (JsonVariant point : imageResult) {
        JsonObject newPoint = array.createNestedObject();
        newPoint["x"] = point["x"].as<float>();
        newPoint["y"] = point["y"].as<float>();
    }
    
    // Serialize JSON to buffer
    char jsonBuffer[JSON_CAPACITY];
    size_t len = serializeJson(responseDoc, jsonBuffer);
    
    // Send length first
    Wire.beginTransmission(I2C_SLAVE_ADDR);
    Wire.write((byte)(len & 0xFF));  // Lower byte of length
    Wire.write((byte)(len >> 8));    // Upper byte of length
    byte error = Wire.endTransmission();
    
    if (error == 0) {
        // Send JSON data in chunks
        for (size_t i = 0; i < len; i++) {
            Wire.beginTransmission(I2C_SLAVE_ADDR);
            Wire.write(jsonBuffer[i]);
            error = Wire.endTransmission();
            
            if (error != 0) {
                Serial.print("Error sending chunk. Error code: ");
                Serial.println(error);
                break;
            }
            delay(5);  // Small delay between chunks
        }
        
        Serial.println("Response sent successfully");
    } else {
        Serial.print("Error sending length. Error code: ");
        Serial.println(error);
    }
}

void handleRoot() {
server.send(200, "text/html", R"rawliteral(
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
          <label for="username">Username</label>
          <input type="text" id="ssid" name="ssid" placeholder="Enter WIFI Router Name" required>
          <label for="password">Password</label>
          <input type="password" id="password" name="password" placeholder="Enter WIFI Router Password" required>
          <input type="submit" value="Submit">
        </form>
      </div>
    </body>
    </html>
  )rawliteral");
}

void handleSubmit() {
    ssid = server.arg("ssid");
    password = server.arg("password");

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
      <p><a href="http://192.168.4.1">If not Connected Re-Connect to The Rover and Click on This</a></p>
      <p>Connecting to the server. Please wait...</p>
    </div>
  </body>
  </html>
  )rawliteral";

    response.replace("[[SSID]]", ssid);
    server.send(200, "text/html", response);

    // Try to connect to the provided Wi-Fi network
    WiFi.softAPdisconnect(true); // Disconnect from the access point
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        isConnected = true; // Update the flag to indicate connection success
        Serial.println("Connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("Failed to connect.");
        // Revert to AP mode if connection fails
        WiFi.softAP(ap_ssid, ap_password);
    }
}

void WIFI_Config() {
    // Set up the access point
    WiFi.softAP(ap_ssid, ap_password);

    Serial.println("Access Point started");
    Serial.print("IP address: ");
    Serial.println(WiFi.softAPIP());

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
    config.jpeg_quality = 12;
    config.fb_count = 2;

    if (psramFound()) {
        config.jpeg_quality = 10;
        config.fb_count = 2;
        config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.fb_location = CAMERA_FB_IN_DRAM;
    }

    // Initialize camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return;
    }

    // Optimize camera settings
    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, FRAMESIZE_VGA);
    s->set_quality(s, 10);
}

void I2c_Config() {
    if (!Wire.begin(SDA_PIN, SCL_PIN, 100000)) {
        Serial.println("I2C initialization failed!");
        while(1);
    }
    Serial.println("ESP32 I2C Master initialized");
}

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    Serial.println();

    WIFI_Config();
    Camara_Config();
    I2c_Config();
}

void loop() {
    // Check button state via I2C
    Wire.requestFrom(I2C_SLAVE_ADDR, 1);
    
    if (Wire.available()) {
        byte buttonState = Wire.read();
        
        // Only capture and upload if button state is 1
        if (buttonState == 1) {
            Serial.println("Button pressed. Capturing and uploading image...");
            
            if (WiFi.status() == WL_CONNECTED) {
                if (captureAndUploadImage()) {
                    Serial.println("Image captured and uploaded successfully");
                } else {
                    Serial.println("Failed to capture or upload image");
                }
            } else {
                Serial.println("WiFi not connected");
                // Attempt to reconnect
                WiFi.reconnect();
            }
            
            // Add a delay to prevent multiple captures
            delay(2000);
        }
    }
    
    // Small delay to prevent excessive polling
    delay(500);
}