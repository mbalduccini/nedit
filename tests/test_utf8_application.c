#include "nedit.h"
#include "text.h"
#include "textP.h"
#include "window.h"
#include "preferences.h"
#include "file.h"
#include "textDrag.h"
#include "undo.h"
#include "search.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <Xm/SelectioB.h>
#include <Xm/ToggleB.h>
#include <Xm/Text.h>
#include <Xm/TextP.h>
#include <X11/Xutil.h>

static Time serverTime(Widget w)
{
    Display *display = XtDisplay(w);
    Window window = XCreateSimpleWindow(display, DefaultRootWindow(display),
            0, 0, 1, 1, 0, 0, 0);
    Atom property = XInternAtom(display, "UTF8_TEST_TIME", False);
    XEvent event;
    XSelectInput(display, window, PropertyChangeMask);
    XChangeProperty(display, window, property, XA_STRING, 8, PropModeReplace,
            (unsigned char *)"", 0);
    XWindowEvent(display, window, PropertyChangeMask, &event);
    XDestroyWindow(display, window);
    return event.xproperty.time;
}

static char *fieldFontName(Widget field)
{
    XmRenderTable table;
    XmRendition rendition;
    XmStringTag *tags;
    Arg arg;
    char *name, *copy;
    int i, count;
    XtVaGetValues(field, XmNrenderTable, &table, NULL);
    count = XmRenderTableGetTags(table, &tags);
    assert(count > 0);
    rendition = XmRenderTableGetRendition(table, tags[0]);
    assert(rendition);
    XtSetArg(arg, XmNfontName, &name);
    XmRenditionRetrieve(rendition, &arg, 1);
    assert(name);
    copy = XtNewString(name);
    XmRenditionFree(rendition);
    for (i = 0; i < count; i++) XtFree(tags[i]);
    XtFree((char *)tags);
    return copy;
}

static void pasteTimeout(XtPointer arg, XtIntervalId *id)
{
    (void)id;
    *(int *)arg = 1;
}

static void checkPrimaryPaste(WindowInfo *window, XEvent *event, const char *sample)
{
    XtAppContext app = XtWidgetToApplicationContext(window->shell);
    XtIntervalId timeout;
    int expired = 0;
    char *value;
    XmTextSetString(window->findText, "");
    BufSelect(window->buffer, 1, strlen(sample) + 1);
    XtCallActionProc(window->findText, "copy-primary", event, NULL, 0);
    timeout = XtAppAddTimeOut(app, 2000, pasteTimeout, &expired);
    while (!expired && XmTextGetLastPosition(window->findText) == 0)
        XtAppProcessEvent(app, XtIMAll);
    if (!expired) XtRemoveTimeOut(timeout);
    value = XmTextGetString(window->findText);
    assert(!strcmp(value, sample)); XtFree(value);
    BufUnselect(window->buffer);
}

static void captureSearchField(Widget field)
{
    Display *display = XtDisplay(field);
    XtAppContext app = XtWidgetToApplicationContext(field);
    Dimension width, height;
    XImage *image;
    FILE *out;
    int x, y;
    unsigned long pixel;
    XSync(display, False);
    while (XtAppPending(app)) XtAppProcessEvent(app, XtIMAll);
    XSync(display, False);
    XtVaGetValues(field, XmNwidth, &width, XmNheight, &height, NULL);
    image = XGetImage(display, XtWindow(field), 0, 0, width, height,
            AllPlanes, ZPixmap);
    assert(image);
    out = fopen("utf8-search.ppm", "wb");
    assert(out);
    fprintf(out, "P6\n%d %d\n255\n", width, height);
    assert(image->red_mask == 0xff0000 && image->green_mask == 0xff00 &&
            image->blue_mask == 0xff);
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            pixel = XGetPixel(image, x, y);
            fputc((pixel >> 16) & 255, out); fputc((pixel >> 8) & 255, out);
            fputc(pixel & 255, out);
        }
    fclose(out);
    XDestroyImage(image);
}

static void checkSearchDialog(WindowInfo *window)
{
    const char *samples[] = {"\xe2\x80\x94", "\xc3\xa9", "\xce\xb2",
        "\xe4\xb8\xad", "\xf0\x90\x90\x80", "e\xcc\x81"};
    char *value, *legacyFont;
    WindowInfo *tab;
    int i, length;
    Time time;
    XmAnyCallbackStruct cb;
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.xbutton.type = ButtonRelease;
    event.xbutton.display = XtDisplay(window->shell);
    event.xbutton.time = CurrentTime;
    memset(&cb, 0, sizeof(cb)); cb.reason = XmCR_ACTIVATE; cb.event = &event;
    SetPrefSearchDlogs(False);
    SetPrefSearchWraps(False);
    SetDocumentEncoding(window, False);
    legacyFont = fieldFontName(window->findText);
    SetDocumentEncoding(window, True);
    for (i = 0; i < 6; i++) {
        const char *sample = samples[i];
        length = strlen(sample);
        BufSetAll(window->buffer, "A");
        BufInsert(window->buffer, 1, sample);
        BufInsert(window->buffer, length + 1, "Z");
        BufSelect(window->buffer, 1, length + 1);
        TextCopyClipboard(window->textArea, CurrentTime);
        BufUnselect(window->buffer);
        TextSetCursorPos(window->lastFocus, 0);
        DoFindDlog(window, SEARCH_FORWARD, True, SEARCH_CASE_SENSE, CurrentTime);
        assert(((XmTextWidget)window->findText)->text.output->data->use_xft);
        assert(XmTextPaste(window->findText));
        value = XmTextGetString(window->findText);
        assert(!strcmp(value, sample)); XtFree(value);
        assert(XmTextGetLastPosition(window->findText) == (i == 5 ? 2 : 1));
        XtCallCallbacks(window->findBtn, XmNactivateCallback, &cb);
        assert(window->buffer->primary.selected);
        assert(window->buffer->primary.start == 1 && window->buffer->primary.end == length + 1);
        BufUnselect(window->buffer); TextSetCursorPos(window->lastFocus, 0);
        checkPrimaryPaste(window, &event, sample);
        XmToggleButtonSetState(window->findRegexToggle, True, True);
        XtCallCallbacks(window->findBtn, XmNactivateCallback, &cb);
        assert(window->buffer->primary.selected);
        assert(window->buffer->primary.start == 1 && window->buffer->primary.end == length + 1);
        XmToggleButtonSetState(window->findRegexToggle, False, True);
        /* Paste a native Motif clipboard back into the custom editor. */
        time = serverTime(window->findText);
        XmTextSetSelection(window->findText, 0,
                XmTextGetLastPosition(window->findText), time);
        assert(XmTextCopy(window->findText, time));
        BufSetAll(window->buffer, "");
        TextSetCursorPos(window->lastFocus, 0);
        TextPasteClipboard(window->textArea, CurrentTime);
        value = BufGetAll(window->buffer);
        assert(!strcmp(value, sample)); free(value);
        /* Restore NEdit's clipboard for the next fields. */
        BufSelect(window->buffer, 0, length);
        TextCopyClipboard(window->textArea, CurrentTime);
        BufUnselect(window->buffer);
    }
    XmTextSetString(window->findText, "Find: \xe2\x80\x94 caf\xc3\xa9 \xce\xb2");
    captureSearchField(window->findText);
    BufSetAll(window->buffer, "Ae\xcc\x81Z");
    TextSetCursorPos(window->lastFocus, 0);
    XtUnmanageChild(window->findDlog);
    DoFindReplaceDlog(window, SEARCH_FORWARD, True, SEARCH_CASE_SENSE, CurrentTime);
    assert(((XmTextWidget)window->replaceText)->text.output->data->use_xft);
    assert(((XmTextWidget)window->replaceWithText)->text.output->data->use_xft);
    XmTextSetString(window->replaceText, "");
    assert(XmTextPaste(window->replaceText));
    XmTextSetString(window->replaceWithText, "");
    assert(XmTextPaste(window->replaceWithText));
    value = XmTextGetString(window->replaceWithText);
    assert(!strcmp(value, samples[5])); XtFree(value);
    XmTextSetString(window->replaceWithText, (char *)samples[3]);
    XtCallCallbacks(window->replaceBtn, XmNactivateCallback, &cb);
    value = BufGetAll(window->buffer);
    assert(!strcmp(value, "A\xe4\xb8\xadZ")); free(value);
    Undo(window);
    XtUnmanageChild(window->replaceDlog);
    BufUnselect(window->buffer); TextSetCursorPos(window->lastFocus, 0);
    BeginISearch(window, SEARCH_FORWARD);
    assert(((XmTextWidget)window->iSearchText)->text.output->data->use_xft);
    assert(XmTextPaste(window->iSearchText));
    value = XmTextGetString(window->iSearchText);
    assert(!strcmp(value, samples[5])); XtFree(value);
    assert(window->buffer->primary.selected);
    assert(window->buffer->primary.start == 1 && window->buffer->primary.end == 4);
    EndISearch(window);
    SetDocumentEncoding(window, False);
    value = fieldFontName(window->findText);
    assert(!strcmp(value, legacyFont)); XtFree(value);
    SetDocumentEncoding(window, True);
    assert(((XmTextWidget)window->findText)->text.output->data->use_xft);
    SetUtf8Font(window, "DejaVu Sans:size=15");
    value = fieldFontName(window->findText);
    assert(!strcmp(value, "DejaVu Sans:size=15")); XtFree(value);
    SetUtf8Font(window, "monospace:size=14");
    tab = CreateDocument(window, "search-font-test");
    SetUtf8Font(tab, "DejaVu Sans:size=15");
    value = fieldFontName(window->findText);
    assert(!strcmp(value, "monospace:size=14")); XtFree(value);
    RaiseDocument(tab);
    value = fieldFontName(tab->findText);
    assert(!strcmp(value, "DejaVu Sans:size=15")); XtFree(value);
    SetDocumentEncoding(tab, False);
    value = fieldFontName(tab->findText);
    assert(!strcmp(value, legacyFont)); XtFree(value); XtFree(legacyFont);
    RaiseDocument(window);
    value = fieldFontName(window->findText);
    assert(!strcmp(value, "monospace:size=14")); XtFree(value);
    CloseWindow(tab);
    puts("UTF-8 clipboard/PRIMARY paste into search fields, font restoration, and real literal/regex Find callbacks passed.");
}

static int dialogMode, closed;

static void chooseFont(XtPointer arg, XtIntervalId *id)
{
    Widget dialog = XtNameToWidget(WindowList->shell, "*utf8Font");
    XmSelectionBoxCallbackStruct cb;
    int count;
    (void)arg;
    (void)id;
    assert(dialog && XtIsManaged(dialog));
    XtVaGetValues(dialog, XmNlistItemCount, &count, NULL);
    assert(count > 0);
    memset(&cb, 0, sizeof(cb));
    if (dialogMode == 2) {
        cb.reason = XmCR_CANCEL;
        XtCallCallbacks(dialog, XmNcancelCallback, &cb);
    } else {
        cb.reason = XmCR_APPLY;
        cb.value = XmStringCreateLocalized("DejaVu Sans:size=15");
        XtCallCallbacks(dialog, XmNapplyCallback, &cb);
        assert(!strcmp(((TextWidget)WindowList->textArea)->text.textD->utf8FontName,
                "DejaVu Sans:size=15"));
        XmStringFree(cb.value);
        cb.reason = XmCR_OK;
        cb.value = XmStringCreateLocalized("monospace:size=13");
        XtCallCallbacks(dialog, XmNokCallback, &cb);
        XmStringFree(cb.value);
    }
    closed = 1;
}

void __wrap_XtAppMainLoop(XtAppContext app)
{
    WindowInfo *window = WindowList;
    textBuffer *buf;
    const char *initial = "Ae\xcc\x81\xe4\xb8\xadZ";
    int i, type, x, y, nx, ny;
    Widget text;
    char *bytes;
    assert(window && window->next == NULL);
    buf = window->buffer;
    assert(buf->encoding == BUF_ENCODING_UTF8);
    BufSetAll(buf, initial);
    SplitPane(window);
    SetUtf8Font(window, "monospace:size=14");
    for (i = 0; i <= window->nPanes; i++) {
        text = i ? window->textPanes[i - 1] : window->textArea;
        assert(TextGetBuffer(text) == buf);
        assert(!strcmp(((TextWidget)text)->text.textD->utf8FontName, "monospace:size=14"));
    }
    /* Real menu callback: round-trip interpretation without changing bytes. */
    XmToggleButtonSetState(window->utf8Item, False, True);
    assert(buf->encoding == BUF_ENCODING_8BIT);
    XmToggleButtonSetState(window->utf8Item, True, True);
    assert(buf->encoding == BUF_ENCODING_UTF8);
    bytes = BufGetAll(buf);
    assert(!strcmp(bytes, initial)); free(bytes);
    checkSearchDialog(window);
    /* Exercise drag completion with the application's real undo callbacks. */
    for (type = DRAG_COPY; type <= DRAG_OVERLAY_COPY; type++) {
        TextWidget tw = (TextWidget)window->textArea;
        BufSetAll(buf, "Ae\xcc\x81Z\nB\xe4\xb8\xadQ");
        ClearUndoList(window); ClearRedoList(window);
        BufRectSelect(buf, 0, 5, 1, 2);
        assert(TextPosToXY((Widget)tw, 1, &x, &y));
        tw->text.btnDownX = x; tw->text.btnDownY = y;
        BeginBlockDrag(tw);
        assert(window->ignoreModify);
        assert(TextPosToXY((Widget)tw, 7, &nx, &ny));
        BlockDragSelection(tw, nx, ny, type);
        FinishBlockDrag(tw);
        assert(!window->ignoreModify);
        bytes = BufGetAll(buf);
        assert(strcmp(bytes, "Ae\xcc\x81Z\nB\xe4\xb8\xadQ"));
        Undo(window);
        {
            char *restored = BufGetAll(buf);
            assert(!strcmp(restored, "Ae\xcc\x81Z\nB\xe4\xb8\xadQ"));
            free(restored);
        }
        Redo(window);
        {
            char *redone = BufGetAll(buf);
            assert(!strcmp(redone, bytes));
            free(redone);
        }
        free(bytes);
    }
    /* Real defaults callback and persisted resource file. */
    XmToggleButtonSetState(window->utf8DefItem, True, True);
    SetPrefUtf8Font("monospace:size=16");
    SaveNEditPrefs(window->shell, True);
    assert(GetPrefUtf8() && !strcmp(GetPrefUtf8Font(), "monospace:size=16"));
    dialogMode = 1; closed = 0;
    XtAppAddTimeOut(app, 1, chooseFont, NULL);
    ChooseUtf8Font(window, True);
    assert(closed);
    assert(!strcmp(((TextWidget)window->textArea)->text.textD->utf8FontName,
            "monospace:size=13"));
    assert(!strcmp(((TextWidget)window->textPanes[0])->text.textD->utf8FontName,
            "monospace:size=13"));
    dialogMode = 2; closed = 0;
    XtAppAddTimeOut(app, 1, chooseFont, NULL);
    ChooseUtf8Font(window, True);
    assert(closed);
    puts("Full application encoding/default menus, persisted resources, split fonts, drag completion/undo, and font chooser Apply/OK/Cancel passed.");
    exit(0);
}
