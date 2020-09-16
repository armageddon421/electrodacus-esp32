//travis version handling
#ifndef GIT_VERSION
    #define GIT_VERSION "undefined version"
#endif

#define STRINGIFY(x) #x
#define TOSTR(x) STRINGIFY(x)
#define VERSION_STR TOSTR(GIT_VERSION)


#include <Arduino.h>

//Networking basics
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

//OTA
#include <ArduinoOTA.h>
#include <Update.h>

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
unsigned long lastWifiRetryTime = 0;
bool wifiSettingsChanged = false;

//MQTT
unsigned long mqLastConnectionAttempt = 0;

//system

bool shouldReboot = false;
bool web_ota_type_spiffs = false;

//------------------------- SETTINGS --------------------

bool s_sta_enabled = false;
String s_sta_hostname = "SBMS";
String s_sta_ssid = "SBMS";
String s_sta_password = "";
String s_ap_ssid = "SBMS";
String s_ap_password = "";


void readWifiSettings()
{
  auto sWifi = SPIFFS.open("/cfg/wifi"); //default mode is read

  const size_t capacity = JSON_OBJECT_SIZE(6) + 200;
  DynamicJsonDocument doc(capacity);

  auto err = deserializeJson(doc, sWifi);

  sWifi.close();

  if(err == DeserializationError::Ok)
  {
    s_sta_enabled = doc["sta_enable"].as<bool>();
    s_sta_hostname = doc["hostname"].as<String>();
    s_sta_ssid = doc["sta_ssid"].as<String>();
    s_sta_password = doc["sta_pw"].as<String>();
    s_ap_ssid = doc["ap_ssid"].as<String>();
    s_ap_password = doc["ap_pw"].as<String>();

    bool rewrite = false;

    if(s_ap_ssid.length() == 0)
    {
      uint64_t uid = ESP.getEfuseMac();
      char ssid[22];
      sprintf(ssid, "SBMS-%04X%08X", (uint32_t)((uid>>32)%0xFFFF), (uint32_t)uid);
      s_ap_ssid = String(ssid);
      doc["ap_ssid"] = s_ap_ssid;
      rewrite = true;
    }

    if(s_ap_password.length() == 0)
    {
      s_ap_password = "electrodacus";
      doc["ap_pw"] = s_ap_password;
      rewrite = true;
    }

    if(rewrite)
    {
      sWifi = SPIFFS.open("/cfg/wifi", "w"); //default mode is read
      serializeJson(doc, sWifi);
      sWifi.flush();
      sWifi.close();
    }
  }
}


bool s_mq_enabled = false;
String s_mq_host;
uint32_t s_mq_port = 1883;
String s_mq_prefix = "/";
String s_mq_user;
String s_mq_password;

void readMqttSettings()
{
  auto sMqtt = SPIFFS.open("/cfg/mqtt"); //default mode is read

  const size_t capacity = JSON_OBJECT_SIZE(6) + 200;
  DynamicJsonDocument doc(capacity);

  auto err = deserializeJson(doc, sMqtt);

  if(err == DeserializationError::Ok)
  {
    s_mq_enabled = doc["mq_enabled"].as<bool>();
    s_mq_host = doc["mq_host"].as<String>();
    s_mq_port = doc["mq_port"].as<uint32_t>();
    s_mq_prefix = doc["mq_prefix"].as<String>();
    s_mq_user = doc["mq_user"].as<String>();
    s_mq_password = doc["mq_password"].as<String>();
  }

  sMqtt.close();
}


bool data_sbms_enabled = true;
bool data_sbms_diff = false;
bool data_s2_enabled = false;

void readDataSettings()
{
  auto sData = SPIFFS.open("/cfg/data"); //default mode is read

  const size_t capacity = JSON_OBJECT_SIZE(3) + 200;
  DynamicJsonDocument doc(capacity);

  auto err = deserializeJson(doc, sData);

  if(err == DeserializationError::Ok)
  {
    data_sbms_enabled = doc["sbms_enabled"].as<bool>();
    data_sbms_diff = doc["sbms_diff"].as<bool>();
    data_s2_enabled = doc["s2_enabled"].as<bool>();
  }

  sData.close();
}


bool system_ota_limit = true;
bool system_ota_arduino = false;

void readSystemSettings()
{
  auto sSys = SPIFFS.open("/cfg/sys"); //default mode is read

  const size_t capacity = JSON_OBJECT_SIZE(1) + 200;
  DynamicJsonDocument doc(capacity);

  auto err = deserializeJson(doc, sSys);

  if(err == DeserializationError::Ok)
  {
    system_ota_limit = doc["ota_limit"].as<bool>();
    system_ota_arduino = doc["ota_arduino"].as<bool>();
  }

  sSys.close();
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
    return mqtt.connect(s_sta_hostname.c_str());
  }
  return mqtt.connect(s_sta_hostname.c_str(), s_mq_user.c_str(), s_mq_password.c_str());
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
      printf("Connecting to %s : %d as %s", s_mq_host.c_str(), s_mq_port, s_sta_hostname.c_str());
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
  StaticJsonDocument<JSON_ARRAY_SIZE(8) + JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(6) + JSON_OBJECT_SIZE(13) + JSON_OBJECT_SIZE(15)> doc; //13 is the root element

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

  //optional calculated fields

  if(data_sbms_diff)
  {
    uint16_t min = -1;
    uint16_t max = 0;

    for(uint8_t i=0; i<8; i++)
    {
      uint16_t v = sbms.cellVoltageMV[i];
      if(v > 0 && v < min) min = v;
      if(v > 0 && v > max) max = v;
    }

    flags["delta"] = max-min;
  }

  mqttPublishJson( &doc, "sbms");

}


//------------------------- WIFI --------------------

void updateWifiState()
{
  if(ap_fallback && s_sta_enabled)
  {
    WiFi.mode(WIFI_MODE_APSTA);
  }
  else if(!ap_fallback && s_sta_enabled)
  {
    WiFi.mode(WIFI_MODE_STA);
  }
  else if(!s_sta_enabled)
  {
    WiFi.mode(WIFI_MODE_AP);
  }

  if(ap_fallback || !s_sta_enabled)
  {
    WiFi.softAP(s_ap_ssid.c_str(), s_ap_password.c_str());
    delay(100);
    WiFi.softAPConfig(IPAddress (192, 168, 4, 1), IPAddress (192, 168, 4, 1), IPAddress (255,255,255,0));
    WiFi.softAPsetHostname("SBMS");
    ArduinoOTA.setHostname("SBMS");
  }
  else
  {
    WiFi.softAPdisconnect();
  }
  
  
  if(s_sta_enabled) {

    WiFi.setHostname(s_sta_hostname.c_str());
    ArduinoOTA.setHostname(s_sta_hostname.c_str());

    WiFi.begin(s_sta_ssid.c_str(), s_sta_password.c_str());
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
  }
  else
  {
    WiFi.disconnect();
  }


}

//-------------------------- OTA ----------------------

bool ota_arduino_started = false;
int ota_arduino_command = 0;

void otaSetup()
{
  ArduinoOTA
    .onStart([]()
    {
      ota_arduino_command = ArduinoOTA.getCommand();

      if (ota_arduino_command == U_SPIFFS)
      {
        SPIFFS.end();
      }
      
    })
    .onEnd([]()
    {
      if (ota_arduino_command == U_SPIFFS)
      {
        SPIFFS.end();
      }
    })
    .onError([](ota_error_t error)
    {
      if (ota_arduino_command == U_SPIFFS)
      {
        SPIFFS.end();
      }
    });
}

void otaUpdate()
{
  bool timeOk = !system_ota_limit || millis() < 300000; // allow OTA only in the first 5 minutes if limit is activated

  if(system_ota_arduino && timeOk && !ota_arduino_started)
  {
    ArduinoOTA.begin();
    ota_arduino_started = true;
  }
  else if((!system_ota_arduino || !timeOk) && ota_arduino_started)
  {
    ArduinoOTA.end();
    ota_arduino_started = false;
  }

  if(ota_arduino_started)
  {
    ArduinoOTA.handle();
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

  readSystemSettings();
  readWifiSettings();
  readMqttSettings();
  readDataSettings();
  

  //setup libraries
  updateWifiState();
  
  //mqttSetup(); //will be set up automatically when enabled
  
  //setup OTA
  otaSetup();
  


  //setup webserver

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->redirect("/index.html");
    });

  //manually handle all interactive pages

  server.on("/sbms.html", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/web/sbms.html");
    });

  server.on("^\\/cfg\\/(.+)$", HTTP_GET, [](AsyncWebServerRequest *request){
      String file = request->pathArg(0);
      request->send(SPIFFS, "/cfg/" + file, String(), false);
  });

  server.on("/rawData", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/javascript", varStore.dumpVars());
    });
  
  server.on("/dummyData", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/testdata");
    });

  server.on("/version", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "text/plain", F(VERSION_STR));
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
        request->send(200, "text/plain", res);
    });

  // Simple Firmware Update Form
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, F("text/html"), F("<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>"));
  });
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    shouldReboot = !Update.hasError() && web_ota_type_spiffs; // only reboot if we updated the spiffs. this allows to update firmware and spiffs and only reboot if both are done.
    AsyncWebServerResponse *response = request->beginResponse(200, F("text/plain"), (!Update.hasError())?"OK":"FAIL");
    response->addHeader("Connection", "close");
    request->send(response);
  },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index){
      if (filename.startsWith(F("spiffs")))
      {
        SPIFFS.end();
        web_ota_type_spiffs = true;
      }
      if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)){
        // error Update.printError(Serial);
      }
    }
    if(!Update.hasError()){
      if(Update.write(data, len) != len){
        // error Update.printError(Serial);
      }
    }
    if(final){
      if(Update.end(true)){
        // success
        SPIFFS.begin();
      } else {
        // error Update.printError(Serial);
      }
    }
  });


  server.serveStatic("/", SPIFFS, "/dist/").setCacheControl("max-age=600"); // Cache static responses for 10 minutes (600 seconds)

  server.onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "Not found");
    });

  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    
    if (request->url() == "/cfg/wifi") {
      fs::File f = SPIFFS.open("/cfg/wifi", "w");
      f.write(data, len);
      request->send(200, "text/plain", "saved");
      readWifiSettings();
      wifiSettingsChanged = true;
    }
    else if (request->url() == "/cfg/mqtt") {
      fs::File f = SPIFFS.open("/cfg/mqtt", "w");
      f.write(data, len);
      request->send(200, "text/plain", "saved");
      readMqttSettings();
      if(mqtt.connected()) mqtt.disconnect(); //cause reinitialization, even if config failed. We want to see the problem immediately rather than later.
    }
    else if (request->url() == "/cfg/data") {
      fs::File f = SPIFFS.open("/cfg/data", "w");
      f.write(data, len);
      request->send(200, "text/plain", "saved");
      readDataSettings();
    }
    else if (request->url() == "/cfg/sys") {
      fs::File f = SPIFFS.open("/cfg/sys", "w");
      f.write(data, len);
      request->send(200, "text/plain", "saved");
      readSystemSettings();
    }

  });

  server.begin();
  

}

void updateLed()
{
  if(s_sta_enabled && WiFi.status() == WL_CONNECTED)
  {
    digitalWrite(BUILTIN_LED, millis()%2000 < 1900);
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
    lastWifiRetryTime = t;
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
  else if(s_sta_enabled && WiFi.status() != WL_CONNECTED)
  {
    
    if(t-lastWifiRetryTime > 2000) //continue retrying every 2s even if AP is on
    {
      lastWifiRetryTime = t;
      updateWifiState();
    }
    else if(t-lastWiFiTime > 20000 && !ap_fallback) //enable AP after 20s
    {
      lastWifiRetryTime = t;
      ap_fallback = true;
      updateWifiState();
    }
    
  }

  return WiFi.status() == WL_CONNECTED;
}


void loop()
{
  // reboot if requested from any source after 1 second (allow time for cpu0 to process networking)
  if(shouldReboot)
  {
    delay(1000);
    ESP.restart();
  }

  otaUpdate();

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

      if(s_mq_enabled && data_sbms_enabled) mqttPublishSBMS(sbms);
    }
    else if(uartEvent == "s2") //this guarantees the variable is stored in the varStore so we can get it
    {
      auto s2array = varStore.getVar("s2");

      if(s_mq_enabled && data_s2_enabled) mqtt.publish((s_mq_prefix + "s2").c_str(), s2array.c_str());
    }
  }
  
  

}
