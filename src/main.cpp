#define VERSION "0.01"
#define AP_NAME "WeatherAP"

#define MQTT_SERVER "iot.makeit.lu"
#define MQTT_PORT 8883

#define DEFAULT_UPDATESERVER "192.168.0.43"
#define DEFAULT_UPDATESERVER_PORT 80
#define DEFAULT_FWUPDATEURL "/updateFW"

//#define ROOM 1 //Living room
//#define ROOM 2 //kitchen
//#define ROOM 3 //bedroom
//#define ROOM 4 //attic
//#define ROOM 5 //basement
//#define ROOM 6 //garage

#include <FS.h>

#include "config.h"
#include <ESP8266WiFi.h>
#include <MQTT.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>

#include <Wire.h>
#include <Adafruit_HTU21DF.h>


WiFiClientSecure net;
MQTTClient client(256);
//{"sensor":"HTU21D","temp":25.30846,"hum":37.68591,"room":1,"version":"0.01","rst":"External System","loc":[49.502,5.9487]}
String topic = "cs18/indoor/" + String(MQTT_USER);
unsigned long lastMillis = 0;

bool shouldSaveConfig = false;

Adafruit_HTU21DF htu21 = Adafruit_HTU21DF();
bool htu21Available = true;
float temp = -1.0;
float hum = -1.0;
int room = ROOM;
double lng = LONGITUDE;
double lat = LATITUDE;

void readConfig() {
  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          // strcpy(username, json["username"]);
          // strcpy(token, json["api_token"]);
          room = json["room"];
          lng = json["lng"];
          lat = json["lat"];

        } else {
          Serial.println("failed to load json config");
        }
      }
    } else {
      Serial.println("config.json not found");
    }
  } else {
    Serial.println("failed to mount FS");
  }
}

void writeConfig() {
  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    // json["username"] = username;
    // json["api_token"] = token;
    json["room"] = room;
    json["lng"] = lng;
    json["lat"] = lat;

    SPIFFS.begin();
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.prettyPrintTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }
}

void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

String getClientID() {
	String mac = WiFi.macAddress();
	mac.replace(":","");
  mac = "weather_" + mac;
  return mac;
}

void connect() {
  Serial.print("checking wifi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }

  Serial.print("\nconnecting...");

  char c_clientID[getClientID().length()+1];
  getClientID().toCharArray(c_clientID, sizeof(c_clientID)+1);

  while (!client.connect(c_clientID, MQTT_USER, MQTT_PASSWORD)) { //TODO replace MQTT_PASSWORD by api_token
    Serial.print(".");
    delay(1000);
  }

  Serial.println("\nconnected!");

  client.subscribe(topic+"/#");
  // client.unsubscribe("/hello");
}

void messageReceived(String &topic, String &payload) {
  Serial.println("incoming: " + topic + " - " + payload);
}

//called when an update message received. Make sure to delete the retained message after update.
boolean updateFW() {
  IPAddress updateIP;
	WiFi.hostByName(DEFAULT_UPDATESERVER, updateIP);
	Serial.print(F(" "));Serial.println(updateIP.toString().c_str());
  //use the MQTT user to find the right update file.
  t_httpUpdate_return ret = ESPhttpUpdate.update(DEFAULT_UPDATESERVER, DEFAULT_UPDATESERVER_PORT, DEFAULT_FWUPDATEURL, MQTT_USER);
	switch (ret) {
		case HTTP_UPDATE_FAILED:
			Serial.printf("\tHTTP_UPDATE_FAILD Error (%d): %s \n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
			return false;
		case HTTP_UPDATE_NO_UPDATES:
			Serial.println("\tHTTP_UPDATE_NO_UPDATES");
			return false;
		case HTTP_UPDATE_OK:
			Serial.println("\tHTTP_UPDATE_OK");
			return true;
	}
}


void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.print("Reset Reason: ");
  Serial.println(ESP.getResetReason());

  if (!htu21.begin()) {
    htu21Available = false;
    Serial.println("Sensor HTU21D missing!");
  }

  readConfig();

  String text = "<p>Device ID: <b>"+getClientID()+"</b></p>";
  char device_text[text.length()+1];
  text.toCharArray(device_text, sizeof(device_text)+1);
  String roomText = F("<p>Room values:<br><b>1</b>=Living room<br><b>2</b>=Kitchen<br><b>3</b>=Bedroom<br><b>4</b>=Attic<br><b>5</b>=Basement<br><b>6</b>=Garage</p>");
  char room_text[roomText.length()+1];
  roomText.toCharArray(room_text, sizeof(room_text)+1);

  // WiFiManagerParameter custom_user_name("user", "user name", username, 12);
  // WiFiManagerParameter custom_api_token("token", "token", token, 21);
  char roomChar[4];
  itoa (room, roomChar, 10);
  static char latChar[9];
  static char lngChar[9];
  dtostrf(lat, 8, 4, latChar);
  dtostrf(lng, 8, 4, lngChar);
  WiFiManagerParameter custom_devicename_label(device_text);
  WiFiManagerParameter custom_room_label(room_text);
  WiFiManagerParameter custom_api_room("room", "Room", roomChar, 1);
  WiFiManagerParameter custom_api_lat("lat", "Latitude", latChar, 9);
  WiFiManagerParameter custom_api_lng("lng", "Longitude", lngChar, 9);

  WiFiManager wifiManager;
  wifiManager.setTimeout(180);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setSaveParamsCallback(saveConfigCallback);
  wifiManager.addParameter(&custom_devicename_label);
  wifiManager.addParameter(&custom_room_label);
  wifiManager.addParameter(&custom_api_room);
  wifiManager.addParameter(&custom_api_lat);
  wifiManager.addParameter(&custom_api_lng);
  // wifiManager.addParameter(&custom_api_token);

  // wifiManager.autoConnect("AutoConnectAP");
  if (ESP.getResetReason() == "Power on" || ESP.getResetReason() == "External System") {
    if (!wifiManager.startConfigPortal(AP_NAME)) {
      Serial.println("failed to connect and hit timeout");
    }
  }

  // strcpy(token, custom_api_token.getValue());
  // strcpy(username, custom_user_name.getValue());
  room = atoi(custom_api_room.getValue());
  lat = atof(custom_api_lat.getValue());
  lng = atof(custom_api_lng.getValue());

  if (shouldSaveConfig) {
    writeConfig();
  }

  // Serial.print("User name: ");
  // Serial.println(username);
  // Serial.print("Token: ");
  // Serial.println(token);

  // Note: Local domain names (e.g. "Computer.local" on OSX) are not supported by Arduino.
  // You need to set the IP address directly.
  //
  // MQTT brokers usually use port 8883 for secure connections.
  client.begin(MQTT_SERVER, MQTT_PORT, net);
  client.onMessage(messageReceived);

  connect();

  //check update
  updateFW();
}

void loop() {
  client.loop();
  delay(10);  // <- fixes some issues with WiFi stability

  if (!client.connected()) {
    connect();
  }

  // // publish a message roughly every second.
  // if (millis() - lastMillis > 60000) {
  //   lastMillis = millis();
  //   //measure and transmit
  //
  // }

  if (htu21Available) {
    temp = htu21.readTemperature();
    hum = htu21.readHumidity();

    StaticJsonBuffer<256> jsonBuffer;

    JsonObject& msg = jsonBuffer.createObject();
    msg["sensor"] = "HTU21D";
    msg["temp"] = temp;
    msg["hum"] = hum;
    msg["room"] = room;
    msg["version"] = VERSION;
    msg["rst"] = ESP.getResetReason();

    JsonArray& location = msg.createNestedArray("loc");
    location.add(lat);
    location.add(lng);

    Serial.print(topic);
    Serial.print(":");
    msg.printTo(Serial);

    String output;
    msg.printTo(output);

    client.publish(topic, output);
    delay(100);
  }

  //deepsleep 10minutes
  ESP.deepSleep(10*60*1000000);
}
