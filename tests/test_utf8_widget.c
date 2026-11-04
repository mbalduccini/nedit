/* Exercise the real Motif text widget actions, not copies of their logic.
   Run on an isolated X display with Xft enabled. */
#include "text.h"
#include "textP.h"
#include "textDisp.h"
#include "nedit.h"
#include "calltips.h"
#include "undo.h"
#include "highlight.h"
#include "../util/nedit_malloc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <X11/Xutil.h>
#include <X11/Shell.h>
#include <Xm/ScrollBar.h>

/* Only application integration outside the standalone widget is stubbed.
   Widget, buffer, selection, display, and undo implementations are real. */
WindowInfo *WidgetToWindow(Widget w) { (void)w; return NULL; }
void TextDKillCalltip(textDisp *textD, int id) { (void)textD; (void)id; }
void TextDRedrawCalltip(textDisp *textD, int callID)
    { (void)textD; (void)callID; }
Pixel AllocColor(Widget w, const char *name, int *r, int *g, int *b)
{
    XColor color;
    Colormap map = DefaultColormapOfScreen(XtScreen(w));
    assert(XParseColor(XtDisplay(w), map, name, &color));
    assert(XAllocColor(XtDisplay(w), map, &color));
    *r = color.red; *g = color.green; *b = color.blue;
    return color.pixel;
}
Boolean GetPrefUndoModifiesSelection(void) { return False; }
void MakeSelectionVisible(WindowInfo *window, Widget w)
    { (void)window; (void)w; }
void RemoveBackupFile(WindowInfo *window) { (void)window; }
void SetSensitive(WindowInfo *window, Widget item, int state)
    { (void)window; (void)item; (void)state; }
void SetBGMenuUndoSensitivity(WindowInfo *window, int state)
    { (void)window; (void)state; }
void SetBGMenuRedoSensitivity(WindowInfo *window, int state)
    { (void)window; (void)state; }
void SetWindowModified(WindowInfo *window, int state)
    { window->fileChanged = state; }

static void recordUndo(int pos, int inserted, int deleted, int restyled,
        const char *oldText, void *arg)
{
    WindowInfo *window = (WindowInfo *)arg;
    (void)restyled;
    if (inserted || deleted) {
        SaveUndoInformation(window, pos, inserted, deleted, oldText);
        window->fileChanged = True;
    }
}

static void assertContents(textBuffer *buf, const char *expected)
{
    char *text = BufGetAll(buf);
    assert(buf->length == (int)strlen(expected));
    assert(strcmp(text, expected) == 0);
    NEditFree(text);
}

static void action(Widget text, const char *name)
{
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.xkey.type = KeyPress;
    event.xkey.display = XtDisplay(text);
    event.xkey.window = XtWindow(text);
    event.xkey.time = CurrentTime;
    XtCallActionProc(text, name, &event, NULL, 0);
}

static void repaint(textDisp *display)
{
    TextDBlankCursor(display);
    TextDRedisplayRect(display, display->left, display->top,
            display->width, display->height);
    XSync(XtDisplay(display->w), False);
}

static void checkPixels(textDisp *display, int x, int width)
{
    int dx, dy, ink = 0;
    XImage *image = XGetImage(XtDisplay(display->w), XtWindow(display->w),
            x, display->top, width, display->ascent + display->descent,
            AllPlanes, ZPixmap);
    assert(image);
    for (dy = 0; dy < image->height; dy++)
        for (dx = 0; dx < image->width; dx++)
            if (XGetPixel(image, dx, dy) != display->bgPixel) ink++;
    XDestroyImage(image);
    assert(ink > 0);
}

static void screenshot(textDisp *display)
{
    FILE *out = fopen("utf8-widget.ppm", "wb");
    XImage *image;
    int x, y;
    unsigned long pixel;
    assert(out);
    image = XGetImage(XtDisplay(display->w), XtWindow(display->w),
            0, 0, display->left + display->width,
            display->top + display->height, AllPlanes, ZPixmap);
    assert(image);
    fprintf(out, "P6\n%d %d\n255\n", image->width, image->height);
    for (y = 0; y < image->height; y++)
        for (x = 0; x < image->width; x++) {
            assert(image->red_mask == 0xff0000 && image->green_mask == 0xff00 &&
                    image->blue_mask == 0xff);
            pixel = XGetPixel(image, x, y);
            fputc((pixel >> 16) & 255, out); fputc((pixel >> 8) & 255, out);
            fputc(pixel & 255, out);
        }
    fclose(out);
    XDestroyImage(image);
}

static void finishStyle(textDisp *display, int pos, void *arg)
{
    textBuffer *styleBuf = (textBuffer *)arg;
    assert(display->styleBuffer == styleBuf);
    assert(BufStartOfCharacter(display->buffer, pos) == pos);
    BufReplace(styleBuf, pos, pos+1, "B");
}

static void checkLayout(textDisp *display)
{
    int p, next, x, y, nx, ny, i, start, end, count;
    int width = display->width, height = display->height;
    int scalarWidth;
    textBuffer *buf = display->buffer;
    styleTableEntry styles[2];
    textBuffer *styleBuf;
    char styleCodes[64];
    const char mixed[] = "A\xc3\xa9\xce\xb2\xd0\x96\xe4\xb8\xad\xf0\x90\x90\x80Z";
    assert(display->utf8Font != NULL);
    assert(Utf8FontHasGlyph(display->utf8Font, 0x4e2d, 0));
    assert(Utf8FontHasGlyph(display->utf8Font, 0x10400, 0));
    BufSetAll(buf, mixed);
    repaint(display);
    for (p = 0; p < buf->length; p = next) {
        next = BufNextCharacter(buf, p);
        assert(TextDPositionToXY(display, p, &x, &y));
        assert(TextDPositionToXY(display, next, &nx, &ny));
        assert(nx > x && ny == y);
        assert(TextDXYToPosition(display, x, y) == p);
        assert(TextDXYToPosition(display, nx - 1, y) == next);
        assert(TextDXYToCharPos(display, (x + nx)/2, y) == p);
        checkPixels(display, x, nx - x);
    }
    /* A proportional font proves pixel mapping is not a byte/column shortcut. */
    XtVaSetValues(display->w, textNutf8Font, "DejaVu Sans:size=14", NULL);
    BufSetAll(buf, "WWW\xc3\xa9\n\xc3\xa9iiiiiiiiiiiiiii");
    TextDSetInsertPosition(display, 3);
    assert(TextDPositionToXY(display, 3, &x, &y));
    TextDMoveDown(display, False);
    assert(TextDPositionToXY(display, display->cursorPos, &nx, &ny));
    assert(abs(nx - x) < Utf8FontWidth(display->utf8Font, "i", 1, 0));
    TextDMoveUp(display, False);
    assert(display->cursorPos == 3);
    XtVaSetValues(display->w, textNutf8Font, "monospace:size=12", NULL);

    /* Highlight boundaries deliberately land inside a multibyte scalar. */
    BufSetAll(buf, mixed);
    memset(styles, 0, sizeof(styles));
    for (i = 0; i < 2; i++) {
        styles[i].font = display->fontStruct;
        styles[i].color = display->fgPixel;
    }
    styles[1].isBold = styles[1].isItalic = True;
    styles[1].underline = True;
    memset(styleCodes, 'A', buf->length);
    styleCodes[1] = 'B'; styleCodes[2] = 'A';
    styleCodes[buf->length] = 0;
    styleBuf = BufCreate(); BufSetAll(styleBuf, styleCodes);
    BufReplace(styleBuf, 1, 2, "?");
    TextDAttachHighlightData(display, styleBuf, styles, 2, '?', finishStyle, styleBuf);
    assert(TextDPositionToXY(display, 1, &x, &y));
    assert(TextDPositionToXY(display, 3, &nx, &ny));
    assert(nx - x == Utf8FontWidth(display->utf8Font, "\xc3\xa9", 2, 3));
    BufSelect(buf, 1, 3);
    repaint(display);
    {
        XImage *image = XGetImage(XtDisplay(display->w), XtWindow(display->w),
                x, display->top, 1, 1, AllPlanes, ZPixmap);
        assert(XGetPixel(image, 0, 0) == display->selectBGPixel);
        XDestroyImage(image);
    }
    BufUnselect(buf);
    TextDAttachHighlightData(display, NULL, NULL, 0, 0, NULL, NULL);
    BufFree(styleBuf);

    /* Hard soft-wraps, whitespace, newlines, and oversize characters. */
    scalarWidth = Utf8FontWidth(display->utf8Font, "\xc3\xa9", 2, 0);
    for (i = 0; i < 3; i++) {
        BufSetAll(buf, i == 0 ? "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9" :
                i == 1 ? "a \xc3\xa9\xc3\xa9 b\n\xf0\x90\x90\x80\tZ" :
                "\xc3\xa9\xc3\xa9\n\xc3\xa9\n");
        TextDSetWrapMode(display, True, 0);
        TextDResize(display, i == 2 ? 1 : scalarWidth * 2, height);
        start = 0; count = 0;
        while (start < buf->length) {
            end = TextDEndOfLine(display, start, True);
            next = TextDCountForwardNLines(display, start, 1, True);
            assert(end >= start && next > start);
            assert(BufStartOfCharacter(buf, end) == end);
            assert(BufStartOfCharacter(buf, next) == next);
            if (i == 0) assert(end-start <= 4);
            start = next;
            assert(++count < 30);
        }
        for (p = 0; p <= buf->length; p = BufNextCharacter(buf, p)) {
            assert(TextDStartOfLine(display, p) <= p);
            assert(BufStartOfCharacter(buf, TextDStartOfLine(display, p)) ==
                    TextDStartOfLine(display, p));
            if (p == buf->length) break;
        }
        /* Force the incremental pre-delete/wrap bookkeeping path. */
        TextDSetInsertPosition(display, 2);
        TextDInsert(display, "\xce\xb2");
        TextDSetInsertPosition(display, 2);
        action(display->w, "delete_next_character");
        assert(display->nBufferLines == TextDCountLines(display, 0, buf->length, True));
        TextDSetWrapMode(display, False, 0);
        TextDResize(display, width, height);
    }
    BufSetAll(buf, "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9");
    TextDSetWrapMode(display, True, 2);
    assert(TextDEndOfLine(display, 0, True) == 4);
    assert(TextDCountForwardNLines(display, 0, 1, True) == 4);
    TextDSetWrapMode(display, False, 0);

    /* Long lines and horizontal scrolling use pixel coordinates consistently. */
    {
        char *longLine = (char *)NEditMalloc(6001);
        for (i = 0; i < 3000; i++) memcpy(longLine + 2*i, "\xc3\xa9", 2);
        longLine[6000] = 0;
        BufSetAll(buf, longLine);
        NEditFree(longLine);
        TextDSetScroll(display, 1, scalarWidth * 2000);
        assert(TextDPositionToXY(display, 4000, &x, &y));
        assert(TextDXYToPosition(display, x, y) == 4000);
        repaint(display);
        TextDSetScroll(display, 1, 0);
    }
    BufSetAll(buf, "\xc3\xa9\tZ");
    BufSetTabDistance(buf, 100);
    assert(TextDPositionToXY(display, 3, &x, &y));
    assert(x - display->left == scalarWidth * 100);
    BufSetTabDistance(buf, 8);

    /* Word editing respects scalar delimiters and never removes partial bytes. */
    BufSetAll(buf, "\xc3\xa9\xe4\xb8\xad \xce\xb1\xce\xb2");
    TextDSetInsertPosition(display, buf->length);
    action(display->w, "backward_word");
    assert(display->cursorPos == 6);
    action(display->w, "delete_previous_word");
    assertContents(buf, "\xce\xb1\xce\xb2");
    TextDSetInsertPosition(display, 0);
    action(display->w, "delete_next_word");
    assertContents(buf, "");
    XtVaSetValues(display->w, textNwordDelimiters, "\xce\xb2", NULL);
    BufSetAll(buf, "\xc3\xa9\xce\xb2\xe4\xb8\xad");
    TextDSetInsertPosition(display, 0);
    action(display->w, "delete_next_word");
    assertContents(buf, "\xce\xb2\xe4\xb8\xad");
    XtVaSetValues(display->w, textNwordDelimiters, ".,;", NULL);

    /* Hard auto-wrap inserts newlines only at ASCII whitespace. */
    BufSetAll(buf, "");
    XtVaSetValues(display->w, textNautoWrap, True, textNwrapMargin, 3, NULL);
    TextDSetInsertPosition(display, 0);
    TextInsertAtCursor(display->w, "\xc3\xa9\xc3\xa9 \xc3\xa9\xc3\xa9", NULL, True, True);
    assertContents(buf, "\xc3\xa9\xc3\xa9\n\xc3\xa9\xc3\xa9");
    XtVaSetValues(display->w, textNautoWrap, False, textNwrapMargin, 0, NULL);

    BufSetAll(buf, "UTF-8 in classic NEdit\nLatin: caf\xc3\xa9   Greek: \xce\xb1\xce\xb2\xce\xb3   Cyrillic: \xd0\x96\xd0\xb4\nCJK: \xe4\xb8\xad\xe6\x96\x87   Supplementary: \xf0\x90\x90\x80\nTabs:\t\xc3\xa9\tZ   Invalid bytes: \x80\xe2\x82");
    repaint(display);
    screenshot(display);
}

int main(int argc, char **argv)
{
    XtAppContext app;
    Widget shell, text, hbar, vbar;
    textBuffer *buf;
    textDisp *display;
    WindowInfo window;
    const char sample[] = "A\xc3\xa9\xe4\xb8\xad\xf0\x90\x90\x80Z";
    const int boundaries[] = {0, 1, 3, 6, 10, 11};
    int i;

    shell = XtVaAppInitialize(&app, "NEditUtf8Test", NULL, 0,
            &argc, argv, NULL, NULL);
    /* Unmanaged auxiliary shells avoid requiring the application UI. */
    hbar = XtVaCreateWidget("hbar", xmScrollBarWidgetClass, shell, NULL);
    vbar = XtVaCreateWidget("vbar", xmScrollBarWidgetClass, shell, NULL);
    text = XtVaCreateManagedWidget("text", textWidgetClass, shell,
            textNhScrollBar, hbar, textNvScrollBar, vbar,
            textNautoShowInsertPos, False, textNemulateTabs, 0,
            textNautoWrap, False, textNautoIndent, False,
            textNblinkRate, 0, NULL);
    XtRealizeWidget(shell);
    buf = BufCreate();
    BufSetEncoding(buf, BUF_ENCODING_UTF8);
    BufSetAll(buf, sample);
    TextSetBuffer(text, buf);
    display = ((TextWidget)text)->text.textD;
    {
        Widget twinShell = XtVaAppCreateShell("twin", "NEditUtf8Test",
                applicationShellWidgetClass, XtDisplay(shell), NULL);
        Widget twinH = XtVaCreateWidget("hbar", xmScrollBarWidgetClass, twinShell, NULL);
        Widget twinV = XtVaCreateWidget("vbar", xmScrollBarWidgetClass, twinShell, NULL);
        Widget twin = XtVaCreateManagedWidget("text", textWidgetClass, twinShell,
                textNhScrollBar, twinH, textNvScrollBar, twinV,
                textNautoShowInsertPos, False, textNblinkRate, 0, NULL);
        TextSetBuffer(twin, buf);
        assert(TextGetBuffer(twin) == buf);
        assert(((TextWidget)twin)->text.textD->utf8Font != NULL);
        TextSetCursorPos(twin, 8);
        assert(TextGetCursorPos(twin) == 6);
        XtDestroyWidget(twinShell);
        assert(TextGetBuffer(text) == buf);
    }
    for (i = 1; i < 6; i++) {
        action(text, "forward_character");
        assert(TextGetCursorPos(text) == boundaries[i]);
    }
    for (i = 4; i >= 0; i--) {
        action(text, "backward_character");
        assert(TextGetCursorPos(text) == boundaries[i]);
    }
    TextSetCursorPos(text, 8);
    assert(TextGetCursorPos(text) == 6);

    memset(&window, 0, sizeof(window));
    window.buffer = buf;
    window.lastFocus = text;
    BufAddModifyCB(buf, recordUndo, &window);
    action(text, "delete_previous_character");
    assertContents(buf, "A\xc3\xa9\xf0\x90\x90\x80Z");
    assert(TextGetCursorPos(text) == 3);
    Undo(&window);
    assertContents(buf, sample);
    Redo(&window);
    assertContents(buf, "A\xc3\xa9\xf0\x90\x90\x80Z");
    action(text, "delete_next_character");
    assertContents(buf, "A\xc3\xa9Z");
    Undo(&window);
    assertContents(buf, "A\xc3\xa9\xf0\x90\x90\x80Z");
    ClearUndoList(&window);
    ClearRedoList(&window);
    BufRemoveModifyCB(buf, recordUndo, &window);

    BufSetAll(buf, sample);
    TextSetCursorPos(text, 1);
    TextDOverstrike(display, "\xce\xb2\xce\xb3");
    assertContents(buf, "A\xce\xb2\xce\xb3\xf0\x90\x90\x80Z");
    assert(TextGetCursorPos(text) == 5);
    XtVaSetValues(text, textNoverstrike, True, NULL);
    action(text, "delete_previous_character");
    assertContents(buf, "A\xce\xb2 \xf0\x90\x90\x80Z");
    XtVaSetValues(text, textNoverstrike, False, NULL);

    BufSetAll(buf, "\xc3\xa9\xe4\xb8\xad\nabc");
    TextSetCursorPos(text, 2);
    action(text, "process_down");
    assert(TextGetCursorPos(text) == 7);
    action(text, "process_up");
    assert(TextGetCursorPos(text) == 2);

    BufSetAll(buf, "\x80\xe2\x82");
    TextSetCursorPos(text, 3);
    action(text, "delete_previous_character");
    assertContents(buf, "\x80\xe2");
    action(text, "delete_previous_character");
    assertContents(buf, "\x80");

    checkLayout(display);

    BufSetEncoding(buf, BUF_ENCODING_8BIT);
    TextDSetFont(display, display->fontStruct);
    BufSetAll(buf, sample);
    TextSetCursorPos(text, 1);
    action(text, "forward_character");
    assert(TextGetCursorPos(text) == 2);
    action(text, "delete_next_character");
    assertContents(buf, "A\xc3\xe4\xb8\xad\xf0\x90\x90\x80Z");

    XtDestroyWidget(shell);
    XtDestroyApplicationContext(app);
    puts("Motif UTF-8 drawing, layout, wrapping, editing, undo/redo, and legacy mode passed.");
    return 0;
}
