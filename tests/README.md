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

It also exercises real clipboard and PRIMARY paste into Find with accented
Latin, Greek, CJK, supplementary scalars and combining sequences, verifying
original bytes and Motif character counts. Real Find button callbacks check
literal and literal-text regex matches; Replace and incremental search verify
their resulting edits/selections. Motif-to-editor clipboard paste, encoding/font
restoration and shared-field tab switching are covered. Phase 2 checks also
exercise Unicode regex operators, case folding and word boundaries, as described
below.

The full editor check opens a temporary file, edits its supplementary scalar
through real actions, saves, reopens, and verifies unchanged UTF-8, malformed
bytes, and embedded NULs. It disables `appendLF` for exact byte comparison.

`tests/utf8-widget.ppm` and `tests/utf8-search.ppm` are ignored screenshot
artifacts for visual inspection.
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

Phase 2 tests cover the proprietary Unicode regex dialect, scalar quantifiers,
compact ranges, Unicode shortcut classes, case pairs, lookahead/lookbehind,
backreferences, captures, replacement case tokens, boundary-safe truncation,
malformed bytes and legacy one-byte matching. ASan/UBSan cover both engines.
Application tests exercise foreign PRIMARY owners offering only STRING,
COMPOUND_TEXT or UTF8_STRING, primary move/delete ordering, secondary quick move
through Motif destination and foreign fallback-only clients, multi-string
COMPOUND_TEXT decoding, clipboard conversion and refused lossy conversion.
Search checks include full-fold expansions, composed/decomposed distinction,
whole-word wrapping, selection-to-search and replacement. Immediate/deferred
syntax patterns are checked after edits and mode switches for uniform styles
across scalars. Calltip fonts restore in legacy mode. Real macros check each
Unicode API, byte compatibility, search/split flags and clipboard; shell-filter
round trips preserve malformed bytes. A 1 MB file with Unicode syntax matches
measures open/edit/save performance on the isolated display.
