/* Copyright 2026. Distributed under the same GPL terms as NEdit. */
#include "motifCompat.h"
#include <X11/StringDefs.h>
#if defined(HAVE_XFT) && NEDIT_MOTIF_XFT
#include <X11/Xft/Xft.h>
#endif

XmRenderTable CreateMotifUtf8RenderTable(Widget widget, const char *name)
{
#if defined(HAVE_XFT) && NEDIT_MOTIF_XFT
    Arg args[4];
    XmRendition rendition;
    XmRenderTable table;
    Display *display = XtDisplay(widget);
    XftFont *font = XftFontOpenName(display,
            XScreenNumberOfScreen(XtScreen(widget)), name);
    if (!font) return NULL;
    /* Supply a separately retained font for every rendition. Motif 2.5.2's
       font-name cache reuses Xft pointers without incrementing references,
       although rendition destruction closes them. Bypass that cache. */
    XtSetArg(args[0], XmNfontType, XmFONT_IS_XFT);
    XtSetArg(args[1], XmNfontName, name);
    XtSetArg(args[2], XmNloadModel, XmLOAD_IMMEDIATE);
    XtSetArg(args[3], XmNxftFont, font);
    rendition = XmRenditionCreate(widget, XmFONTLIST_DEFAULT_TAG, args, 4);
    if (!rendition) {
        XftFontClose(display, font);
        return NULL;
    }
    table = XmRenderTableAddRenditions(NULL, &rendition, 1, XmMERGE_REPLACE);
    /* The rendition owns the Xft reference, including on table failure. */
    XmRenditionFree(rendition);
    return table;
#else
    (void)widget;
    (void)name;
    return NULL;
#endif
}

void InitMotifDefaultFonts(Widget shell)
{
#if defined(HAVE_XFT) && NEDIT_MOTIF_XFT && XmVersion >= 2005
    typedef struct {
        String defaultFont, buttonFont, buttonTable;
        String labelFont, labelTable, textFont, textTable;
    } FontResources;
    static XtResource resources[] = {
#define FONT_RESOURCE(name, class, member) \
        {name, class, XtRString, sizeof(String), \
         XtOffsetOf(FontResources, member), XtRImmediate, NULL}
        FONT_RESOURCE(XmNdefaultFontList, XmCDefaultFontList, defaultFont),
        FONT_RESOURCE(XmNbuttonFontList, XmCButtonFontList, buttonFont),
        FONT_RESOURCE(XmNbuttonRenderTable, XmCButtonRenderTable, buttonTable),
        FONT_RESOURCE(XmNlabelFontList, XmCLabelFontList, labelFont),
        FONT_RESOURCE(XmNlabelRenderTable, XmCLabelRenderTable, labelTable),
        FONT_RESOURCE(XmNtextFontList, XmCTextFontList, textFont),
        FONT_RESOURCE(XmNtextRenderTable, XmCTextRenderTable, textTable)
#undef FONT_RESOURCE
    };
    FontResources specified;
    XmRenderTable proportional = NULL, fixed = NULL;
    Arg args[3];
    Cardinal count = 0;

    /* Read only whether a resource was supplied. Explicit user font lists
       and render tables retain precedence over our application defaults. */
    XtGetApplicationResources(shell, &specified, resources,
            XtNumber(resources), NULL, 0);
    if (specified.defaultFont) return;
    if ((!specified.buttonFont && !specified.buttonTable) ||
            (!specified.labelFont && !specified.labelTable))
        proportional = CreateMotifUtf8RenderTable(shell, "Sans:size=9");
    if (!specified.textFont && !specified.textTable)
        fixed = CreateMotifUtf8RenderTable(shell, "Monospace:size=9");
    if (proportional && !specified.buttonFont && !specified.buttonTable) {
        XtSetArg(args[count], XmNbuttonRenderTable, proportional); count++;
    }
    if (proportional && !specified.labelFont && !specified.labelTable) {
        XtSetArg(args[count], XmNlabelRenderTable, proportional); count++;
    }
    if (fixed) {
        XtSetArg(args[count], XmNtextRenderTable, fixed); count++;
    }
    /* VendorShell copies the tables; descendant shells and widgets inherit
       them. Avoid string render-table defaults, which trigger Motif 2.5.2's
       unsafe Xft font-name cache during widget initialization. */
    if (count) XtSetValues(shell, args, count);
    if (proportional) XmRenderTableFree(proportional);
    if (fixed) XmRenderTableFree(fixed);
#else
    (void)shell;
#endif
}
