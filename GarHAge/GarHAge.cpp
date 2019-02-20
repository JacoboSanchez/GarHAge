/*   
 * GarHAge
 * a Home-Automation-friendly ESP8266-based MQTT Garage Door Controller
 * Licensed under the MIT License, Copyright (c) 2017 marthoc
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Adafruit_Sensor.h>
#include "config.h"
#include "Door.h"

const char* garhageVersion = "2.0.0";

// Mapping NodeMCU Ports to Arduino GPIO Pins
// Allows use of NodeMCU Port nomenclature in config.h
#define D0 16
#define D1 5
#define D2 4
#define D3 0
#define D4 2
#define D5 14
#define D6 12 
#define D7 13
#define D8 15

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

const bool static_ip = STATIC_IP;
IPAddress ip(IP);
IPAddress gateway(GATEWAY);
IPAddress subnet(SUBNET);

const bool web_update_enable = WEB_UPDATE_ENABLED;
const char* update_username = WEB_UPDATE_USER;
const char* update_password = WEB_UPDATE_PASS;

const char* mqtt_broker = MQTT_BROKER;
const char* mqtt_clientId = MQTT_CLIENTID;
const char* mqtt_username = MQTT_USERNAME;
const char* mqtt_password = MQTT_PASSWORD;

const bool activeHighRelay = ACTIVE_HIGH_RELAY;

GarageDoor door1 = GarageDoor((char *)"GarDoor1", (char *)"garage/door/1/action", (char *)"garage/door/1/status", D2, D2, D5, (char *)"NO");
GarageDoor door2 = GarageDoor((char *)"GarDoor2", (char *)"garage/door/2/action", (char *)"garage/door/2/status", D1, D1);
Door door3 = Door((char *)"AuxDoor1", (char *)"garage/aux_door/1/action", (char *)"garage/aux_door/1/status", D3, (char *)"NO");
Door door4 = Door((char *)"AuxDoor2", (char *)"garage/aux_door/2/action", (char *)"garage/aux_door/2/status", D4, (char *)"NO");

Door * doors[] = { &door1, &door2, &door3, &door4 };

const int numDoors = sizeof(doors)/4;

const bool dht_enabled = DHT_ENABLED;
const char* dhtTempTopic = MQTT_TEMPERATURE_TOPIC;
const char* dhtHumTopic = MQTT_HUMIDITY_TOPIC;

const unsigned long dht_publish_interval_s = DHT_PUBLISH_INTERVAL;
unsigned long dht_lastReadTime = 0;
const int relayActiveTime = RELAY_ACTIVE_TIME;
const unsigned int deadTime = 4000;
const String availabilityBase = MQTT_CLIENTID;
const String availabilitySuffix = "/availability";
const String availabilityTopicStr = availabilityBase + availabilitySuffix;
const char* availabilityTopic = availabilityTopicStr.c_str();
const char* birthMessage = "online";
const char* lwtMessage = "offline";

// Home Assistant MQTT Discovery variables

const bool discoveryEnabled = HA_MQTT_DISCOVERY;
String discoveryPrefix = HA_MQTT_DISCOVERY_PREFIX;

// GarHAge API topic variables
// Messages published to the api topic are first processed by processIncomingMessage()
// processIncomingMessage() calls processAPIMessage(), which determines the appropriate action

String apiSuffix = "/api";
String apiTopicStr = availabilityBase + apiSuffix;
const char* apiTopic = apiTopicStr.c_str();

WiFiClient espClient;
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
PubSubClient client(espClient);
DHT dht(DHTPIN, DHTTYPE);

// Wifi setup function

void setup_wifi() {

  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.print(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  if (static_ip) {
    WiFi.config(ip, gateway, subnet);
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.print(" WiFi connected - IP address: ");
  Serial.println(WiFi.localIP());
}

void setup_ota() {
  ArduinoOTA.setHostname(mqtt_clientId);

  ArduinoOTA.onStart([]() {
    Serial.println("Start OTA");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("End OTA");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("OTA Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("OTA Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("OTA Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("OTA Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("OTA End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("Ready for OTA Upload...");
}

// Function that publishes an update when called
void publish_door_status(Door *door) {
  if (door->statusEnabled()) { 
    char* stat;
    if (door->hasControl()) {
      if (door->isOpen()) 
        { stat = (char *)"open"; }
      else 
        { stat = (char *)"closed"; }
    } else {
      if (door->isOpen()) 
        { stat = (char *)"ON"; }
      else 
        { stat = (char *)"OFF"; }
    }
    
    Serial.print(door->getAlias());
    Serial.print(" ");
    Serial.print(stat);
    Serial.print("! Publishing to ");
    Serial.print(door->getMqttStatus());
    Serial.println("...");
    client.publish(door->getMqttStatus(), stat, true);
    door->setStatusString(stat);
  }
}

// Function that publishes birthMessage

void publish_birth_message() {
  // Publish the birthMessage
  Serial.print("Publishing birth message \"");
  Serial.print(birthMessage);
  Serial.print("\" to ");
  Serial.print(availabilityTopic);
  Serial.println("...");
  client.publish(availabilityTopic, birthMessage, true);
}

// Function that toggles the relevant relay-connected output pin

void toggleRelay(int pin) {
  if (activeHighRelay) {
    digitalWrite(pin, HIGH);
    delay(relayActiveTime);
    digitalWrite(pin, LOW);
  }
  else {
    digitalWrite(pin, LOW);
    delay(relayActiveTime);
    digitalWrite(pin, HIGH);
  }
}

// Function that publishes the status of all enabled doors (for API use)

void publish_enabled_doors() {
  for (int i = 0; i < numDoors; i++) {
    publish_door_status(doors[i]);
  }
}

// Function that runs in loop() to read and publish values from the DHT sensor

void dht_read_publish() {
  // Read values from sensor
  float hum = dht.readHumidity();
  float tempRaw = dht.readTemperature();

  // Check if there was an error reading values
  if (isnan(hum) || isnan(tempRaw)) {
    Serial.print("Failed to read from DHT sensor; will try again in ");
    Serial.print(dht_publish_interval_s);
    Serial.println(" seconds...");
    return;
  }

  bool celsius = DHT_TEMPERATURE_CELSIUS;
  float temp;
  if (celsius) {
    temp = tempRaw;
  }
  else {
    temp = (tempRaw * 1.8 + 32);
  }

  char payload[4];

  // Publish the temperature payload via MQTT 
  dtostrf(temp, 4, 0, payload);
  Serial.print("Publishing DHT Temperature payload: ");
  Serial.print(payload);
  Serial.print(" to ");
  Serial.print(dhtTempTopic);
  Serial.println("...");
  client.publish(dhtTempTopic, payload, false);

  // Publish the humidity payload via MQTT
  dtostrf(hum, 4, 0, payload);
  Serial.print("Publishing DHT Humidity payload: ");
  Serial.print(payload);
  Serial.print(" to ");
  Serial.print(dhtHumTopic);
  Serial.println("...");
  client.publish(dhtHumTopic, payload, false);

}

void publish_ha_mqtt_discovery_door(Door *door, const int doorNum){
  char* component;
  char* nodeName;

  const size_t bufferSize = JSON_OBJECT_SIZE(13);
  DynamicJsonBuffer jsonBuffer(bufferSize);

  JsonObject& root = jsonBuffer.createObject();
  root["name"] = door->getAlias();
  root["avty_t"] = availabilityTopic;

  if(door->statusEnabled()) {
    root["stat_t"] = door->getMqttStatus();
  }

  if (door->hasControl()) {
    root["cmd_t"] = door->getMqttAction();
     if(door->statusEnabled()) {
       root["pl_stop"] = "STATE";
     }

    component = (char *)"/cover/";
    nodeName = (char *)"/gardoor";
  }
  else {
    root["dev_cla"] = "door";

    component = (char *)"/binary_sensor/";
    nodeName = (char *)"/auxdoor";
  }

  // Prepare payload for publishing
  String payloadStr = "";
  root.printTo(payloadStr);
  const char* payload = payloadStr.c_str();

  // Prepare topic for publishing
  String topicStr = discoveryPrefix;
  topicStr += component;
  topicStr += mqtt_clientId;
  topicStr += nodeName;
  topicStr += doorNum;
  topicStr += "/config";
  const char* topic = topicStr.c_str();

  // Publish payload
  Serial.print("Publishing MQTT Discovery payload for ");
  Serial.print(door->getAlias());
  Serial.println("...");
  client.publish(topic, payload, true);
}

void publish_ha_mqtt_discovery_temperature() {
  const size_t bufferSize = JSON_OBJECT_SIZE(7);
  DynamicJsonBuffer jsonBuffer(bufferSize);

  bool celsius = DHT_TEMPERATURE_CELSIUS;
  String uom = "";
  if (celsius) {
    uom = "°C";
  }
  else {
    uom = "°F";
  }

  String alias = DHT_TEMPERATURE_ALIAS;

  JsonObject& root = jsonBuffer.createObject();
  root["name"] = alias;
  root["stat_t"] = dhtTempTopic;
  root["unit_of_meas"] = uom;
  root["avty_t"] = availabilityTopic;

  // Prepare payload for publishing
  String payloadStr = "";
  root.printTo(payloadStr);
  const char* payload = payloadStr.c_str();

  // Prepare topic for publishing
  String clientID = mqtt_clientId;
  String suffix = "/temperature/config";
  String topicStr = discoveryPrefix + "/sensor/" + clientID + suffix;
  const char* topic = topicStr.c_str();

  // Publish payload
  Serial.print("Publishing MQTT Discovery payload for ");
  Serial.print(alias);
  Serial.println("...");
  client.publish(topic, payload, false);

}

void publish_ha_mqtt_discovery_humidity() {
  const size_t bufferSize = JSON_OBJECT_SIZE(7);
  DynamicJsonBuffer jsonBuffer(bufferSize);

  String alias = DHT_HUMIDITY_ALIAS;

  JsonObject& root = jsonBuffer.createObject();
  root["name"] = alias;
  root["stat_t"] = dhtHumTopic;
  root["unit_of_meas"] = "%";
  root["avty_t"] = availabilityTopic;

  // Prepare payload for publishing
  String payloadStr = "";
  root.printTo(payloadStr);
  const char* payload = payloadStr.c_str();

  // Prepare topic for publishing
  String clientID = mqtt_clientId;
  String suffix = "/humidity/config";
  String topicStr = discoveryPrefix + "/sensor/" + clientID + suffix;
  const char* topic = topicStr.c_str();

  // Publish payload
  Serial.print("Publishing MQTT Discovery payload for ");
  Serial.print(alias);
  Serial.println("...");
  client.publish(topic, payload, false);

}

void publish_ha_mqtt_discovery() {
  Serial.println("Publishing Home Assistant MQTT Discovery payloads...");
  for (int i = 0; i < numDoors; i++) {
    publish_ha_mqtt_discovery_door(doors[i], i);
  }

  if (dht_enabled) {
    publish_ha_mqtt_discovery_temperature();
    publish_ha_mqtt_discovery_humidity();
  }
}

// Function called by processIncomingMessage() when a message matches the API topic
// Processes the incoming payload on the API topic
// Supported API calls:
// STATE_ALL - publishes the current state of all enabled devices (including temp/humidity)
// STATE_DOORS - publishes the current state of all doors only
// STATE_DHT - forces a read/publish of temp/humidity
// DISCOVERY - publishes HA discovery payloads for all enabled devices
void processAPIMessage(String payload) {
  if (payload == "STATE_ALL") {
    Serial.println("API Request: STATE_ALL - Publishing state of all enabled devices...");
    publish_birth_message();
    publish_enabled_doors();
    if (dht_enabled) { dht_read_publish(); }
  }

  else if (payload == "STATE_DOORS") {
    Serial.println("API Request: STATE_DOORS - Publishing state of all enabled doors...");
    publish_birth_message();
    publish_enabled_doors();
  }

  else if (payload == "STATE_DHT") {
    if (dht_enabled) { 
      Serial.println("API Request: STATE_DHT - Forcing Temperature and Humidity read/publish...");
      publish_birth_message();
      dht_read_publish(); 
    }
    else Serial.println("API Request ERROR: STATE_DHT requested but DHT is not enabled!");
  }

  else if (payload == "DISCOVERY") {
    if (discoveryEnabled) {
      Serial.println("API Request: DISCOVERY - Publishing Home Assistant MQTT Discovery payloads...");
      publish_ha_mqtt_discovery();
    }
    else Serial.println("API Request ERROR: DISCOVERY requested but HA MQTT discovery not enabled!");
  }

  else {
    Serial.print("API Request ERROR: Payload ");
    Serial.print(payload);
    Serial.println(" matches no API function!");
  }
}

// Function called by callback() when a message is received 
// Passes the message topic as the "topic" parameter and the message payload as the "payload" parameter
// Calls doorX_sanityCheck() to verify that the door is in a different state than requested before taking action, else trigger a status update

void processIncomingMessage(String topic, String payload) {
  bool topic_found_in_doors = false;
  for (int i = 0; i < numDoors; i++) {
    // If topic was found in a previous door, skip
    if (!topic_found_in_doors) {
      if (topic == doors[i]->getMqttAction()) {
        // Assume payload will be found, but set to false in else statement otherwise
        topic_found_in_doors = true;

        if (payload == "OPEN") {
          // If status is enabled, check first
          if (doors[i]->statusEnabled() && doors[i]->isOpen()) {
            Serial.print("OPEN requested but ");
            Serial.print(doors[i]->getAlias());
            Serial.println(" is already open. Publishing status update instead!");
            publish_door_status(doors[i]);
          }
          else {
            Serial.print("Triggering ");
            Serial.print(doors[i]->getAlias());
            Serial.println(" OPEN relay!");
            toggleRelay(doors[i]->getOpenPin());
          }
        }

        else if (payload == "CLOSE") {
          // If status is enabled, check first
          if (doors[i]->statusEnabled() && !doors[i]->isOpen()) {
            Serial.print("CLOSE requested but ");
            Serial.print(doors[i]->getAlias());
            Serial.println(" is already closed. Publishing status update instead!");
            publish_door_status(doors[i]);
          }
          else {
            Serial.print("Triggering ");
            Serial.print(doors[i]->getAlias());
            Serial.println(" CLOSE relay!");
            toggleRelay(doors[i]->getClosePin());
          }
        }

        else if (payload == "STATE") {
          Serial.print("Publishing on-demand status update for ");
          Serial.print(doors[i]->getAlias());
          Serial.println("!");
          publish_birth_message();
          publish_door_status(doors[i]);
        }

        else { topic_found_in_doors = false; }
      }
    }
  }
  if (!topic_found_in_doors) {
    if (topic == apiTopic) {
      processAPIMessage(payload);
    } else { 
      Serial.println("Message arrived on action topic with unknown payload... taking no action!");
    }
  }
}

// Callback when MQTT message is received; calls processIncomingMessage(), passing topic and payload as parameters

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  
  Serial.println();

  String incomingTopic = topic;
  payload[length] = '\0';
  String incomingPayload = (char*)payload;
  processIncomingMessage(incomingTopic, incomingPayload);
}

// Function that serial prints the status of devices, for use in setup().

void print_device_status() {
  Serial.print("GarHAge version: ");
  Serial.println(garhageVersion);

  Serial.print("Relay Mode     : ");
  if (activeHighRelay) {
    Serial.println("Active-High");
  }
  else {
    Serial.println("Active-Low");
  }

  for (int i = 0; i < numDoors; i++) {
    Serial.print(doors[i]->getAlias());
    Serial.println(":");

    Serial.print("Status updates : ");
    if (doors[i]->statusEnabled()) {
      Serial.println("Enabled");
    }
    else { Serial.println("Disabled"); }

    Serial.print("Control        : ");
    if (doors[i]->hasControl()) {
      Serial.println("Enabled");
    }
    else { Serial.println("Disabled"); }
  }

  Serial.print("DHT Sensor     : ");
  if (dht_enabled) {
    Serial.println("Enabled");
  }
  else {
    Serial.println("Disabled");
  }
}

// Function that runs in loop() to connect/reconnect to the MQTT broker, and publish the current door statuses on connect

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(mqtt_clientId, mqtt_username, mqtt_password, availabilityTopic, 0, true, lwtMessage)) {
      Serial.println("Connected!");

      // Subscribe to API topic
      Serial.print("Subscribing to ");
      Serial.print(apiTopic);
      Serial.println("...");
      client.subscribe(apiTopic);

      // Publish discovery payloads before other messages so that entities are created first
      if (discoveryEnabled) {
        publish_ha_mqtt_discovery();
      }

      // Publish the birth message on connect/reconnect
      publish_birth_message();

      
      for (int i = 0; i < numDoors; i++) {
        // Subscribe to the action topics to listen for action messages
        Serial.print("Subscribing to ");
        Serial.print(doors[i]->getMqttAction());
        Serial.println("...");
        client.subscribe(doors[i]->getMqttAction());

        // Publish the current door status on connect/reconnect to ensure status is synced with whatever happened while disconnected
        publish_door_status(doors[i]);
      }

      // Publish the current temperature and humidity readings 
      if (dht_enabled) {
        dht_read_publish();
        dht_lastReadTime = millis();
      }

    } 
    else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println("; try again in 5 seconds...");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  // Setup the output and input pins used in the sketch
  // Set the lastStatusValue variables to the state of the status pins at setup time
  for (int i = 0; i < numDoors; i++) {
    if (doors[i]->hasControl()) {
      const int openPin = doors[i]->getOpenPin();
      const int closePin = doors[i]->getClosePin();

      pinMode(openPin, OUTPUT);
      pinMode(closePin, OUTPUT);
      if (activeHighRelay) {
        digitalWrite(openPin, LOW);
        digitalWrite(closePin, LOW);
      }
      else {
        digitalWrite(openPin, HIGH);
        digitalWrite(closePin, HIGH);
      }
    }
    if (doors[i]->statusEnabled()) {
      const int statusPin = doors[i]->getStatusPin();

      pinMode(statusPin, INPUT_PULLUP);
      doors[i]->setLastStatusValue(digitalRead(statusPin));
    }
  }

  // Setup serial output, connect to wifi, connect to MQTT broker, set MQTT message callback
  Serial.begin(115200);

  Serial.println();
  Serial.print("Starting GarHAge...");
  Serial.println();

  print_device_status();
  
  setup_wifi();

  if (web_update_enable) {
    httpUpdater.setup(&httpServer, update_username, update_password);
    httpServer.begin();
    Serial.println("Ready for HTTP Update...");
  }

  setup_ota();
  if (dht_enabled) { dht.begin(); }
  client.setServer(mqtt_broker, 1883);
  client.setCallback(callback);
}

void loop() {
  // Connect/reconnect to the MQTT broker and listen for messages
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  if (web_update_enable) {
    httpServer.handleClient();
  }

  ArduinoOTA.handle();
  
  // Check door open/closed status each loop and publish changes
  for (int i = 0; i < numDoors; i++) {
    if (doors[i]->statusEnabled()) {
      const int currentStatusValue = digitalRead(doors[i]->getStatusPin());

      if (currentStatusValue != doors[i]->getLastStatusValue()) {
        unsigned long currentTime = millis();
        // Delay for bounce
        if (currentTime - doors[i]->getLastSwitchTime() >= deadTime) {
          publish_door_status(doors[i]);
          doors[i]->setLastStatusValue(currentStatusValue);
          doors[i]->setLastSwitchTime(currentTime);
        }
      }
    }
  }

  // Run DHT function to read/publish if enabled and interval is OK
  if (dht_enabled) {
    unsigned long currentTime = millis();
    if (currentTime - dht_lastReadTime > (dht_publish_interval_s * 1000)) {
      dht_read_publish();
      dht_lastReadTime = millis();
    }
  }
  
}
