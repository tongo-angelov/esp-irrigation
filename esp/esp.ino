#include <Preferences.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <RTClib.h>

#include "wifi.h"

DS1302 rtc(27, 25, 26);

// IO PINS
const int LIQUID_SENSOR = 33;
const int SOIL_SENSOR_ONE = 34;
const int SOIL_SENSOR_TWO = 35;
const int RELAY_ONE = 22;
const int RELAY_TWO = 23;

// INVERTED RELAY STATE
const int RELAY_ON = LOW;
const int RELAY_OFF = HIGH;

// PREFERENCES' IDS
const char *APP = "irrigation";
const char *APP_LIQUID = "liquid";
const char *APP_TARGET_ONE = "one";
const char *APP_TARGET_TWO = "two";
const char *APP_TARGET_TIME = "time";
const char *APP_TARGET_TIMEOUT = "timeout";

Preferences preferences;

// WIFI
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWD;

// WEBSOCKETS
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// VARS
int sensor = 1;
int liquid = 0;
const int target = 1000;

int sensorOne = 0;
int targetOne = 2000;

int sensorTwo = 0;
int targetTwo = 2000;

char timeBuf[9];
char targetTime[] = "18:00:00";
int timeout = 30;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>ESP Input Form</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  </head><body>
  <h1>ESP</h1>
</body></html>)rawliteral";

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    data[len] = 0;
    processCommand((char *)data);
  }
}

void processCommand(char *json)
{
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, json);

  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  JsonObject data = doc["data"];

  Serial.println("Setting preferences");
  preferences.begin(APP, false);

  if (!data["liquid"].isNull())
  {
    int target = data["liquid"] == "true" ? 1 : 0;
    if (sensor != target)
    {
      sensor = target;
      preferences.putBool(APP_LIQUID, sensor);
      Serial.println("liquid = " + String(sensor));
    }
  }
  else if (!data["one"].isNull())
  {
    int target = data["one"];
    if (targetOne != target && target > 0 && target < 4095)
    {
      targetOne = target;
      preferences.putInt(APP_TARGET_ONE, targetOne);
      Serial.println("targetOne = " + String(targetOne));
    }
  }
  else if (!data["two"].isNull())
  {
    int target = data["two"];
    if (targetTwo != target && target > 0 && target < 4095)
    {
      targetTwo = target;
      preferences.putInt(APP_TARGET_TWO, targetTwo);
      Serial.println("targetTwo = " + String(targetTwo));
    }
  }
  else if (!data["timeout"].isNull())
  {
    int target = data["timeout"];
    if (timeout != target && target > 0 && target < 600)
    {
      timeout = target;
      preferences.putInt(APP_TARGET_TIMEOUT, timeout);
      Serial.println("timeout = " + String(timeout));
    }
  }
  else if (!data["target"].isNull())
  {
    String sTargetTime = data["target"];
    sTargetTime.toCharArray(targetTime, sizeof(targetTime));
    preferences.putString(APP_TARGET_TIME, targetTime);
    Serial.println("targetTime = " + String(targetTime));
  }

  preferences.end();
  Serial.println("Preferences set");
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    Serial.println();
    break;
  case WS_EVT_DISCONNECT:
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
    Serial.println();
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}

void initWebSocket()
{
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void setup()
{
  Serial.begin(115200);

  pinMode(LIQUID_SENSOR, INPUT);
  pinMode(SOIL_SENSOR_ONE, INPUT);
  pinMode(SOIL_SENSOR_TWO, INPUT);

  pinMode(RELAY_ONE, OUTPUT);
  pinMode(RELAY_TWO, OUTPUT);

  initWiFi();

  Serial.println("Loading defaults");
  preferences.begin(APP, false);

  sensor = preferences.getBool(APP_LIQUID, sensor);
  Serial.print("sensor = ");
  Serial.println(sensor);

  targetOne = preferences.getInt(APP_TARGET_ONE, targetOne);
  Serial.print("targetOne = ");
  Serial.println(targetOne);

  targetTwo = preferences.getInt(APP_TARGET_TWO, targetTwo);
  Serial.print("targetTwo = ");
  Serial.println(targetTwo);

  timeout = preferences.getInt(APP_TARGET_TIMEOUT, timeout);
  Serial.print("timeout = ");
  Serial.println(timeout);

  String sTargetTime = preferences.getString(APP_TARGET_TIME, targetTime);
  sTargetTime.toCharArray(targetTime, sizeof(targetTime));
  Serial.print("target = ");
  Serial.println(targetTime);

  preferences.end();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/html", index_html); });

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  initWebSocket();
  server.begin();

  rtc.begin();
  if (!rtc.isrunning())
  {
    Serial.println("RTC is NOT running!");
    rtc.adjust(DateTime(__DATE__, __TIME__));
  }
}

void initWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

void sendStatus()
{
  String send = "";
  StaticJsonDocument<400> json;
  json["type"] = "status";

  JsonObject jsonData = json.createNestedObject("data");

  jsonData["LIQUID_SENSOR"] = String(liquid);
  jsonData["LIQUID_ACTIVE"] = String(sensor);
  jsonData["SOIL_SENSOR_ONE"] = String(sensorOne);
  jsonData["SOIL_TARGET_ONE"] = String(targetOne);
  jsonData["SOIL_SENSOR_TWO"] = String(sensorTwo);
  jsonData["SOIL_TARGET_TWO"] = String(targetTwo);
  jsonData["ESP_TIME"] = timeBuf;
  jsonData["TARGET"] = targetTime;
  jsonData["TIMEOUT"] = String(timeout);

  serializeJson(json, send);
  ws.textAll(send);
}

void loop()
{
  liquid = analogRead(LIQUID_SENSOR);
  sensorOne = analogRead(SOIL_SENSOR_ONE);
  sensorTwo = analogRead(SOIL_SENSOR_TWO);

  if (!sensor || liquid > target)
  {
    if (sensorOne > targetOne)
    {
      digitalWrite(RELAY_ONE, RELAY_ON);
    }
    else
    {
      digitalWrite(RELAY_ONE, RELAY_OFF);
    }

    if (sensorTwo > targetTwo)
    {
      digitalWrite(RELAY_TWO, RELAY_ON);
    }
    else
    {
      digitalWrite(RELAY_TWO, RELAY_OFF);
    }
  }
  else
  {
    digitalWrite(RELAY_ONE, RELAY_OFF);
    digitalWrite(RELAY_TWO, RELAY_OFF);
  }

  sendStatus();

  DateTime now = rtc.now();

  sprintf(timeBuf, "%02d:%02d:%02d",
          now.hour(),
          now.minute(),
          now.second());

  // Serial.println(timeBuf);

  delay(1000);
}
