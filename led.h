/**
  led.h - MAX7219 driver for 7-segment displays

  Copyright (c) 2017-2019 Costin STROIE <costinstroie@eridu.eu.org>

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

#ifndef LED_H
#define LED_H

#define DIGITS  8

#include "Arduino.h"
#include "config.h"

// The basic digits font
const uint8_t FONT[] = {0x7E, 0x30, 0x6D, 0x79, 0x33, 0x5B, 0x5F, 0x70,
                        0x7F, 0x7B, 0x00, 0x43, 0x4E, 0x25, 0x01, 0x00
                       };

// Animation symbols, the last one is the end-animation
const uint8_t ANIM[][2] = {{0x0C, 0x60}, {0x06, 0x30}, {0x42, 0x18}, {0x48, 0x48}, {0x4E, 0x78}};

enum LEDOps {OP_NOOP,   OP_DIGIT0, OP_DIGIT1, OP_DIGIT2, OP_DIGIT3,
             OP_DIGIT4, OP_DIGIT5, OP_DIGIT6, OP_DIGIT7, OP_DECODE,
             OP_INTENS, OP_SCNLMT, OP_SHTDWN, OP_DSPTST = 0x0F
            };

class LED {
  public:
    LED();

    void init(uint8_t dataPin, uint8_t clkPin, uint8_t csPin, uint8_t digits);

    void decodemode(uint8_t value);
    void intensity(uint8_t value);
    void scanlimit(uint8_t value);
    void shutdown(bool yesno);
    void displaytest(bool yesno);

    void print(uint8_t pos, uint8_t data, bool dp = false);
    void write(uint8_t pos, uint8_t data);
    void clear();

    void fbDisplay();
    void fbPrint(uint8_t pos, uint8_t data, bool dp = false);
    void fbWrite(uint8_t pos, uint8_t data);
    void fbClear();

    uint8_t getAnim(uint8_t idx, uint8_t pos);

  private:
    uint8_t fbData[DIGITS] = {0};

    uint8_t SPI_MOSI; // The clock is signaled on this pin
    uint8_t SPI_CLK;  // This one is driven LOW for chip selection
    uint8_t SPI_CS;   // The maximum number of devices we use

    void sendSPI(uint8_t reg, uint8_t data);
};

#endif /* LED_H */
