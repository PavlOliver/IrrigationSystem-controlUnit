#include <string>
#include <vector>
#include <ArduinoJson.h>

struct ZoneStart {
  String daysOfWeek;
  String program;
  int hours;
  int minutes;
  String zone;
  int pin;
  int duration;
};

class Start {
private:
  std::vector<ZoneStart> starts;
  int dayOfWeek;
public:
  void loadFromString(JsonArray starts, JsonArray programs, JsonArray durations, int pin, String zone);
  String toBinary(int number);
  String toString();
  std::vector<ZoneStart>& getStarts();
};