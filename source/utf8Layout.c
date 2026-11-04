/* Copyright 2026. Distributed under the same GPL terms as NEdit. */
#include "utf8Layout.h"
#include "utf8.h"
#include "../util/nedit_malloc.h"

#include <limits.h>
#include <string.h>

utf8LineLayout *Utf8LayoutCreate(const char *text, int length, int tabDist,
        char nullSubsChar, utf8MeasureProc measure, void *arg)
{
    utf8LineLayout *layout;
    utf8LayoutCell *cell;
    int pos, consumed, valid, columns, advance;
    size_t renderCapacity = 1;
    int i;
    char *render;
    int capacity = length < 128 ? length : 128;
    unsigned long scalar;

    if (length < 0 || (length > 0 && text == NULL) || measure == NULL ||
            tabDist < 1 ||
            (size_t)length > (size_t)-1 / sizeof(utf8LayoutCell) ||
            (size_t)length > ((size_t)-1 - 1) / (MAX_EXP_CHAR_LEN + 3)) return NULL;
    layout = (utf8LineLayout *)NEditMalloc(sizeof(utf8LineLayout));
    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        renderCapacity += c < 32 || c == 127 || c == (unsigned char)nullSubsChar
                ? MAX_EXP_CHAR_LEN : c >= 128 ? 3 : 1;
    }
    layout->nCells = layout->byteLength = layout->columns = layout->width = 0;
    layout->references = 1;
    layout->renderStorage = render = (char *)NEditMalloc(renderCapacity);
    layout->cells = length == 0 ? NULL : (utf8LayoutCell *)
            NEditMalloc((size_t)capacity * sizeof(utf8LayoutCell));

    for (pos = 0; pos < length; pos += consumed) {
        Utf8Decode(text + pos, length - pos, &scalar, &valid);
        consumed = Utf8NextGrapheme(text + pos, length - pos, &columns);
        if (scalar == '\n') {
            Utf8LayoutFree(layout);
            return NULL;
        }
        if (layout->nCells == capacity) {
            capacity = capacity > length/2 ? length : capacity * 2;
            layout->cells = (utf8LayoutCell *)NEditRealloc(layout->cells,
                    (size_t)capacity * sizeof(utf8LayoutCell));
        }
        cell = &layout->cells[layout->nCells];
        cell->renderText = render;
        cell->byteStart = pos;
        cell->byteEnd = pos + consumed;
        cell->columnStart = layout->columns;
        cell->xStart = layout->width;
        if (scalar == '\t') {
            cell->renderLength = 1;
            cell->renderText[0] = ' ';
            columns = tabDist - (layout->columns % tabDist);
        } else if (consumed == 1 && valid) {
            cell->renderLength = BufExpandCharacter(text[pos], layout->columns,
                    cell->renderText, tabDist, nullSubsChar);
            columns = cell->renderLength;
        } else {
            cell->renderLength = valid ? consumed : 3;
            memcpy(cell->renderText, valid ? text + pos : "\xef\xbf\xbd",
                    cell->renderLength);
            if (!valid) columns = 1;
        }
        advance = measure(cell->renderText, cell->renderLength, pos, arg);
        if (scalar == '\t') {
            if (advance < 0 || (advance > 0 && columns > INT_MAX / advance)) {
                Utf8LayoutFree(layout);
                return NULL;
            }
            advance *= columns;
        }
        if (advance < 0 || advance > INT_MAX - layout->width ||
                columns > INT_MAX - layout->columns) {
            Utf8LayoutFree(layout);
            return NULL;
        }
        layout->columns += columns;
        layout->width += advance;
        cell->columnEnd = layout->columns;
        cell->xEnd = layout->width;
        layout->nCells++;
        render += cell->renderLength;
    }
    layout->byteLength = length;
    return layout;
}

void Utf8LayoutFree(utf8LineLayout *layout)
{
    if (layout == NULL || --layout->references) return;
    NEditFree(layout->renderStorage);
    NEditFree(layout->cells);
    NEditFree(layout);
}

int Utf8LayoutByteToX(const utf8LineLayout *layout, int pos)
{
    int low = 0, high = layout->nCells, mid;
    if (pos <= 0) return 0;
    if (pos >= layout->byteLength) return layout->width;
    while (low < high) {
        mid = low + (high - low) / 2;
        if (layout->cells[mid].byteEnd <= pos) low = mid + 1;
        else high = mid;
    }
    return layout->cells[low].xStart;
}

int Utf8LayoutXToByte(const utf8LineLayout *layout, int x, int nearestCursor)
{
    int low = 0, high = layout->nCells, mid, threshold;
    const utf8LayoutCell *cell;
    if (x < 0) return 0;
    while (low < high) {
        mid = low + (high - low) / 2;
        cell = &layout->cells[mid];
        threshold = nearestCursor
                ? cell->xStart + (cell->xEnd - cell->xStart) / 2 : cell->xEnd;
        if (x >= threshold) low = mid + 1;
        else high = mid;
    }
    return low == layout->nCells ? layout->byteLength
            : layout->cells[low].byteStart;
}

void Utf8LayoutRetain(utf8LineLayout *layout)
{
    if (layout) layout->references++;
}
