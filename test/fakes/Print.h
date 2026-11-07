/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host-test stand-in for the Arduino Print class, only the parts
             Adafruit_GFX text drawing uses
**************************************************************************/

#ifndef FAKE_PRINT_H
#define FAKE_PRINT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

class Print {
    public:
        virtual ~Print() {}
        virtual size_t write(uint8_t c) = 0;
        virtual size_t write(const uint8_t* buffer, size_t size) {
            size_t n = 0;
            while (size--) n += write(*buffer++);
            return n;
        }
        size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
};

#endif
