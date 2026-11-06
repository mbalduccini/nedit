/* Copyright 2026. Distributed under the same GPL terms as NEdit. */
#ifndef NEDIT_UTF8_BREAK_H
#define NEDIT_UTF8_BREAK_H
#define UTF8_BREAK_ALLOWED 1
#define UTF8_BREAK_EMERGENCY 2
/* Byte-indexed UAX #14 opportunities. Caller frees length+1 flags. Emergency
   breaks preserve glue/joiners and punctuation; callers also require grapheme
   boundaries. No normalization or file-byte changes are performed.
   NULL text or negative length returns NULL; empty text returns one flag. */
unsigned char *Utf8LineBreaks(const char *text, int length);
#endif
