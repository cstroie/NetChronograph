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
  digitalWrite(SPI_MOSI, HIGH); // The blue led is connected here, lights on low
  pinMode(SPI_CLK, OUTPUT);
  digitalWrite(SPI_CLK, LOW);   // Data bit is read on rising edge of the clock
  pinMode(SPI_CS, OUTPUT);
  digitalWrite(SPI_CS, HIGH);   // Data is latched on rising edge of CS
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
    this->sendSPI(DIGITS - pos, fbData[pos]);
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
  Print an array of valid symbols starting at the specified position

  @param pos position (0 is left)
  @param data the character/digit to print
  @param len number of characters
*/
void LED::fbPrint(uint8_t pos, uint8_t* data, uint8_t len) {
  if (len > DIGITS)
    len = DIGITS;
  fbClear();
#ifdef DEBUG
  Serial.print(F("PRT: "));
#endif
  for (uint8_t i = 0; i < len; i++) {
    fbPrint(pos + i, data[i] & (LED_DP - 1), data[i] & LED_DP);
#ifdef DEBUG
    if (data[i] < 0x10 or
        (data[i] > LED_DP and data[i] - LED_DP < 0x10))
      Serial.print("0");
    if (data[i] > LED_DP) {
      Serial.print(data[i] - LED_DP, 16);
      Serial.print(".");
    }
    else
      Serial.print(data[i], 16);
    if (i < len - 1)
      Serial.print(F(" "));
    else
      Serial.println();
#endif
  }
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
  Write an array of arbitrary data starting at the specified position

  @param pos starting position (0 is left)
  @param data the data to write
  @param len number of characters
*/
void LED::fbWrite(uint8_t pos, uint8_t* data, uint8_t len) {
  if (len > DIGITS)
    len = DIGITS;
  fbClear();
#ifdef DEBUG
  Serial.print(F("WRT: "));
#endif
  for (uint8_t i = 0; i < len; i++) {
    fbWrite(pos + i, data[i]);
#ifdef DEBUG
    if (data[i] < 0x10 or
        (data[i] > LED_DP and data[i] - LED_DP < 0x10))
      Serial.print("0");
    if (data[i] > LED_DP) {
      Serial.print(data[i] - LED_DP, 16);
      Serial.print(".");
    }
    else
      Serial.print(data[i], 16);
    if (i < len - 1)
      Serial.print(F(", "));
    else
      Serial.println();
#endif
  }
}

/**
  Clear the framebuffer
*/
void LED::fbClear() {
  memset(fbData, 0, DIGITS);
}

/**
  Get the animation symbol from the specified index for
  the first or second position.

  @param idx the index in animation vector (phase)
  @param pos the first or second position character
  @return the symbol
*/
uint8_t LED::getAnim(uint8_t idx, uint8_t pos) {
  return ANIM[idx & 0x03][pos & 0x01];
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
  this->shiftOut(SPI_MOSI, SPI_CLK, MSBFIRST, reg);
  this->shiftOut(SPI_MOSI, SPI_CLK, MSBFIRST, data);
  /* Latch data */
  digitalWrite(SPI_CS, HIGH);
#endif
}

/**
  Shift out the data, using a short delay

  @param dataPin the data out pin
  @param clockPin the clock pin
  @param bitOrder the bit order, MSB or LSB first
  @param val the byte to send
*/
void LED::shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t val) {
  uint8_t i;
  for (i = 0; i < 8; i++) {
    if (bitOrder == LSBFIRST)
      digitalWrite(dataPin, !!(val & (1 << i)));
    else
      digitalWrite(dataPin, !!(val & (1 << (7 - i))));
    delay(1);
    digitalWrite(clockPin, HIGH);
    delay(1);
    digitalWrite(clockPin, LOW);
  }
}
