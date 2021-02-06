#include <EEPROM.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include "bsec.h"
#include "config.h"

const uint8_t bsec_config_iaq[] = {
/* Configure the BSEC library with information about the sensor
    18v/33v = Voltage at Vdd. 1.8V or 3.3V
    3s/300s = BSEC operating mode, BSEC_SAMPLE_RATE_LP or BSEC_SAMPLE_RATE_ULP
    4d/28d = Operating age of the sensor in days
    generic_18v_3s_4d
    generic_18v_3s_28d
    generic_18v_300s_4d
    generic_18v_300s_28d
    generic_33v_3s_4d
    generic_33v_3s_28d
    generic_33v_300s_4d
    generic_33v_300s_28d
*/
#include "config/generic_33v_3s_4d/bsec_iaq.txt"
};

#ifdef HTTPUpdateServer
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>

ESP8266WebServer httpUpdateServer(80);
ESP8266HTTPUpdateServer httpUpdater;
#endif

#define STATE_SAVE_PERIOD UINT32_C(12 * 60 * 60 * 1000) // 12h

void checkConnection();
void loadState();
void updateState();

WiFiClient espClient;
PubSubClient client(espClient);
Bsec iaqSensor;
uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE] = {0};
uint16_t stateUpdateCounter = 0;

void setup()
{
  EEPROM.begin(BSEC_MAX_STATE_BLOB_SIZE + 1); // 1st address for the length
  Serial.begin(115200);
  Wire.begin();
  iaqSensor.begin(BME680_I2C_ADDR_SECONDARY, Wire);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("\nStarting");
  String output = "\nBSEC library version " + String(iaqSensor.version.major) + "." + String(iaqSensor.version.minor) + "." + String(iaqSensor.version.major_bugfix) + "." + String(iaqSensor.version.minor_bugfix);
  Serial.println(output);

  iaqSensor.setConfig(bsec_config_iaq);

  loadState();

  std::array<bsec_virtual_sensor_t, 7> ulpOutputs = {
      BSEC_OUTPUT_RAW_GAS,
      BSEC_OUTPUT_IAQ,
      BSEC_OUTPUT_STATIC_IAQ,
      BSEC_OUTPUT_CO2_EQUIVALENT,
      BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
      BSEC_OUTPUT_STABILIZATION_STATUS,
      BSEC_OUTPUT_RUN_IN_STATUS,
  };
  std::array<bsec_virtual_sensor_t, 5> lpOutputs = {
      BSEC_OUTPUT_RAW_TEMPERATURE,
      BSEC_OUTPUT_RAW_PRESSURE,
      BSEC_OUTPUT_RAW_HUMIDITY,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
  };

  iaqSensor.updateSubscription(ulpOutputs.data(), ulpOutputs.size(), BSEC_SAMPLE_RATE_ULP);
  iaqSensor.updateSubscription(lpOutputs.data(), lpOutputs.size(), BSEC_SAMPLE_RATE_LP);

  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

#ifdef HTTPUpdateServer
  MDNS.begin(MQTT_CLIENT_NAME);

  httpUpdater.setup(&httpUpdateServer, USER_HTTP_USERNAME, USER_HTTP_PASSWORD);
  httpUpdateServer.begin();

  MDNS.addService("http", "tcp", 80);
#endif

  client.setServer(MQTT_SERVER, MQTT_PORT);

  checkConnection();
}

void loop()
{
  checkConnection();
  client.loop();

  if (iaqSensor.run())
  { // If new data is available
    StaticJsonDocument<2048> data;
    data["mcu_name"] = MQTT_CLIENT_NAME;

    data["temperature"] = iaqSensor.temperature;
    data["humidity"] = iaqSensor.humidity;
    data["pressure"] = iaqSensor.pressure;

    data["iaq"] = iaqSensor.iaq;
    data["staticIaq"] = iaqSensor.staticIaq;
    data["co2Equivalent"] = iaqSensor.co2Equivalent;
    data["breathVocEquivalent"] = iaqSensor.breathVocEquivalent;

    data["iaqAccuracy"] = iaqSensor.iaqAccuracy;
    data["staticIaqAccuracy"] = iaqSensor.staticIaqAccuracy;
    data["co2Accuracy"] = iaqSensor.co2Accuracy;
    data["breathVocAccuracy"] = iaqSensor.breathVocAccuracy;

    data["gasResistance"] = iaqSensor.gasResistance;
    data["rawTemperature"] = iaqSensor.rawTemperature;
    data["rawHumidity"] = iaqSensor.rawHumidity;

    data["status"] = iaqSensor.status;
    data["bme680Status"] = iaqSensor.bme680Status;
    data["stabStatus"] = iaqSensor.stabStatus;
    data["runInStatus"] = iaqSensor.runInStatus;

    String s;
    serializeJson(data, s);
    client.publish(MQTT_CLIENT_NAME "/state", s.c_str());
    updateState();
  }

#ifdef HTTPUpdateServer
  httpUpdateServer.handleClient();
#endif
}

void checkConnection()
{
  if (client.connected())
  {
    return;
  }
  digitalWrite(LED_BUILTIN, LOW);
  int retries = 0;
  while (!client.connected())
  {
    if (retries < 150)
    {
      Serial.print("Attempting MQTT connection...");
      if (client.connect(MQTT_CLIENT_NAME, MQTT_USER, MQTT_PASS, MQTT_CLIENT_NAME "/availability", 0, true, "offline"))
      {
        Serial.println("connected");
        client.publish(MQTT_CLIENT_NAME "/availability", "online", true);
      }
      else
      {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 5 seconds");
        retries++;
        delay(5000);
      }
    }
    else
    {
      ESP.restart();
    }
  }
  digitalWrite(LED_BUILTIN, HIGH);
}

void loadState()
{
  if (EEPROM.read(0) == BSEC_MAX_STATE_BLOB_SIZE)
  {
    Serial.println("Reading state from EEPROM");

    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++)
    {
      bsecState[i] = EEPROM.read(i + 1);
      Serial.print(bsecState[i], HEX);
      Serial.print(" ");
    }
    Serial.println();

    iaqSensor.setState(bsecState);
  }
  else
  {
    Serial.println("Erasing EEPROM");

    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE + 1; i++)
      EEPROM.write(i, 0);

    EEPROM.commit();
  }
}

void updateState()
{
  bool update = false;
  if (stateUpdateCounter == 0)
  {
    // First state update when IAQ accuracy is >= 3
    if (iaqSensor.iaqAccuracy >= 3)
    {
      update = true;
    }
  }
  else if ((stateUpdateCounter * STATE_SAVE_PERIOD) < millis())
  {
    update = true;
  }

  if (update)
  {
    stateUpdateCounter++;
    iaqSensor.getState(bsecState);

    Serial.println("Writing state to EEPROM");

    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++)
    {
      EEPROM.write(i + 1, bsecState[i]);
      Serial.print(bsecState[i], HEX);
      Serial.print(" ");
    }
    Serial.println();

    EEPROM.write(0, BSEC_MAX_STATE_BLOB_SIZE);
    EEPROM.commit();
  }
}