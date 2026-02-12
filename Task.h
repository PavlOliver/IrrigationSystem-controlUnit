struct Task {
  int endHours;
  int endMinutes;
  time_t endTime;
  int pin;
  String zone;
};

struct State {
  const int pin;
  bool state;
};

struct Date {
  int dayOfYear;
  int year;
};

struct PinStart {
  time_t startTime;
  int pin;
};