//travis version handling
#ifndef GIT_VERSION
    #define GIT_VERSION "undefined version"
#endif

#define STRINGIFY(x) #x
#define TOSTR(x) STRINGIFY(x)
#define VERSION_STR TOSTR(GIT_VERSION)


#include <Arduino.h>

#include <WiFi.h>

//JSON includes
#include <ArduinoJson.h>
 
//web server includes
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

//MQTT includes
#include <WiFiClient.h>
#include <PubSubClient.h>

//file system
#include <SPIFFS.h>

//rtos drivers
#include "driver/uart.h"
#include "esp_log.h"

//local libraries
#include "jsvarStore.hpp"
#include "sbmsData.hpp"

// Set LED_BUILTIN if it is not defined by Arduino framework
// #define LED_BUILTIN 2

//instances
AsyncWebServer server(80);

WiFiClient mqttWifiClient;
PubSubClient mqtt(mqttWifiClient);

JsvarStore varStore;

//------------------------- GLOBALS ---------------------

//WIFI
bool ap_fallback = false;
unsigned long lastWiFiTime = 0;
bool wifiSettingsChanged = false;

//MQTT
unsigned long mqLastConnectionAttempt = 0;

//------------------------- SETTINGS --------------------

bool s_sta_enabled = false;
String s_hostname = "SBMS";
String s_ssid = "SBMS";
String s_password = "";

void readWifiSettings()
{
  auto sWifi = SPIFFS.open("/sWifi.txt"); //default mode is read

  s_sta_enabled = sWifi.readStringUntil('\r') == "1"; if(sWifi.peek() == '\n') sWifi.read();
  s_hostname = sWifi.readStringUntil('\r'); if(sWifi.peek() == '\n') sWifi.read();
  s_ssid = sWifi.readStringUntil('\r'); if(sWifi.peek() == '\n') sWifi.read();
  s_password = sWifi.readStringUntil('\r');
  sWifi.close();
}

void writeWifiSettings(const char *hostname, const char *ssid, const char *pw)
{

  auto sWifi = SPIFFS.open("/sWifi.txt", "w");

  sWifi.printf("1\r\n");
  sWifi.printf("%s\r\n", hostname);
  sWifi.printf("%s\r\n", ssid);
  sWifi.printf("%s\r\n", pw);

  sWifi.flush();
  sWifi.close();
  
}

bool s_mq_enabled = false;
String s_mq_host;
uint32_t s_mq_port = 1883;
String s_mq_prefix = "/";
String s_mq_user;
String s_mq_password;

void readMqttSettings()
{
  auto sMqtt = SPIFFS.open("/sMqtt.txt"); //default mode is read

  s_mq_enabled = sMqtt.readStringUntil('\r') == "1"; if(sMqtt.peek() == '\n') sMqtt.read();
  s_mq_host = sMqtt.readStringUntil('\r'); if(sMqtt.peek() == '\n') sMqtt.read();
  s_mq_port = sMqtt.readStringUntil('\r').toInt(); if(sMqtt.peek() == '\n') sMqtt.read();
  s_mq_prefix = sMqtt.readStringUntil('\r'); if(sMqtt.peek() == '\n') sMqtt.read();
  s_mq_user = sMqtt.readStringUntil('\r'); if(sMqtt.peek() == '\n') sMqtt.read();
  s_mq_password = sMqtt.readStringUntil('\r'); if(sMqtt.peek() == '\n') sMqtt.read();

  sMqtt.close();
}

void writeMqttSettings(bool enabled, const char *host, uint32_t port, const char *prefix, const char *user, const char *password)
{

  auto sMqtt = SPIFFS.open("/sMqtt.txt", "w");

  sMqtt.printf("%d\r\n", enabled?1:0);
  sMqtt.printf("%s\r\n", host);
  sMqtt.printf("%d\r\n", port);
  sMqtt.printf("%s\r\n", prefix);
  sMqtt.printf("%s\r\n", user);
  sMqtt.printf("%s\r\n", password);

  sMqtt.flush();
  sMqtt.close();
  
}


//------------------------- TEMPLATES --------------------

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
  if(var == "mq_enabled")
    return s_mq_enabled?"checked":String();
  if(var == "mq_host")
    return s_mq_host;
  if(var == "mq_port")
    return String(s_mq_port);
  if(var == "mq_prefix")
    return s_mq_prefix;
  if(var == "mq_user")
    return s_mq_user;
  if(var == "mq_password")
    return s_mq_password;
  return String();
}


//------------------------- MQTT --------------------

void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  //we won't receive anything for now
}

void mqttSetup()
{

  //mqttWifiClient.setCACert(ca_cert);
  mqtt.setServer(s_mq_host.c_str(), s_mq_port);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(60); //default is 15 seconds
  //mqtt.setBufferSize(265); //override max package size (default 256)
  
}

bool mqttConnect()
{
  if(s_mq_user.isEmpty())
  {
    return mqtt.connect(s_hostname.c_str());
  }
  return mqtt.connect(s_hostname.c_str(), s_mq_user.c_str(), s_mq_password.c_str());
}

void mqttUpdate()
{
  if(!s_mq_enabled)
  {
    if(mqtt.connected())
    {
      mqtt.disconnect();
    }
  }
  else
  {
    if(!mqtt.connected() && millis() - mqLastConnectionAttempt > 1000)
    {
      printf("Connecting to %s : %d as %s", s_mq_host.c_str(), s_mq_port, s_hostname.c_str());
      mqttSetup();
      if (mqttConnect())
      {
        //client.subscribe(TOPIC);
      }
      else
      {
        mqLastConnectionAttempt = millis();
      }
    }
    mqtt.loop();
  }
}

struct MqttJsonWriter {
  // Writes one byte, returns the number of bytes written (0 or 1)
  size_t write(uint8_t c)
  { 
    buf[bufi++] = c;
    if(bufi == MQTT_MAX_PACKET_SIZE) flush();
    return 1;
  }
  // Writes several bytes, returns the number of bytes written
  size_t write(const uint8_t *buffer, size_t length)
  { 
    size_t i = 0;
    while(i < length && bufi < MQTT_MAX_PACKET_SIZE)
    {
      buf[bufi++] = buffer[i++];
    }

    if(bufi == MQTT_MAX_PACKET_SIZE) flush();
    
    return i; 
  }

  uint8_t buf[MQTT_MAX_PACKET_SIZE];
  size_t bufi;

  MqttJsonWriter()
  {
    bufi = 0;
  }

  void flush()
  {
    size_t written = 0;
    while(written < bufi)
    {
      size_t thisWrite = mqtt.write(buf + written, bufi - written);
      if(thisWrite == 0) return; //error, couldn't even write a single byte. Prevent infinite loop.
      written += thisWrite;
    }

    bufi = 0;
  }
};

void mqttPublishJson(const JsonDocument *doc, const String topic)
{

  mqtt.beginPublish((s_mq_prefix + topic).c_str(), measureJson(*doc), false);

  MqttJsonWriter writer;
  serializeJson(*doc, writer);

  writer.flush();

  mqtt.endPublish();
}

void mqttPublishSBMS(const SbmsData &sbms)
{
  //size calculated by https://arduinojson.org/v6/assistant/
  StaticJsonDocument<JSON_ARRAY_SIZE(8) + JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(6) + JSON_OBJECT_SIZE(12) + JSON_OBJECT_SIZE(15)> doc;

  doc["time"]["year"] = sbms.year;
  doc["time"]["month"] = sbms.month;
  doc["time"]["day"] = sbms.day;
  doc["time"]["hour"] = sbms.hour;
  doc["time"]["minute"] = sbms.minute;
  doc["time"]["second"] = sbms.second;
  
  doc["soc"] = sbms.stateOfChargePercent;

  JsonArray volt = doc.createNestedArray("cellsMV");
  for(uint8_t i=0; i<8; i++)
  {
    volt.add(sbms.cellVoltageMV[i]);
  }

  doc["tempInt"] = sbms.temperatureInternalTenthC / 10.0;
  doc["tempExt"] = sbms.temperatureExternalTenthC / 10.0;

  JsonObject curr = doc.createNestedObject("currentMA");

  curr["battery"] = sbms.batteryCurrentMA;
  curr["pv1"] = sbms.pv1CurrentMA;
  curr["pv2"] = sbms.pv2CurrentMA;
  curr["extLoad"] = sbms.extLoadCurrentMA;

  doc["ad2"] = sbms.ad2;
  doc["ad3"] = sbms.ad3;
  doc["ad4"] = sbms.ad4;

  doc["heat1"] = sbms.heat1;
  doc["heat2"] = sbms.heat2;

  JsonObject flags = doc.createNestedObject("flags");

  flags["OV"] = sbms.getFlag(SbmsData::FlagBit::OV);
  flags["OVLK"] = sbms.getFlag(SbmsData::FlagBit::OVLK);
  flags["UV"] = sbms.getFlag(SbmsData::FlagBit::UV);
  flags["UVLK"] = sbms.getFlag(SbmsData::FlagBit::UVLK);
  flags["IOT"] = sbms.getFlag(SbmsData::FlagBit::IOT);
  flags["COC"] = sbms.getFlag(SbmsData::FlagBit::COC);
  flags["DOC"] = sbms.getFlag(SbmsData::FlagBit::DOC);
  flags["DSC"] = sbms.getFlag(SbmsData::FlagBit::DSC);
  flags["CELF"] = sbms.getFlag(SbmsData::FlagBit::CELF);
  flags["OPEN"] = sbms.getFlag(SbmsData::FlagBit::OPEN);
  flags["LVC"] = sbms.getFlag(SbmsData::FlagBit::LVC);
  flags["ECCF"] = sbms.getFlag(SbmsData::FlagBit::ECCF);
  flags["CFET"] = sbms.getFlag(SbmsData::FlagBit::CFET);
  flags["EOC"] = sbms.getFlag(SbmsData::FlagBit::EOC);
  flags["DFET"] = sbms.getFlag(SbmsData::FlagBit::DFET);

  mqttPublishJson( &doc, "sbms");

}


//------------------------- WIFI --------------------

void updateWifiState()
{

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


//------------------------- SERIAL --------------------
#define UART_RX_BUF 1024
#define UART_TX_BUF 0
#define UART_RES_STRLEN 10
#define UART_RES_NUM_ELEMENTS 10

//queue for uart events
static QueueHandle_t uart_queue;

//queue for result events
static QueueHandle_t uart_result_queue;



void uartPrintf(const char *fmt, ...)
{

  va_list args;
  va_start(args,fmt);//Initialiasing the List 

  size_t size_string=vsnprintf(NULL,0,fmt,args); //Calculating the size of the formed string 

  char string[size_string+1]; //Initialising the string, leave room for 0 byte
  

  vsnprintf(string,size_string+1,fmt,args); //Storing the outptut into the string 

  va_end(args);

  uart_write_bytes(UART_NUM_0, string, size_string);

}


void uartTask(void *parameter)
{
  uart_event_t event;
  uint8_t rxBuf[UART_RX_BUF];

  for(;;)
  {

    if(xQueueReceive(uart_queue, (void * )&event, (portTickType)portMAX_DELAY)) {

      if (event.type == UART_DATA) {
        size_t len = 0;
        ESP_ERROR_CHECK(uart_get_buffered_data_len(UART_NUM_0, (size_t*)&len));

        size_t readLen = uart_read_bytes(UART_NUM_0, rxBuf, len, 10);

        for(size_t i=0; i<readLen; i++)
        {
          String parsed = varStore.handleChar((char) rxBuf[i]);

          if(!parsed.isEmpty())
          {
            char parseEvent[UART_RES_STRLEN];
            strlcpy(parseEvent, parsed.c_str(), UART_RES_STRLEN);

            xQueueSendToBack(uart_result_queue, parseEvent, 0); //don't wait in case the queue is full
          }
        }
      }
      else
      {
        varStore.reset();
      }
      
    }
  }
}

String uartPopEvent()
{
  char event[UART_RES_STRLEN];

  if(xQueueReceive(uart_result_queue, event, 0))
  {
    return event; //auto-cast to String
  }

  return String((char*)0); //return empty string without reserving a nullbyte
}


void setupSerial()
{

  //create queue for result events

  uart_result_queue = xQueueCreate(UART_RES_NUM_ELEMENTS, UART_RES_STRLEN);


  //configure uart and create reading task

  uart_config_t uartConfig = {
        .baud_rate = 921600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

  uart_param_config(UART_NUM_0, &uartConfig);

  uart_driver_install(UART_NUM_0, UART_RX_BUF, UART_TX_BUF, 20, &uart_queue, 0);

  xTaskCreate(uartTask, "uart", 2048 + UART_RX_BUF, NULL, 15, NULL);

}





void setup()
{
  
  //setup peripherals
  setupSerial();

  pinMode(LED_BUILTIN, OUTPUT);

  //load settings
  SPIFFS.begin();

  readWifiSettings();

  readMqttSettings();
  
  

  //setup libraries
  updateWifiState();
  
  //mqttSetup(); //will be set up automatically when enabled
  
  


  //setup webserver

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->redirect("/index.html");
    });

  //special handling for favicon
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/web/static/favicon.ico");
    });

  //manually handle all interactive pages
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

      
      const String &name = p_name->value();
      const String &ssid = p_ssid->value();
      const String &pw = p_pw->value();

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

  server.on("/sMqtt", HTTP_POST, [](AsyncWebServerRequest *request){

    //Check if POST (but not File) parameter exists
    if(request->hasParam("mqHost", true) && request->hasParam("mqPort", true) && request->hasParam("mqPrefix", true) && request->hasParam("mqUser", true) && request->hasParam("mqPassword", true))
    {
      AsyncWebParameter* p_host = request->getParam("mqHost", true);
      AsyncWebParameter* p_port = request->getParam("mqPort", true);
      AsyncWebParameter* p_prefix = request->getParam("mqPrefix", true);
      AsyncWebParameter* p_user = request->getParam("mqUser", true);
      AsyncWebParameter* p_password = request->getParam("mqPassword", true);


      bool mqEnabled = request->hasParam("mqEnable", true);

      const String &host = p_host->value();
      uint32_t port = p_port->value().toInt();
      const String &prefix = p_prefix->value();
      const String &user = p_user->value();
      const String &password = p_password->value();

      writeMqttSettings(mqEnabled, host.c_str(), port, prefix.c_str(), user.c_str(), password.c_str());
      readMqttSettings();
      

      if(mqEnabled == s_mq_enabled && host == s_mq_host && port == s_mq_port && prefix == s_mq_prefix && user == s_mq_user && password == s_mq_password) { //success
        request->send(SPIFFS, "/web/set_response.html", String(), false, [](const String &var){
          if(var == "message") return String(F("MQTT Settings stored successfully. Data should be coming in as soon as the connection was successful."));
          return String();
        });
      }
      else {
        request->send(SPIFFS, "/web/set_response.html", String(), false, [](const String &var){
          if(var == "message") return String(F("Error storing parameters. Read values do not match what should have been written."));
          return String();
        });
      }
      
      if(mqtt.connected()) mqtt.disconnect(); //cause reinitialization, even if config failed. We want to see the problem immediately rather than later.
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

  server.on("/debug", HTTP_GET, [](AsyncWebServerRequest *request){

        size_t num_tasks = uxTaskGetNumberOfTasks();
        uartPrintf("debug\r\n");
        TaskStatus_t tasks[num_tasks];
        uint32_t run_time;

        size_t num = uxTaskGetSystemState(tasks, num_tasks, &run_time);

        run_time /= 100;

        String res;
        res.reserve(50*num);

        String states[] = {"RUN", "RDY", "BLK", "SUS", "DEL"};

        for(size_t x = 0; x < num; x++)
        {
          uint16_t percentage = tasks[x].ulRunTimeCounter / run_time;

          res += tasks[x].pcTaskName;
          res += "\t\t";
          res += states[tasks[x].eCurrentState];
          res += "\t\t";
          res += tasks[x].uxBasePriority;
          res += "\t\t";
          res += tasks[x].ulRunTimeCounter;
          res += "\t\t";
          res += percentage;
          res += "%\r\n";



        }
        uartPrintf("%s\r\n", res.c_str());
        request->send(200, "text/plain", res);
    });


  server.serveStatic("/static/", SPIFFS, "/web/static/").setCacheControl("max-age=600"); // Cache static responses for 10 minutes (600 seconds)

  server.onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "Not found");
    });

  server.begin();
  

}

void updateLed()
{
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

bool handleWiFi()
{
  auto t = millis();

  if(wifiSettingsChanged)
  {
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

  return WiFi.status() == WL_CONNECTED;
}


void loop()
{
  bool connected = handleWiFi();

  updateLed();

  if(connected)
  {
    mqttUpdate();
  }


  //pop one event per loop
  String uartEvent = uartPopEvent();

  if(!uartEvent.isEmpty())
  {
    if(uartEvent == "sbms") //this guarantees the variable is stored in the varStore so we can get it
    {
      auto sbms = SbmsData(varStore.getVar("sbms"));

      if(s_mq_enabled) mqttPublishSBMS(sbms);
    }
  }
  
  

}
