#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include <WiFiClient.h>

#include "settings.h"

WiFiClient espClient;

PubSubClient mqtt_client(espClient);

void setup()
{
  Serial.begin(BAUD_RATE);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    //Serial.print(".");
  }

  setup_ota();

  mqtt_client.setServer(MQTT_HOST, atoi(MQTT_PORT));
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
    }
  }

  ArduinoOTA.handle();

  if (!mqtt_client.connected())
  {
    long now = millis();

    if (now - LAST_RECONNECT_ATTEMPT > 5000)
    {
      LAST_RECONNECT_ATTEMPT = now;

      if (mqtt_reconnect())
      {
        LAST_RECONNECT_ATTEMPT = 0;
      }
    }
  }
  else
  {
    mqtt_client.loop();
  }

  read_p1_serial();
}


void send_mqtt_message(const char *topic, char *payload)
{
  bool result = mqtt_client.publish(topic, payload, false);
}

bool mqtt_reconnect()
{
  int MQTT_RECONNECT_RETRIES = 0;

  while (!mqtt_client.connected() && MQTT_RECONNECT_RETRIES < MQTT_MAX_RECONNECT_TRIES)
  {
    MQTT_RECONNECT_RETRIES++;
    if (mqtt_client.connect(HOSTNAME, MQTT_USER, MQTT_PASS))
    {
      char *message = new char[16 + strlen(HOSTNAME) + 1];
      strcpy(message, "p1 meter alive: ");
      strcat(message, HOSTNAME);
      mqtt_client.publish("hass/status", message);

      // Serial.printf("MQTT root topic: %s\n", MQTT_ROOT_TOPIC);
    }
    else
    {
//      Serial.print(F("MQTT Connection failed: rc="));
//      Serial.println(mqtt_client.state());
//      Serial.println(F(" Retrying in 5 seconds"));
//      Serial.println("");

      delay(5000);
    }
  }

  if (MQTT_RECONNECT_RETRIES >= MQTT_MAX_RECONNECT_TRIES)
  {
    // Serial.printf("*** MQTT connection failed, giving up after %d tries ...\n", MQTT_RECONNECT_RETRIES);
    return false;
  }

  return true;
}

void send_metric(String name, long metric)
{
  if (metric > 0) {
//    Serial.print(F("Sending metric to broker: "));
//    Serial.print(name);
//    Serial.print(F("="));
//    Serial.println(metric);

    char output[10];
    ltoa(metric, output, sizeof(output));

    String topic = String(MQTT_ROOT_TOPIC) + "/" + name;
    send_mqtt_message(topic.c_str(), output);
  }
}

void send_data_to_broker()
{
  send_metric("consumption_low_tarif", CONSUMPTION_LOW_TARIF);
  send_metric("consumption_high_tarif", CONSUMPTION_HIGH_TARIF);
  send_metric("actual_consumption", ACTUAL_CONSUMPTION);
  send_metric("instant_power_usage", INSTANT_POWER_USAGE);
  send_metric("instant_power_current", INSTANT_POWER_CURRENT);
  send_metric("gas_meter_m3", GAS_METER_M3);

  send_metric("actual_tarif_group", ACTUAL_TARIF);
  send_metric("short_power_outages", SHORT_POWER_OUTAGES);
  send_metric("long_power_outages", LONG_POWER_OUTAGES);
  send_metric("short_power_drops", SHORT_POWER_DROPS);
  send_metric("short_power_peaks", SHORT_POWER_PEAKS);
}

unsigned int CRC16(unsigned int crc, unsigned char *buf, int len)
{
  for (int pos = 0; pos < len; pos++) {
    crc ^= (unsigned int)buf[pos];

    for (int i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }

  return crc;
}

bool isNumber(char *res, int len)
{
  for (int i = 0; i < len; i++) {
    if (((res[i] < '0') || (res[i] > '9')) && (res[i] != '.' && res[i] != 0)) {
      return false;
    }
  }

  return true;
}

int FindCharInArrayRev(char array[], char c, int len)
{
  for (int i = len - 1; i >= 0; i--) {
    if (array[i] == c) {
      return i;
    }
  }

  return -1;
}

long getValue(char *buffer, int maxlen, char startchar, char endchar)
{
  int s = FindCharInArrayRev(buffer, startchar, maxlen - 2);
  int l = FindCharInArrayRev(buffer, endchar, maxlen - 2) - s - 1;

  char res[16];
  memset(res, 0, sizeof(res));

  if (strncpy(res, buffer + s + 1, l))
  {
    if (endchar == '*')
    {
      if (isNumber(res, l))
        return (1000 * atof(res));
    }
    else if (endchar == ')')
    {
      if (isNumber(res, l))
        return atof(res);
    }
  }
  return 0;
}

bool decode_telegram(int len)
{
  int startChar = FindCharInArrayRev(telegram, '/', len);
  int endChar = FindCharInArrayRev(telegram, '!', len);
  bool validCRCFound = false;

  /*
  for (int cnt = 0; cnt < len; cnt++)
    Serial.print(telegram[cnt]);
  */
  
  if (startChar >= 0)
  {
    currentCRC = CRC16(0x0000, (unsigned char *) telegram + startChar, len - startChar);
  }
  else if (endChar >= 0)
  {
    currentCRC = CRC16(currentCRC, (unsigned char*)telegram + endChar, 1);

    char messageCRC[5];
    strncpy(messageCRC, telegram + endChar + 1, 4);

    messageCRC[4] = 0;
    validCRCFound = (strtol(messageCRC, NULL, 16) == currentCRC);

//    if (validCRCFound)
//      Serial.println(F("CRC Valid!"));
//    else
//      Serial.println(F("CRC Invalid!"));

    currentCRC = 0;
  }
  else
  {
    currentCRC = CRC16(currentCRC, (unsigned char*) telegram, len);
  }

  if (strncmp(telegram, "1-0:1.8.1", strlen("1-0:1.8.1")) == 0)
  {
    CONSUMPTION_LOW_TARIF = getValue(telegram, len, '(', '*');
  }

  if (strncmp(telegram, "1-0:1.8.2", strlen("1-0:1.8.2")) == 0)
  {
    CONSUMPTION_HIGH_TARIF = getValue(telegram, len, '(', '*');
  }

  if (strncmp(telegram, "1-0:1.7.0", strlen("1-0:1.7.0")) == 0)
  {
    ACTUAL_CONSUMPTION = getValue(telegram, len, '(', '*');
  }

  if (strncmp(telegram, "1-0:21.7.0", strlen("1-0:21.7.0")) == 0)
  {
    INSTANT_POWER_USAGE = getValue(telegram, len, '(', '*');
  }

  if (strncmp(telegram, "1-0:31.7.0", strlen("1-0:31.7.0")) == 0)
  {
    INSTANT_POWER_CURRENT = getValue(telegram, len, '(', '*');
  }

  if (strncmp(telegram, "0-1:24.2.1", strlen("0-1:24.2.1")) == 0)
  {
    GAS_METER_M3 = getValue(telegram, len, '(', '*');
  }

  if (strncmp(telegram, "0-0:96.14.0", strlen("0-0:96.14.0")) == 0)
  {
    ACTUAL_TARIF = getValue(telegram, len, '(', ')');
  }

  if (strncmp(telegram, "0-0:96.7.21", strlen("0-0:96.7.21")) == 0)
  {
    SHORT_POWER_OUTAGES = getValue(telegram, len, '(', ')');
  }

  if (strncmp(telegram, "0-0:96.7.9", strlen("0-0:96.7.9")) == 0)
  {
    LONG_POWER_OUTAGES = getValue(telegram, len, '(', ')');
  }

  if (strncmp(telegram, "1-0:32.32.0", strlen("1-0:32.32.0")) == 0)
  {
    SHORT_POWER_DROPS = getValue(telegram, len, '(', ')');
  }

  if (strncmp(telegram, "1-0:32.36.0", strlen("1-0:32.36.0")) == 0)
  {
    SHORT_POWER_PEAKS = getValue(telegram, len, '(', ')');
  }

  return validCRCFound;
}

void read_p1_serial()
{
  if (Serial.available())
  {
    memset(telegram, 0, sizeof(telegram));

    while (Serial.available())
    {
      ESP.wdtDisable();
      int len = Serial.readBytesUntil('\n', telegram, P1_MAXLINELENGTH);
      ESP.wdtEnable(1);

      telegram[len] = '\n';
      telegram[len + 1] = 0;
      yield();

      bool result = decode_telegram(len + 1);
      if (result)
        send_data_to_broker();
    }
  }
}

void setup_ota()
{
  //Serial.println(F("Arduino OTA activated."));

  ArduinoOTA.setPort(8266);

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]()
  {
//    Serial.println(F("Arduino OTA: Start"));
  });

  ArduinoOTA.onEnd([]()
  {
//    Serial.println(F("Arduino OTA: End (Running reboot)"));
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
  {
//    Serial.printf("Arduino OTA Progress: %u%%\r", (progress / (total / 100)));
  });

//  ArduinoOTA.onError([](ota_error_t error)
//  {
//    
//    Serial.printf("Arduino OTA Error[%u]: ", error);
//    if (error == OTA_AUTH_ERROR)
//      Serial.println(F("Arduino OTA: Auth Failed"));
//    else if (error == OTA_BEGIN_ERROR)
//      Serial.println(F("Arduino OTA: Begin Failed"));
//    else if (error == OTA_CONNECT_ERROR)
//      Serial.println(F("Arduino OTA: Connect Failed"));
//    else if (error == OTA_RECEIVE_ERROR)
//      Serial.println(F("Arduino OTA: Receive Failed"));
//    else if (error == OTA_END_ERROR)
//      Serial.println(F("Arduino OTA: End Failed"));
//  });

  ArduinoOTA.begin();
//  Serial.println(F("Arduino OTA finished"));
}
