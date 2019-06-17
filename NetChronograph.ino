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
const char VERSION[]  = "0.14";

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
#define SCR_DEF SCR_HHMM
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

// LED driver for MAX7219
#include "led.h"
LED led;
enum SCREENS {SCR_HHMM, SCR_HHMMSS, SCR_HHMMTT, SCR_DDLLYYYY, SCR_VCC, SCR_ALL};  // Screens
uint8_t scrDefault = SCR_DEF;                     // The default screen to display
uint8_t scrCurrent = scrDefault;                  // The current screen to display
uint8_t scrDelay = 5;                             // Time (in seconds) to return to the default screen

// Network Time Protocol
#include "ntp.h"
NTP ntp;

// DHT11 sensor
#include <SimpleDHT.h>
bool                dhtOK         = false;        // The temperature/humidity sensor presence flag
bool                dhtDegF       = false;        // Show temperature in Celsius or Fahrenheit
bool                dhtShowRH     = true;         // Show temperature and relative humidity, alternatively
const unsigned long dhtDelay      = 1000UL * 10;  // Delay between sensor readings
const int           pinDHT        = 3;            // Temperature/humidity sensor input pin
SimpleDHT11         dht(pinDHT);                  // The DHT22 temperature/humidity sensor

// OTA
int otaPort     = 8266;
int otaProgress = -1;

// Set ADC to Voltage
ADC_MODE(ADC_VCC);
const unsigned long vccDelay      = 1000UL;       // Delay between Vcc readings

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
    }
  }
}

/**
  Read the DHT22 sensor

  @param temp temperature
  @param hmdt humidity
  @param drop drop the reading (read twice)
  @return success
*/
bool dhtRead(byte *temp, byte *hmdt, bool drop = false) {
  static uint32_t nextTime = 0;

  if (millis() >= nextTime) {
    byte t = 0, h = 0;
    dhtOK = false;
    if (drop)
      // Read and drop
      dht.read(NULL, NULL, NULL);
    else {
      // Read and store
      if (dht.read(&t, &h, NULL) == SimpleDHTErrSuccess) {
        if (dhtDegF)
          *temp = (uint8_t)(((9 * (int)t) + 160) / 5);
        else
          *temp = t;
        *hmdt = h;
        dhtOK = true;
      }
    }
#ifdef DEBUG
    if (!dhtOK) Serial.println(F("ERR: DHT11"));
#endif
    // Repeat after the delay
    nextTime += dhtDelay;
  }
  return dhtOK;
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
    // Display "  HH.MM  "
    uint8_t msg[] = {ntpOK ? (dt.hh / 10) : 0x0E, ntpOK ? (dt.hh % 10 + DOT) : (0x0E + DOT),
                     ntpOK ? (dt.mm / 10) : 0x0E, ntpOK ? (dt.mm % 10) : 0x0E
                    };
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
    // Read the temperature
    static byte temp, hmdt;
    dhtRead(&temp, &hmdt);
    // Choose from temperature and humidity
    bool dhtHT = dhtShowRH and (dt.ss & 0x04);
    byte dhtVal = dhtHT ? hmdt : temp;
    // Display "HH.MM TTc" or "HH.MM RH%" or "--.-- -- "
    uint8_t msg[] = {ntpOK ? (dt.hh / 10) : 0x0E, ntpOK ? (dt.hh % 10 + DOT) : (0x0E + DOT),
                     ntpOK ? (dt.mm / 10) : 0x0E, ntpOK ? (dt.mm % 10) : 0x0E,
                     0x0A,
                     dhtOK ? (dhtVal / 10) : 0x0E,
                     dhtOK ? (dhtVal % 10) : 0x0E,
                     dhtOK ? (dhtHT ? 0x0D : (dhtDegF ? 0x0F : 0x0C)) : 0x0A
                    };
    led.fbPrint(0, msg, sizeof(msg) / sizeof(*msg));
    led.fbDisplay();
#ifdef DEBUG
    if (!ntpOK) Serial.println(F("ERR: NTP"));
    Serial.print(dt.hh);
    Serial.print(DOT ? ":" : " ");
    Serial.print(dt.mm);
    Serial.print(" ");
    if (dhtOK) Serial.print(dhtVal);
    else       Serial.print("--");
    if (dhtHT) Serial.print("%");
    else       Serial.print(dhtDegF ? "F" : "C");
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
    // Display
    led.fbClear();
    uint8_t msg[] = {ntpOK ? (dt.hh / 10) : 0x0E, ntpOK ? (dt.hh % 10 + DOT) : (0x0E + DOT),
                     ntpOK ? (dt.mm / 10) : 0x0E, ntpOK ? (dt.mm % 10 + DOT) : (0x0E + DOT),
                     ntpOK ? (dt.ss / 10) : 0x0E, ntpOK ? (dt.ss % 10) : 0x0E
                    };
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
    // Display
    uint8_t data[] = {ntpOK ? (dt.dd / 10) : 0x0E, ntpOK ? (dt.dd % 10 + LED_DP) : (0x0E + LED_DP),
                      ntpOK ? (dt.ll / 10) : 0x0E, ntpOK ? (dt.ll % 10 + LED_DP) : (0x0E + LED_DP),
                      ntpOK ? 0x02 : 0x0E, ntpOK ? 0x00 : 0x0E,
                      ntpOK ? (dt.yy / 10) : 0x0E, ntpOK ? dt.yy % 10 : 0x0E,
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
    // Display
    uint8_t data[] = {0x00, 0x00, 0x00,
                      (vcc / 1000) + LED_DP,
                      (vcc % 1000) / 100,
                      (vcc % 100) / 10,
                      (vcc % 10),
                      0x0B,
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
  Serial.begin(115200);
  Serial.println();
  Serial.print(NODENAME);
  Serial.print(" ");
  Serial.print(VERSION);
  Serial.print(" ");
  Serial.println(__DATE__);
#endif

  // Initialize the LEDs
  // For ESP8266-01: DIN GPIO1 (TXD), CLK GPIO0, LOAD GPIO2
  led.init(1, 0, 2, 8);
  // Disable the display test
  led.displaytest(false);
  // Decode nothing
  led.decodemode(0);
  // Clear the display
  led.clear();
  // Power on the display
  led.shutdown(false);

  // Set the DHT pin mode to INPUT_PULLUP
  dht.setPinInputMode(INPUT_PULLUP);

  /*
    // Display "NETCHRON"
    uint8_t mgsWelcome[] = {0x76, 0x4F, 0x70, 0x4E, 0x37, 0x46, 0x7E, 0x76};
  */
  // Display "NtChrono"
  uint8_t mgsWelcome[] = {0x76, 0x0F, 0x4E, 0x17, 0x05, 0x1D, 0x15, 0x1D};
  led.fbWrite(0, mgsWelcome, sizeof(mgsWelcome) / sizeof(*mgsWelcome));
  led.fbDisplay();
  // Reduce the brightness progressively
  for (uint8_t i = 15; i >= 1; i--) {
    // Set the brightness
    led.intensity(i);
    delay(100);
  }
  delay(1000);

  // Try to connect to WiFi
  while (!WiFi.isConnected()) wifiConnect();

  // OTA Update
  ArduinoOTA.setPort(otaPort);
  ArduinoOTA.setHostname(NODENAME);
  // ArduinoOTA.setPassword((const char *)"123");

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
    if (error == OTA_AUTH_ERROR)
      Serial.println(F("Auth Failed"));
    else if (error == OTA_BEGIN_ERROR)
      Serial.println(F("Begin Failed"));
    else if (error == OTA_CONNECT_ERROR)
      Serial.println(F("Connect Failed"));
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println(F("Receive Failed"));
    else if (error == OTA_END_ERROR)
      Serial.println(F("End Failed"));
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
}

/**
  Main Arduino loop
*/
void loop() {
  // Handle OTA
  ArduinoOTA.handle();
  yield();

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
