# UTF-8 support for the classic branch

## Objective and scope

Add general UTF-8 support to classic NEdit while retaining its Motif interface,
gap buffer, syntax definitions, and existing single-byte editing mode. This is
an editor-core project, not a substitution for particular punctuation.

The first implementation phase covers drawing, layout, and ordinary editing.
Search, regular expressions, macros, clipboard interoperability, and advanced
multilingual layout remain explicit follow-on work. Do not describe a drawing
prototype as complete Unicode support.

## Effort estimate

These are rough engineering estimates for an experienced C/X11 developer, not
delivery commitments. They include integration and regression testing, and may
change after the first working renderer.

| Scope | Approximate effort |
| --- | --- |
| Limited UTF-8 drawing prototype | Several days to two weeks |
| Dependable UTF-8 editing, selection, clipboard, and search | One to three months |
| Broad multilingual support, including combining marks, shaping, and bidirectional text | Several additional months |

The main cost is separating bytes, Unicode scalar values, user-perceived
characters (grapheme clusters), display columns, and pixels. Classic NEdit
currently treats these as interchangeable in many places.

## Architecture and compatibility decisions

1. **Keep UTF-8 bytes and byte offsets internally.** Retain `char *` gap-buffer
   storage and byte-based modification callbacks, undo records, style buffers,
   range sets, and file I/O. Do not convert the whole buffer to wide characters.
   `BufGetCharacter` remains a byte accessor for compatibility; new helpers
   explicitly decode Unicode scalar values and move between their boundaries.
2. **Make encoding a buffer property.** Style buffers and existing single-byte
   documents retain their current behavior. Start with an opt-in UTF-8 mode;
   add a user-visible encoding preference only when its renderer is usable.
   Split views share the document's encoding. Later expose UTF-8 / legacy mode
   per document and a default for new documents. Do not silently reinterpret
   every non-ASCII legacy file as UTF-8.
3. **Preserve original bytes.** Decode strictly: reject overlong encodings,
   surrogates, values above U+10FFFF, bad continuations, and truncated sequences.
   For malformed input, consume one byte at a time and display a replacement
   glyph; retain the original bytes for save and undo. No implicit normalization
   or lossy transcoding. Continue supporting ASCII controls and the existing NUL
   substitution mechanism.
4. **Use one interpretation throughout drawing and editing.** First establish
   scalar-value boundaries, then add grapheme boundaries. Do not use a UTF-8
   continuation-byte heuristic as a decoder. Keep logical character movement
   separate from display width. Wide characters, tabs, and zero-width marks
   need explicit handling rather than counting encoded bytes as columns.
5. **Use a font backend abstraction.** Xft plus Fontconfig is the initial choice
   for the UTF-8 renderer. These libraries are installed in this workspace.
   Keep the existing X core-font renderer for legacy mode and builds without
   the new backend. Xft supports glyph drawing, not complex-script shaping or
   bidirectional layout; Pango/HarfBuzz and a bidi engine are later decisions.
6. **Keep compatibility boundaries visible in the code and documentation.**
   Existing macros may deliberately manipulate byte offsets. Do not globally
   snap every low-level buffer mutation, reinterpret their indices, or change
   `strlen` to a character count. Such changes would break style buffers and
   compatibility. Audit each user-facing operation and later introduce explicit
   Unicode-aware macro APIs.

## Phase 1: drawing, layout, and editing

### Milestone 1: shared UTF-8 primitives and editing boundaries

- Add a bounded decoder, previous/next/start/end boundary helpers, and tests
  covering ASCII, all UTF-8 sequence lengths, malformed input, and edge cases.
- Add opt-in UTF-8 mode and gap-buffer helpers that decode across the gap.
- Make Left/Right, Backspace/Delete, cursor placement, selection endpoints,
  vertical movement's column counts, and overstrike operate on whole scalars
  in UTF-8 mode. Keep existing byte behavior in legacy mode.
- Preserve byte counts in callbacks and undo. Verify restoration of multibyte
  edits and avoid changing undo grouping until it has dedicated coverage.
- Keep this mode internal until drawing and layout have been integrated. A
  byte-safe editing foundation is useful, but is not yet a usable UTF-8 UI.

### Milestone 2: UTF-8 font drawing

Primary files: `source/textDisp.c`, `source/textDisp.h`, `source/text.c`,
`source/textP.h`, `source/highlight.c`, `source/preferences.c`,
`source/window.c`, and the build makefiles.

- Introduce a font object that provides ascent, descent, space/column advance,
  text measurement, glyph drawing, style selection, and resource cleanup.
- Implement Xft font loading and Fontconfig fallback for missing glyphs. Match
  font choice, fallback, and advance calculations between measure and draw.
- Support normal, bold, italic, and bold-italic syntax styles. The existing
  parallel style buffer remains indexed by bytes; style transitions inside a
  UTF-8 scalar must not split its encoding into independently drawn fragments.
- Draw selections, background styles, underlines, range sets, and the cursor
  using the same advances as the text. Handle clipping, expose events,
  scrolling, split views, font changes, and resource ownership.
- Retain legacy XLFD preferences; add a separate UTF-8 font preference using
  Fontconfig names. Replacing the legacy font selector alone is insufficient.
- Show a missing-glyph indicator when no installed font contains a scalar;
  valid decoding cannot guarantee that every Unicode glyph is installed.

### Milestone 3: consistent layout and usable ordinary editing

Primary files: `source/textDisp.c`, `source/textBuf.c`, `source/text.c`,
`source/textDrag.c`, `source/shift.c`, `source/undo.c`, and `source/file.c`.

- Build a reusable line mapping from byte boundaries to columns and pixel
  advances. Use it for drawing, hit testing, selection, cursor positioning,
  vertical movement, horizontal scrolling, wrapping, and column reporting.
- Remove fixed-font shortcuts that assume every byte has the same advance.
  Soft wrapping and redraw ranges must never begin inside a UTF-8 scalar.
- Define tab stops and wide-character column semantics consistently. Add a
  Unicode property library for widths and grapheme segmentation rather than
  maintaining ad hoc tables. Decide how rectangular selections intersect wide
  characters and tabs; never cut a multibyte sequence to satisfy a column edge.
- Audit insertion, replacement, overstrike, deletion, word movement,
  double-click selection, rectangle operations, drag/overlay, shifting,
  paragraph fill, smart-indent hooks, and programmatic cursor changes.
- Handle combining marks and grapheme sequences as an explicit extension of
  scalar editing. Until then, scalar-safe editing may take multiple movements
  for one user-perceived character; document that limit accurately.
- Audit Motif input-manager handling and dynamic input buffers. The current
  input buffer is only 20 bytes and discards input on `XBufferOverflow`.
- Exercise save/reopen, undo/redo, autosave, and inserted files with UTF-8,
  malformed bytes, ASCII controls, tabs, and CRLF. Keep encoding unchanged.
- Add the document/default encoding preference when this milestone is usable;
  update help and release notes to describe precisely what is supported.

## Phase 2: clipboard, search, highlighting, and macros

- Negotiate `UTF8_STRING` for X PRIMARY and CLIPBOARD, with correct legacy
  `STRING` / `COMPOUND_TEXT` fallbacks. Audit secondary selection and Motif
  clipboard paths, not just ordinary paste.
- Make literal search boundary-safe. Exact UTF-8 byte matching is useful, but
  case-insensitive matching, word boundaries, and Unicode case conversion
  require Unicode properties. Do not normalize file text implicitly.
- Upgrade the proprietary regex engine so `.`, repetition, bracket classes,
  captures, and replacement operate on decoded characters where appropriate.
  Evaluate extending it versus adding an alternate engine without silently
  changing NEdit's regex dialect or existing syntax definitions.
- Audit incremental syntax reparsing and style-buffer synchronization. Existing
  ASCII patterns may work on UTF-8 bytes, but that is not Unicode regex support.
- Preserve existing macro offset/string behavior and add documented Unicode
  length, substring, traversal, and case APIs. Audit smart-indent and shell
  filters that invoke byte-based string operations.
- Update Find/Replace dialogs, selection-to-search paths, calltips, and relevant
  input/output conversions. Identify ways that old search, macros, or filters
  can still produce malformed UTF-8; do not claim they are fully supported
  before those paths have been migrated.

## Phase 3: broad multilingual layout

- Integrate grapheme segmentation, script shaping, Unicode line breaking,
  bidirectional layout, and visual/logical cursor mapping.
- Assess a Pango-based layout layer or HarfBuzz plus a bidi library. Keep the
  byte-to-layout mapping so selections, styles, and undo still refer to the
  original document bytes.
- Cover Arabic, Hebrew, Indic scripts, combining sequences, emoji/ZWJ
  sequences, fallback fonts, and input-method preedit/commit behavior.
- Extend printing/export and dialogs as needed; editor-area Unicode rendering
  alone does not automatically update every Motif widget or output path.

## Verification and acceptance

Use meaningful automated buffer/decoder tests and Xvfb-based widget integration
tests. Inspect actual glyph output and selection/cursor alignment visually;
successful compilation is not proof that layout works.

Test ASCII, accented Latin, Greek, Cyrillic, CJK, supplementary-plane scalars,
mixed text, tabs, controls, empty buffers, trailing newlines, long wrapped
lines, malformed and truncated sequences, and scalars spanning the gap.
Extend the corpus to combining marks, emoji sequences, and complex scripts as
their milestones are implemented.

Acceptance for the first usable phase:

- UTF-8 is decoded generally, with no special cases for particular punctuation.
- Drawing, measurement, mouse placement, keyboard movement, wrapping, and
  selection agree; ordinary editing never splits a valid UTF-8 scalar.
- Save/reopen preserves the original bytes outside intentional edits; malformed
  input remains recoverable. Undo/redo restores complete byte sequences.
- Legacy mode and style buffers retain their established behavior.
- Unicode search, macro behavior, rectangle/grapheme limits, and complex-script
  limitations are documented rather than implicitly promised.
- Representative large-file and long-line workloads remain responsive; avoid
  rescanning an entire document for each cursor move.

## Trying this implementation

Work is on branch `classic-UTF-8`. Build on Linux/Termux with `make linux`;
Xft and Fontconfig development files and `pkg-config` are required. Launch the
workspace binary explicitly:

```sh
./source/nedit -svrname '' -utf8 -utf8font 'monospace:size=12' path/to/file
```

The empty server name keeps this instance standalone, including when an X
resource normally enables server mode. It does not contact the running editor.

Use `-wrap` for wrapping at the window edge, or `-nowrap` to scroll long lines.
Use a UTF-8 locale (for example `C.UTF-8`) for Motif input. The `-utf8` option
selects UTF-8 interpretation for documents in that process; it does not transcode
file bytes. New tabs and split panes use the same encoding policy, and split
panes share the document buffer. Use `-8bit` for legacy documents.

The default remains legacy mode. Separate preferences are read/saved as:

```text
nedit.utf8: True
nedit.utf8Font: monospace:size=12
```

They can also be supplied using `-xrm`. The existing Font dialog continues to
configure legacy XLFD fonts; UTF-8 fonts currently use `-utf8font` or the resource.
A document encoding selector and a GUI Fontconfig font selector remain pending.
For a build without Xft use `make linux UTF8_XFT=0`; it retains the legacy
renderer and document mode. Backend switches trigger object rebuilding.
Other platform makefiles currently retain their legacy build configuration.

## Progress

Implemented and validated a first usable drawing/layout/editing increment:

- Strict bounded decoding for all Unicode scalar values, byte-preserving recovery
  for malformed input, and character boundaries across the gap buffer.
- Opt-in UTF-8 document buffers with byte offsets retained for undo, styles, file
  I/O, and existing application callbacks.
- Xft glyph drawing and Fontconfig fallback, including normal, bold, italic, and
  bold-italic styles. Missing installed glyphs use the font's missing-glyph box.
  Fallback fonts are reduced when necessary to fit the stable line height.
- A scalar line map used for drawing, width measurement, cursor positioning,
  hit testing, and pixel-aware vertical movement. Window soft wrapping uses the
  same glyph advances; explicit wrap margins count logical columns. Tabs and
  controls expand without interpreting their bytes as separate UTF-8 characters.
- Scalar-safe Left/Right, Backspace/Delete, plain selection endpoints, overstrike,
  configured-delimiter word movement/deletion, and hard auto-wrap at whitespace.
  Non-ASCII scalars currently count as one logical column even if glyph widths
  differ; this is not yet a terminal-style East Asian column-width policy.
- Selection backgrounds, syntax colors/faces/underlines, clipping, horizontal
  scroll measurement, resize, and separate Fontconfig font resource updates.
- Motif input lookup retries with a dynamically sized buffer on overflow. Full
  interactive input-method/preedit testing remains pending.
- Decoder/buffer/layout tests covering every Unicode scalar, malformed input,
  every gap position in mixed text, selection boundaries, and pixel mappings.
- Isolated Xvfb widget tests for real editing actions, undo/redo, glyph pixels,
  installed CJK/supplementary fallback glyphs, proportional hit testing/vertical
  movement, style transitions inside scalar bytes, selections, resize, wrapping,
  and legacy byte mode. A generated screenshot has been visually inspected.
- Full application edit/save/reopen checks preserving UTF-8, malformed bytes,
  and embedded NULs. Existing `appendLF` and line-ending preferences still apply;
  byte-preservation checks explicitly disable the automatic trailing newline.

Build/check commands are in `tests/README.md`. Existing compiler warnings remain
in older code. No installed binary or running editor is replaced by this work.

## Remaining work and limits of this increment

Phase 1 is not complete. In particular:

- Rectangular copy/replace/delete, column drag/overlay, paragraph fill, and shifts
  still contain byte-based column operations and need migration before they are
  reliable on non-ASCII text. Their old implementations can corrupt UTF-8;
  ordinary plain-selection editing is the supported path in this increment.
- Grapheme editing and Unicode width properties are pending. Combining sequences
  and emoji/ZWJ sequences may need several cursor moves/deletions. Shaping and
  bidirectional layout are absent; Arabic/Indic/Hebrew are not fully supported.
- Search/regex, case conversion, highlighting patterns, macros, and clipboard
  remain byte-oriented or use legacy X selection protocols. ASCII highlighting
  can work, but this is not Unicode regex support. Such operations may still
  construct invalid UTF-8; malformed bytes are rendered without being rewritten.
- Undo restores complete ordinary scalar edits; undo grouping retains its
  existing byte-count classification. Printing, calltips, and other Motif dialogs
  still use their previous rendering paths.
- Long-line performance needs further profiling and layout caching; this
  implementation creates temporary maps on demand and redraws complete visible
  rows. It avoids per-scalar document scans, but does not yet meet the final
  large-file responsiveness acceptance criterion.

The next implementation step is Unicode column/grapheme semantics and migration
of rectangular/column editing, followed by clipboard and search in phase 2.
