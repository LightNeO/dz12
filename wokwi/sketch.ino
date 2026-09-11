/*
 * AirGuard C1 — Wokwi-модель ключового тестового сценарію CS-1
 * «CO2 → тривога → MQTT-повідомлення» (dz12, Частина 2)
 *
 * Модель імітує прошивку AirGuard C1 та підсистеми HIL-стенда:
 *   - Потенціометр (GPIO34) = DAC-ін'єкція HIL-агента (set_dac): 0–3.3 В → 400–5000 ppm
 *   - LED (GPIO2)            = RGB-індикатор/тривога: HIGH = alarm triggered
 *   - Serial (115200)        = UART CLI/лог DUT: boot-банер, статус, JSON-payload (замість WiFi/MQTT)
 *
 * Відповідність вимогам PRD, які демонструє модель:
 *   FR-001  мапінг аналогового сигналу → ppm (лінійний, як у прошивці)
 *   FR-007  тривога при CO2 > threshold (default 1000 ppm)
 *   FR-008  гістерезис скидання тривоги (threshold - hysteresis, default 100 ppm)
 *   FR-011  структура MQTT JSON-payload (друкується у Serial як payload перед publish)
 *   FR-003  CLI: команди status / config set / version через Serial Monitor
 *
 * Керування у Serial Monitor (115200):
 *   - введіть "status"  → JSON-статус пристрою
 *   - введіть "config set alarm_threshold_ppm 800" → зміна порогу (FR-004)
 *   - введіть "version" → версія прошивки
 *   - поверніть потенціометр: >1000 ppm → LED (тревога), <900 ppm → LED off
 */

#include <ArduinoJson.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ---------- Конфіг (як у AirGuard C1, FR-004) ----------
int   alarmThresholdPpm = 1000;   // alarm_threshold_ppm
int   hysteresisPpm     = 100;   // hysteresis_ppm
const char* deviceId    = "airguard-01";
const char* fwVersion   = "1.2.0";

// ---------- Піни ----------
const int PIN_CO2_ADC = 34;      // MQ-135 аналоговий вхід / DAC-ін'єкція стенда
const int PIN_LED      = 2;      // LED тривоги (у пристрої — RGB + buzzer)

// ---------- Calibration (FR-001): 0..4095 ADC → 400..5000 ppm ----------
const int  ADC_MIN = 0;
const int  ADC_MAX = 4095;
const int  PPM_MIN = 400;
const int  PPM_MAX = 5000;

// Вимоги для реальної MQTT-моделі (опційно: Wi-Fi у Wokwi підтримується).
// Для демо-сценарію payload публікується у Serial (перехоплення робить pytest-слухач у стенді).
const char* WIFI_SSID = "Wokwi-GUEST";
const char* MQTT_BROKER = "test.mosquitto.org";   // у HIL-стенді — локальний Mosquitto
const int   MQTT_PORT = 1883;

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

String serialInput = "";
unsigned long lastMeasureMs = 0;
const unsigned long MEASURE_PERIOD_MS = 1000;  // у демо — 1 с (у пристрої 30 с, FR-001)

// ---------- Ініціалізація ----------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  // Boot-банер як у реальному DUT (FR-003 / NFR-010)
  Serial.println();
  Serial.println("=== AirGuard C1 boot ===");
  Serial.printf("fw_version=%s device_id=%s\n", fwVersion, deviceId);
  Serial.printf("config: alarm_threshold_ppm=%d hysteresis_ppm=%d\n",
                alarmThresholdPpm, hysteresisPpm);

  // WiFi + MQTT — опційно (для повної E2E-моделі у Wokwi). Не блокує демо тривоги.
  WiFi.begin(WIFI_SSID, "", 6, 0, false);
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  Serial.println("App started");
}

// ---------- Вимірювання CO2 (FR-001) ----------
int readCo2Ppm() {
  int adc = analogRead(PIN_CO2_ADC);             // 0..4095 (12-bit, 0..3.3 В)
  long ppm = (long)(adc - ADC_MIN) * (PPM_MAX - PPM_MIN) / (ADC_MAX - ADC_MIN) + PPM_MIN;
  return (int)ppm;
}

// ---------- Логіка тривоги: поріг + гістерезис (FR-007, FR-008) ----------
bool alarmActive = false;
void updateAlarm(int co2Ppm) {
  if (!alarmActive && co2Ppm > alarmThresholdPpm) {
    alarmActive = true;                          // перехід у тривогу
  } else if (alarmActive && co2Ppm < alarmThresholdPpm - hysteresisPpm) {
    alarmActive = false;                        // скидання лише нижче порогу - гістерезис
  }
  digitalWrite(PIN_LED, alarmActive ? HIGH : LOW);
}

// ---------- JSON-payload за контрактом FR-011 ----------
void publishMeasurement(int co2Ppm) {
  JsonDocument doc;                              // ArduinoJson v7
  doc["co2_ppm"]   = co2Ppm;
  doc["temp_c"]    = 23.4;                       // у стенді — вимір SHT31 (FR-002)
  doc["humi_rh"]   = 45.0;
  doc["alarm"]     = alarmActive ? "triggered" : "idle";
  doc["device_id"] = deviceId;
  doc["fw"]        = fwVersion;
  doc["ts"]        = millis() / 1000;            // у пристрої — UNIX-time

  String payload;
  serializeJson(doc, payload);
  Serial.print("MQTT_PUBLISH test/airguard/01 ");
  Serial.println(payload);                       // pytest-слухач ловить саме це

  if (mqtt.connected()) {
    mqtt.publish("test/airguard/01", payload.c_str());  // повна E2E-модель
  }
}

// ---------- Міні-CLI (FR-003/FR-004) ----------
void handleCommand(String cmd) {          // приймаємо за значенням: trim() мутує рядок
  cmd.trim();
  if (cmd == "status") {
    Serial.printf("STATUS {\"co2_ppm\":%d,\"alarm\":\"%s\",\"threshold\":%d,\"free_heap\":%u}\n",
                  readCo2Ppm(), alarmActive ? "triggered" : "idle",
                  alarmThresholdPpm, ESP.getFreeHeap());
  } else if (cmd == "version") {
    Serial.printf("VERSION v%s\n", fwVersion);
  } else if (cmd.startsWith("config set alarm_threshold_ppm ")) {
    int v = cmd.substring(31).toInt();
    if (v >= 400 && v <= 5000) {                 // валідація меж (FR-005)
      alarmThresholdPpm = v;
      Serial.printf("CONFIG OK alarm_threshold_ppm=%d\n", v);
    } else {
      Serial.println("CONFIG ERROR value out of range 400..5000");
    }
  } else if (cmd.length() > 0) {
    Serial.println("ERROR unknown command");
  }
}

// ---------- Головний цикл ----------
void loop() {
  // CLI: не блокує вимірювання
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n') { handleCommand(serialInput); serialInput = ""; }
    else if (ch != '\r') serialInput += ch;
  }

  // Цикл вимірювання (FR-001)
  unsigned long now = millis();
  if (now - lastMeasureMs >= MEASURE_PERIOD_MS) {
    lastMeasureMs = now;
    int co2 = readCo2Ppm();
    updateAlarm(co2);
    publishMeasurement(co2);
  }

  // Спроба MQTT-конекта кожні 5 с (FR-012: реконект, у пристрої — backoff)
  static unsigned long lastMqttTryMs = 0;
  static bool wifiPrinted = false;
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiPrinted) {
      Serial.printf("WIFI connected ip=%s\n", WiFi.localIP().toString().c_str());
      wifiPrinted = true;
    }
    if (!mqtt.connected() && now - lastMqttTryMs >= 5000) {
      lastMqttTryMs = now;
      if (mqtt.connect(deviceId)) Serial.println("MQTT connected to broker");
    }
  }
  mqtt.loop();
}