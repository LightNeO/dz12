#include <Arduino.h>

namespace {
constexpr uint8_t PWM_OUTPUT_PIN = 9;
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr int MIN_PPM = 400;
constexpr int MAX_PPM = 5000;
int simulatedPpm = 400;
char input[48] = {};
uint8_t inputLength = 0;

void applyPpm() {
  const long duty = map(simulatedPpm, MIN_PPM, MAX_PPM, 0, 255);
  analogWrite(PWM_OUTPUT_PIN, constrain(duty, 0L, 255L));
}

void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  set <400..5000>"));
  Serial.println(F("  inc [ppm]"));
  Serial.println(F("  dec [ppm]"));
  Serial.println(F("  status"));
  Serial.println(F("  help"));
}

bool parseNumber(const String& text, long& number) {
  if (text.length() == 0) return false;
  char* end = nullptr;
  number = strtol(text.c_str(), &end, 10);
  return end != text.c_str() && *end == '\0';
}

void handleCommand(String command) {
  command.trim();
  if (command == "help") {
    printHelp();
    return;
  }
  if (command == "status") {
    Serial.print(F("AGENT ppm="));
    Serial.print(simulatedPpm);
    Serial.println(F(" output=D9_PWM"));
    return;
  }

  const int separator = command.indexOf(' ');
  const String operation = separator < 0 ? command : command.substring(0, separator);
  const String argument = separator < 0 ? String() : command.substring(separator + 1);
  long value = 0;

  if (operation == "set" && parseNumber(argument, value)) {
    simulatedPpm = static_cast<int>(constrain(value, static_cast<long>(MIN_PPM), static_cast<long>(MAX_PPM)));
  } else if ((operation == "inc" || operation == "dec") &&
             (argument.length() == 0 || parseNumber(argument, value))) {
    if (argument.length() == 0) value = 100;
    simulatedPpm += operation == "inc" ? value : -value;
    simulatedPpm = constrain(simulatedPpm, MIN_PPM, MAX_PPM);
  } else {
    Serial.println(F("ERROR use: set <ppm>, inc [ppm], dec [ppm], status, help"));
    return;
  }

  applyPpm();
  Serial.print(F("AGENT OK ppm="));
  Serial.println(simulatedPpm);
}

void readSerial() {
  while (Serial.available()) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\n' || ch == '\r') {
      if (inputLength > 0) {
        input[inputLength] = '\0';
        handleCommand(String(input));
        inputLength = 0;
      }
    } else if (inputLength < sizeof(input) - 1) {
      input[inputLength++] = ch;
    } else {
      inputLength = 0;
      Serial.println(F("ERROR command too long"));
    }
  }
}
}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  pinMode(PWM_OUTPUT_PIN, OUTPUT);
  applyPpm();
  Serial.println(F("AirGuard sensor agent ready"));
  Serial.println(F("D9 PWM maps 400..5000 ppm"));
  printHelp();
}

void loop() {
  readSerial();
}
