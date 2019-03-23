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
const char VERSION[]  = "0.6";

// WiFi
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

// LED driver for MAX7219
#include "led.h"
LED led;

// Network Time Protocol
#include "ntp.h"
NTP ntp;

// DHT11 sensor
#include <SimpleDHT.h>
bool                dhtOK         = false;        // The temperature/humidity sensor presence flag
bool                dhtDrop       = true;         // Always drop the first reading
const unsigned long dhtDelay      = 10000UL;      // Delay between sensor readings
const int           pinDHT        = 3;            // Temperature/humidity sensor input pin
SimpleDHT22         dht(pinDHT);                  // The DHT22 temperature/humidity sensor

// OTA
int otaPort = 8266;

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
    led.fbClear();
    led.fbWrite(0, 0x4E);
    led.fbWrite(1, 0x7E);
    led.fbWrite(2, 0x76);
    led.fbWrite(3, 0x76);
    led.fbWrite(6, led.getAnim(idxAnim, 0));
    led.fbWrite(7, led.getAnim(idxAnim, 1));
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
      led.fbClear();
      led.fbWrite(0, 0x1E);
      led.fbWrite(1, 0x3C);
      led.fbWrite(2, 0x30);
      led.fbWrite(3, 0x47);
      led.fbWrite(4, 0x30);
      led.fbWrite(5, 0x00);
      led.fbWrite(6, 0x77);
      led.fbWrite(7, 0x67);
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
    if (drop)
      // Read and drop
      dhtOK = dht.read(NULL, NULL, NULL) != SimpleDHTErrSuccess;
    else {
      // Read and store
      if (dht.read(&t, &h, NULL) == SimpleDHTErrSuccess) {
        if (temp) *temp = t;
        if (hmdt) *hmdt = h;
        dhtOK = true;
      }
    }
#ifdef DEBUG
    if (!dhtOK) Serial.println(F("Failed to read the DHT11 sensor"));
#endif
    // Repeat after the delay
    nextTime += dhtDelay;
  }
  return dhtOK;
}

/**
  Display the current time in HH.MM format
*/
bool showTimeHHMM() {
  // Keep the last UNIX time
  static unsigned long ltm = 0;
  // Get the date and time
  unsigned long utm = ntp.getSeconds();
  // Continue only if the second has changed
  if (utm != ltm) {
    ltm = utm;
    // Compute the date an time
    datetime_t dt = ntp.getDateTime(utm);
    // Display
    led.fbClear();
    led.fbPrint(2, dt.hh / 10);
    led.fbPrint(3, dt.hh % 10, true);
    led.fbPrint(4, dt.mm / 10);
    led.fbPrint(5, dt.mm % 10);
    led.fbDisplay();
#ifdef DEBUG
    Serial.print(dt.hh);
    Serial.print(":");
    Serial.print(dt.mm);
    Serial.println();
#endif
  }
  return true;
}

/**
  Display the current time in HH.MM format and temperature
*/
bool showTimeTempHHMM() {
  // Keep the last UNIX time
  static unsigned long ltm = 0;
  // Get the date and time
  unsigned long utm = ntp.getSeconds();
  // Continue only if the second has changed
  if (utm != ltm) {
    ltm = utm;
    // Compute the date an time
    datetime_t dt = ntp.getDateTime(utm);
    // Read the temperature
    static byte temp, hmdt;
    dhtRead(&temp, &hmdt);
    // Display
    led.fbClear();
    // Time
    led.fbPrint(0, dt.hh / 10);
    led.fbPrint(1, dt.hh % 10, true);
    led.fbPrint(2, dt.mm / 10);
    led.fbPrint(3, dt.mm % 10);
    if (dhtOK) {
      // Temperature
      led.fbPrint(6, temp % 10);
      if (temp >= 10)
        led.fbPrint(5, temp / 10);
    }
    else {
      // Error '--'
      led.fbPrint(6, 0x0E);
      led.fbPrint(5, 0x0E);
    }
    // Celsius symbol
    led.fbPrint(7, 0x0B);
    led.fbDisplay();
#ifdef DEBUG
    Serial.print(dt.hh);
    Serial.print(":");
    Serial.print(dt.mm);
    Serial.print(" ");
    if (dhtOK)
      Serial.print(temp);
    else
      Serial.print("--");
    Serial.print("C");
    Serial.println();
#endif
  }
  return true;
}

/**
  Display the current time in HH.MM.SS format
*/
bool showTimeHHMMSS() {
  // Keep the last UNIX time
  static unsigned long ltm = 0;
  // Get the date and time
  unsigned long utm = ntp.getSeconds();
  // Continue only if the second has changed
  if (utm != ltm) {
    ltm = utm;
    // Compute the date an time
    datetime_t dt = ntp.getDateTime(utm);
    // Display
    led.fbClear();
    led.fbPrint(1, dt.hh / 10);
    led.fbPrint(2, dt.hh % 10, true);
    led.fbPrint(3, dt.mm / 10);
    led.fbPrint(4, dt.mm % 10, true);
    led.fbPrint(5, dt.ss / 10);
    led.fbPrint(6, dt.ss % 10);
    led.fbDisplay();
#ifdef DEBUG
    Serial.print(dt.hh);
    Serial.print(":");
    Serial.print(dt.mm);
    Serial.print(":");
    Serial.print(dt.ss);
    Serial.println();
#endif
  }
  return true;
}

/**
  Display the current date in DD.LL.YYYY format
*/
bool showDateDDLLYYYY() {
  // Keep the last UNIX time
  static unsigned long ltm = 0;
  // Get the date and time
  unsigned long utm = ntp.getSeconds();
  // Continue only if the second has changed
  if (utm != ltm) {
    ltm = utm;
    // Compute the date an time
    datetime_t dt = ntp.getDateTime(utm);
    // Display
    led.fbClear();
    led.fbPrint(0, dt.dd / 10);
    led.fbPrint(1, dt.dd % 10, true);
    led.fbPrint(2, dt.ll / 10);
    led.fbPrint(3, dt.ll % 10, true);
    led.fbPrint(4, 2);
    led.fbPrint(5, 0);
    led.fbPrint(6, dt.yy / 10);
    led.fbPrint(7, dt.yy % 10);
    led.fbDisplay();
#ifdef DEBUG
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
  // For ESP8266-01: DIN GPIO2, CLK GPIO0, LOAD TXD (GPIO1)
  led.init(2, 0, 1, 8);
  // Do a display test for a second
  led.displaytest(true);
  delay(100);
  led.displaytest(false);
  // Decode nothing
  led.decodemode(0);
  // Clear the display
  led.clear();
  // Set the brightness
  led.intensity(0);
  // Power on the display
  led.shutdown(false);

  /*
    // Display "NETCHRON"
    led.fbClear();
    led.fbWrite(0, 0x76);
    led.fbWrite(1, 0x4F);
    led.fbWrite(2, 0x70);
    led.fbWrite(3, 0x4E);
    led.fbWrite(4, 0x37);
    led.fbWrite(5, 0x46);
    led.fbWrite(6, 0x7E);
    led.fbWrite(7, 0x76);
    led.fbDisplay();
  */
  // Display "NtChrono"
  led.fbClear();
  led.fbWrite(0, 0x76);
  led.fbWrite(1, 0x0F);
  led.fbWrite(2, 0x4E);
  led.fbWrite(3, 0x17);
  led.fbWrite(4, 0x05);
  led.fbWrite(5, 0x1D);
  led.fbWrite(6, 0x15);
  led.fbWrite(7, 0x1D);
  led.fbDisplay();
  delay(1000);

  // Try to connect to WiFi
  while (!WiFi.isConnected()) wifiConnect();

  // OTA Update
  ArduinoOTA.setPort(otaPort);
  ArduinoOTA.setHostname(NODENAME);
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
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
    static int otaProgress = -1;
    const int steps = 10;
    int otaPrg = progress / (total / steps);
    if (otaProgress != otaPrg and otaPrg < steps) {
      otaProgress = otaPrg;
      // Display one '|' for each even tick and '||' for each odd tick
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
    led.fbClear();
    led.fbWrite(0, 0x4F);
    led.fbWrite(1, 0x46);
    led.fbWrite(2, 0x46);
    led.fbWrite(3, 0x7E);
    led.fbWrite(4, 0x46);
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
  led.fbClear();
  led.fbWrite(0, 0x5B);
  led.fbWrite(1, 0x3B);
  led.fbWrite(2, 0x76);
  led.fbWrite(3, 0x4E);
  led.fbWrite(6, led.getAnim(0, 0));
  led.fbWrite(7, led.getAnim(0, 1));
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
}

/**
  Main Arduino loop
*/
void loop() {
  // Handle OTA
  ArduinoOTA.handle();
  yield();

  showTimeTempHHMM();
}
