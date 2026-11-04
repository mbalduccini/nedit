# UTF-8 checks

From the repository root:

```sh
make linux
make -C tests check
make -C tests check-widget
make -C tests check-sanitize
```

The decoder/buffer/layout checks do not require an X display. They cover every
Unicode scalar, malformed encodings, gap splits, legacy mode, logical columns,
selection boundaries, tab expansion, and pixel mappings with different advances.

The widget/application checks require Python, Xvfb, Xft, Fontconfig, and utf8proc. They
create an isolated X display and temporary application preferences; the user's
running editor, files, and preferences are not used. Glyph fallback checks require
installed fonts containing U+4E2D and U+10400 (Noto CJK and Noto Deseret are
available in this workspace). The Xvfb fixture uses 24-bit TrueColor.

The widget test invokes actual Motif movement/deletion/word/auto-wrap actions
and real undo/redo. It tests glyph pixels and availability, proportional-font
mouse placement and vertical movement, style changes inside a scalar, selections,
resize, pixel/column wrapping, oversize characters, and legacy byte mode.
Application menu/calltip/backup hooks are stubbed only in the standalone widget.
A warning about absent application-only translation actions is expected.

The application control test uses the real editor startup and menu callbacks.
It checks document encoding toggles, saved default resources, split-pane font
inheritance, drag completion with undo/redo, and the Fontconfig chooser's
Apply, OK and Cancel buttons.

The full editor check opens a temporary file, edits its supplementary scalar
through real actions, saves, reopens, and verifies unchanged UTF-8, malformed
bytes, and embedded NULs. It disables `appendLF` for exact byte comparison.

`tests/utf8-widget.ppm` is an ignored screenshot artifact for visual inspection.
All tests pass without requiring an image converter. See `doc/UTF8-PLAN.md` for
supported operations and the still-pending Unicode features.

Phase 1 checks include combining/ZWJ grapheme cursor and deletion actions,
rectangle edge policies, real column drag/cancel and undo, Unicode paragraph
fill/shift, CRLF/include-file, smart indent, encoding/font switches, split panes,
and raw incremental-backup bytes. Synthetic keyboard mapping uses only the
isolated X display. Most input lookup calls use real Motif; one linker-wrapped
lookup supplies the XBufferOverflow contract to verify a 60-byte commit retry.
The suite reports cached cursor/hit-test timings for a 200 KB line and a full
open/edit/save timing for a 2.2 MB file. The sanitizer target covers the standalone
decoder/buffer/layout and rectangular operations with ASan and UBSan.
