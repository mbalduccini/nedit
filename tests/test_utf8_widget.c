/* Exercise the real Motif text widget actions, not copies of their logic.
   Run on an isolated X display with Xft enabled. */
#include "text.h"
#include "textP.h"
#include "textDisp.h"
#include "nedit.h"
#include "calltips.h"
#include "undo.h"
#include "textDrag.h"
#include "utf8.h"
#include <hb.h>
#include <time.h>
#include <Xm/XmIm.h>
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
/* Force the documented XIM overflow contract once, while retaining real
   Motif lookup for every other key. This verifies complete long commits. */
static unsigned int shapedGlyphCount;
static int missingShapedGlyph;
void __real_hb_shape(hb_font_t *font, hb_buffer_t *buffer,
        const hb_feature_t *features, unsigned int count);
void __wrap_hb_shape(hb_font_t *font, hb_buffer_t *buffer,
        const hb_feature_t *features, unsigned int count)
{
    hb_glyph_info_t *glyphs;
    unsigned int i;
    __real_hb_shape(font, buffer, features, count);
    glyphs = hb_buffer_get_glyph_infos(buffer, &shapedGlyphCount);
    missingShapedGlyph = 0;
    for (i = 0; i < shapedGlyphCount; i++)
        if (!glyphs[i].codepoint) missingShapedGlyph = 1;
}

static int overflowCommit;
int __real_XmImMbLookupString(Widget w, XKeyPressedEvent *event, char *text,
        int capacity, KeySym *keysym, Status *status);
int __wrap_XmImMbLookupString(Widget w, XKeyPressedEvent *event, char *text,
        int capacity, KeySym *keysym, Status *status)
{
    int i;
    if (!overflowCommit)
        return __real_XmImMbLookupString(w, event, text, capacity, keysym, status);
    if (capacity < 60) { *status = XBufferOverflow; return 60; }
    for (i = 0; i < 20; i++) memcpy(text + i*3, "e\xcc\x81", 3);
    *status = XLookupChars;
    overflowCommit = 0;
    return 60;
}

static WindowInfo *testWindow;
WindowInfo *WidgetToWindow(Widget w) { (void)w; return testWindow; }
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
    if (buf->length != (int)strlen(expected) || strcmp(text, expected))
        fprintf(stderr, "Actual: [%s] (%d), expected: [%s] (%lu)\n", text, buf->length, expected, (unsigned long)strlen(expected));
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
    XRaiseWindow(XtDisplay(display->w), XtWindow(XtParent(display->w)));
    XSync(XtDisplay(display->w), False);
    while (XtAppPending(XtWidgetToApplicationContext(display->w)))
        XtAppProcessEvent(XtWidgetToApplicationContext(display->w), XtIMAll);
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
    assert(ink > 0 && ink < width * (display->ascent + display->descent));
}

static void screenshot(textDisp *display, const char *path)
{
    FILE *out = fopen(path, "wb");
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

static void requestWideIndent(Widget w, XtPointer arg, XtPointer data)
{
    smartIndentCBStruct *request = (smartIndentCBStruct *)data;
    (void)w;
    (void)arg;
    if (request->reason == NEWLINE_INDENT_NEEDED) request->indentRequest = 9;
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
    BufSetAll(buf, "");
    XtVaSetValues(display->w, textNsmartIndent, True, NULL);
    XtAddCallback(display->w, textNsmartIndentCallback, requestWideIndent, NULL);
    TextDSetInsertPosition(display, 0);
    TextInsertAtCursor(display->w,
            "\xc3\xa9\xc3\xa9 \xc3\xa9\xc3\xa9 \xc3\xa9\xc3\xa9", NULL, True, True);
    assertContents(buf, "\xc3\xa9\xc3\xa9\n\xc3\xa9\xc3\xa9\n\xc3\xa9\xc3\xa9");
    XtRemoveCallback(display->w, textNsmartIndentCallback, requestWideIndent, NULL);
    XtVaSetValues(display->w, textNsmartIndent, False, NULL);
    BufSetAll(buf, ""); TextDSetInsertPosition(display, 0);
    TextInsertAtCursor(display->w, "aa \xcc\x81 bb cc", NULL, True, True);
    assertContents(buf, "aa \xcc\x81\nbb\ncc");
    XtVaSetValues(display->w, textNautoWrap, False, textNwrapMargin, 0, NULL);

    BufSetAll(buf, "UTF-8 in classic NEdit\nLatin: caf\xc3\xa9   Greek: \xce\xb1\xce\xb2\xce\xb3   Cyrillic: \xd0\x96\xd0\xb4\nCJK: \xe4\xb8\xad\xe6\x96\x87   Supplementary: \xf0\x90\x90\x80\nCombining: e\xcc\x81 a\xcc\x88   Tabs:\t\xc3\xa9\tZ   Invalid bytes: \x80\xe2\x82");
    repaint(display);
    screenshot(display, "utf8-widget.ppm");
}

static void checkPhaseOne(textDisp *display, WindowInfo *window)
{
    textBuffer *buf = display->buffer;
    Widget text = display->w;
    TextWidget tw = (TextWidget)text;
    const char *cluster = "e\xcc\x81\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb";
    int p, next, x, y, nx, ny, i, type;
    int width = display->width, height = display->height;
    char *longLine;
    clock_t began;
    BufSetAll(buf, cluster);
    for (p = 0; p < buf->length; p = next) {
        next = BufNextGrapheme(buf, p);
        assert(TextDPositionToXY(display, p, &x, &y));
        assert(TextDPositionToXY(display, next, &nx, &ny));
        assert(TextDXYToPosition(display, nx - 1, ny) == next);
        assert(TextDXYToCharPos(display, (x + nx) / 2, y) == p);
        TextSetCursorPos(text, p + 1);
        assert(TextGetCursorPos(text) == p);
    }
    assert(Utf8FontWidth(display->utf8Font, "e\xcc\x81", 3, 0) ==
            Utf8FontWidth(display->utf8Font, "e", 1, 0));
    BufSetAll(buf, "eZ"); TextSetCursorPos(text, 1);
    TextDOverstrike(display, "\xcc\x81");
    assertContents(buf, "e\xcc\x81Z");
    assert(TextGetCursorPos(text) == 3);
    BufSetAll(buf, "\xcc\x81Z"); TextSetCursorPos(text, 0);
    TextInsertAtCursor(text, "e", NULL, True, True);
    assertContents(buf, "e\xcc\x81Z");
    assert(TextGetCursorPos(text) == 3);
    BufSetAll(buf, "A\xe4\xb8\xadZ"); TextSetCursorPos(text, 4);
    XtVaSetValues(text, textNoverstrike, True, NULL);
    action(text, "delete_previous_character");
    assertContents(buf, "A  Z");
    XtVaSetValues(text, textNoverstrike, False, NULL);
    BufSetAll(buf, cluster);
    BufAddModifyCB(buf, recordUndo, window);
    TextSetCursorPos(text, buf->length);
    action(text, "delete_previous_character");
    assertContents(buf, "e\xcc\x81");
    Undo(window); assertContents(buf, cluster);
    Redo(window); assertContents(buf, "e\xcc\x81");
    action(text, "delete_previous_character"); assertContents(buf, "");
    ClearUndoList(window); ClearRedoList(window);
    BufSetAll(buf, "Ae\xcc\x81\xe4\xb8\xadZ\nA\xe4\xb8\xad" "e\xcc\x81Z");
    ClearUndoList(window);
    BufRectSelect(buf, 0, buf->length, 1, 3);
    action(text, "delete_next_character");
    assertContents(buf, "A Z\nAe\xcc\x81Z");
    Undo(window);
    assertContents(buf, "Ae\xcc\x81\xe4\xb8\xadZ\nA\xe4\xb8\xad" "e\xcc\x81Z");
    ClearUndoList(window); ClearRedoList(window);
    BufRemoveModifyCB(buf, recordUndo, window);

    /* Copy/move/overlay and cancellation exercise actual drag state. */
    for (type = DRAG_COPY; type <= DRAG_OVERLAY_COPY; type++) {
        BufSetAll(buf, "Ae\xcc\x81Z\nB\xe4\xb8\xadQ");
        BufRectSelect(buf, 0, 5, 1, 2);
        assert(TextDPositionToXY(display, 1, &x, &y));
        tw->text.btnDownX = x; tw->text.btnDownY = y;
        BeginBlockDrag(tw);
        assert(TextDPositionToXY(display, 7, &nx, &ny));
        BlockDragSelection(tw, nx, ny, type);
        for (p = 0; p < buf->length;) {
            int valid;
            p += BufGetCodepoint(buf, p, NULL, &valid);
            assert(valid);
        }
        CancelBlockDrag(tw);
        assertContents(buf, "Ae\xcc\x81Z\nB\xe4\xb8\xadQ");
    }
    /* Actual Motif key lookup must commit UTF-8 bytes in a UTF-8 locale. */
    {
        int minimum, maximum, perCode;
        KeySym unicode = 0x00e9, *old;
        XEvent event, mapping;
        XDisplayKeycodes(XtDisplay(text), &minimum, &maximum);
        old = XGetKeyboardMapping(XtDisplay(text), maximum, 1, &perCode);
        XChangeKeyboardMapping(XtDisplay(text), maximum, 1, &unicode, 1);
        XSync(XtDisplay(text), False);
        memset(&mapping, 0, sizeof(mapping));
        mapping.xmapping.type = MappingNotify;
        mapping.xmapping.display = XtDisplay(text);
        mapping.xmapping.request = MappingKeyboard;
        mapping.xmapping.first_keycode = maximum; mapping.xmapping.count = 1;
        XRefreshKeyboardMapping(&mapping.xmapping);
        BufSetAll(buf, ""); TextSetCursorPos(text, 0);
        memset(&event, 0, sizeof(event));
        event.xkey.type = KeyPress; event.xkey.display = XtDisplay(text);
        event.xkey.window = XtWindow(text); event.xkey.keycode = maximum;
        XtCallActionProc(text, "self_insert", &event, NULL, 0);
        assertContents(buf, "\xc3\xa9");
        BufSetAll(buf, ""); TextSetCursorPos(text, 0);
        overflowCommit = 1;
        XtCallActionProc(text, "self_insert", &event, NULL, 0);
        assert(!overflowCommit && buf->length == 60);
        for (p = 0; p < 60; p += 3)
            assert(BufGetCharacter(buf, p) == 'e' && BufNextGrapheme(buf, p) == p + 3);
        XChangeKeyboardMapping(XtDisplay(text), maximum, perCode, old, 1);
        XRefreshKeyboardMapping(&mapping.xmapping);
        XFree(old);
    }
    /* Breaks never land inside combining or ZWJ sequences. */
    BufSetAll(buf, cluster);
    TextDSetWrapMode(display, True, 1);
    p = 0;
    while (p < buf->length) {
        next = TextDCountForwardNLines(display, p, 1, True);
        assert(next > p && BufStartOfGrapheme(buf, next) == next);
        p = next;
    }
    TextDSetWrapMode(display, False, 0);
    TextDResize(display, width, height);

    BufSetAll(buf, "aa \xcc\x81 bb cc");
    TextDSetWrapMode(display, True, 3);
    assert(TextDCountForwardNLines(display, 0, 1, True) == 6);
    TextDSetWrapMode(display, False, 0);

    /* Confirm reuse of long-line measurements after thousands of hit tests. */
    longLine = (char *)NEditMalloc(200001);
    for (i = 0; i < 100000; i++) memcpy(longLine + 2*i, "\xc3\xa9", 2);
    longLine[200000] = 0;
    BufSetAll(buf, longLine); NEditFree(longLine);
    TextDPositionToXY(display, 20, &x, &y);
    began = clock();
    for (i = 0; i < 1000; i++) {
        TextSetCursorPos(text, i % 10 * 2);
        assert(TextDPositionToXY(display, 20, &x, &y));
        assert(TextDXYToPosition(display, x, y) == 20);
    }
    printf("1000 cached hit tests on a 200 KB line: %.3f CPU seconds.\n",
            (double)(clock() - began)/CLOCKS_PER_SEC);
    assert((double)(clock() - began)/CLOCKS_PER_SEC < 5.0);
    BufSetEncoding(buf, BUF_ENCODING_8BIT); TextDRefreshEncoding(display);
    BufSetAll(buf, "\xc3\xa9"); TextSetCursorPos(text, 1);
    BufSetEncoding(buf, BUF_ENCODING_UTF8); TextDRefreshEncoding(display);
    assert(display->cursorPos == 0);
    assert(display->utf8Font != NULL);
}

/* Raster equality verifies that HarfBuzz positions/normalizes marks in drawing,
   in addition to width and hit-test consistency. Document bytes stay distinct. */
static XImage *clusterImage(textDisp *display, const char *text)
{
    BufSetAll(display->buffer, text);
    TextDSetInsertPosition(display, 0);
    repaint(display);
    checkPixels(display, display->left, 24);
    return XGetImage(XtDisplay(display->w), XtWindow(display->w), display->left,
            display->top, 64, display->ascent + display->descent, AllPlanes, ZPixmap);
}

static void checkPhaseThree(textDisp *display)
{
    textBuffer *buf = display->buffer;
    int x, y, nx, ny, p, next, i, j;
    int width = display->width, height = display->height;
    XImage *composed, *decomposed;
    const char *shaped[] = { "e\xcc\x81", "x\xcc\x81", "🇺🇸", "👩‍💻", "👍🏽", "1️⃣" };
    const unsigned int glyphCounts[] = {1, 2, 1, 1, 1, 1};
    const char *wrapped[] = { "中文。日本", "中（文）日", "a\xc2\xa0" "b c",
        "a\xe2\x81\xa0" "b c", "ab\xe2\x80\x8b" "cd", "ééééé", "🇺🇸👩‍💻中", "中A" };
    const int margins[] = {4, 4, 2, 1, 2, 2, 2, 1};
    const int breaks[] = {3, 3, 5, 6, 5, 4, 8, 3};
    const char *hard[] = {"中文。日本", "中（文）日", "a\xc2\xa0" "b c", "ab\xe2\x80\x8b" "cd"};
    const char *expected[] = {"中\n文。\n日本", "中\n（文）\n日", ("a\xc2\xa0" "b\nc"), "ab\xe2\x80\x8b\ncd"};
    const int hardMargins[] = {4, 4, 2, 2};
    TextDSetWrapMode(display, False, 0);
    TextDSetScroll(display, 1, 0);
    XtVaSetValues(display->w, textNutf8Font, "DejaVu Sans:size=14", NULL);
    composed = clusterImage(display, "é");
    decomposed = clusterImage(display, "e\xcc\x81");
    assert(composed && decomposed);
    for (j = 0; j < composed->height; j++) for (i = 0; i < composed->width; i++)
        assert(XGetPixel(composed, i, j) == XGetPixel(decomposed, i, j));
    XDestroyImage(composed); XDestroyImage(decomposed);
    for (i = 0; i < (int)(sizeof(shaped)/sizeof(*shaped)); i++) {
        utf8Font *probe = Utf8FontOpen(display->w, "DejaVu Sans:size=14");
        assert(probe);
        shapedGlyphCount = 0;
        Utf8FontWidth(probe, shaped[i], strlen(shaped[i]), 0);
        fprintf(stderr, "Shaping probe %s: %u glyphs, missing=%d\n", shaped[i],
                shapedGlyphCount, missingShapedGlyph);
        assert(shapedGlyphCount == glyphCounts[i] && !missingShapedGlyph);
        Utf8FontClose(probe);
        BufSetAll(buf, shaped[i]);
        assert(BufNextGrapheme(buf, 0) == buf->length);
        assert(TextDPositionToXY(display, 0, &x, &y));
        assert(TextDPositionToXY(display, buf->length, &nx, &ny));
        fprintf(stderr, "Shaped cluster %s: %d pixels\n", shaped[i], nx-x);
        assert(nx > x && nx-x < 64);
        assert(TextDXYToPosition(display, nx-1, ny) == buf->length);
        repaint(display); checkPixels(display, x, nx-x);
    }
    for (i = 0; i < 4; i++)
        assert(Utf8FontWidth(display->utf8Font, "e\xcc\x81", 3, i) ==
                Utf8FontWidth(display->utf8Font, "é", 2, i));
    {
        char marks[603];
        marks[0] = 'x';
        for (i = 0; i < 300; i++) memcpy(marks+1+2*i, "\xcc\x81", 2);
        marks[601] = 'Z'; marks[602] = 0;
        BufSetAll(buf, marks);
        assert(TextDPositionToXY(display, 601, &x, &y));
        assert(x-display->left == Utf8FontWidth(display->utf8Font, "x\xcc\x81", 3, 0));
        repaint(display);
    }
    BufSetAll(buf, "Shaping: é = é   x́ ä ô ñ\nEmoji: 👩‍💻 👨‍👩‍👧‍👦 👍🏽 🇺🇸 1️⃣\nPresentation: ☺︎ ☺️   ❤️\nCJK wrapping: 中文。日本（言語）");
    TextDSetInsertPosition(display, 0); TextDSetScroll(display, 1, 0);
    repaint(display); checkPixels(display, display->left, 64);
    screenshot(display, "utf8-shaping.ppm");
    XtVaSetValues(display->w, textNutf8Font, "monospace:size=12", NULL);
    for (i = 0; i < (int)(sizeof(wrapped)/sizeof(*wrapped)); i++) {
        BufSetAll(buf, wrapped[i]);
        TextDSetWrapMode(display, True, margins[i]);
        assert(TextDCountForwardNLines(display, 0, 1, True) == breaks[i]);
        p = 0;
        while (p < buf->length) {
            next = TextDCountForwardNLines(display, p, 1, True);
            assert(next > p && BufStartOfGrapheme(buf, next) == next);
            p = next;
        }
        BufInsert(buf, 0, "é");
        assert(display->nBufferLines == TextDCountLines(display, 0, buf->length, True));
        BufRemove(buf, 0, 2);
        assert(TextDCountForwardNLines(display, 0, 1, True) == breaks[i]);
        TextDSetWrapMode(display, False, 0);
    }
    /* Glued text remains reachable when it is wider than the viewport. */
    BufSetAll(buf, "aaaaaaaa\xc2\xa0" "bbbbbbbb");
    TextDResize(display, 16, height);
    TextDSetWrapMode(display, True, 0);
    assert(TextDCountLines(display, 0, buf->length, True) == 0);
    assert(XtIsManaged(display->hScrollBar));
    TextDSetScroll(display, 1, 24);
    assert(display->horizOffset > 0);
    TextDSetScroll(display, 1, 0);
    TextDSetWrapMode(display, False, 0);
    TextDResize(display, width, height);
    for (i = 0; i < 4; i++) {
        BufSetAll(buf, ""); TextDSetInsertPosition(display, 0);
        XtVaSetValues(display->w, textNautoWrap, True, textNwrapMargin, hardMargins[i], NULL);
        TextInsertAtCursor(display->w, (char *)hard[i], NULL, True, True);
        assertContents(buf, expected[i]);
    }
    BufSetAll(buf, ""); TextDSetInsertPosition(display, 0);
    XtVaSetValues(display->w, textNwrapMargin, 2, NULL);
    TextInsertAtCursor(display->w, "aa ", NULL, True, True);
    assertContents(buf, "aa\n");
    XtVaSetValues(display->w, textNautoWrap, False, textNwrapMargin, 0, NULL);
}

int main(int argc, char **argv)
{
    XtAppContext app;
    Widget shell, auxiliaries, text, hbar, vbar;
    textBuffer *buf;
    textDisp *display;
    WindowInfo window;
    const char sample[] = "A\xc3\xa9\xe4\xb8\xad\xf0\x90\x90\x80Z";
    const int boundaries[] = {0, 1, 3, 6, 10, 11};
    int i;

    XtSetLanguageProc(NULL, NULL, NULL);
    shell = XtVaAppInitialize(&app, "NEditUtf8Test", NULL, 0,
            &argc, argv, NULL, NULL);
    /* Scrollbar management must not replace the application shell's sole
       managed child, or pixel tests would read an unmapped text window. */
    auxiliaries = XtVaAppCreateShell("aux", "NEditUtf8Test",
            applicationShellWidgetClass, XtDisplay(shell), NULL);
    hbar = XtVaCreateWidget("hbar", xmScrollBarWidgetClass, auxiliaries, NULL);
    vbar = XtVaCreateWidget("vbar", xmScrollBarWidgetClass, auxiliaries, NULL);
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
        Widget twinH = XtVaCreateWidget("hbar", xmScrollBarWidgetClass, auxiliaries, NULL);
        Widget twinV = XtVaCreateWidget("vbar", xmScrollBarWidgetClass, auxiliaries, NULL);
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
    testWindow = &window;
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
    assertContents(buf, "A\xce\xb2\xce\xb3 \xf0\x90\x90\x80Z");
    assert(TextGetCursorPos(text) == 5);
    XtVaSetValues(text, textNoverstrike, True, NULL);
    action(text, "delete_previous_character");
    assertContents(buf, "A\xce\xb2  \xf0\x90\x90\x80Z");
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
    checkPhaseOne(display, &window);
    checkPhaseThree(display);

    BufSetEncoding(buf, BUF_ENCODING_8BIT);
    TextDSetFont(display, display->fontStruct);
    BufSetAll(buf, sample);
    TextSetCursorPos(text, 1);
    action(text, "forward_character");
    assert(TextGetCursorPos(text) == 2);
    action(text, "delete_next_character");
    assertContents(buf, "A\xc3\xe4\xb8\xad\xf0\x90\x90\x80Z");
    BufSetAll(buf, "A\001Z"); TextSetCursorPos(text, 2);
    XtVaSetValues(text, textNoverstrike, True, NULL);
    action(text, "delete_previous_character");
    assertContents(buf, "A Z");

    XtDestroyWidget(shell);
    XtDestroyWidget(auxiliaries);
    XtDestroyApplicationContext(app);
    puts("Motif UTF-8 drawing, layout, wrapping, editing, undo/redo, and legacy mode passed.");
    return 0;
}
