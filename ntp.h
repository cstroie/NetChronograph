/**
  ntp.h - Network Time Protocol

  Copyright (c) 2017-2018 Costin STROIE <costinstroie@eridu.eu.org>

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

#ifndef NTP_H
#define NTP_H

#include "Arduino.h"
#include <WiFiUdp.h>

static const uint8_t daysInMonth[] PROGMEM = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

struct datetime_t {
  uint8_t yy;
  uint8_t ll;
  uint8_t dd;
  uint8_t hh;
  uint8_t mm;
  uint8_t ss;
};

class NTP {
  public:
    NTP();
    unsigned long init(const char *ntpServer, int ntpPort = 123);
    unsigned long init(const char *ntpServer, int ntpPort = 123, float tz = 0);
    void          setServer(const char *ntpServer, int ntpPort = 123);
    void          setTZ(float tz = 0);
    void          report(unsigned long utm, char *buf, size_t len);
    unsigned long getSeconds(bool sync = true);
    unsigned long getUptime(char *buf, size_t len);
    datetime_t    getDateTime(unsigned long utm);
    unsigned long getUnixTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);
    uint8_t       getDOW(uint16_t year, uint8_t month, uint8_t day);
    void          getDST(unsigned long utm);
    bool          checkDST(uint16_t year, uint8_t month, uint8_t day, uint8_t hour);
    bool          checkDST(unsigned long utm);
    bool          valid       = false;                // Flag to know the time is accurate
    uint8_t       dstBeginDay;                        // The last Sunday in March
    uint8_t       dstEndDay;                          // The last Sunday on October
    unsigned long dstBegin;                           // The last Sunday in March, 3 AM, in UNIX time
    unsigned long dstEnd;                             // The last Sunday on October, 4 AM, in UNIX time
    bool          isDST       = false;                // Flag to know if DST is on
  private:
    unsigned long getNTP();
    char          server[50];                         // NTP server to connect to (RFC5905)
    int           port     = 123;                     // NTP port
    unsigned long nextSync = 0UL;                     // Next time to syncronize
    unsigned long delta    = 0UL;                     // Difference between real time and internal clock
    float         TZ       = 0;                       // Time zone
};

#endif /* NTP_H */
