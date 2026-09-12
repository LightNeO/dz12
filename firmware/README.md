# AirGuard hardware firmware

This directory contains two independent Arduino-framework projects:

- `dut_esp32s3`: firmware for ESP32-S3 DevKitC-1 N16R8;
- `agent_nano`: sensor-injection agent for Arduino Nano.

## Wiring

| Signal | ESP32-S3 | Arduino Nano |
|---|---:|---:|
| Simulated CO2 analog input | GPIO4 | D9 PWM through voltage divider |
| Alarm LED | GPIO2 -> resistor -> LED -> GND | - |
| Common ground | GND | GND |

The Nano is a 5 V board and the ESP32 ADC is not 5 V tolerant. Do not connect D9
directly to GPIO4. Use a divider, for example:

```text
Nano D9 --- 10 kOhm ---+--- ESP32 GPIO4
                       |
                      20 kOhm
                       |
                      GND
```

The divider limits 5 V to approximately 3.33 V. A capacitor from GPIO4 to GND
(for example 100 nF) is recommended to smooth the PWM signal, but the ESP32
firmware also averages 16 ADC samples.

The LED must have a series resistor. Use the resistor value appropriate for the
LED; a typical starting value is 220-1k Ohm.

## Agent commands

Open the Nano serial monitor at 115200 baud:

```text
set 800
set 1500
inc 100
dec 100
status
```

The Nano maps 400..5000 ppm to 0..255 PWM duty on D9.

## DUT commands

Open the ESP32 serial monitor at 115200 baud:

```text
status
config set alarm_threshold_ppm 1000
config set hysteresis_ppm 100
config set measure_period_s 5
config save
config get
factory_reset
```

The DUT turns the LED on when CO2 is above the threshold and turns it off only
below `threshold - hysteresis`. The firmware is configured for real WiFi/MQTT.
WiFi and broker settings are stored in the local ignored file
`dut_esp32s3/src/secrets.h`; use `secrets.example.h` as a template on another
machine.

## Local MQTT broker

The PC can run Mosquitto in Docker. Its current LAN address is `192.168.50.46`.
Start the broker from the `mqtt` directory:

```bash
docker compose up -d
```

Subscribe to AirGuard messages:

```bash
mosquitto_sub -h 192.168.50.46 -p 1883 -t 'test/airguard/#' -v
```

The current broker configuration allows anonymous access on the local network,
which is suitable for this isolated homework LAN but not for production.

Temperature and humidity are emitted as JSON `null` because no SHT31 sensor is
present. A buzzer is not required for this hardware configuration; the LED is
the only alarm actuator.
