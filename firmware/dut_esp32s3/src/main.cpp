#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

#define MQTT_ENABLED 1
#if MQTT_ENABLED
#include <PubSubClient.h>
#endif

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

namespace {
constexpr uint8_t CO2_ADC_PIN = 4;
constexpr uint8_t ALARM_LED_PIN = 2;
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t DEFAULT_PERIOD_MS = 30000;
constexpr uint32_t MIN_PERIOD_MS = 5000;
constexpr uint32_t MAX_PERIOD_MS = 300000;
constexpr int MIN_PPM = 400;
constexpr int MAX_PPM = 5000;

struct Config {
  int alarmThresholdPpm = 1000;
  int hysteresisPpm = 100;
  uint32_t measurePeriodMs = DEFAULT_PERIOD_MS;
  char mqttTopic[64] = "test/airguard/01";
};

Preferences preferences;
Config config;
bool alarmActive = false;
int latestCo2Ppm = MIN_PPM;
uint32_t lastMeasureMs = 0;
uint32_t lastWifiAttemptMs = 0;
uint32_t lastMqttAttemptMs = 0;
char commandBuffer[128] = {};
size_t commandLength = 0;

#if MQTT_ENABLED
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
#endif

void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  help"));
  Serial.println(F("  status"));
  Serial.println(F("  version"));
  Serial.println(F("  sensor co2"));
  Serial.println(F("  config get"));
  Serial.println(F("  config set alarm_threshold_ppm <400..5000>"));
  Serial.println(F("  config set hysteresis_ppm <50..500>"));
  Serial.println(F("  config set measure_period_s <5..300>"));
  Serial.println(F("  config set mqtt_topic <topic>"));
  Serial.println(F("  config save"));
  Serial.println(F("  factory_reset"));
  Serial.println(F("  reboot"));
}

void setDefaults() {
  config = Config{};
}

void loadConfig() {
  preferences.begin("airguard", true);
  config.alarmThresholdPpm = preferences.getInt("threshold", 1000);
  config.hysteresisPpm = preferences.getInt("hysteresis", 100);
  config.measurePeriodMs = preferences.getULong("period_ms", DEFAULT_PERIOD_MS);
  String topic = preferences.getString("topic", "test/airguard/01");
  topic.toCharArray(config.mqttTopic, sizeof(config.mqttTopic));
  preferences.end();
  if (config.alarmThresholdPpm < MIN_PPM || config.alarmThresholdPpm > MAX_PPM ||
      config.hysteresisPpm < 50 || config.hysteresisPpm > 500 ||
      config.measurePeriodMs < MIN_PERIOD_MS || config.measurePeriodMs > MAX_PERIOD_MS ||
      config.mqttTopic[0] == '\0') {
    setDefaults();
  }
}

void saveConfig() {
  preferences.begin("airguard", false);
  preferences.putInt("threshold", config.alarmThresholdPpm);
  preferences.putInt("hysteresis", config.hysteresisPpm);
  preferences.putULong("period_ms", config.measurePeriodMs);
  preferences.putString("topic", config.mqttTopic);
  preferences.end();
  Serial.println(F("CONFIG SAVED"));
}

int readCo2Ppm() {
  // The Nano output is 0..3.3 V after the divider. Averaging reduces PWM ripple.
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 16; ++i) {
    sum += analogRead(CO2_ADC_PIN);
    delayMicroseconds(200);
  }
  const int adc = static_cast<int>(sum / 16);
  const long ppm = map(adc, 0, 4095, MIN_PPM, MAX_PPM);
  return static_cast<int>(constrain(ppm, static_cast<long>(MIN_PPM), static_cast<long>(MAX_PPM)));
}

void updateAlarm(int co2Ppm) {
  if (!alarmActive && co2Ppm > config.alarmThresholdPpm) {
    alarmActive = true;
  } else if (alarmActive && co2Ppm < config.alarmThresholdPpm - config.hysteresisPpm) {
    alarmActive = false;
  }
  digitalWrite(ALARM_LED_PIN, alarmActive ? HIGH : LOW);
}

void publishMeasurement() {
  // Temperature/humidity are null until an SHT31 is installed.
  Serial.printf("MQTT topic=%s {\"co2_ppm\":%d,\"temp_c\":null,\"humi_rh\":null,\"alarm\":\"%s\",\"device_id\":\"airguard-01\",\"fw\":\"v1.2.0\",\"ts\":%lu}\n",
                config.mqttTopic, latestCo2Ppm, alarmActive ? "triggered" : "idle",
                millis() / 1000UL);
#if MQTT_ENABLED
  if (mqttClient.connected()) {
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"co2_ppm\":%d,\"temp_c\":null,\"humi_rh\":null,\"alarm\":\"%s\",\"device_id\":\"airguard-01\",\"fw\":\"v1.2.0\",\"ts\":%lu}",
             latestCo2Ppm, alarmActive ? "triggered" : "idle", millis() / 1000UL);
    mqttClient.publish(config.mqttTopic, payload, true);
  }
#endif
}

void printStatus() {
  Serial.printf("STATUS {\"co2_ppm\":%d,\"alarm\":\"%s\",\"threshold\":%d,\"hysteresis\":%d,\"period_s\":%lu,\"wifi\":\"%s\",\"free_heap\":%u}\n",
                latestCo2Ppm, alarmActive ? "triggered" : "idle",
                config.alarmThresholdPpm, config.hysteresisPpm,
                config.measurePeriodMs / 1000UL,
                WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
                ESP.getFreeHeap());
}

bool parseIntStrict(const String& value, long& result) {
  if (value.length() == 0) return false;
  char* end = nullptr;
  result = strtol(value.c_str(), &end, 10);
  return end != value.c_str() && *end == '\0';
}

void handleCommand(String command) {
  command.trim();
  if (command == "help") {
    printHelp();
  } else if (command == "status") {
    latestCo2Ppm = readCo2Ppm();
    updateAlarm(latestCo2Ppm);
    printStatus();
  } else if (command == "version") {
    Serial.println(F("VERSION v1.2.0"));
  } else if (command == "sensor co2") {
    Serial.printf("CO2 %d ppm\n", readCo2Ppm());
  } else if (command == "config get") {
    Serial.printf("CONFIG threshold=%d hysteresis=%d period_s=%lu topic=%s\n",
                  config.alarmThresholdPpm, config.hysteresisPpm,
                  config.measurePeriodMs / 1000UL, config.mqttTopic);
  } else if (command == "config save") {
    saveConfig();
  } else if (command == "factory_reset") {
    preferences.begin("airguard", false);
    preferences.clear();
    preferences.end();
    setDefaults();
    Serial.println(F("FACTORY RESET OK, rebooting"));
    delay(100);
    ESP.restart();
  } else if (command == "reboot") {
    Serial.println(F("REBOOTING"));
    delay(100);
    ESP.restart();
  } else if (command.startsWith("config set ")) {
    const int separator = command.indexOf(' ', 11);
    if (separator < 0) {
      Serial.println(F("CONFIG ERROR expected key and value"));
      return;
    }
    const String key = command.substring(11, separator);
    const String value = command.substring(separator + 1);
    long number = 0;
    if (key == "mqtt_topic") {
      if (value.length() == 0 || value.length() >= sizeof(config.mqttTopic) || value.indexOf(' ') >= 0) {
        Serial.println(F("CONFIG ERROR invalid topic"));
      } else {
        value.toCharArray(config.mqttTopic, sizeof(config.mqttTopic));
        Serial.println(F("CONFIG OK mqtt_topic"));
      }
    } else if (!parseIntStrict(value, number)) {
      Serial.println(F("CONFIG ERROR invalid integer"));
    } else if (key == "alarm_threshold_ppm" && number >= MIN_PPM && number <= MAX_PPM) {
      config.alarmThresholdPpm = number;
      Serial.println(F("CONFIG OK alarm_threshold_ppm"));
    } else if (key == "hysteresis_ppm" && number >= 50 && number <= 500) {
      config.hysteresisPpm = number;
      Serial.println(F("CONFIG OK hysteresis_ppm"));
    } else if (key == "measure_period_s" && number >= 5 && number <= 300) {
      config.measurePeriodMs = static_cast<uint32_t>(number) * 1000UL;
      Serial.println(F("CONFIG OK measure_period_s"));
    } else {
      Serial.println(F("CONFIG ERROR unknown key or value out of range"));
    }
  } else if (command.length() > 0) {
    Serial.println(F("ERROR unknown command"));
  }
}

void readSerial() {
  while (Serial.available()) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\n' || ch == '\r') {
      if (commandLength > 0) {
        commandBuffer[commandLength] = '\0';
        handleCommand(String(commandBuffer));
        commandLength = 0;
      }
    } else if (commandLength < sizeof(commandBuffer) - 1) {
      commandBuffer[commandLength++] = ch;
    } else {
      commandLength = 0;
      Serial.println(F("ERROR command too long"));
    }
  }
}

void maintainNetwork() {
#if MQTT_ENABLED
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWifiAttemptMs >= 10000) {
      lastWifiAttemptMs = millis();
      Serial.printf("WIFI connecting ssid=%s\n", AIRGUARD_WIFI_SSID);
      WiFi.begin(AIRGUARD_WIFI_SSID, AIRGUARD_WIFI_PASSWORD);
    }
    return;
  }
  if (!mqttClient.connected() && millis() - lastMqttAttemptMs >= 5000) {
    lastMqttAttemptMs = millis();
    Serial.printf("MQTT connecting host=%s port=%u\n", AIRGUARD_MQTT_HOST, AIRGUARD_MQTT_PORT);
    const bool connected = mqttClient.connect("airguard-01", AIRGUARD_MQTT_USER, AIRGUARD_MQTT_PASSWORD);
    if (connected) {
      Serial.println(F("MQTT connected"));
    } else {
      Serial.printf("MQTT connect failed state=%d\n", mqttClient.state());
    }
  }
  mqttClient.loop();
#else
  (void)lastWifiAttemptMs;
#endif
}
}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  pinMode(ALARM_LED_PIN, OUTPUT);
  digitalWrite(ALARM_LED_PIN, LOW);
  analogReadResolution(12);
  loadConfig();

#if MQTT_ENABLED
  WiFi.mode(WIFI_STA);
  mqttClient.setServer(AIRGUARD_MQTT_HOST, AIRGUARD_MQTT_PORT);
#endif

  Serial.println(F("\n=== AirGuard C1 boot ==="));
  Serial.println(F("fw_version=v1.2.0 device_id=airguard-01"));
  Serial.printf("config: alarm_threshold_ppm=%d hysteresis_ppm=%d period_s=%lu\n",
                config.alarmThresholdPpm, config.hysteresisPpm,
                config.measurePeriodMs / 1000UL);
  Serial.println(F("App started"));
  Serial.printf("WIFI target=%s MQTT broker=%s:%u\n", AIRGUARD_WIFI_SSID,
                AIRGUARD_MQTT_HOST, AIRGUARD_MQTT_PORT);
  lastMeasureMs = millis() - config.measurePeriodMs;
}

void loop() {
  readSerial();
  maintainNetwork();

  if (millis() - lastMeasureMs >= config.measurePeriodMs) {
    lastMeasureMs = millis();
    latestCo2Ppm = readCo2Ppm();
    updateAlarm(latestCo2Ppm);
    publishMeasurement();
  }
  delay(2);
}
