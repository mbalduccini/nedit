/* Strict, bounded UTF-8 decoding. Buffer positions remain byte offsets. */
#ifndef NEDIT_UTF8_H_INCLUDED
#define NEDIT_UTF8_H_INCLUDED

/* Returns bytes consumed (0 at end of input). Invalid input consumes one byte
   and produces U+FFFD, with *valid == 0. Output arguments may be NULL. */
int Utf8Decode(const char *text, int available, unsigned long *scalar,
        int *valid);

#endif
