/* Copyright 2026. Distributed under the same GPL terms as NEdit. */
#include "utf8.h"
#include "../util/nedit_malloc.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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

/* Unicode operations explicitly keep byte offsets and malformed bytes. */
int Utf8Encode(unsigned long c, char out[4])
{
    if (c < 0x80) {
        out[0] = c;
        return 1;
    }
    if (c < 0x800) {
        out[0] = 0xc0 | (c >> 6);
        out[1] = 0x80 | (c & 63);
        return 2;
    }
    if (c < 0x10000) {
        out[0] = 0xe0 | (c >> 12);
        out[1] = 0x80 | ((c >> 6) & 63);
        out[2] = 0x80 | (c & 63);
        return 3;
    }
    out[0] = 0xf0 | (c >> 18);
    out[1] = 0x80 | ((c >> 12) & 63);
    out[2] = 0x80 | ((c >> 6) & 63);
    out[3] = 0x80 | (c & 63);
    return 4;
}
char *Utf8CaseMap(const char *s, int length, int upper, int firstOnly, int change)
{
    int p = 0, n = 0, step, valid;
    unsigned long c;
    char *out = NEditMalloc((size_t)length * 4 + 1);
    while (p < length) {
        step = Utf8Decode(s + p, length - p, &c, &valid);
        if (valid && change && (!firstOnly || p == 0)) {
#ifdef HAVE_UTF8PROC
            c = upper ? utf8proc_toupper(c) : utf8proc_tolower(c);
#else
            if (c < 256)
                c = upper ? toupper(c) : tolower(c);
#endif
            n += Utf8Encode(c, out + n);
        } else {
            memcpy(out + n, s + p, step);
            n += step;
        }
        p += step;
    }
    out[n] = 0;
    return out;
}
int Utf8ScalarCount(const char *s, int length)
{
    int p = 0, n = 0;
    while (p < length) {
        p += Utf8Decode(s + p, length - p, NULL, NULL);
        n++;
    }
    return n;
}
int Utf8ScalarOffset(const char *s, int length, int index)
{
    int p = 0;
    while (p < length && index-- > 0)
        p += Utf8Decode(s + p, length - p, NULL, NULL);
    return p;
}
int Utf8Previous(const char *s, int length, int position)
{
    int p, start, step;
    if (position > length)
        position = length;
    if (position <= 0)
        return 0;
    start = position > 4 ? position - 4 : 0;
    for (p = start; p < position;) {
        step = Utf8Decode(s + p, length - p, NULL, NULL);
        if (p + step >= position)
            return p;
        p += step;
    }
    return position - 1;
}
int Utf8IsDelimiter(unsigned long c, const char *delimiters)
{
    unsigned long d;
    int length;
#ifdef HAVE_UTF8PROC
    int k = utf8proc_category(c);
    if (!c || (c >= 9 && c <= 13) || c == 0x85 || k == UTF8PROC_CATEGORY_ZS ||
        k == UTF8PROC_CATEGORY_ZL || k == UTF8PROC_CATEGORY_ZP)
        return 1;
#else
    if (!c || (c < 256 && isspace(c)))
        return 1;
#endif
    if (delimiters) {
        length = strlen(delimiters);
        while (length > 0) {
            int n = Utf8Decode(delimiters, length, &d, NULL);
            if (c == d)
                return 1;
            delimiters += n;
            length -= n;
        }
        return 0;
    }
#ifdef HAVE_UTF8PROC
    return !(k >= UTF8PROC_CATEGORY_LU && k <= UTF8PROC_CATEGORY_NO) &&
           k != UTF8PROC_CATEGORY_PC;
#else
    return c < 256 && !isalnum(c) && c != '_';
#endif
}

int Utf8FoldScalar(unsigned long c, char out[16])
{
#ifdef HAVE_UTF8PROC
    utf8proc_int32_t folded[4];
    int i, n = 0,
           count = utf8proc_decompose_char(c, folded, 4, UTF8PROC_CASEFOLD, NULL);
    if (count < 0 || count > 4)
        return Utf8Encode(c, out);
    for (i = 0; i < count; i++)
        n += Utf8Encode(folded[i], out + n);
    return n;
#else
    return Utf8Encode(c < 256 ? tolower(c) : c, out);
#endif
}
char *Utf8CaseFold(const char *s, int length)
{
    char *out = NEditMalloc((size_t)length * 4 + 1);
    int p = 0, n = 0, step, valid;
    unsigned long c;
    while (p < length) {
        step = Utf8Decode(s + p, length - p, &c, &valid);
        if (valid)
            n += Utf8FoldScalar(c, out + n);
        else {
            memcpy(out + n, s + p, step);
            n += step;
        }
        p += step;
    }
    out[n] = 0;
    return out;
}
