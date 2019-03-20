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
const char VERSION[]  = "0.2";

// WiFi
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

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
  // Try to connect to WiFi
  if (!WiFi.isConnected()) {
    // Display "Conn"
    led.fbClear();
    led.fbWrite(0, 0x4E);
    led.fbWrite(1, 0x1D);
    led.fbWrite(2, 0x15);
    led.fbWrite(3, 0x15);
    led.fbDisplay();
#ifdef DEBUG
    Serial.print(F("WiFi connecting "));
#endif
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (!WiFi.isConnected()) {
#ifdef DEBUG
      Serial.print(".");
#endif
      // FIXME Do some animation
      delay(1000);
    };
#ifdef DEBUG
    Serial.println(F(" done."));
#endif
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
    if (!ok) Serial.println(F("Failed to read the DHT11 sensor"));
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
  static uint8_t ss = 99;
  // Get the date and time
  unsigned long utm = ntp.getSeconds();
  datetime_t dt = ntp.getDateTime(utm);
  // Check for DST and compute again if needed
  if (ntp.dstCheck(dt.yy, dt.ll, dt.dd, dt.hh))
    dt = ntp.getDateTime(utm + 3600);
  // Display only if the data has changed
  if (dt.ss != ss) {
    ss = dt.ss;
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
  static uint8_t ss = 99;
  // Get the date and time
  unsigned long utm = ntp.getSeconds();
  datetime_t dt = ntp.getDateTime(utm);
  // Check for DST and compute again if needed
  if (ntp.dstCheck(dt.yy, dt.ll, dt.dd, dt.hh))
    dt = ntp.getDateTime(utm + 3600);
  // Display only if the data has changed
  if (dt.ss != ss) {
    // Read the temperature
    static byte temp, hmdt;
    dhtRead(&temp, &hmdt);
    // Display
    ss = dt.ss;
    led.fbClear();
    led.fbPrint(0, dt.hh / 10);
    led.fbPrint(1, dt.hh % 10, true);
    led.fbPrint(2, dt.mm / 10);
    led.fbPrint(3, dt.mm % 10);
    if (dhtOK) {
      led.fbPrint(6, temp % 10);
      if (temp >= 10)
        led.fbPrint(5, temp / 10);
    }
    else
      led.fbPrint(6, 0x0E);
    led.fbPrint(7, 0x0B);
    led.fbDisplay();
#ifdef DEBUG
    Serial.print(dt.hh);
    Serial.print(":");
    Serial.print(dt.mm);
    Serial.print(" ");
    Serial.print(temp);
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
  static uint8_t ss = 99;
  // Get the date and time
  unsigned long utm = ntp.getSeconds();
  datetime_t dt = ntp.getDateTime(utm);
  // Check for DST and compute again if needed
  if (ntp.dstCheck(dt.yy, dt.ll, dt.dd, dt.hh))
    dt = ntp.getDateTime(utm + 3600);
  // Display only if the data has changed
  if (dt.ss != ss) {
    ss = dt.ss;
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
  delay(1000);
  led.displaytest(false);
  // Decode nothing
  led.decodemode(0);
  // Clear the display
  led.clear();
  // Set the brightness
  led.intensity(1);
  // Power on the display
  led.shutdown(false);

  // Try to connect to WiFi
  wifiConnect();

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
    int otaPrg = progress / (total / 5);
    if (otaProgress != otaPrg and otaPrg < 5) {
      otaProgress = otaPrg;
      // Display one "-" for each tick
      led.write(4 - otaPrg, 0x01);
#ifdef DEBUG
      Serial.printf("Progress: %u%%\r", otaProgress * 10);
#endif
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    // Display "UP Error"
    led.fbClear();
    led.fbWrite(0, 0x3E);
    led.fbWrite(1, 0x67);
    led.fbWrite(2, 0x00);
    led.fbWrite(3, 0x4F);
    led.fbWrite(4, 0x05);
    led.fbWrite(5, 0x05);
    led.fbWrite(6, 0x1D);
    led.fbWrite(7, 0x05);
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

  // Configure NTP
  ntp.init(NTP_SERVER);
  ntp.setTZ(NTP_TZ);
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
