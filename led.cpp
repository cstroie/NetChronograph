/**
  led.cpp - MAX7219 driver for 7-segment displays

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

#include "Arduino.h"

#include "led.h"

LED::LED() {
}

/**
  Initialize the display

  @param dataPin the CipSelect pin
  @param clkPin the CipSelect pin
  @param csPin the CipSelect pin
  @param lines the scan lines number
*/
void LED::init(uint8_t dataPin, uint8_t clkPin, uint8_t csPin, uint8_t digits) {
  /* Pin configuration */
  SPI_MOSI = dataPin;
  SPI_CLK = clkPin;
  SPI_CS = csPin;
  /* Configure the pins for software SPI */
#ifndef DEBUG
  pinMode(SPI_MOSI, OUTPUT);
  pinMode(SPI_CLK, OUTPUT);
  pinMode(SPI_CS, OUTPUT);
  digitalWrite(SPI_CS, HIGH);
#endif
  // Set the scan limit
  this->scanlimit(digits);
}

/**
  Set the decoding mode

  @param value the decoding mode 0..0x0F
*/
void LED::decodemode(uint8_t value) {
  this->sendSPI(OP_DECODE, value & 0x0F);
}

/**
  Set the LED brightness

  @param value the brightness 0..0x0F
*/
void LED::intensity(uint8_t value) {
  if (value <= 0x0F)
    this->sendSPI(OP_INTENS, value);
}

/**
  Set the scan limit

  @param value the scan limit 0..0x07
*/
void LED::scanlimit(uint8_t value) {
  uint8_t _scanlimit = ((value - 1) & 0x07) + 1;
  this->sendSPI(OP_SCNLMT, _scanlimit - 1);
}

/**
  LEDs off / on

  @param yesno the on/off switch
*/
void LED::shutdown(bool yesno) {
  uint8_t data = yesno ? 0 : 1;
  this->sendSPI(OP_SHTDWN, data);
}

/**
  Display test mode

  @param yesno the test on/off switch
*/
void LED::displaytest(bool yesno) {
  uint8_t data = yesno ? 1 : 0;
  this->sendSPI(OP_DSPTST, data);
}

/**
  Print a valid symbol at the specified position

  @param pos position
  @param data the character/digit to print
  @param dp decimal point
*/
void LED::print(uint8_t pos, uint8_t data, bool dp) {
  uint8_t value = FONT[data & 0x0F];
  if (dp)
    value |= 0x80;
  this->write(pos, value);
}

/**
  Write arbitrary data at the specified position

  @param pos position
  @param data the data to write
*/
void LED::write(uint8_t pos, uint8_t data) {
  this->sendSPI((pos & 0x07) + 1, data);
}

/**
  Clear the matrix (all leds off)
*/
void LED::clear() {
  for (uint8_t pos = 0; pos < DIGITS; pos++)
    this->sendSPI(pos + 1, 0x00);
}

/**
  Display the framebuffer
*/
void LED::fbDisplay() {
  for (uint8_t pos = 0; pos < DIGITS; pos++)
    this->sendSPI(DIGITS - pos - 1, fbData[pos]);
}

/**
  Print a valid symbol at the specified position

  @param pos position (0 is left)
  @param data the character/digit to print
  @param dp decimal point
*/
void LED::fbPrint(uint8_t pos, uint8_t data, bool dp) {
  uint8_t value = FONT[data & 0x0F];
  if (dp)
    value |= 0x80;
  this->fbWrite(pos, value);
}

/**
  Write arbitrary data at the specified position

  @param pos position (0 is left)
  @param data the data to write
*/
void LED::fbWrite(uint8_t pos, uint8_t data) {
  this->fbData[pos & 0x07] = data;
}

/**
  Clear the framebuffer
*/
void LED::fbClear() {
  memset(fbData, 0, DIGITS);
}

/**
  Send the data using software SPI

  @param reg the register
  @param data the data for the register
*/
void LED::sendSPI(uint8_t reg, uint8_t data) {
#ifdef DEBUG
  Serial.print("SPI: ");
  Serial.print(reg, 16);
  Serial.print(" ");
  Serial.print(data, 16);
  Serial.println();
#else
  /* Chip select */
  digitalWrite(SPI_CS, LOW);
  /* Send the data */
  shiftOut(SPI_MOSI, SPI_CLK, MSBFIRST, reg);
  shiftOut(SPI_MOSI, SPI_CLK, MSBFIRST, data);
  /* Latch data */
  digitalWrite(SPI_CS, HIGH);
#endif
}
