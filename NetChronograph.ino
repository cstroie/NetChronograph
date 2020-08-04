/**
  NetChronograph - A desk clock NTP-synchronized

  Copyright (C) 2019 Costin STROIE <costinstroie@eridu.eu.org>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


// User settings
#include "config.h"

// Project name and version
const char NODENAME[] = "NetChrono";
const char nodename[] = "netchrono";
const char VERSION[]  = "0.18";

// WiFi
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

#include <DNSServer.h>            // Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     // Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager WiFi Configuration Magic

// Safe values
#ifndef SCR_DEF
#define SCR_DEF       SCR_HHMM
#endif
#ifndef SCR_BRGHT
#define SCR_BRGHT     1
#endif
#ifndef NTP_SERVER
#define NTP_SERVER    ("pool.ntp.org")
#endif
#ifndef NTP_PORT
#define NTP_PORT      (123)
#endif
#ifndef NTP_TZ
#define NTP_TZ        (0)
#endif
#ifndef MQTT_ID
#define MQTT_ID       ("netchrono")
#endif
#ifndef MQTT_SERVER
#define MQTT_SERVER   ("test.mosquitto.org")
#endif
#ifndef USE_MQTT_SSL
#define USE_MQTT_SSL
#endif

// LED driver for MAX7219
#include "led.h"
LED led;
enum SCREENS {SCR_HHMM, SCR_HHMMSS, SCR_HHMMTT, SCR_DDLLYYYY, SCR_VCC, SCR_ALL};  // Screens
uint8_t scrDefault = SCR_DEF;           // The default screen to display
uint8_t scrCurrent = scrDefault;        // The current screen to display
uint8_t scrDelay   = 5;                 // Time (in seconds) to return to the default screen

// Network Time Protocol
#include "ntp.h"
NTP ntp;

// MQTT
#include <PubSubClient.h>
#ifdef USE_MQTT_SSL
WiFiClientSecure    wifiClient;                                 // Secure WiFi TCP client for MQTT
#else
WiFiClient          wifiClient;                                 // Plain WiFi TCP client for MQTT
#endif
PubSubClient        mqttClient(wifiClient);                     // MQTT client, based on WiFi client
#ifdef DEVEL
const char          mqttId[]       = MQTT_ID "-dev";            // Development MQTT client ID
#else
const char          mqttId[]       = MQTT_ID;                   // Production MQTT client ID
#endif
char                mqttServer[40] = MQTT_SERVER;               // MQTT server address to connect to
#ifdef USE_MQTT_SSL
const int           mqttPort       = 8883;                      // Secure MQTT port
#else
const int           mqttPort       = 1883;                      // Plain MQTT port
#endif
const unsigned long mqttDelay      = 5000UL;                    // Delay between reconnection attempts
unsigned long       mqttNextTime   = 0UL;                       // Next time to reconnect
// Various MQTT topics
const char          mqttTopicCmd[] = "command";
const char          mqttTopicSns[] = "sensor/netchrono";
const char          mqttTopicRpt[] = "report";

// Sensors
const unsigned long snsDelay    = 300 * 1000UL;                           // Delay between sensor readings
unsigned long       snsNextTime = 0UL;                                    // Next time to read the sensors

// DS18B20 sensor
#include <OneWire.h>
#include <DallasTemperature.h>
#define ONE_WIRE_BUS 3                  // Data wire is plugged into pin 3
OneWire oneWire(ONE_WIRE_BUS);          // Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
DallasTemperature sensors(&oneWire);    // Pass our oneWire reference to Dallas Temperature.
DeviceAddress     dsAddr;               // The detected DS18B20 address
int16_t           dsVal   = 0;          // Will use integer temperature
bool              dsOK    = true;       // The temperature sensor presence flag
bool              dsDegF  = false;      // Show temperature in Celsius or Fahrenheit
uint8_t           dsDelay = 2;          // Delay between sensor readings (in seconds)

// OTA
int otaPort     = 8266;
int otaProgress = -1;

// Set ADC to Voltage
ADC_MODE(ADC_VCC);
const unsigned long vccDelay = 1000UL;  // Delay between Vcc readings

/**
  Convert IPAddress to char array
*/
char charIP(const IPAddress ip, char *buf, size_t len, boolean pad = false) {
  if (pad) snprintf_P(buf, len, PSTR("%3d.%3d.%3d.%3d"), ip[0], ip[1], ip[2], ip[3]);
  else     snprintf_P(buf, len, PSTR("%d.%d.%d.%d"),     ip[0], ip[1], ip[2], ip[3]);
}

/**
  Try to connect to WiFi
*/
void wifiConnect(int timeout = 300) {
  // Set the host name
  WiFi.hostname(NODENAME);
  // Set the mode
  WiFi.mode(WIFI_STA);
  // Try to connect to WiFi
  if (!WiFi.isConnected()) {
    // The animation symbol index
    uint8_t idxAnim = 0;
    // Display "CONN"
    uint8_t msgConn[] = {0x4E, 0x7E, 0x76, 0x76, 0x00, 0x00, led.getAnim(idxAnim, 0), led.getAnim(idxAnim, 1)};
    led.fbWrite(0, msgConn, sizeof(msgConn) / sizeof(*msgConn));
    led.fbDisplay();
#ifdef DEBUG
    Serial.print(F("WiFi connecting "));
#endif
#ifdef WIFI_SSID
    WiFi.begin(WIFI_SSID, WIFI_PASS);
#else
    WiFi.begin();
#endif
    int tries = 0;
    while (!WiFi.isConnected() and ++tries < timeout) {
#ifdef DEBUG
      Serial.print(".");
#endif
      // Do some animation
      delay(100);
      idxAnim++;
      led.fbWrite(6, led.getAnim(idxAnim, 0));
      led.fbWrite(7, led.getAnim(idxAnim, 1));
      led.fbDisplay();
    };
    if (WiFi.isConnected()) {
#ifdef DEBUG
      Serial.println(F(" done."));
#endif
      // End animation
      led.fbWrite(6, led.getAnim(4, 0));
      led.fbWrite(7, led.getAnim(4, 1));
      led.fbDisplay();
    }
    else {
      // Display "WIFI AP"
      uint8_t msgWiFi[] = {0x1E, 0x3C, 0x30, 0x47, 0x30, 0x00, 0x77, 0x67};
      led.fbWrite(0, msgWiFi, sizeof(msgWiFi) / sizeof(*msgWiFi));
      led.fbDisplay();
      // Use the WiFi manager
      WiFiManager wifiManager;
      wifiManager.setTimeout(timeout);
      // Add support for mqtt server
      WiFiManagerParameter custom_mqtt_server("server", "MQTT server", mqttServer, 40);
      wifiManager.addParameter(&custom_mqtt_server);
      //wifiManager.setAPCallback(wifiCallback);
#ifndef DEBUG
      wifiManager.setDebugOutput(false);
#endif
      if (!wifiManager.autoConnect(NODENAME)) {
#ifdef DEBUG
        Serial.println(F("No WiFi network."));
#endif
        delay(3000);
        // Reset and try again
        ESP.reset();
        delay(5000);
      }
      // Copy the MQTT server address
      strncpy(mqttServer, custom_mqtt_server.getValue(), 40);
    }
  }
}

/**
  Publish char array to topic
*/
boolean mqttPub(const char *payload, const char *lvl1, const char *lvl2 = NULL, const char *lvl3 = NULL, const boolean retain = false) {
  const int bufSize = 100;
  char buf[bufSize] = "";
  strncpy(buf, lvl1, bufSize);
  if (lvl2 != NULL) {
    strcat(buf, "/");
    strncat(buf, lvl2, bufSize - strlen(buf) - 1);
  }
  if (lvl3 != NULL) {
    strcat(buf, "/");
    strncat(buf, lvl3, bufSize - strlen(buf) - 1);
  }
  yield();
  return mqttClient.publish(buf, payload, retain);
}

/**
  Publish char array to topic and retain
*/
boolean mqttPubRet(const char *payload, const char *lvl1, const char *lvl2 = NULL, const char *lvl3 = NULL, const boolean retain = true) {
  return mqttPub(payload, lvl1, lvl2, lvl3, retain);
}

/**
  Publish integer to topic
*/
boolean mqttPub(const int payload, const char *lvl1, const char *lvl2 = NULL, const char *lvl3 = NULL, const boolean retain = false) {
  const int bufSize = 16;
  char buf[bufSize] = "";
  snprintf(buf, bufSize, "%d", payload);
  return mqttPub(buf, lvl1, lvl2, lvl3, retain);
}

/**
  Publish integer to topic and retain
*/
boolean mqttPubRet(const int payload, const char *lvl1, const char *lvl2 = NULL, const char *lvl3 = NULL, const boolean retain = true) {
  return mqttPub(payload, lvl1, lvl2, lvl3, retain);
}

/**
  Subscribe to topic or topic/subtopic
*/
void mqttSubscribe(const char *lvl1, const char *lvl2 = NULL, bool all = false) {
  const int bufSize = 100;
  char buf[bufSize] = "";
  strncpy(buf, lvl1, bufSize);
  if (lvl2 != NULL) {
    strcat(buf, "/");
    strncat(buf, lvl2, bufSize - strlen(buf) - 1);
  }
  if (all) strcat(buf, "/#");
  mqttClient.subscribe(buf);
}

/**
  Try to reconnect to MQTT server

  @return boolean reconnection success
*/
boolean mqttReconnect() {
#ifdef DEBUG
  Serial.println(F("MQTT connecting..."));
#endif
  const int bufSize = 64;
  char buf[bufSize] = "";
  // The report topic
  strncpy(buf, mqttTopicRpt, bufSize);
  strcat(buf, "/");
  strcat(buf, nodename);
  // Connect and set LWM to "offline"
  if (mqttClient.connect(mqttId, buf, 0, true, "offline")) {
    // Publish the "online" status
    mqttPubRet("online", buf);
    // Publish the connection report
    strcat_P(buf, PSTR("/wifi"));
    mqttPubRet(WiFi.hostname().c_str(),   buf, "hostname");
    mqttPubRet(WiFi.macAddress().c_str(), buf, "mac");
    mqttPubRet(WiFi.SSID().c_str(),       buf, "ssid");
    mqttPubRet(WiFi.RSSI(),               buf, "rssi");
    // Buffer for IPs
    char ipbuf[16] = "";
    charIP(WiFi.localIP(),   ipbuf, sizeof(ipbuf));
    mqttPubRet(ipbuf, buf, "ip");
    charIP(WiFi.gatewayIP(), ipbuf, sizeof(ipbuf));
    mqttPubRet(ipbuf, buf, "gw");
    // Subscribe to command topic
    mqttSubscribe(mqttTopicCmd, nodename, true);
#ifdef DEBUG
    Serial.print(F("MQTT connected to "));
    Serial.print(mqttServer);
    Serial.print(F(" port "));
    Serial.print(mqttPort);
    Serial.println(F("."));
#endif
  }
  yield();
  return mqttClient.connected();
}

/**
  Message arrived in MQTT subscribed topics

  @param topic the topic the message arrived on (const char[])
  @param payload the message payload (byte array)
  @param length the length of the message payload (unsigned int)
*/
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  // Make a limited copy of the payload and make sure it ends with \0
  char message[100] = "";
  if (length > 100) length = 100;
  memcpy(message, payload, length);
  message[length] = '\0';
#ifdef DEBUG
  Serial.printf("MQTT %s: %s\r\n", topic, message);
#endif
  // Decompose the topic
  char *pRoot = NULL, *pTrunk = NULL, *pBranch = NULL;
  if (pRoot = strtok(topic, "/"))
    if (pTrunk = strtok(NULL, "/"))
      if (pBranch = strtok(NULL, "/"))
        // Dispatcher
        if (strcmp(pRoot, mqttTopicCmd) == 0)
          if (strcmp(pTrunk, nodename) == 0)
            if (strcmp(pBranch, "restart") == 0)
              // Restart
              ESP.restart();
            else if (strcmp(pBranch, "mode") == 0)
              // Set the display mode
              scrCurrent = atoi(message) % SCR_ALL;
            else if (strcmp(pBranch, "bright") == 0)
              // Set the brightness
              led.intensity(atoi(message));
}

/**
  Read the DS18B20 sensor
  @return success
*/
bool dsRead() {
  static uint32_t nextTime = 0;
  if (millis() >= nextTime) {
    dsOK = false;
    if (sensors.isConnected(dsAddr)) {
      sensors.requestTemperaturesByAddress(dsAddr);
      dsVal = sensors.getTemp((uint8_t*) dsAddr) / 128;
      // Convert to Fahrenheit, if needed
      if (dsDegF)
        dsVal = ((9 * dsVal) + 160) / 5;
      dsOK = true;
    }
#ifdef DEBUG
    if (!dsOK) Serial.println(F("ERR: DS18B20"));
#endif
    // Repeat after the delay
    nextTime += dsDelay * 1000UL;
  }
  return dsOK;
}

/**
  Display the current time in HH.MM format
*/
bool showHHMM() {
  // Keep the last UNIX time
  static unsigned long ltm = 0;
  // Get the date and time
  unsigned long utm = ntp.getSeconds();
  // Continue only if the second has changed
  if (utm != ltm) {
    ltm = utm;
    // Check if the time data is valid
    bool ntpOK = ntp.isValid();
    // Compute the date an time
    datetime_t dt = ntp.getDateTime(utm);
    // Check if the time is accurate, flash the separator if so
    uint8_t DOT = ((ntp.isAccurate() and (dt.ss & 0x01)) == true) ? 0x00 : LED_DP;
    // Display "  HH.MM  " or "  --.--  "
    uint8_t msg[] = {ntpOK ? (dt.hh / 10) : CHR_M,
                     ntpOK ? (dt.hh % 10 + DOT) : (CHR_M + DOT),
                     ntpOK ? (dt.mm / 10) : CHR_M,
                     ntpOK ? (dt.mm % 10) : CHR_M
                    };
    led.fbClear();
    led.fbPrint(2, msg, sizeof(msg) / sizeof(*msg));
    led.fbDisplay();
#ifdef DEBUG
    if (!ntpOK) Serial.println(F("ERR: NTP"));
    Serial.print(dt.hh);
    Serial.print(DOT ? ":" : " ");
    Serial.print(dt.mm);
    Serial.println();
#endif
  }
  return true;
}

/**
  Display the current time in HH.MM format and temperature
*/
bool showHHMMTT() {
  // Keep the last UNIX time
  static unsigned long ltm = 0;
  // Get the date and time
  unsigned long utm = ntp.getSeconds();
  // Continue only if the second has changed
  if (utm != ltm) {
    ltm = utm;
    // Check if the time data is valid
    bool ntpOK = ntp.isValid();
    // Compute the date an time
    datetime_t dt = ntp.getDateTime(utm);
    // Check if the time is accurate, flash the separator if so
    uint8_t DOT = ((ntp.isAccurate() and (dt.ss & 0x01)) == true) ? 0x00 : LED_DP;
    // Display "HH.MM TTc" or "--.-- -- "
    uint8_t msg[] = {ntpOK ? (dt.hh / 10) : CHR_M,                                                  // tenths of hours
                     ntpOK ? (dt.hh % 10 + DOT) : (CHR_M + DOT),                                    // units of hours
                     ntpOK ? (dt.mm / 10) : CHR_M,                                                  // tenths of minutes
                     ntpOK ? (dt.mm % 10) : CHR_M,                                                  // units of minutes
                     dsOK ? (dsVal < -9 ? CHR_M : CHR_S) : CHR_S,                                   // '-' if temp is below -9, space otherwise
                     dsOK ? (abs(dsVal) > 9 ? (dsVal / 10) : (dsVal < 0 ? CHR_M : CHR_S)) : CHR_M,  // tenths of temp if below -10 or over 10, '-' or ' ' otherwise
                     dsOK ? (dsVal % 10) : CHR_M,                                                   // units of temp
                     dsOK ? (dsDegF ? CHR_F : CHR_C) : CHR_S                                        // C or F
                    };
    led.fbPrint(0, msg, sizeof(msg) / sizeof(*msg));
    led.fbDisplay();
#ifdef DEBUG
    if (!ntpOK) Serial.println(F("ERR: NTP"));
    Serial.print(dt.hh);
    Serial.print(DOT ? ":" : " ");
    Serial.print(dt.mm);
    Serial.print(" ");
    if (dsOK) Serial.print(dsVal);
    else      Serial.print("--");
    Serial.print(dsDegF ? "F" : "C");
    Serial.println();
#endif
  }
  return true;
}

/**
  Display the current time in HH.MM.SS format
*/
bool showHHMMSS() {
  // Keep the last UNIX time
  static unsigned long ltm = 0;
  // Get the date and time
  unsigned long utm = ntp.getSeconds();
  // Continue only if the second has changed
  if (utm != ltm) {
    ltm = utm;
    // Check if the time data is valid
    bool ntpOK = ntp.isValid();
    // Compute the date an time
    datetime_t dt = ntp.getDateTime(utm);
    // Check if the time is accurate, flash the separator if so
    uint8_t DOT = ((ntp.isAccurate() and (dt.ss & 0x01)) == true) ? 0x00 : LED_DP;
    // Display " HH.MM.SS " or " --.--.-- "
    uint8_t msg[] = {ntpOK ? (dt.hh / 10) : CHR_M,
                     ntpOK ? (dt.hh % 10 + DOT) : (CHR_M + DOT),
                     ntpOK ? (dt.mm / 10) : CHR_M,
                     ntpOK ? (dt.mm % 10 + DOT) : (CHR_M + DOT),
                     ntpOK ? (dt.ss / 10) : CHR_M,
                     ntpOK ? (dt.ss % 10) : CHR_M
                    };
    led.fbClear();
    led.fbPrint(1, msg, sizeof(msg) / sizeof(*msg));
    led.fbDisplay();
#ifdef DEBUG
    if (!ntpOK) Serial.println(F("ERR: NTP"));
    Serial.print(dt.hh);
    Serial.print(DOT ? ":" : " ");
    Serial.print(dt.mm);
    Serial.print(DOT ? ":" : " ");
    Serial.print(dt.ss);
    Serial.println();
#endif
  }
  return true;
}

/**
  Display the current date in DD.LL.YYYY format
*/
bool showDDLLYYYY() {
  // Keep the last UNIX time
  static unsigned long ltm = 0;
  // Get the date and time
  unsigned long utm = ntp.getSeconds();
  // Continue only if the second has changed
  if (utm != ltm) {
    ltm = utm;
    // Check if the time data is valid
    bool ntpOK = ntp.isValid();
    // Compute the date an time
    datetime_t dt = ntp.getDateTime(utm);
    // Display "DD.MM.YYYY" or "--.--.----"
    uint8_t data[] = {ntpOK ? (dt.dd / 10) : CHR_M,
                      ntpOK ? (dt.dd % 10 + LED_DP) : (CHR_M + LED_DP),
                      ntpOK ? (dt.ll / 10) : CHR_M,
                      ntpOK ? (dt.ll % 10 + LED_DP) : (CHR_M + LED_DP),
                      ntpOK ? 0x02 : CHR_M,
                      ntpOK ? 0x00 : CHR_M,
                      ntpOK ? (dt.yy / 10) : CHR_M,
                      ntpOK ? dt.yy % 10 : CHR_M,
                     };
    led.fbPrint(0, data, sizeof(data) / sizeof(*data));
    led.fbDisplay();
#ifdef DEBUG
    if (!ntpOK) Serial.println(F("ERR: NTP"));
    Serial.print(dt.dd);
    Serial.print(".");
    Serial.print(dt.ll);
    Serial.print(".");
    Serial.print(dt.yy + 2000);
    Serial.println();
#endif
  }
  return true;
}

/**
  Display the voltage V.vvvU format
*/
bool showVcc() {
  static uint32_t nextTime = 0;

  if (millis() >= nextTime) {
    // Read the Vcc (mV)
    int vcc = ESP.getVcc();
    // Display "   X.xxxV"
    uint8_t data[] = {0x00, 0x00, 0x00,
                      (vcc / 1000) + LED_DP,
                      (vcc % 1000) / 100,
                      (vcc % 100) / 10,
                      (vcc % 10),
                      CHR_V,
                     };
    led.fbPrint(0, data, sizeof(data) / sizeof(*data));
    led.fbDisplay();
#ifdef DEBUG
    Serial.print(vcc / 1000);
    Serial.print(".");
    Serial.print(vcc % 1000);
    Serial.print("V");
    Serial.println();
#endif
    // Repeat after the delay
    nextTime += vccDelay;
  }
  return true;
}

/**
  Main Arduino setup function
*/
void setup() {
#ifdef DEBUG
  // Init the serial com
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
  Serial.println();
  Serial.print(NODENAME);
  Serial.print(" ");
  Serial.print(VERSION);
  Serial.print(" ");
  Serial.println(__DATE__);
#endif

  // Initialize the LEDs
#ifdef DEBUG
  // For NodeMCU: DIN GPIO4, CLK GPIO5, LOAD GPIO16
  led.init(4, 5, 16, 8);
#else
  // For ESP8266-01: DIN GPIO1 (TXD), CLK GPIO0, LOAD GPIO2
  led.init(1, 0, 2, 8);
#endif
  // Disable the display test
  led.displaytest(false);
  // Decode nothing
  led.decodemode(0);
  // Clear the display
  led.clear();
  // Power on the display
  led.shutdown(false);

  // Start up the OW sensors
  sensors.begin();
  // The first sensor is the DS18B20 temperature sensor and the only one
  sensors.getAddress(dsAddr, 0);

  /*
    // Display "NETCHRON"
    uint8_t mgsWelcome[] = {0x76, 0x4F, 0x70, 0x4E, 0x37, 0x46, 0x7E, 0x76};
  */
  // Display "NtChrono"
  uint8_t mgsWelcome[] = {0x76, 0x0F, 0x4E, 0x17, 0x05, 0x1D, 0x15, 0x1D};
  led.fbWrite(0, mgsWelcome, sizeof(mgsWelcome) / sizeof(*mgsWelcome));
  led.fbDisplay();
  // Reduce the brightness progressively
  for (uint8_t i = 15; i >= SCR_BRGHT; i--) {
    // Set the brightness
    led.intensity(i);
    delay(100);
  }
  delay(1000);

  // Try to connect to WiFi
  while (!WiFi.isConnected()) wifiConnect();

  // MQTT
#ifdef USE_MQTT_SSL
  wifiClient.setInsecure();
#endif
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(mqttCallback);

  // OTA Update
  ArduinoOTA.setPort(otaPort);
  ArduinoOTA.setHostname(NODENAME);
#ifdef OTA_PASS
  ArduinoOTA.setPassword((const char *)OTA_PASS);
#endif

  ArduinoOTA.onStart([]() {
    // Restart the progress
    otaProgress = -1;
    // Display "UP"
    led.fbClear();
    led.fbWrite(0, 0x3E);
    led.fbWrite(1, 0x67);
    led.fbDisplay();
#ifdef DEBUG
    Serial.println(F("OTA Start"));
#endif
  });

  ArduinoOTA.onEnd([]() {
    // Clear the display
    led.clear();
#ifdef DEBUG
    Serial.println(F("\nOTA Finished"));
#endif
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    const int steps = 10;
    int otaPrg = progress / (total / steps);
    if (otaProgress != otaPrg and otaPrg < steps) {
      otaProgress = otaPrg;
      // Display one '|' for each odd tick and '||' for each even tick
      if (otaPrg & 0x01)
        led.write((steps - otaPrg) / 2, 0x36);
      else
        led.write((steps - otaPrg) / 2 - 1, 0x06);
#ifdef DEBUG
      Serial.printf("Progress: %u%%\r", otaProgress * 10);
#endif
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    // Display "ERROR  x"
    uint8_t msgError[] = {0x4F, 0x46, 0x46, 0x7E, 0x46};
    led.fbWrite(0, msgError, sizeof(msgError) / sizeof(*msgError));
    led.fbPrint(7, error);
    led.fbDisplay();
#ifdef DEBUG
    Serial.printf("Error[%u]: ", error);
    if      (error == OTA_AUTH_ERROR)     Serial.print(F("Auth Failed\r\n"));
    else if (error == OTA_BEGIN_ERROR)    Serial.print(F("Begin Failed\r\n"));
    else if (error == OTA_CONNECT_ERROR)  Serial.print(F("Connect Failed\r\n"));
    else if (error == OTA_RECEIVE_ERROR)  Serial.print(F("Receive Failed\r\n"));
    else if (error == OTA_END_ERROR)      Serial.print(F("End Failed\r\n"));
#endif
  });

  // Start OTA handling
  ArduinoOTA.begin();

  // Display "SYNC"
  uint8_t msgSync[] = {0x5B, 0x3B, 0x76, 0x4E, 0x00, 0x00, led.getAnim(0, 0), led.getAnim(0, 1)};
  led.fbWrite(0, msgSync, sizeof(msgSync) / sizeof(*msgSync));
  led.fbDisplay();
  // Configure NTP
  ntp.init(NTP_SERVER, NTP_PORT, NTP_TZ);
  // Display and end-animation
  delay(100);
  led.fbWrite(6, led.getAnim(2, 0));
  led.fbWrite(7, led.getAnim(2, 1));
  led.fbDisplay();
#ifdef DEBUG
  char ntpReport[32];
  ntp.report(ntp.getSeconds(), ntpReport, 32);
  Serial.println(ntpReport);
#endif

  // Power off the display
  led.shutdown(true);
  // Delay
  delay(500);
  // Power on the display
  led.shutdown(false);

  // Start the sensor timer
  snsNextTime = millis();
}

/**
  Main Arduino loop
*/
void loop() {
  // Handle OTA
  ArduinoOTA.handle();
  yield();

  // Process incoming MQTT messages and maintain connection
  if (!mqttClient.loop())
    // Not connected, check if it's time to try to reconnect
    if (millis() >= mqttNextTime)
      // Try to reconnect every mqttDelay seconds
      if (!mqttReconnect()) mqttNextTime = millis() + mqttDelay;
  yield();

  // Read the temperature
  dsOK = dsRead();

  // Read the other sensors and publish telemetry
  if (millis() >= snsNextTime) {
    // Repeat sensor reading after the delay
    snsNextTime += snsDelay;
    // Report the temperature
    if (dsOK)
      mqttPubRet(dsVal, mqttTopicSns, "temperature");
    // Free Heap
    int heap = ESP.getFreeHeap();
    // Read the Vcc (mV) and add to the round median filter
    int vcc  = ESP.getVcc();
    // Get RSSI
    int rssi = WiFi.RSSI();
    // Create the reporting topic
    char topic[32] = "";
    char text[32] = "";
    strncpy(topic, mqttTopicRpt, sizeof(topic));
    strcat(topic, "/");
    strcat(topic, nodename);
    // Uptime in seconds and text
    unsigned long ups = 0;
    char upt[32] = "";
    ups = ntp.getUptime(upt, sizeof(upt));
    mqttPubRet(ups, topic, "uptime");
    mqttPubRet(upt, topic, "uptime", "text");
    // Free heap
    mqttPubRet(heap, topic, "heap");
    // Power supply
    snprintf(text, sizeof(text), "%d.%d", vcc / 1000, vcc % 1000);
    mqttPubRet(text, topic, "vcc");
    // Add the WiFi topic and publish the RSSI value
    mqttPubRet(rssi, topic, "wifi", "rssi");
  }

  // Choose the screen to display
  switch (scrCurrent) {
    case SCR_HHMM:
      showHHMM();
      break;
    case SCR_HHMMSS:
      showHHMMSS();
      break;
    case SCR_HHMMTT:
      showHHMMTT();
      break;
    case SCR_DDLLYYYY:
      showDDLLYYYY();
      break;
    case SCR_VCC:
      showVcc();
      break;
    default:
      showHHMM();
  }
}
