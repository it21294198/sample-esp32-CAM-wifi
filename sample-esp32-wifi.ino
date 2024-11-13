#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "base64.h"
#include "soc/soc.h"          // Disable brownout problems
#include "soc/rtc_cntl_reg.h" // Disable brownout problems

// Select camera model
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// WiFi credentials
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";

// Queue handle for frame buffer queue
QueueHandle_t frameQueue;
QueueHandle_t uploadQueue;

// Task handles
TaskHandle_t captureTaskHandle;
TaskHandle_t uploadTaskHandle;

// Struct to hold frame buffer data
struct FrameData
{
    uint8_t *buf;
    size_t len;
};

// Camera capture task
void captureTask(void *parameter)
{
    while (true)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            // Capture photo
            camera_fb_t *fb = esp_camera_fb_get();
            if (!fb)
            {
                Serial.println("Camera capture failed");
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            // Allocate memory for frame data
            FrameData *frame = (FrameData *)malloc(sizeof(FrameData));
            frame->len = fb->len;
            frame->buf = (uint8_t *)malloc(fb->len);
            memcpy(frame->buf, fb->buf, fb->len);

            // Return the frame buffer to the driver
            esp_camera_fb_return(fb);

            // Send frame to processing queue
            if (xQueueSend(frameQueue, &frame, portMAX_DELAY) != pdPASS)
            {
                // If queue is full, free the allocated memory
                free(frame->buf);
                free(frame);
                Serial.println("Frame queue full");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Small delay between captures
    }
}

// Process and encode task
void processTask(void *parameter)
{
    while (true)
    {
        FrameData *frame;
        // Receive frame from queue
        if (xQueueReceive(frameQueue, &frame, portMAX_DELAY) == pdPASS)
        {
            // Convert to base64
            String *base64Image = new String(base64::encode(frame->buf, frame->len));

            // Free the original frame data
            free(frame->buf);
            free(frame);

            // Send to upload queue
            if (xQueueSend(uploadQueue, &base64Image, portMAX_DELAY) != pdPASS)
            {
                delete base64Image;
                Serial.println("Upload queue full");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to prevent watchdog triggers
    }
}

// Upload task
void uploadTask(void *parameter)
{
    while (true)
    {
        String *base64Image;
        // Receive encoded image from queue
        if (xQueueReceive(uploadQueue, &base64Image, portMAX_DELAY) == pdPASS)
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                // Prepare JSON payload
                StaticJsonDocument<1024> doc;
                doc["title"] = "ESP32-CAM Photo";
                doc["userId"] = 1;
                doc["body"] = *base64Image;
                String jsonPayload;
                serializeJson(doc, jsonPayload);

                // Clean up base64 string
                delete base64Image;

                // Make POST request
                HTTPClient http;
                http.begin("https://jsonplaceholder.typicode.com/posts"); // Replace with your server endpoint
                http.addHeader("Content-Type", "application/json");

                int httpResponseCode = http.POST(jsonPayload);
                if (httpResponseCode > 0)
                {
                    String response = http.getString();
                    Serial.println("HTTP Response code: " + String(httpResponseCode));
                    Serial.println("Response: " + response);
                }
                else
                {
                    Serial.println("Error on HTTP request");
                    Serial.println("Error code: " + String(httpResponseCode));
                }
                http.end();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to prevent watchdog triggers
    }
}

void setup()
{
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector

    Serial.begin(115200);
    Serial.setDebugOutput(true);
    Serial.println();

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

    if (psramFound())
    {
        config.jpeg_quality = 10;
        config.fb_count = 2;
        config.grab_mode = CAMERA_GRAB_LATEST;
    }
    else
    {
        config.frame_size = FRAMESIZE_SVGA;
        config.fb_location = CAMERA_FB_IN_DRAM;
    }

    // Initialize camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("Camera init failed with error 0x%x", err);
        return;
    }

    // Optimize camera settings
    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, FRAMESIZE_VGA);
    s->set_quality(s, 10);

    // Connect to WiFi
    WiFi.begin(ssid, password);
    WiFi.setSleep(false);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");

    // Create queues
    frameQueue = xQueueCreate(2, sizeof(FrameData *));
    uploadQueue = xQueueCreate(2, sizeof(String *));

    // Create tasks
    xTaskCreatePinnedToCore(
        captureTask,
        "CaptureTask",
        8192,
        NULL,
        1,
        &captureTaskHandle,
        0);

    xTaskCreatePinnedToCore(
        processTask,
        "ProcessTask",
        8192,
        NULL,
        1,
        NULL,
        1);

    xTaskCreatePinnedToCore(
        uploadTask,
        "UploadTask",
        8192,
        NULL,
        1,
        &uploadTaskHandle,
        1);
}

void loop()
{
    // Main loop is empty as everything is handled by tasks
    vTaskDelay(pdMS_TO_TICKS(1000));
}