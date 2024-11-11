#include <WiFi.h>
#include <HTTPClient.h>;
#include <ArduinoJson.h>;

// const char *ssid = "";
// const char *password = "";

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
  Serial.print("\nConnected to the WiFi Network IP :");
  Serial.print(WiFi.localIP());
}

void loop()
{
  if ((WiFi.status() == WL_CONNECTED))
  {
    long random_number = random(1, 10);
    HTTPClient client;

    client.begin("https://jsonplaceholder.typicode.com/posts/" + String(random_number));
    int http_status_code = client.GET();

    if (http_status_code > 0)
    {
      String payload = client.getString();
      Serial.println("\nStatus Code : " + String(http_status_code));
      Serial.println(payload);
    }
    else
    {
      Serial.print("Error on HTTP request");
    }
  }
  else
  {
    Serial.print("Connection Lost");
  }
  delay(10000);
}
