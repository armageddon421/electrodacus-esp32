#include <Arduino.h>

#include <WiFi.h>

//web server includes
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

//file system
#include <SPIFFS.h>

// Set LED_BUILTIN if it is not defined by Arduino framework
// #define LED_BUILTIN 2

AsyncWebServer server(80);

void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

void setup()
{
  Serial.begin(921600);

  
  pinMode(LED_BUILTIN, OUTPUT);

  SPIFFS.begin();

  uint64_t uid = ESP.getEfuseMac();
  char ssid[22];
  sprintf(ssid, "SBMS-%04X%08X", (uint32_t)((uid>>32)%0xFFFF), (uint32_t)uid);

  WiFi.mode(WIFI_MODE_APSTA);

  
  WiFi.softAP(ssid, "electrodacus");
  delay(100);
  WiFi.softAPConfig(IPAddress (192, 168, 4, 1), IPAddress (192, 168, 4, 1), IPAddress (255,255,255,0));


  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->redirect("/index.html");
    });

  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/web/SBMS.html");
    });

  server.serveStatic("/static/", SPIFFS, "/web/static/").setCacheControl("max-age=600"); // Cache static responses for 10 minutes (600 seconds)

  server.onNotFound(notFound);

  server.begin();
  
}

void loop()
{
  // turn the LED on (HIGH is the voltage level)
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("blink");
  // wait for a second
  //delay(1000);
  
  // turn the LED off by making the voltage LOW
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("blonk");
   // wait for a second
  //delay(1000);
}