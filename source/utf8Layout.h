/* A grapheme-based line map shared by drawing, hit testing, and wrapping.
   Offsets are bytes; columns use Unicode grapheme widths and tab/control expansion;
   advances are pixels supplied by the renderer, including grapheme shaping. */
#ifndef NEDIT_UTF8_LAYOUT_H_INCLUDED
#define NEDIT_UTF8_LAYOUT_H_INCLUDED

#include "textBuf.h"

typedef int (*utf8MeasureProc)(const char *text, int length, int byteStart,
        void *arg);

typedef struct {
    int byteStart, byteEnd;
    int columnStart, columnEnd;
    int xStart, xEnd;
    int renderLength;
    char *renderText;
} utf8LayoutCell;

typedef struct {
    int nCells, byteLength, columns, width;
    utf8LayoutCell *cells;
    char *renderStorage;
    int references;
} utf8LineLayout;

/* The input is a single display line, without a newline. Invalid bytes render
   as U+FFFD but byte offsets still refer to the unchanged input. Returns NULL
   for invalid arguments or layout sizes/advances exceeding the int API. */
utf8LineLayout *Utf8LayoutCreate(const char *text, int length, int tabDist,
        char nullSubsChar, utf8MeasureProc measure, void *arg);
void Utf8LayoutFree(utf8LineLayout *layout);
void Utf8LayoutRetain(utf8LineLayout *layout);
int Utf8LayoutByteToX(const utf8LineLayout *layout, int pos);
int Utf8LayoutXToByte(const utf8LineLayout *layout, int x, int nearestCursor);

#endif
