/* Copyright 2026. Distributed under the same GPL terms as NEdit. */
#include "utf8Break.h"
#include "utf8.h"
#include "../util/nedit_malloc.h"
#ifdef HAVE_ICU
#include <unicode/ubrk.h>
#include <unicode/utext.h>
#include <unicode/uchar.h>
#endif

unsigned char *Utf8LineBreaks(const char *text, int length)
{
    unsigned char *flags;
    int p = 0, next;
#ifdef HAVE_ICU
    int previous = 0, segment = 0, glue = 0, i;
#endif
    unsigned long scalar;
    if (!text || length < 0) return NULL;
    flags = NEditCalloc((size_t)length + 1, 1);
#ifdef HAVE_ICU
    UErrorCode status = U_ZERO_ERROR;
    UText subject = UTEXT_INITIALIZER;
    UBreakIterator *iterator = ubrk_open(UBRK_LINE, "root", NULL, 0, &status);
    utext_openUTF8(&subject, text, length, &status);
    if (U_SUCCESS(status)) ubrk_setUText(iterator, &subject, &status);
    if (U_SUCCESS(status)) {
        for (i = ubrk_first(iterator); i != UBRK_DONE; i = ubrk_next(iterator))
            if (i >= 0 && i <= length) flags[i] = UTF8_BREAK_ALLOWED;
    }
    ubrk_close(iterator);
    utext_close(&subject);
#endif
    flags[length] |= UTF8_BREAK_ALLOWED;
    while (p < length) {
        next = p + Utf8Decode(text + p, length - p, &scalar, NULL);
#ifdef HAVE_ICU
        int property = u_getIntPropertyValue(scalar, UCHAR_LINE_BREAK);
        if (property == U_LB_GLUE || property == U_LB_WORD_JOINER) glue = 1;
        if (p && property != U_LB_CLOSE_PUNCTUATION &&
                property != U_LB_CLOSE_PARENTHESIS && property != U_LB_EXCLAMATION &&
                property != U_LB_INFIX_NUMERIC && property != U_LB_BREAK_SYMBOLS &&
                property != U_LB_QUOTATION && previous != U_LB_OPEN_PUNCTUATION &&
                previous != U_LB_QUOTATION)
            flags[p] |= UTF8_BREAK_EMERGENCY;
        if (property != U_LB_COMBINING_MARK && property != U_LB_ZWJ)
            previous = property;
#else
        if (scalar == ' ' || scalar == '\t' || scalar == '\n')
            flags[next] |= UTF8_BREAK_ALLOWED;
        if (p) flags[p] |= UTF8_BREAK_EMERGENCY;
#endif
#ifdef HAVE_ICU
        if (flags[next] & UTF8_BREAK_ALLOWED) {
            /* Keep an unbreakable run joined by NBSP/WJ intact even when it
               exceeds the viewport. Ordinary long words retain emergency wrap. */
            if (glue) for (i = segment; i < next; i++)
                flags[i] &= ~UTF8_BREAK_EMERGENCY;
            segment = next;
            glue = 0;
        }
#endif
        p = next;
    }
    return flags;
}
