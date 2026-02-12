#include "HardwareSerial.h"
#include "WString.h"
#include <cstdlib>
#include <string>
#include <iostream>
#include "Start.h"
#include <Arduino.h>

void Start::loadFromString(JsonArray starts, JsonArray programs, JsonArray durations, int pin, String zone) {
  ZoneStart zs;
  for (int i = 0; i < starts.size(); i++) {
    zs.zone = zone;
    zs.pin = pin;
    String start = starts[i].as<String>();
    zs.daysOfWeek = toBinary(start.substring(0, 3).toInt());
    zs.hours = start.substring(3, 5).toInt();
    zs.minutes = start.substring(5, 7).toInt();
    zs.program = programs[i].as<String>();
    zs.duration = durations[i];
    this->starts.push_back(zs);
  }
}

String Start::toBinary(int number) {
  String binary = "";
  for (int i = 6; i >= 0; i--) {
    //porovna s and binarnu reprezentaciu number s 1 na mieste i.
    if (number & 1 << i) {
      binary += '1';
    } else {
      binary += '0';
    }
  }
  return binary;
}

String Start::toString() {
  String str = "#####\n";
  for (auto st : this->starts) {
    str += st.zone + "(" + String(st.pin) + ")-" + st.program + "\n" + String(st.hours) + ":" + String(st.minutes) + " " + st.daysOfWeek + +", duration: " + String(st.duration) + " mins" + "\n#####\n";
  }
  return str;
}

std::vector<ZoneStart>& Start::getStarts() {
  return this->starts;
}