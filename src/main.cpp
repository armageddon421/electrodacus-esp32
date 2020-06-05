//travis version handling
#ifndef VERSION
    #define VERSION "undefined version"
#endif

#define STRINGIFY(x) #x
#define TOSTR(x) STRINGIFY(x)
#define VERSION_STR TOSTR(VERSION)


#include <Arduino.h>

#include <WiFi.h>

//web server includes
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

//file system
#include <SPIFFS.h>

//local libraries
#include "jsvarStore.hpp"
#include "sbmsData.hpp"

// Set LED_BUILTIN if it is not defined by Arduino framework
// #define LED_BUILTIN 2

//instances
AsyncWebServer server(80);

JsvarStore varStore(Serial);


bool s_sta_enabled = false;
String s_hostname = "SBMS";
String s_ssid = "SBMS";
String s_password = "";

bool ap_fallback = false;
unsigned long lastWiFiTime = 0;
bool wifiSettingsChanged = false;

String templateVersion(const String& var)
{
  if(var == "VERSION")
    return F(VERSION_STR);
  return String();
}

String templateSettings(const String& var)
{
  if(var == "wifi_hostname")
    return s_hostname;
  if(var == "wifi_ssid")
    return s_ssid;
  if(var == "wifi_pw")
    return s_password;
  return String();
}

void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

void readWifiSettings() {
  auto sWifi = SPIFFS.open("/sWifi.txt"); //default mode is read

  s_sta_enabled = sWifi.readStringUntil('\r') == "1"; if(sWifi.peek() == '\n') sWifi.read();
  s_hostname = sWifi.readStringUntil('\r'); if(sWifi.peek() == '\n') sWifi.read();
  s_ssid = sWifi.readStringUntil('\r'); if(sWifi.peek() == '\n') sWifi.read();
  s_password = sWifi.readStringUntil('\r');
  sWifi.close();
}

void writeWifiSettings(const char *hostname, const char *ssid, const char *pw) {

  auto sWifi = SPIFFS.open("/sWifi.txt", "w");

  sWifi.printf("1\r\n");
  sWifi.printf("%s\r\n", hostname);
  sWifi.printf("%s\r\n", ssid);
  sWifi.printf("%s\r\n", pw);

  sWifi.flush();
  sWifi.close();
  
}

void updateWifiState() {

  if(ap_fallback || !s_sta_enabled)
  {
    uint64_t uid = ESP.getEfuseMac();
    char ssid[22];
    sprintf(ssid, "SBMS-%04X%08X", (uint32_t)((uid>>32)%0xFFFF), (uint32_t)uid);


    WiFi.mode(WIFI_MODE_APSTA);

    
    

    WiFi.softAP(ssid, "electrodacus");
    delay(100);
    WiFi.softAPConfig(IPAddress (192, 168, 4, 1), IPAddress (192, 168, 4, 1), IPAddress (255,255,255,0));
    WiFi.softAPsetHostname("SBMS");
  }
  else if(s_sta_enabled) {
    WiFi.mode(WIFI_MODE_STA);

    WiFi.setHostname(s_hostname.c_str());

    WiFi.begin(s_ssid.c_str(), s_password.c_str());
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
  }


}

void setup()
{
  Serial.begin(921600);

  
  pinMode(LED_BUILTIN, OUTPUT);

  SPIFFS.begin();

  readWifiSettings();

  updateWifiState();

  

  
  


  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->redirect("/index.html");
    });

  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/web/index.html", String(), false, templateVersion);
    });

  server.on("/sbms.html", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/web/sbms.html");
    });

  server.on("/settings.html", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/web/settings.html", String(), false, templateSettings);
    });

  server.on("/sWifi", HTTP_POST, [](AsyncWebServerRequest *request){

    //Check if POST (but not File) parameter exists
    if(request->hasParam("ssid", true) && request->hasParam("pw", true) && request->hasParam("hostname", true))
    {
      AsyncWebParameter* p_name = request->getParam("hostname", true);
      AsyncWebParameter* p_ssid = request->getParam("ssid", true);
      AsyncWebParameter* p_pw = request->getParam("pw", true);

      
      const String name = p_name->value();
      const String ssid = p_ssid->value();
      const String pw = p_pw->value();

      writeWifiSettings(name.c_str(), ssid.c_str(), pw.c_str());
      readWifiSettings();

      if(name == s_hostname && ssid == s_ssid && pw == s_password) { //success
        request->send(SPIFFS, "/web/set_response.html", String(), false, [](const String &var){
          if(var == "message") return String(F("WiFi Settings stored successfully. Connect to your WiFi to access this device again."));
          return String();
        });
        wifiSettingsChanged = true;
      }
      else {
        request->send(SPIFFS, "/web/set_response.html", String(), false, [](const String &var){
          if(var == "message") return String(F("Error storing parameters. Read values do not match what should have been written."));
          return String();
        });
        wifiSettingsChanged = false;
      }
      
      
    }
    else
    {
      request->send(200, "text/plain", "Error. Missing Parameters.");
    }
    
    
  });

    


  server.on("/rawData", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/javascript", varStore.dumpVars());
    });
  
  server.on("/dummyData", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/testdata");
    });


  server.serveStatic("/static/", SPIFFS, "/web/static/").setCacheControl("max-age=600"); // Cache static responses for 10 minutes (600 seconds)

  server.onNotFound(notFound);

  server.begin();
  
}

void updateLed() {
  if(s_sta_enabled && WiFi.status() == WL_CONNECTED)
  {
    digitalWrite(BUILTIN_LED, HIGH);
  }
  else if(s_sta_enabled) {
    if(ap_fallback){
      digitalWrite(BUILTIN_LED, millis()%500 < 100);
    }
    else {
      digitalWrite(BUILTIN_LED, millis()%1500 < 100);
    }
  }
  else {
    digitalWrite(BUILTIN_LED, millis()%1500 < 750);
  }

}

void loop()
{
  auto t = millis();

  updateLed();

  if(wifiSettingsChanged) {
    wifiSettingsChanged = false;
    lastWiFiTime = t + 5000; //give it a little extra time
    ap_fallback = false;
    updateWifiState();
  }

  if(s_sta_enabled && WiFi.status() == WL_CONNECTED)
  {
    lastWiFiTime = t;
    if(ap_fallback) {
      ap_fallback = false;
      updateWifiState();
    }
    
  }
  else if(s_sta_enabled && !ap_fallback && WiFi.status() != WL_CONNECTED && t-lastWiFiTime > 10000)
  {
    ap_fallback = true;
    updateWifiState();
  }


  String parsed = varStore.update();
  if(parsed.length() > 0)
  {
    if(parsed == "sbms") //this guarantees the variable is stored in the varStore so we can get it
    {
      auto sbms = SbmsData(varStore.getVar("sbms"));

    }
  }
  

}
