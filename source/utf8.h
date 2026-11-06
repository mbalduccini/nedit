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

int Utf8FoldScalar(unsigned long scalar, char out[16]);
char *Utf8CaseFold(const char *, int length);
int Utf8Encode(unsigned long scalar, char out[4]);
/* Simple Unicode case mapping, without normalization; caller frees result. */
char *Utf8CaseMap(const char *, int length, int upper, int firstOnly, int change);
int Utf8ScalarCount(const char *, int length);
int Utf8ScalarOffset(const char *, int length, int index);
int Utf8Previous(const char *, int length, int position);
int Utf8IsDelimiter(unsigned long scalar, const char *delimiters);
#endif
