/* Copyright 2026. Distributed under the same GPL terms as NEdit. */
#ifndef NEDIT_MOTIF_COMPAT_H_INCLUDED
#define NEDIT_MOTIF_COMPAT_H_INCLUDED
#include <Xm/Xm.h>

/* OpenMotif exposes USE_XFT; Motif 2.5 publishes XM_UTF8 instead. Avoid
   depending on private widget records to detect native UTF-8 renditions. */
#if (defined(USE_XFT) && USE_XFT) || \
        (XmVersion >= 2005 && defined(XM_UTF8) && XM_UTF8)
#define NEDIT_MOTIF_XFT 1
#else
#define NEDIT_MOTIF_XFT 0
#endif

XmRenderTable CreateMotifUtf8RenderTable(Widget widget, const char *name);
void InitMotifDefaultFonts(Widget shell);

#endif
