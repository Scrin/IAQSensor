# IAQSensor

Indoor Air Quality sensor for Home Assistant using a BME680 gas sensor on a ESP8266 NodeMCU

## Setup:

- Clone/download this repository
- Copy `include/config.h.example` as `include/config.h`
- Set your configuration in `config.h`
- Build/flash like any other PlatformIO project
- The SCL pin of BME680 should be connected to GPIO5 (pin labeled D1) and SDA pin of BME680 to GPIO4 (pin labeled D2)

For Home Assistant you'll want something like this in your configuration.yaml:

```
sensor:
  - platform: mqtt
    name: "Indoor air quality"
    state_topic: "IAQSensor/state"
    availability_topic: "IAQSensor/availability"
    value_template: "{{ value_json.iaq }}"
```

You can add other available measurements in a similar manner by setting an appropriate value_template, for example:

```
  - platform: mqtt
    name: "Indoor temperature"
    state_topic: "IAQSensor/state"
    availability_topic: "IAQSensor/availability"
    unit_of_measurement: "ºC"
    value_template: "{{ value_json.temperature }}"
```

Available measurements/data:

    temperature
    humidity
    pressure

    iaq
    staticIaq
    co2Equivalent
    breathVocEquivalent

    iaqAccuracy
    staticIaqAccuracy
    co2Accuracy
    breathVocAccuracy

    gasResistance
    rawTemperature
    rawHumidity

    status
    bme680Status
    stabStatus
    runInStatus

Some very crude "documentation" about the output values can be found in the [BSEC library](https://github.com/BoschSensortec/BSEC-Arduino-library/blob/master/src/inc/bsec_datatypes.h)

## Over The Air update:

Documentation: https://arduino-esp8266.readthedocs.io/en/latest/ota_updates/readme.html#web-browser

Basic steps:

- Use PlatformIO: Build
- Browse to http://IP_ADDRESS/update or http://hostname.local/update
- Select .pio/build/nodemcuv2/firmware.bin from work directory as Firmware and press Update Firmware
