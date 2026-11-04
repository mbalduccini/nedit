/* Strict, bounded UTF-8 decoding. Buffer positions remain byte offsets. */
#ifndef NEDIT_UTF8_H_INCLUDED
#define NEDIT_UTF8_H_INCLUDED

/* Returns bytes consumed (0 at end of input). Invalid input consumes one byte
   and produces U+FFFD, with *valid == 0. Output arguments may be NULL. */
int Utf8Decode(const char *text, int available, unsigned long *scalar,
        int *valid);
/* Extended grapheme traversal and terminal-style columns from utf8proc.
   Ambiguous characters occupy one column. Invalid bytes are separate units. */
int Utf8NextGrapheme(const char *text, int available, int *columns);
int Utf8NextGraphemeZ(const char *text, int *columns);
int Utf8IsIgnorable(unsigned long scalar);
int Utf8CharWidth(unsigned long scalar);

#endif
