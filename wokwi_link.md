# Wokwi-модель ключового тестового сценарію — AirGuard C1 (DZ-12, Частина 2)

## Що моделюється

Ключовий критичний сценарій з PRD — **CS-1: «CO2 → тривога → MQTT-повідомлення»**,
тобто E2E-ланцюжок з Lesson 12, скорочений до віртуального стенда:

| Елемент HIL-стенда | Елемент Wokwi-моделі |
|---|---|
| DAC-ін'єкція HIL-агента (`hil.set_dac(mV)`) — signal injection у вхід MQ-135 | **Потенціометр** на GPIO34 (0–3.3 В) |
| ADC + мапінг «напруга → ppm» (FR-001) | `analogRead()` + лінійний мапінг 0..4095 → 400..5000 ppm |
| Тривога: RGB LED + buzzer при CO2 > порогу (FR-007) з гістерезисом (FR-008) | **Червоний LED** на GPIO2 |
| UART CLI DUT: boot-банер, `status`, `config set`, `version` (FR-003/FR-004) | **Serial Monitor** 115200 |
| MQTT-публікація JSON-payload (FR-011) | Рядок `MQTT_PUBLISH test/airguard/01 {…}` у Serial + опційний publish на test.mosquitto.org (WiFi у Wokwi працює) |
| Перехоплення pytest-підписником | У стенді — `MqttListener` на `test/#`; у моделі payload видно у Serial |

## Посилання на онлайн-модель

> **ЗАПОВНИТИ ПІСЛЯ СТВОРЕННЯ ПРОЄКТУ У WOKWI:**
>
> **Посилання:** `https://wokwi.com/projects/<ID вашого проєкту>`
>
> **Скріншот роботи:** вставити сюди `screenshot_wokwi.png`
> (Serial Monitor з payload-ами + LED при тривозі + потенціометр повернутий на високе значення)

### Як отримати посилання (2 хвилини)

1. Відкрити **https://wokwi.com** → увійти (Sign in with Google/GitHub).
2. **New Project → ESP32** (C++).
3. У вкладці **sketch.ino** — вставити вміст файлу `wokwi/sketch.ino` з цього комплекту.
4. Перемкнутись на вкладку **diagram.json** — вставити вміст `wokwi/diagram.json`
   (потенціометр, LED, резистор і з'єднання з'являться автоматично).
4a. Відкрити/створити вкладку **libraries.txt** — вставити вміст `wokwi/libraries.txt`
   (`ArduinoJson`, `PubSubClient`). Wokwi підтягне бібліотеки при збірці.
5. Натиснути **▶ Play (Run)**. У Serial Monitor:
   ```
   === AirGuard C1 boot ===
   fw_version=1.2.0 device_id=airguard-01
   App started
   MQTT_PUBLISH test/airguard/01 {"co2_ppm":712,...,"alarm":"idle"}
   ```
6. **Продемонструвати сценарій CS-1:**
   - повернути потенціометр вправо → `co2_ppm` зростає; коли > 1000 → **LED спалахує червоним**,
     payload містить `"alarm":"triggered"` (FR-007);
   - повернути трохи нижче порогу (900–1000 ppm) → тривога НЕ зникає (гістерезис, FR-008);
   - нижче 900 ppm → `"alarm":"idle"`, LED гасне;
   - ввести в Serial: `config set alarm_threshold_ppm 800` → тривога спрацьовує раніше (FR-004/FR-005);
   - ввести `status` → JSON-статус з `free_heap` (те, що читає `DeviceDriver.status()` у тестах).
7. **Save** (Ctrl+S) → кнопка **Share** у правому верхньому куті → копіювати посилання → вставити вище.
8. Скріншот: модель + Serial Monitor (панель можна розширити) → зберегти як `screenshot_wokwi.png`.

## Очікуваний результат (PASS-критерії демо-сценарію)

| Крок демо | Очікування (PASS) |
|---|---|
| Потенціометр < ~35% (≤1000 ppm) | payload `"alarm":"idle"`, LED OFF |
| Потенціометр > ~45% (>1000 ppm) | payload `"alarm":"triggered"`, **LED ON** |
| Потенціометр трохи назад (900–1000 ppm) | тривога тримається (гістерезис 100 ppm) |
| Потенціометр < ~33% (<900 ppm) | `"alarm":"idle"`, LED OFF |
| `config set alarm_threshold_ppm 800` | `CONFIG OK alarm_threshold_ppm=800`, тривога від 800 ppm |
| `config set alarm_threshold_ppm 9000` | `CONFIG ERROR value out of range` (валідація FR-005) |
| `status` | JSON з co2_ppm / alarm / threshold / free_heap |

## Як модель пов'язана з основною частиною ДЗ

Модель — це «пролабораторія» сценарію, який на реальному стенді виконує тест
`tests/e2e/test_alarm_e2e.py::test_co2_alarm_full_chain` (див. `traceability_matrix.md`):
та сама логіка порогу/гістерезису, той самий контракт payload, той самий boot-банер,
який парсить `DeviceDriver.wait_for_pattern('App started')`. Тому модель можна
використовувати і як «віртуальний DUT» для репетиції тестів без заліза.

## Файли

- `wokwi/diagram.json` — схема віртуального стенда (ESP32 + потенціометр + LED + резистор 220 Ом)
- `wokwi/sketch.ino` — модель прошивки AirGuard C1 (поріг, гістерезис, JSON, міні-CLI)

> Примітка: якщо Wokwi підсвітить незнайдений pin у `diagram.json` (найменування пінів
> відрізняється між ревізіями плати), перетягніть два кінці проводу в GUI — з'єднання
> GPIO34→SIG, 3V3→VCC, GND→GND, D2→резистор→LED.