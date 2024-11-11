#include <WiFi.h>

const char *ssid = "";
const char *password = "";

void setup()
{
  // put your setup code here, to run once:
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
  // put your main code here, to run repeatedly:
}
