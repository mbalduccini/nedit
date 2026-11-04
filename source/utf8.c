/* Copyright 2026. Distributed under the same GPL terms as NEdit. */
#include "utf8.h"
#ifdef HAVE_UTF8PROC
#include <utf8proc.h>
#endif

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

int Utf8IsIgnorable(unsigned long scalar)
{
#ifdef HAVE_UTF8PROC
    return utf8proc_get_property(scalar)->ignorable;
#else
    (void)scalar;
    return 0;
#endif
}

int Utf8CharWidth(unsigned long scalar)
{
#ifdef HAVE_UTF8PROC
    return utf8proc_charwidth((utf8proc_int32_t)scalar);
#else
    (void)scalar;
    return 1;
#endif
}

int Utf8NextGrapheme(const char *text, int available, int *columns)
{
    unsigned long previous, scalar;
    int pos, length, valid, width, nextWidth;
#ifdef HAVE_UTF8PROC
    utf8proc_int32_t state = 0;
#endif
    pos = Utf8Decode(text, available, &previous, &valid);
    width = valid ? Utf8CharWidth(previous) : 1;
    if (pos && valid) while (pos < available) {
        length = Utf8Decode(text + pos, available - pos, &scalar, &valid);
        if (!valid) break;
#ifdef HAVE_UTF8PROC
        if (utf8proc_grapheme_break_stateful(previous, scalar, &state)) break;
#else
        break;
#endif
        nextWidth = Utf8CharWidth(scalar);
        if (nextWidth > width) width = nextWidth;
        previous = scalar;
        pos += length;
    }
    if (columns) *columns = pos ? width : 0;
    return pos;
}

int Utf8NextGraphemeZ(const char *text, int *columns)
{
    unsigned long previous, scalar;
    int pos = 0, length, valid, width = 0, available, nextWidth;
#ifdef HAVE_UTF8PROC
    utf8proc_int32_t state = 0;
#endif
    while (text[pos]) {
        for (available = 1; available < 4 && text[pos + available]; available++);
        length = Utf8Decode(text + pos, available, &scalar, &valid);
        if (pos) {
            if (!valid) break;
#ifdef HAVE_UTF8PROC
            if (utf8proc_grapheme_break_stateful(previous, scalar, &state)) break;
#else
            break;
#endif
        }
        nextWidth = valid ? Utf8CharWidth(scalar) : 1;
        if (nextWidth > width) width = nextWidth;
        previous = scalar;
        pos += length;
        if (!valid) break;
    }
    if (columns) *columns = width;
    return pos;
}
