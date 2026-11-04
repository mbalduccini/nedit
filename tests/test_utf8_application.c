#include "nedit.h"
#include "text.h"
#include "textP.h"
#include "window.h"
#include "preferences.h"
#include "file.h"
#include "textDrag.h"
#include "undo.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <Xm/SelectioB.h>
#include <Xm/ToggleB.h>

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
