#include "nedit.h"
#include "text.h"
#include "textP.h"
#include "window.h"
#include "preferences.h"
#include "file.h"
#include "textDrag.h"
#include "undo.h"
#include "search.h"
#include "selectionCodec.h"
#include "textSel.h"
#include "highlight.h"
#include "highlightData.h"
#include "shift.h"
#include "utf8.h"
#include "calltips.h"
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
    XPutBackEvent(display,&event);
    XtAppNextEvent(XtWidgetToApplicationContext(w),&event);
    XtDispatchEvent(&event);
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

/* Foreign clients offering one encoding exercise actual asynchronous fallback. */
static Atom foreignTarget;
static const char *foreignText;
static int foreignUtf8, foreignDeleted;
static Boolean foreignSelection(Widget w, Atom *selection, Atom *target, Atom *type,
                                XtPointer *value, unsigned long *length, int *format)
{
    (void)selection;
    if (*target == XInternAtom(XtDisplay(w), "DELETE", False)) {
        foreignDeleted = 1;
        *type = *target;
        *format = 8;
        *length = 0;
        *value = NULL;
        return True;
    }
    if (*target != foreignTarget)
        return False;
    *value = EncodeSelectionText(XtDisplay(w), foreignText, strlen(foreignText),
                                 foreignUtf8, *target, length);
    *type = *target;
    *format = 8;
    return *value != NULL;
}
static void waitForPaste(WindowInfo *window, int move)
{
    XtAppContext app = XtWidgetToApplicationContext(window->shell);
    int expired = 0;
    XtIntervalId timer = XtAppAddTimeOut(app, 2000, pasteTimeout, &expired);
    while (!expired && (!window->buffer->length || (move && !foreignDeleted)))
        XtAppProcessEvent(app, XtIMAll);
    if (!expired)
        XtRemoveTimeOut(timer);
    assert(!expired);
}
static void checkSelectionCodecs(WindowInfo *window)
{
    Display *d = XtDisplay(window->shell);
    Atom utf8 = XInternAtom(d, "UTF8_STRING", False);
    Atom compound = XInternAtom(d, "COMPOUND_TEXT", False);
    char *encoded, *decoded, *value;
    XTextProperty property;
    char *parts[] = {"café", "β 中"};
    unsigned long size, length;
    int i;
    Atom targets[3];
    targets[0] = utf8;
    targets[1] = compound;
    targets[2] = XA_STRING;
    encoded = EncodeSelectionText(d, "café", 5, True, XA_STRING, &size);
    assert(encoded && size == 4 && !memcmp(encoded, "caf\351", 4));
    decoded = DecodeSelectionText(d, encoded, size, XA_STRING, True, &length);
    assert(decoded && length == 5 && !strcmp(decoded, "café"));
    free(decoded);
    free(encoded);
    assert(!EncodeSelectionText(d, "中", 3, True, XA_STRING, &size));
    assert(!DecodeSelectionText(d, "中", 3, utf8, False, &size));
    encoded = EncodeSelectionText(d, "caf\351", 4, False, utf8, &size);
    assert(encoded && size == 5 && !strcmp(encoded, "café"));
    free(encoded);
    encoded =
        EncodeSelectionText(d, "café β 中", strlen("café β 中"), True, compound, &size);
    assert(encoded);
    decoded = DecodeSelectionText(d, encoded, size, compound, True, &length);
    assert(decoded && !strcmp(decoded, "café β 中"));
    free(decoded);
    free(encoded);
    assert(Xutf8TextListToTextProperty(d, parts, 2, XCompoundTextStyle, &property) ==
           Success);
    decoded = DecodeSelectionText(d, (char *)property.value, property.nitems, compound,
                                  True, &length);
    assert(decoded && length == strlen(parts[0]) + 1 + strlen(parts[1]) &&
           !memcmp(decoded, "café\0β 中", length));
    free(decoded);
    XFree(property.value);
    assert(!EncodeSelectionText(d, "é\0β", 5, True, compound, &size));
    /* Embedded NULs and malformed bytes remain recoverable in UTF8_STRING. */
    encoded = EncodeSelectionText(d, "é\0\200", 4, True, utf8, &size);
    decoded = DecodeSelectionText(d, encoded, size, utf8, True, &length);
    assert(decoded && length == 4 && !memcmp(decoded, "é\0\200", 4));
    free(decoded);
    free(encoded);
    for (i = 0; i < 3; i++) {
        BufSetAll(window->buffer, "");
        TextSetCursorPos(window->textArea, 0);
        foreignTarget = targets[i];
        foreignUtf8 = i != 2;
        foreignText = i == 2 ? "caf\351" : "café β 中";
        foreignDeleted = 0;
        assert(XtOwnSelection(window->findText, XA_PRIMARY,
                              serverTime(window->findText), foreignSelection, NULL,
                              NULL));
        InsertPrimarySelection(window->textArea, CurrentTime, False);
        waitForPaste(window, False);
        value = BufGetAll(window->buffer);
        assert(!strcmp(value, i == 2 ? "café" : "café β 中"));
        free(value);
        BufSetAll(window->buffer, "");
        TextSetCursorPos(window->textArea, 0);
        MovePrimarySelection(window->textArea, CurrentTime, False);
        waitForPaste(window, True);
        value = BufGetAll(window->buffer);
        assert(!strcmp(value, i == 2 ? "café" : "café β 中"));
        free(value);
        XtDisownSelection(window->findText, XA_PRIMARY, CurrentTime);
    }
    assert(PutClipboardText(window->textArea, serverTime(window->textArea), "caf\351",
                            4, False));
    value = GetClipboardText(window->textArea, True, &length);
    assert(value && !strcmp(value, "café"));
    free(value);
    assert(PutClipboardText(window->textArea, serverTime(window->textArea), "café β 中",
                            strlen("café β 中"), True));
    value = GetClipboardText(window->textArea, True, &length);
    assert(value && !strcmp(value, "café β 中"));
    free(value);
    puts("X PRIMARY fallback/move, STRING Latin-1, COMPOUND_TEXT, UTF8_STRING and "
         "clipboard conversion passed.");
}
/* A foreign Motif destination accepts only the requested fallback target. */
static char *foreignReceived;
static int foreignReceiveDone, foreignAttempts;
static Atom attemptedTargets[3];
static void receiveSecondary(Widget w, XtPointer arg, Atom *selection, Atom *type,
                             XtPointer value, unsigned long *length, int *format)
{
    (void)arg;
    (void)selection;
    if (value && *format == 8)
        foreignReceived =
            DecodeSelectionText(XtDisplay(w), value, *length, *type, True, length);
    XtFree(value);
    foreignReceiveDone = 1;
}
static Boolean foreignDestination(Widget w, Atom *selection, Atom *target, Atom *type,
                                  XtPointer *value, unsigned long *length, int *format)
{
    XSelectionRequestEvent *request = XtGetSelectionRequest(w, *selection, 0);
    Display *display = XtDisplay(w);
    Atom propertyType, *pair = NULL;
    unsigned long items, remaining;
    int propertyFormat;
    XEvent event;
    if (*target != XInternAtom(display, "INSERT_SELECTION", False))
        return False;
    assert(XGetWindowProperty(display, request->requestor, request->property, 0, 2,
                              False, AnyPropertyType, &propertyType, &propertyFormat,
                              &items, &remaining, (unsigned char **)&pair) == Success);
    assert(propertyFormat == 32 && items == 2 && foreignAttempts < 3);
    attemptedTargets[foreignAttempts++] = pair[1];
    if (pair[1] != foreignTarget) {
        XFree(pair);
        return False;
    }
    foreignReceiveDone = 0;
    XtGetSelectionValue(w, pair[0], pair[1], receiveSecondary, NULL, request->time);
    XFree(pair);
    while (!foreignReceiveDone) {
        XtAppNextEvent(XtWidgetToApplicationContext(w), &event);
        XtDispatchEvent(&event);
    }
    *type = *target;
    *value = NULL;
    *length = 0;
    *format = 8;
    return foreignReceived != NULL;
}
static void checkSecondarySelection(WindowInfo *window)
{
    WindowInfo *destination = CreateDocument(window, "secondary-destination");
    XtAppContext app = XtWidgetToApplicationContext(window->shell);
    int expired = 0;
    XtIntervalId timer;
    char *text;
    int i;
    Atom targets[3];
    targets[0] = XInternAtom(XtDisplay(window->shell), "UTF8_STRING", False);
    targets[1] = XInternAtom(XtDisplay(window->shell), "COMPOUND_TEXT", False);
    targets[2] = XA_STRING;
    SetDocumentEncoding(destination, True);
    BufSetAll(destination->buffer, "");
    TextSetCursorPos(destination->textArea, 0);
    TakeMotifDestination(destination->textArea, serverTime(destination->textArea));
    BufSetAll(window->buffer, "é β 中");
    BufSecondarySelect(window->buffer, 0, window->buffer->length);
    SendSecondarySelection(window->textArea, serverTime(window->textArea), True);
    timer = XtAppAddTimeOut(app, 2000, pasteTimeout, &expired);
    while (!expired &&
           (!destination->buffer->length || window->buffer->secondary.selected))
        XtAppProcessEvent(app, XtIMAll);
    if (!expired)
        XtRemoveTimeOut(timer);
    assert(!expired && window->buffer->length == 0);
    text = BufGetAll(destination->buffer);
    assert(!strcmp(text, "é β 中"));
    free(text);
    CloseWindow(destination);
    RaiseDocument(window);
    for (i = 1; i < 3; i++) {
        foreignTarget = targets[i];
        foreignAttempts = 0;
        foreignReceived = NULL;
        expired = 0;
        assert(XtOwnSelection(
            window->findText,
            XInternAtom(XtDisplay(window->shell), "MOTIF_DESTINATION", False),
            serverTime(window->findText), foreignDestination, NULL, NULL));
        BufSetAll(window->buffer, i == 1 ? "é β 中" : "café");
        BufSecondarySelect(window->buffer, 0, window->buffer->length);
        SendSecondarySelection(window->textArea, serverTime(window->textArea), True);
        timer = XtAppAddTimeOut(app, 2000, pasteTimeout, &expired);
        while (!expired && window->buffer->secondary.selected)
            XtAppProcessEvent(app, XtIMAll);
        if (!expired)
            XtRemoveTimeOut(timer);
        assert(!expired && window->buffer->length == 0 && foreignAttempts == i + 1);
        assert(foreignReceived && !strcmp(foreignReceived, i == 1 ? "é β 中" : "café"));
        free(foreignReceived);
        foreignReceived = NULL;
        assert(attemptedTargets[0] == targets[0] && attemptedTargets[1] == targets[1]);
        if (i == 2)
            assert(attemptedTargets[2] == targets[2]);
        XtDisownSelection(
            window->findText,
            XInternAtom(XtDisplay(window->shell), "MOTIF_DESTINATION", False),
            CurrentTime);
    }
    puts("Secondary quick move preserves Unicode text, including foreign-client "
         "fallback.");
}
static void checkUnicodeSearch(WindowInfo *window)
{
    int start, end, bw, fw, copyStart, copyEnd, replacedLength;
    char *replaced;
    const char *text = "Straße Σς 中文 éé\n𐐀";
    assert(SearchString(text, "STRASSE", SEARCH_FORWARD, SEARCH_LITERAL | SEARCH_UTF8,
                        False, 0, &start, &end, &bw, &fw, NULL) &&
           start == 0 && end == 7);
    assert(SearchString(text, "σσ", SEARCH_FORWARD, SEARCH_LITERAL | SEARCH_UTF8, False,
                        0, &start, &end, NULL, NULL, NULL) &&
           start == 8 && end == 12);
    assert(!SearchString("ß", "s", SEARCH_FORWARD, SEARCH_LITERAL | SEARCH_UTF8, False,
                         0, &start, &end, NULL, NULL, NULL));
    assert(!SearchString("é", "é", SEARCH_FORWARD, SEARCH_LITERAL | SEARCH_UTF8, False,
                         0, &start, &end, NULL, NULL, NULL));
    assert(SearchString("β xβ", "β", SEARCH_FORWARD,
                        SEARCH_CASE_SENSE_WORD | SEARCH_UTF8, True, 3, &start, &end,
                        NULL, NULL, " ") &&
           start == 0 && end == 2);
    assert(SearchString("xβ β", "β", SEARCH_BACKWARD,
                        SEARCH_CASE_SENSE_WORD | SEARCH_UTF8, True, 1, &start, &end,
                        NULL, NULL, " ") &&
           start == 4 && end == 6);
    assert(!SearchString("中β", "β", SEARCH_FORWARD,
                         SEARCH_CASE_SENSE_WORD | SEARCH_UTF8, False, 0, &start, &end,
                         NULL, NULL, " "));
    assert(SearchString("é中", ".", SEARCH_FORWARD, SEARCH_REGEX | SEARCH_UTF8, False,
                        1, &start, &end, NULL, NULL, NULL) &&
           start == 2 && end == 5);
    replaced = ReplaceAllInString("é中β", "(.)", "\\U\\1", SEARCH_REGEX | SEARCH_UTF8,
                                  &copyStart, &copyEnd, &replacedLength, NULL);
    assert(replaced && copyStart == 0 && copyEnd == 7 && !strcmp(replaced, "É中Β"));
    free(replaced);
    replaced = ReplaceAllInString("é中", "(?=.)", "|", SEARCH_REGEX | SEARCH_UTF8,
                                  &copyStart, &copyEnd, &replacedLength, NULL);
    assert(replaced && copyStart == 0 && copyEnd == 2 && !strcmp(replaced, "|é|"));
    free(replaced);
    while (XtAppPending(XtWidgetToApplicationContext(window->shell)))
        XtAppProcessEvent(XtWidgetToApplicationContext(window->shell), XtIMAll);
    SetDocumentEncoding(window, True);
    BufSetAll(window->buffer, "é中β");
    TextSetCursorPos(window->lastFocus, 0);
    BufSelect(window->buffer, 0, 2);
    assert(
        SearchAndReplace(window, SEARCH_FORWARD, "(.)", "\\U\\1", SEARCH_REGEX, False));
    replaced = BufGetAll(window->buffer);
    assert(!strcmp(replaced, "É中β"));
    free(replaced);
    serverTime(window->textArea);
    BufSelect(window->buffer, 0, window->buffer->length);
    assert(window->buffer->primary.selected);
    UpcaseSelection(window);
    replaced = BufGetAll(window->buffer);
    assert(!strcmp(replaced, "É中Β"));
    free(replaced);
    DowncaseSelection(window);
    replaced = BufGetAll(window->buffer);
    assert(!strcmp(replaced, "é中β"));
    free(replaced);
    BufSetAll(window->buffer, "β β");
    TextSetCursorPos(window->lastFocus, 0);
    serverTime(window->textArea);
    BufSelect(window->buffer, 0, 2);
    SearchForSelected(window, SEARCH_FORWARD, SEARCH_CASE_SENSE, False, CurrentTime);
    {
        XtAppContext app = XtWidgetToApplicationContext(window->shell);
        int expired = 0;
        XtIntervalId timer = XtAppAddTimeOut(app, 2000, pasteTimeout, &expired);
        while (!expired && window->buffer->primary.start != 3)
            XtAppProcessEvent(app, XtIMAll);
        if (!expired)
            XtRemoveTimeOut(timer);
        assert(!expired && window->buffer->primary.end == 5);
    }
    BufSetAll(window->buffer, "é中β");
    serverTime(window->textArea);
    BufSelect(window->buffer, 0, window->buffer->length);
    ReplaceInSelection(window, "(.)", "<\\1>", SEARCH_REGEX);
    replaced = BufGetAll(window->buffer);
    assert(!strcmp(replaced, "<é><中><β>"));
    free(replaced);
    puts("Unicode literal full folding/whole-word wrap, scalar regex replacement, and "
         "menu case conversion passed.");
}
static void assertScalarStyles(WindowInfo *window)
{
    const char *text=BufAsString(window->buffer);
    int p=0,n,i,style;
    while(p<window->buffer->length) {
        n=Utf8Decode(text+p,window->buffer->length-p,NULL,NULL);
        style=HighlightCodeOfPos(window,p);
        for(i=1;i<n;i++) assert(HighlightCodeOfPos(window,p+i)==style);
        p+=n;
    }
}
static void checkUnicodeHighlighting(WindowInfo *window)
{
    char patterns[]="C:1:0{\nGreek:\"[α-ω]+\":::Keyword::\n"
        "CJK:\"(中+)\":::String::D\nZero:\"(?=𐐀)\":::Comment::D\n}";
    int style;
    StopHighlighting(window);
    assert(LoadHighlightString(patterns,False));
    SetLanguageMode(window,FindLanguageMode("C"),False);
    BufSetAll(window->buffer,"αβ 中中 𐐀\nβ 中\n");StartHighlighting(window,False);
    assert(window->highlightData);
    assertScalarStyles(window);
    style=HighlightCodeOfPos(window,0);
    assert(!strcmp(HighlightStyleOfCode(window,style),"Keyword"));
    style=HighlightCodeOfPos(window,5);
    assert(!strcmp(HighlightStyleOfCode(window,style),"String"));
    BufInsert(window->buffer,2,"γ");assertScalarStyles(window);
    BufRemove(window->buffer,2,4);assertScalarStyles(window);
    BufReplace(window->buffer,5,8,"𐐀");assertScalarStyles(window);
    SetDocumentEncoding(window,False);SetDocumentEncoding(window,True);
    assertScalarStyles(window);
    StopHighlighting(window);
    {
        char contextPatterns[]="C:0:2{Context:\"(?<=中{2})β\":::Keyword::\n}";
        assert(LoadHighlightString(contextPatterns,False));
        BufSetAll(window->buffer,"中中β");StartHighlighting(window,False);
        assert(!strcmp(HighlightStyleOfCode(window,HighlightCodeOfPos(window,6)),"Keyword"));
        BufReplace(window->buffer,0,3,"文");assertScalarStyles(window);
        assert(strcmp(HighlightStyleOfCode(window,HighlightCodeOfPos(window,6)),"Keyword"));
        BufReplace(window->buffer,0,3,"中");assertScalarStyles(window);
        assert(!strcmp(HighlightStyleOfCode(window,HighlightCodeOfPos(window,6)),"Keyword"));
    }
    StopHighlighting(window);SetLanguageMode(window,PLAIN_LANGUAGE_MODE,False);
    puts("Unicode immediate/deferred syntax patterns, zero-width progress, scalar contexts, incremental edits, and encoding changes passed.");
}
static int renditionType(Widget w)
{
    XmRenderTable table;
    XmRendition rendition;
    XmStringTag *tags;
    Arg arg;
    int count,i,type;
    XtVaGetValues(w,XmNrenderTable,&table,NULL);
    count=XmRenderTableGetTags(table,&tags);assert(count);
    rendition=XmRenderTableGetRendition(table,tags[0]);assert(rendition);
    XtSetArg(arg,XmNfontType,&type);XmRenditionRetrieve(rendition,&arg,1);
    XmRenditionFree(rendition);
    for(i=0;i<count;i++) XtFree(tags[i]);XtFree((char *)tags);
    return type;
}
static void checkUnicodeCalltip(WindowInfo *window)
{
    textDisp *display=((TextWidget)window->lastFocus)->text.textD;
    int tip,legacy;
    SetDocumentEncoding(window,False);
    tip=ShowCalltip(window,"Legacy",False,0,TIP_LEFT,TIP_BELOW,TIP_SLOPPY);assert(tip);
    legacy=renditionType(display->calltipW);KillCalltip(window,tip);
    SetDocumentEncoding(window,True);
    tip=ShowCalltip(window,"café β 中",False,0,TIP_LEFT,TIP_BELOW,TIP_SLOPPY);assert(tip);
    assert(renditionType(display->calltipW)==XmFONT_IS_XFT);KillCalltip(window,tip);
    SetDocumentEncoding(window,False);
    tip=ShowCalltip(window,"Legacy",False,0,TIP_LEFT,TIP_BELOW,TIP_SLOPPY);assert(tip);
    assert(renditionType(display->calltipW)==legacy);KillCalltip(window,tip);
    SetDocumentEncoding(window,True);
    puts("Unicode calltip font and legacy font restoration passed.");
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
    checkSelectionCodecs(window);
    checkSecondarySelection(window);
    checkUnicodeSearch(window);
    checkUnicodeHighlighting(window);
    checkUnicodeCalltip(window);
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
