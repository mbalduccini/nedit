/* Copyright 2026. Distributed under the same GPL terms as NEdit. */
#ifndef NEDIT_SELECTION_CODEC_H
#define NEDIT_SELECTION_CODEC_H
#include <X11/Intrinsic.h>
/* Standard X encodings, allocated result and explicit byte lengths. */
char *EncodeSelectionText(Display *, const char *, unsigned long, int utf8, Atom target,
                          unsigned long *length);
char *DecodeSelectionText(Display *, const char *, unsigned long, Atom type, int utf8,
                          unsigned long *length);
void RequestTextSelection(Widget, Atom, int utf8, Time, XtSelectionCallbackProc,
                          XtPointer);
int PutClipboardText(Widget, Time, const char *, unsigned long, int utf8);
char *GetClipboardText(Widget, int utf8, unsigned long *length);
#endif
