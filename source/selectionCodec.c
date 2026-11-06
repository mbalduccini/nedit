/* Copyright 2026. Distributed under the same GPL terms as NEdit. */
/* X selection encoding is independent of the document's byte storage. */
#include "selectionCodec.h"
#include "utf8.h"
#include "../util/misc.h"
#include "../util/nedit_malloc.h"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <Xm/CutPaste.h>
#include <string.h>
#include <stdlib.h>

static char *latin1(const char *s, unsigned long size, int toUtf8, unsigned long *len)
{
    unsigned long p = 0, n = 0, c;
    int step, valid;
    char *out = NEditMalloc(size * 2 + 1);
    while (p < size) {
        if (toUtf8) {
            c = (unsigned char)s[p++];
            n += Utf8Encode(c, out + n);
        } else {
            step = Utf8Decode(s + p, size - p, &c, &valid);
            if (!valid || c > 255) {
                NEditFree(out);
                return NULL;
            }
            out[n++] = c;
            p += step;
        }
    }
    out[n] = 0;
    *len = n;
    return out;
}
char *EncodeSelectionText(Display *d, const char *s, unsigned long size, int utf8,
                          Atom target, unsigned long *len)
{
    char *out, *input;
    XTextProperty property;
    int status;
    if (target == XA_STRING) {
        if (utf8)
            return latin1(s, size, 0, len);
        out = NEditMalloc(size + 1);
        memcpy(out, s, size);
        out[size] = 0;
        *len = size;
        return out;
    }
    if (target == XInternAtom(d, "UTF8_STRING", False)) {
        if (!utf8)
            return latin1(s, size, 1, len);
        out = NEditMalloc(size + 1);
        memcpy(out, s, size);
        out[size] = 0;
        *len = size;
        return out;
    }
    if (target != XInternAtom(d, "COMPOUND_TEXT", False) || memchr(s, 0, size))
        return NULL;
    input = utf8 ? NEditMalloc(size + 1) : latin1(s, size, 1, len);
    if (utf8) {
        memcpy(input, s, size);
        input[size] = 0;
    }
    status = Xutf8TextListToTextProperty(d, &input, 1, XCompoundTextStyle, &property);
    NEditFree(input);
    if (status != Success) {
        if (status >= 0)
            XFree(property.value);
        return NULL;
    }
    out = NEditMalloc(property.nitems + 1);
    memcpy(out, property.value, property.nitems);
    *len = property.nitems;
    out[*len] = 0;
    XFree(property.value);
    return out;
}
char *DecodeSelectionText(Display *d, const char *s, unsigned long size, Atom type,
                          int utf8, unsigned long *len)
{
    char *out, **list;
    int count, status, i;
    unsigned long total = 0, partLength;
    char *part;
    XTextProperty property;
    if (type == XA_STRING) {
        if (utf8)
            return latin1(s, size, 1, len);
    } else if (type == XInternAtom(d, "UTF8_STRING", False)) {
        if (!utf8)
            return latin1(s, size, 0, len);
    } else if (type == XInternAtom(d, "COMPOUND_TEXT", False)) {
        property.value = (unsigned char *)s;
        property.encoding = type;
        property.format = 8;
        property.nitems = size;
        status = Xutf8TextPropertyToTextList(d, &property, &list, &count);
        if (status != Success) {
            if (status >= 0)
                XFreeStringList(list);
            return NULL;
        }
        /* X text properties separate list entries with NUL. Preserve all
           entries and their separators through the explicit byte length. */
        for (i = 0; i < count; i++)
            total += strlen(list[i]) + (i != 0);
        out = NEditMalloc(total + 1);
        total = 0;
        for (i = 0; i < count; i++) {
            if (i)
                out[total++] = 0;
            if (utf8) {
                partLength = strlen(list[i]);
                memcpy(out + total, list[i], partLength);
            } else {
                part = latin1(list[i], strlen(list[i]), 0, &partLength);
                if (!part) {
                    NEditFree(out);
                    XFreeStringList(list);
                    return NULL;
                }
                memcpy(out + total, part, partLength);
                NEditFree(part);
            }
            total += partLength;
        }
        out[total] = 0;
        *len = total;
        XFreeStringList(list);
        return out;
    } else
        return NULL;
    out = NEditMalloc(size + 1);
    memcpy(out, s, size);
    out[size] = 0;
    *len = size;
    return out;
}
typedef struct {
    Atom selection, targets[3];
    int index, utf8;
    Time time;
    XtSelectionCallbackProc callback;
    XtPointer arg;
} textRequest;
static void receiveText(Widget w, XtPointer arg, Atom *selection, Atom *type,
                        XtPointer value, unsigned long *len, int *format)
{
    textRequest *r = arg;
    char *out = NULL;
    unsigned long length = 0;
    Atom outputType = XA_STRING;
    int outputFormat = 8;
    if (value && *format == 8)
        out = DecodeSelectionText(XtDisplay(w), value, *len, *type, r->utf8, &length);
    XtFree(value);
    if (!out && ++r->index < 3) {
        XtGetSelectionValue(w, r->selection, r->targets[r->index], receiveText, r,
                            r->time);
        return;
    }
    if (!out)
        outputType = XT_CONVERT_FAIL;
    r->callback(w, r->arg, selection, &outputType, out, &length, &outputFormat);
    NEditFree(r);
}
void RequestTextSelection(Widget w, Atom selection, int utf8, Time time,
                          XtSelectionCallbackProc callback, XtPointer arg)
{
    textRequest *r = NEditNew(textRequest);
    r->selection = selection;
    r->index = 0;
    r->utf8 = utf8;
    r->time = time;
    r->callback = callback;
    r->arg = arg;
    r->targets[0] = utf8 ? XInternAtom(XtDisplay(w), "UTF8_STRING", False) : XA_STRING;
    r->targets[1] = XInternAtom(XtDisplay(w), "COMPOUND_TEXT", False);
    r->targets[2] = utf8 ? XA_STRING : XInternAtom(XtDisplay(w), "UTF8_STRING", False);
    XtGetSelectionValue(w, selection, r->targets[0], receiveText, r, time);
}
int PutClipboardText(Widget w, Time time, const char *text, unsigned long size,
                     int utf8)
{
    Display *d = XtDisplay(w);
    Window window = XtWindow(w);
    long item = 0;
    XmString label;
    const char *names[] = {"UTF8_STRING", "COMPOUND_TEXT", "STRING"};
    int i, status, success = 0;
    unsigned long length;
    char *encoded;
    if (SpinClipboardLock(d, window) != ClipboardSuccess)
        return 0;
    label = XmStringCreateSimple("NEdit");
    status = SpinClipboardStartCopy(d, window, label, time, w, NULL, &item);
    XmStringFree(label);
    if (status == ClipboardSuccess) {
        for (i = 0; i < 3; i++) {
            encoded = EncodeSelectionText(d, text, size, utf8,
                                          XInternAtom(d, names[i], False), &length);
            if (encoded) {
                if (SpinClipboardCopy(d, window, item, (char *)names[i], encoded,
                                      length, 0, NULL) == ClipboardSuccess)
                    success = 1;
                NEditFree(encoded);
            }
        }
        SpinClipboardEndCopy(d, window, item);
    }
    SpinClipboardUnlock(d, window);
    return success;
}
char *GetClipboardText(Widget w, int utf8, unsigned long *length)
{
    Display *d = XtDisplay(w);
    Window window = XtWindow(w);
    char *names[] = {"UTF8_STRING", "COMPOUND_TEXT", "STRING"};
    unsigned long size, received;
    long id = 0;
    int i;
    char *data, *out = NULL;
    if (!utf8) {
        names[0] = "STRING";
        names[2] = "UTF8_STRING";
    }
    for (i = 0; i < 3; i++) {
        if (SpinClipboardInquireLength(d, window, names[i], &size) != ClipboardSuccess)
            continue;
        data = NEditMalloc(size + 1);
        if (SpinClipboardRetrieve(d, window, names[i], data, size, &received, &id) ==
            ClipboardSuccess) {
            out = DecodeSelectionText(d, data, received,
                                      XInternAtom(d, names[i], False), utf8, length);
        }
        NEditFree(data);
        if (out)
            return out;
    }
    return NULL;
}
