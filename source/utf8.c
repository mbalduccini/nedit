/* Copyright 2026. Distributed under the same GPL terms as NEdit. */
#include "utf8.h"

int Utf8Decode(const char *text, int available, unsigned long *scalar,
        int *valid)
{
    const unsigned char *bytes = (const unsigned char *)text;
    unsigned long value = 0xfffd;
    int length = 1, ok = 0, i;

    if (available <= 0) {
        if (scalar != 0) *scalar = 0;
        if (valid != 0) *valid = 0;
        return 0;
    }
    if (bytes[0] < 0x80) {
        value = bytes[0];
        ok = 1;
    } else if (bytes[0] >= 0xc2 && bytes[0] <= 0xf4) {
        length = bytes[0] < 0xe0 ? 2 : bytes[0] < 0xf0 ? 3 : 4;
        if (available >= length) {
            ok = 1;
            for (i = 1; i < length; i++)
                if ((bytes[i] & 0xc0) != 0x80) ok = 0;
            if (length == 3 &&
                    ((bytes[0] == 0xe0 && bytes[1] < 0xa0) ||
                     (bytes[0] == 0xed && bytes[1] >= 0xa0))) ok = 0;
            if (length == 4 &&
                    ((bytes[0] == 0xf0 && bytes[1] < 0x90) ||
                     (bytes[0] == 0xf4 && bytes[1] >= 0x90))) ok = 0;
            if (ok) {
                value = bytes[0] & (length == 2 ? 0x1f :
                        length == 3 ? 0x0f : 0x07);
                for (i = 1; i < length; i++)
                    value = (value << 6) | (bytes[i] & 0x3f);
            }
        }
    }
    if (!ok) length = 1;
    if (scalar != 0) *scalar = value;
    if (valid != 0) *valid = ok;
    return length;
}
