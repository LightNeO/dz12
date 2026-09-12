# AirGuard: налаштування з нуля

Інструкція описує повне локальне налаштування стенда:

```text
Arduino Nano sensor agent -> ESP32-S3 DUT -> WiFi -> ПК -> Mosquitto MQTT
```

Arduino Nano емуляє аналоговий сигнал датчика CO2. ESP32-S3 читає сигнал через
ADC, керує LED тривоги і публікує вимірювання в локальний MQTT broker на ПК.

## 1. Вимоги

Потрібні:

- ESP32-S3 DevKitC-1 N16R8;
- Arduino Nano з USB-Serial адаптером, наприклад CH340;
- LED і послідовний резистор;
- резистори для дільника напруги, наприклад 10 кОм і 20 кОм;
- USB-кабелі з передачею даних;
- ПК у тій самій WiFi-мережі, що й ESP32;
- WiFi 2.4 GHz;
- Docker.

Баззер і SHT31 для поточної конфігурації не потрібні. Без SHT31 температура і
вологість передаються як JSON `null`. Без баззера тривога індикується LED.

## 2. Встановлення інструментів на Arch Linux

Встановіть Python, `pip`, virtualenv і Docker:

```bash
sudo pacman -S --needed python python-pip python-virtualenv docker docker-compose
```

Додайте поточного користувача до груп для Docker і USB Serial:

```bash
sudo usermod -aG docker,uucp "$USER"
```

Повністю вийдіть із графічної сесії та увійдіть знову. Для найнадійнішого
застосування груп можна перезавантажити ПК:

```bash
reboot
```

Перевірте:

```bash
docker --version
python3 --version
id -nG
```

У списку груп повинні бути `docker` і `uucp`.

## 3. Встановлення PlatformIO

Використовуйте окреме virtualenv, щоб не змінювати системний Python:

```bash
python3 -m venv "$HOME/.venvs/platformio"
"$HOME/.venvs/platformio/bin/pip" install -U pip platformio
export PATH="$HOME/.venvs/platformio/bin:$PATH"
```

Щоб команда `pio` залишилася доступною після перезапуску термінала:

```bash
printf '\nexport PATH="$HOME/.venvs/platformio/bin:$PATH"\n' >> "$HOME/.bashrc"
```

Перевірка:

```bash
pio --version
```

## 4. Перевірка адреси ПК

Знайдіть IPv4-адресу WiFi-інтерфейсу:

```bash
ip -4 addr show
```

У прикладі цього стенда адреса ПК була:

```text
192.168.50.46
```

Цю адресу потрібно використовувати як MQTT broker host. Вона може змінитися
після підключення до іншої мережі або зміни DHCP lease.

## 5. Налаштування WiFi і MQTT для ESP32

Відкрийте файл:

```text
firmware/dut_esp32s3/src/secrets.h
```

Якщо його немає, створіть на основі:

```text
firmware/dut_esp32s3/src/secrets.example.h
```

Мінімальний вміст:

```cpp
#pragma once

#define AIRGUARD_WIFI_SSID "YOUR_WIFI_SSID"
#define AIRGUARD_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define AIRGUARD_MQTT_HOST "192.168.50.46"
#define AIRGUARD_MQTT_PORT 1883
#define AIRGUARD_MQTT_USER ""
#define AIRGUARD_MQTT_PASSWORD ""
```

Замініть WiFi SSID, пароль і IP-адресу ПК на свої значення.

Файл `secrets.h` ігнорується Git. Не додавайте його до репозиторію і не
публікуйте пароль у звітах або скриншотах.

## 6. Запуск локального MQTT broker

Перейдіть до каталогу broker:

```bash
cd /home/light/emb/dz12/mqtt
```

Запустіть Mosquitto:

```bash
docker compose up -d
```

Перевірте стан:

```bash
docker compose ps
```

Очікувано:

```text
airguard-mosquitto   Up
0.0.0.0:1883->1883/tcp
```

Перевірте лог broker:

```bash
docker compose logs --no-log-prefix --tail=50 mosquitto
```

Має бути:

```text
mosquitto version 2.x running
Opening ipv4 listen socket on port 1883
```

Поточний конфіг дозволяє anonymous MQTT-доступ у локальній мережі. Для
навчального ізольованого стенда це допустимо. Для production потрібно додати
MQTT username/password і firewall-обмеження.

Фактична команда для запуска москіта після налаштування усього:
docker exec -it airguard-mosquitto   mosquitto_sub -h 127.0.0.1 -p 1883   -t 'test/airguard/#' -v

## 7. Підключення Arduino Nano до ESP32

Nano працює з логікою 5 В, а ADC ESP32 не є 5-вольтним входом. Не підключайте
Nano D9 безпосередньо до GPIO4.

Використайте дільник:

```text
Nano D9 --- 10 кОм ---+--- ESP32 GPIO4
                      |
                    20 кОм
                      |
                     GND
```

Додатково з'єднайте:

```text
Nano GND -------- ESP32 GND
```

Конденсатор приблизно 100 нФ між GPIO4 і GND бажаний для згладжування PWM.
Прошивка ESP32 також усереднює 16 ADC-вимірювань.

Підключення LED:

```text
ESP32 GPIO2 --- резистор 220...1000 Ом --- анод LED
катод LED -------------------------------- GND
```

## 8. Прошивка Arduino Nano

Підключіть тільки Nano до USB і знайдіть порт:

```bash
pio device list
```

Для CH340 порт зазвичай має вигляд `/dev/ttyUSB0`.

Зберіть прошивку:

```bash
cd /home/light/emb/dz12/firmware/agent_nano
pio run
```

Для Nano зі старим bootloader:

```bash
pio run -e nanoatmega328 -t upload --upload-port /dev/ttyUSB0
```

Для Nano з новим bootloader:

```bash
pio run -e nanoatmega328new -t upload --upload-port /dev/ttyUSB0
```

Якщо завантаження не синхронізується, закрийте Serial Monitor, від'єднайте і
під'єднайте Nano, повторіть upload та натисніть `RESET` одразу після появи
рядка `Uploading ... firmware.hex`.

Відкрийте монітор:

```bash
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

Перевірте команди:

```text
status
set 800
status
set 1500
status
```

Очікуваний формат:

```text
AGENT OK ppm=1500
AGENT ppm=1500 output=D9_PWM
```

## 9. Прошивка ESP32-S3

Підключіть ESP32-S3 до USB, не закриваючи або після закриття Nano Monitor.
Знайдіть порти:

```bash
pio device list
```

Порт ESP32 часто має вигляд `/dev/ttyACM0`, але використовуйте фактичне
значення з команди.

Зберіть прошивку:

```bash
cd /home/light/emb/dz12/firmware/dut_esp32s3
pio run
```

Прошийте ESP32:

```bash
pio run -t upload --upload-port /dev/ttyACM0
```

Відкрийте монітор:

```bash
pio device monitor --port /dev/ttyACM0 --baud 115200
```

Очікуваний boot-лог містить:

```text
=== AirGuard C1 boot ===
fw_version=v1.2.0 device_id=airguard-01
App started
WIFI target=YOUR_WIFI_SSID MQTT broker=192.168.50.46:1883
```

Через кілька секунд очікуються:

```text
WIFI connecting ssid=YOUR_WIFI_SSID
MQTT connecting host=192.168.50.46 port=1883
MQTT connected
```

## 10. MQTT subscriber

В окремому терміналі підпишіться на повідомлення:

```bash
docker exec -it airguard-mosquitto \
  mosquitto_sub -h 127.0.0.1 -p 1883 \
  -t 'test/airguard/#' -v
```

Не закривайте цей термінал під час тесту.

## 11. Функціональний тест

У моніторі ESP32 виконайте:

```text
config set alarm_threshold_ppm 1000
config set hysteresis_ppm 100
config set measure_period_s 5
config save
status
```

Для нормального стану в моніторі Nano:

```text
set 800
```

LED ESP32 має бути вимкнений, а MQTT payload має містити:

```json
"alarm":"idle"
```

Для запуску тривоги:

```text
set 1500
```

Через максимум один вимірювальний цикл LED має загорітися, а MQTT payload
має містити:

```json
"alarm":"triggered"
```

Для перевірки гістерезису:

```text
set 950
```

LED має залишатися увімкненим, тому що `950` не нижче за `1000 - 100`.

Після:

```text
set 850
```

LED має згаснути, а наступний payload має містити `"alarm":"idle"`.

## 12. Команди DUT

```text
help
status
version
sensor co2
config get
config set alarm_threshold_ppm 1000
config set hysteresis_ppm 100
config set measure_period_s 5
config set mqtt_topic test/airguard/01
config save
factory_reset
reboot
```

## 13. Типові проблеми

### `No module named pip`

Встановіть pip через Arch package manager:

```bash
sudo pacman -S --needed python-pip python-virtualenv
```

### `Permission denied: /dev/ttyUSB0`

Додайте користувача до групи `uucp` і перелогіньтеся:

```bash
sudo usermod -aG uucp "$USER"
reboot
```

### `stk500_getsync()` під час прошивки Nano

Спробуйте інший профіль bootloader:

```bash
pio run -e nanoatmega328new -t upload --upload-port /dev/ttyUSB0
```

Якщо не допомогло, спробуйте `nanoatmega328`. Закрийте всі Serial Monitor перед
upload.

### `MQTT connect failed state=-2`

Код `-2` означає, що ESP32 не встановив TCP-з'єднання з broker. Перевірте:

```bash
docker compose ps
ip -4 addr show
docker compose logs --no-log-prefix --tail=50 mosquitto
```

Перевірте, що IP у `secrets.h` збігається з актуальною адресою ПК, ESP32 і ПК
підключені до однієї мережі, а WiFi не використовує client isolation.

Якщо в логах broker з'явився рядок на кшталт:

```text
New client connected ... as airguard-01
```

з'єднання ESP32 з MQTT успішне.

### `InvalidProjectConfError` у PlatformIO

Перевірте, що `platformio.ini` DUT має такий формат:

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
lib_deps =
    knolleary/PubSubClient @ ^2.8
```

### LED не реагує

Перевірте полярність LED, послідовний резистор, спільну землю і підключення до
GPIO2. Перевірте, що напруга з дільника GPIO4 не перевищує приблизно 3.3 В.

## 14. Зупинка і повторний запуск broker

Зупинити broker:

```bash
cd /home/light/emb/dz12/mqtt
docker compose down
```

Запустити знову:

```bash
docker compose up -d
```

Дані Mosquitto зберігаються у Docker volume і не втрачаються під час звичайного
`docker compose down`.
