/*   
 * GarHAge
 * a Home-Automation-friendly ESP8266-based MQTT Garage Door Controller
 * Licensed under the MIT License, Copyright (c) 2017 marthoc
 * 
 * User-configurable Parameters 
*/

// Wifi Parameters

#define WIFI_SSID "your-ssid-name"
#define WIFI_PASSWORD "your-ssid-password"

// Static IP Parameters

#define STATIC_IP false
#define IP 192,168,1,100
#define GATEWAY 192,168,1,1
#define SUBNET 255,255,255,0

// Web Update Parameters
#define WEB_UPDATE_ENABLED false
#define WEB_UPDATE_USER "your-web-update-name"
#define WEB_UPDATE_PASS "your-web-update-password"

// MQTT Parameters

#define MQTT_BROKER "w.x.y.z"
#define MQTT_CLIENTID "GarHAge"
#define MQTT_USERNAME "your-mqtt-username"
#define MQTT_PASSWORD "your-mqtt-password"
#define HA_MQTT_DISCOVERY false
#define HA_MQTT_DISCOVERY_PREFIX "homeassistant"

// Relay Parameters

#define ACTIVE_HIGH_RELAY true
#define RELAY_ACTIVE_TIME 500

// Temperature and Humidity Sensor Parameters

#define DHT_ENABLED false
#define DHTPIN D7
#define DHTTYPE DHT11 // or: DHT21 or DHT22
#define MQTT_TEMPERATURE_TOPIC "garage/temperature"
#define MQTT_HUMIDITY_TOPIC "garage/humidity"
#define DHT_PUBLISH_INTERVAL 300
#define DHT_TEMPERATURE_CELSIUS true
#define DHT_TEMPERATURE_ALIAS "Garage Temperature"
#define DHT_HUMIDITY_ALIAS "Garage Humidity"
