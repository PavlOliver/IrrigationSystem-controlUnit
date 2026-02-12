#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <Arduino_GFX_Library.h>
#include <XPT2046_Touchscreen.h>
#include "Start.h"
#include "Task.h"
#include "DHT.h"
#include "config.h"
#include <ArduinoOTA.h>

#define TFT_SCK 18
#define TFT_MOSI 23
#define TFT_MISO 19
#define TFT_CS 22
#define TFT_DC 21
#define TFT_RESET 17
#define CS_PIN 5

#define DHTPIN 2
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

int wasSend = -1;
time_t now;

std::vector<Start> starts;
std::vector<Task> tasks;
std::vector<PinStart> mStarts;

unsigned long lastTime = 0;
unsigned long timerDelay = 10000;

int lastLog = 0;
Start s;

State PINS[] = { { 27, false }, { 16, false }, { 4, false }, { 13, false } };
const size_t CAPACITY = JSON_ARRAY_SIZE(1024);
StaticJsonDocument<CAPACITY> doc;

Arduino_ESP32SPI bus = Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO);
Arduino_ILI9341 display = Arduino_ILI9341(&bus, TFT_RESET);
XPT2046_Touchscreen ts(CS_PIN);

HTTPClient http;
struct tm timeinfo;

Date skipDay;


void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.println("Connecting" + String(ssid));
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());

  //display setup
  display.begin();
  display.setRotation(1);
  display.fillScreen(GREEN);

  pinMode(33, OUTPUT);
  pinMode(35, INPUT);
  digitalWrite(33, HIGH);

  display.setTextSize(2);
  display.setTextColor(WHITE);

  for (auto pin : PINS) {
    pinMode(pin.pin, OUTPUT);
    digitalWrite(pin.pin, HIGH);
  }
  configTime(0, 0, "europe.pool.ntp.org");
  setenv("TZ", "CET-1CEST,M3.5.0/02,M10.5.0/03", 1);
  tzset();
  dht.begin();
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("OTA začalo: " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA dokončené!");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA chyba[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Autentifikácia zlyhala");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Začiatok zlyhal");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Pripojenie zlyhalo");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Prijímanie zlyhalo");
    else if (error == OTA_END_ERROR) Serial.println("Ukončenie zlyhalo");
  });
  ArduinoOTA.begin();
}

void loop() {
  ArduinoOTA.handle();
  if (getLocalTime(&timeinfo)) {
    now = mktime(&timeinfo);
    //Serial.println(now);
    //Serial.println(lastTime);
    //Serial.println();

    if ((millis() - lastTime) >= timerDelay) {
      lastTime = millis();
      now = mktime(&timeinfo);
      //Serial.println("timeee:" + String(now));
      if (WiFi.status() == WL_CONNECTED) {
        String serverPath = serverName + "/api/auto/" + apikey;

        getAuto();

        getManual();

        for (int i = 0; i < 4; i++) {
          Serial.println(String(PINS[i].pin) + " = " + String(PINS[i].state));
        }
        for (int i = 0; i < 4; i++) {
          display.setCursor(20, 20 + i * 30);
          display.fillRect(20, 20 + i * 30, 200, 30, GREEN);
          display.setTextSize(2);
          display.setTextColor(WHITE);
          display.print(String(PINS[i].pin) + " = " + String(PINS[i].state));
        }
        if (!(skipDay.dayOfYear == timeinfo.tm_yday && skipDay.year == timeinfo.tm_year)) {
          for (ZoneStart st : s.getStarts()) {
            if (st.daysOfWeek.charAt((timeinfo.tm_wday + 6) % 7) == '1' && st.hours == timeinfo.tm_hour && st.minutes == timeinfo.tm_min) {
              if (getState(st.pin) == false) {
                changeState(st.pin, true);

                delay(10);
                //Serial.println(String(st.zone) + " is being Watered" + getState(st.pin));
                Task t;

                t.pin = st.pin;
                t.zone = st.zone;
                t.endTime = now + (st.duration * 60);
                tasks.push_back(t);
                // Serial.println("duration:" + st.duration);
                //Serial.println("End on timeeee: " + String(t.endTime));

                http.begin(serverPath.c_str());
                http.addHeader("Content-Type", "application/json");

                http.POST(autoStartString(st.program, st.zone, st.duration));
                http.end();
              }
            }
          }
        }
        tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
                                   [&](const Task& task) {
                                     if (now >= task.endTime) {
                                       if (getState(task.pin)) {
                                         changeState(task.pin, false);

                                         http.begin(serverPath.c_str());
                                         http.addHeader("Content-Type", "application/json");

                                         http.POST(autoStop(task.zone));
                                         http.end();

                                         return true;
                                       }
                                       return false;
                                     }
                                     return false;
                                   }),
                    tasks.end());
        //Serial.println(dht.readTemperature());
        if (wasSend != timeinfo.tm_hour) {
          float humidity = dht.readHumidity();
          float temp = dht.readTemperature();
          if (humidity <= 100 && temp < 50) {
            http.begin((serverName + "/api/weather").c_str());
            http.addHeader("Content-Type", "application/json");
            int moisture = 100 - map(analogRead(35), 1350, 3750, 0, 100);
            http.POST(sendWeather(temp, humidity, moisture));
            http.end();
            wasSend = timeinfo.tm_hour;
          }
        }
      } else {
        Serial.println("WiFi Disconnected");
      }
    }
  } else {
    Serial.println("Failed to obtain time");
  }
}

void changeState(int pinValue, bool newState) {
  for (auto& pinState : PINS) {
    if (pinState.pin == pinValue) {
      pinState.state = newState;
      digitalWrite(pinState.pin, !pinState.state);
      //Serial.println("new state is " + String(pinState.state));
    }
  }
}

bool getState(int pinValue) {
  for (auto& pinState : PINS) {
    if (pinState.pin == pinValue) {
      return pinState.state;
    }
  }
  return false;
}

String autoStartString(String program, String zone, int duration) {
  StaticJsonDocument<200> docToSend;
  docToSend["program"] = program;
  docToSend["zone"] = zone;
  docToSend["duration"] = duration;
  docToSend["action"] = "history";
  String dataToSend;
  serializeJson(docToSend, dataToSend);
  return dataToSend;
}

String autoStop(String zone) {
  StaticJsonDocument<200> docToSend;
  docToSend["action"] = "stop";
  docToSend["zone"] = zone;
  String dataToSend;
  serializeJson(docToSend, dataToSend);
  return dataToSend;
}

String sendWeather(float temperature, int humidity, int moisture) {
  StaticJsonDocument<200> docToSend;
  docToSend["apikey"] = apikey;
  docToSend["temp"] = temperature;
  docToSend["humidity"] = humidity;
  docToSend["moisture"] = moisture;
  String dataToSend;
  serializeJson(docToSend, dataToSend);
  return dataToSend;
}

void sendManualStart() {
  http.begin((serverName + "/api/manual").c_str());
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<200> docToSend;
  docToSend["action"] = "Start";
  String dataToSend;
  serializeJson(docToSend, dataToSend);
  http.POST(dataToSend);
  http.end();
}

void getManual() {
  http.begin((serverName + "/api/manual/" + apikey).c_str());
  http.GET();

  DynamicJsonDocument doc(1024);
  String dataFromServer = http.getString();
  http.end();
  deserializeJson(doc, dataFromServer);
  JsonArray jsonArray = doc.as<JsonArray>();
  for (int i = 0; i < jsonArray.size(); ++i) {
    JsonObject obj = jsonArray[i];
    int pin = obj["pin"];
    String title = obj["title"];
    bool running = obj["running"];

    if (getState(pin) && !running) {
      changeState(pin, false);
      mStarts.erase(std::remove_if(mStarts.begin(), mStarts.end(),
                                   [&](const PinStart& ps) {
                                     if (ps.pin == pin) {
                                       sendDuration(ps);
                                       return true;
                                     }
                                     return false;
                                   }),
                    mStarts.end());
    } else if (!getState(pin) && running) {
      changeState(pin, true);
      PinStart ps;
      ps.startTime = now;
      ps.pin = pin;
      mStarts.push_back(ps);
    }
  }
}

void sendDuration(PinStart pinStart) {
  StaticJsonDocument<200> docToSend;
  docToSend["action"] = "update";
  docToSend["duration"] = now - pinStart.startTime;
  docToSend["pin"] = pinStart.pin;
  String dataToSend;
  serializeJson(docToSend, dataToSend);
  http.begin((serverName + "/api/manual/" + apikey).c_str());
  http.addHeader("Content-Type", "application/json");

  http.POST(dataToSend);
  http.end();
}

void getAuto() {
  http.begin((serverName + "/api/auto/" + apikey + "?log=" + String(lastLog)).c_str());
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    String payload = http.getString();
    http.end();
    if (payload.charAt(0) == '1') {
      skipDay.dayOfYear = timeinfo.tm_yday;
      skipDay.year = timeinfo.tm_year;
    }
    if (payload.length() > 1) {
      s.getStarts().clear();
      deserializeJson(doc, payload);
      JsonArray jsonArray = doc.as<JsonArray>();
      for (int i = 0; i < jsonArray.size(); ++i) {
        JsonObject obj = jsonArray[i];
        if (obj["log"]) {
          lastLog = obj["log"];
          // Serial.print("Loggggg ");
          // Serial.println(lastLog);
        }
        JsonArray startsArray = obj["starts"];
        JsonArray programsArray = obj["programs"];
        JsonArray durationsArray = obj["durations"];
        int pin = obj["pin"];
        String title = obj["title"];
        int skp = obj["skip"];
        if (skp == 1) {
          skipDay.dayOfYear = timeinfo.tm_yday;
          skipDay.year = timeinfo.tm_year;
        }
        s.loadFromString(startsArray, programsArray, durationsArray, pin, title);
        //Serial.println(s.toString());
      }
    }
  }
}
