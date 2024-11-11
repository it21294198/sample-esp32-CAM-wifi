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
    long random_number = random(1, 10);
    HTTPClient client;

    String url = "https://jsonplaceholder.typicode.com/posts/" + String(random_number);
    client.begin(url);
    int http_status_code = client.GET();

    if (http_status_code > 0)
    {
      String payload = client.getString();
      Serial.println("\nStatus Code: " + String(http_status_code));
      Serial.println("Payload:\n" + payload);

      StaticJsonDocument<500> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (!error)
      {
        int id = doc["id"];
        const char *title = doc["title"];

        Serial.println("Id: " + String(id));
        Serial.println("Title: " + String(title));
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
