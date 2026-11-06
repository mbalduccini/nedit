/* Copyright 2026. Distributed under the same GPL terms as NEdit. */
#ifndef NEDIT_UTF8_FONT_H_INCLUDED
#define NEDIT_UTF8_FONT_H_INCLUDED
#include <X11/Intrinsic.h>

typedef struct _utf8Font utf8Font;
utf8Font *Utf8FontOpen(Widget w, const char *name);
void Utf8FontClose(utf8Font *font);
int Utf8FontHasGlyph(utf8Font *font, unsigned long scalar, int style);
int Utf8FontAscent(utf8Font *font);
int Utf8FontDescent(utf8Font *font);
/* Measure/draw one grapheme (or an expanded ASCII control label) at a time.
   HarfBuzz shapes within this cell; style is normal/bold/italic/bold-italic 0..3. */
int Utf8FontWidth(utf8Font *font, const char *text, int length, int style);
void Utf8FontDraw(utf8Font *font, Pixel color, int x, int baseline,
        const char *text, int length, int style,
        int clipX, int clipY, int clipWidth, int clipHeight);
#endif
