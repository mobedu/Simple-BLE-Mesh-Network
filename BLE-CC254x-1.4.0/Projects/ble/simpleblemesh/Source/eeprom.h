
/*

  Copyright (c) 2013 RedBearLab

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal 
  in the Software without restriction, including without limitation the rights 
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

*/
unsigned long eeprom_read_long(unsigned short addr);
void eeprom_write_long(unsigned short address, unsigned long value);
void eeprom_write_25(unsigned char *data);
void eeprom_write_bytes(unsigned short addr, unsigned char* data, unsigned char len);
void eeprom_page_write(unsigned short addr, unsigned char wdata0, unsigned char wdata1, unsigned char wdata2, unsigned char wdata3);
void eeprom_write(unsigned short addr, unsigned char wdata);
unsigned char eeprom_read(unsigned short addr);
void eeprom_read_bytes(unsigned short addr, unsigned char* data, unsigned char len);
