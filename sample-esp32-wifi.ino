#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char *ssid = "";     // WiFi SSID
const char *password = ""; // WiFi Password

void setup()
{
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");

  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(500);
  }
  Serial.print("\nConnected to the WiFi Network IP: ");
  Serial.println(WiFi.localIP());
}

void loop()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient client;
    client.begin("https://jsonplaceholder.typicode.com/posts");
    client.addHeader("Content-Type", "application/json");

    // Serialize data for POST request
    StaticJsonDocument<200> doc;
    doc["title"] = "Test Post Title";
    doc["body"] = "This is a test post body content";
    doc["userId"] = 1;
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // Make POST request
    int http_status_code = client.POST(jsonPayload);

    if (http_status_code > 0)
    {
      String responsePayload = client.getString();
      Serial.println("\nStatus Code: " + String(http_status_code));
      Serial.println("Response Payload:\n" + responsePayload);

      // Deserialize the response
      StaticJsonDocument<500> responseDoc;
      DeserializationError error = deserializeJson(responseDoc, responsePayload);

      if (!error)
      {
        int id = responseDoc["id"];
        const char *title = responseDoc["title"];
        const char *body = responseDoc["body"];
        int userId = responseDoc["userId"];

        Serial.println("\nDeserialized Response:");
        Serial.println("ID: " + String(id));
        Serial.println("User ID: " + String(userId));
        Serial.println("Title: " + String(title));
        Serial.println("Body: " + String(body));
      }
      else
      {
        Serial.println("Failed to parse JSON");
      }

      client.end();
    }
    else
    {
      Serial.println("Error on HTTP request");
    }
  }
  else
  {
    Serial.println("Connection Lost");
  }
  delay(10000); // Wait 10 seconds before the next request
}
