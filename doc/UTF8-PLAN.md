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

- Extend the Phase 1 grapheme mapping with script shaping, Unicode line
  breaking, bidirectional layout, and visual/logical cursor mapping.
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
Xft, Fontconfig, utf8proc development files, and `pkg-config` are required. Launch the
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

They can also be supplied using `-xrm`. Preferences > UTF-8 Document switches
interpretation for the current document and all its panes, without transcoding
bytes or altering undo records. Preferences > Default Settings > UTF-8 by Default
sets the policy for newly opened documents. Save Defaults persists this setting.
Preferences > UTF-8 Font lists installed Fontconfig families and accepts a family
and size, with Apply and OK. The corresponding Default Settings entry sets the
font for new documents. Existing Text Fonts continues to configure legacy XLFD
fonts. Actions `set_encoding("utf8")`, `set_encoding("8bit")`, and
`set_utf8_font("monospace:size=12")` provide the same document controls.
For a build without Xft use `make linux UTF8_XFT=0`; it retains the legacy
renderer and document mode. Backend switches trigger object rebuilding.
Other platform makefiles currently retain their legacy build configuration.

## Phase 1 completion

Phase 1 is implemented for the Linux/Xft configuration, retaining a separate
legacy single-byte build. The following work is complete:

- Strict bounded UTF-8 decoding, scalar access across the gap, and byte-preserving
  malformed-input rendering; byte offsets remain the low-level API contract.
- Xft drawing with normal/bold/italic/bold-italic faces, Fontconfig fallback,
  clipping, colors, selections, underlines, range sets, and resource cleanup.
- utf8proc extended grapheme boundaries for ordinary cursor motion, deletion,
  selections, overstrike, mouse placement, configured-delimiter word editing,
  and wrapping. Combining sequences and emoji/ZWJ sequences are indivisible for
  these operations; this does not imply script shaping or emoji ligature drawing.
- Unicode columns: ordinary characters use utf8proc width, East Asian wide
  characters use two columns, ambiguous characters use one, and grapheme width
  is the maximum constituent scalar width. Combining marks use zero advance;
  tabs go to logical tab stops and ASCII controls retain their expanded labels.
  Pixels remain actual font advances, so proportional text is measured by glyphs.
- Rectangular copy, insert, delete, replace, overlay, drag/drop/cancel, column shifting,
  and paragraph filling preserve complete graphemes. The left rectangle edge
  rounds past a non-tab grapheme that straddles it; the right edge includes a
  straddling grapheme. Insertion into a wide grapheme snaps after it. Tabs may be
  split into padding. Copy/delete follow the same whole-character edge policy;
  overlay and overstrike preserve the remaining column extent with padding.
  A combining mark committed separately in overstrike mode joins its base
  without erasing the next grapheme. Wrapping skips whitespace with attached
  combining marks when choosing a whitespace break.
- Document/default encoding controls and a GUI Fontconfig font chooser; font and
  encoding settings are preserved in split panes and cloned documents.
- Motif committed-text input with dynamic retry on overflow, UTF-8 locale lookup,
  and cursor spot updates. Smart-indent callbacks retain their documented byte
  positions and intact UTF-8 strings. ASCII indentation rules remain unchanged.
- Save/reopen, CRLF conversion, include-file and incremental-backup checks,
  including malformed bytes and NULs. Existing line-ending and appendLF policies
  still apply; incremental backups use Unix newlines and append a final newline.
- A physical-line grapheme boundary cache plus up to 16 display-line maps, bounded
  by one MiB of source text except a single oversized line. Text, styles, fonts,
  tabs, buffer attachment and encoding changes invalidate affected measurements.
  Cursor redraw finds visible cells using binary search instead of traversing
  every offscreen cell.

## Phase 2 completion

Phase 2 is implemented for the Linux/Xft/utf8proc configuration.

- PRIMARY, CLIPBOARD and secondary/Motif destination transfers negotiate
  `UTF8_STRING`, `COMPOUND_TEXT` and Latin-1 `STRING`. `STRING` is now encoded
  correctly: unrepresentable Unicode is refused rather than mislabeled or
  replaced. Legacy document bytes are interpreted as Latin-1 at this protocol
  boundary; file storage is unchanged. Raw malformed bytes and embedded NULs
  remain recoverable through the editor's UTF8_STRING path. COMPOUND_TEXT is
  offered only when Xlib converts the text completely. Primary move requests
  deletion only after insertion and while the original owner still owns it.
- Literal searches start and end at scalar boundaries. Case-insensitive literal
  matching uses full Unicode case folding (for example `Straße` / `STRASSE`),
  with matches mapped back to original byte offsets. A fold expansion cannot
  be partially matched. Whole-word matching uses Unicode whitespace plus the
  configured word delimiters; forward/backward wrapping is covered. No search
  implicitly normalizes text: composed and decomposed accents remain distinct.
- The existing regex parser and VM are instantiated with Unicode scalar cells,
  preserving NEdit's dialect, switches, lookarounds, bounded/lazy quantifiers,
  captures, backreferences and replacement syntax. `.`, classes, ranges and
  repetition consume scalars; `\w` includes letters, marks, numbers and connector
  punctuation, `\d` includes Unicode decimal digits, and `\s` uses Unicode
  whitespace with the existing newline switches. Insensitive regex matching
  uses simple one-scalar folding: sigma variants match, but `ß` does not match
  the two regex characters `ss`. Regex numeric escapes retain their existing
  0..255 values, interpreted as scalars in UTF-8 mode. Invalid bytes each form
  a separate recoverable unit. Existing compiled-pattern and substitution size
  limits still apply; Unicode substitution truncation preserves scalar edges.
- Syntax patterns use the document's regex mode, including immediate/deferred
  parsing, captures for coloring, incremental reparsing and encoding changes.
  Style buffers stay byte-indexed. Character context distances count scalars
  in UTF-8 mode. A scoped decoded-subject map avoids decoding the full document
  again for each syntax match; maps are released before mutation.
- Find, Replace and incremental search fields and calltips use a native Motif
  Xft rendition matching the document font, restoring their original font in
  legacy mode. Selection-to-search, selected filenames and tag-selection paths
  negotiate text encodings. Motif still requires a UTF-8 locale.
- Upper/lower-case commands apply Unicode simple mappings to selected text or
  the preceding grapheme. Existing byte-based macro functions remain intact;
  explicit Unicode APIs are listed below. Bulk replacement shares decoded and
  folded subject maps. Shell filters continue exchanging original bytes, and
  bundled smart-indent programs retain byte offsets and their ASCII rules.

### Explicit Unicode macro APIs

| Function | Contract |
| --- | --- |
| `unicode_length(string)` | Count scalars; each malformed byte counts once |
| `unicode_substring(string, start [, end])` | Scalar indices, end exclusive; negative indices count from the end |
| `unicode_next(string, byteOffset)` / `unicode_previous(string, byteOffset)` | Return the next/previous scalar boundary as a byte offset |
| `unicode_grapheme_length(string)` | Count extended grapheme clusters |
| `unicode_next_grapheme(string, byteOffset)` / `unicode_previous_grapheme(string, byteOffset)` | Traverse extended grapheme boundaries and return byte offsets |
| `unicode_toupper(string)` / `unicode_tolower(string)` | Simple Unicode mappings, preserving malformed bytes |
| `unicode_casefold(string)` | Full Unicode folding, without normalization |

Traversal clamps offsets to the string. Next at end and previous at zero stay
there; an offset inside a scalar/grapheme moves to its enclosing end/start.
These functions require utf8proc; legacy-only builds report that requirement.
`search()` uses document interpretation automatically. `search_string()`,
`replace_in_string()` and `split()` accept an optional `"utf8"` argument alongside
their existing optional arguments; returned search/capture positions remain
bytes. For example:

```text
unicode_length("é中")                         # 2
unicode_substring("é中", 1)                   # "中"
search_string("Straße", "STRASSE", 0, "utf8")  # 0
replace_in_string("é中", "(.)", "<\\1>", "regex", "utf8")
```

`length()`, `substring()`, `replace_substring()`, document ranges and legacy
case functions retain their original contracts. Custom macros and smart-indent
programs that use these byte APIs can still split a sequence intentionally;
use the explicit traversal APIs when doing character edits. Shell commands
must themselves support UTF-8 if they transform text. The editor does not
repair, normalize or transcode their output.

## Verification

`tests/README.md` describes the build and checks. The automated corpus exercises
all Unicode scalars, malformed/truncated input, every gap position, grapheme and
rectangle boundaries, logical/pixel mappings, actual widget actions, undo/redo,
styles, fallback, scrolling, wrapping, encoding changes, split views, synthetic
Motif key lookup, long input commits, full application menus/font chooser,
drag completion with undo/redo, and file operations.
ASan/UBSan checks cover decoder/buffer/layout, column operations and both regex
engines. Phase 2 additionally checks foreign selection owners, encoding
fallbacks, full-fold and regex search/replacement, scalar syntax contexts,
calltip fonts, every explicit Unicode macro API and raw shell-filter output.
The legacy-only build passes an isolated byte-editing/search/save smoke test.

Representative workspace results: 1,000 cursor moves and hit tests on a 200 KB
line take about 0.18 CPU seconds after caching; a 2.2 MB mixed UTF-8 file
opens, edits and saves in roughly 0.6-1.2 seconds. These are local measurements,
not a performance guarantee for all fonts, files or machines. Long wrapped files
still use NEdit's established whole-document wrap counting.

## Follow-on work and supported limits

Phase 3 remains open:

- Grapheme-safe editing is implemented; shaping, bidirectional layout, Unicode
  line breaking and visual/logical navigation for complex scripts are not.
  Simple combining marks draw over their base; typographic mark attachment,
  Indic/Arabic shaping, emoji ligatures and bidirectional Hebrew need phase 3.
- Motif owns preedit display. UTF-8 committed input and overflow handling are
  covered; external IME-specific preedit presentations remain phase 3 work.
  Printing/export and Motif dialogs beyond the search fields and calltips still
  use their established font paths.
- Unicode case conversion uses simple mappings, without locale-specific Turkish
  casing or context-sensitive Greek final-sigma conversion. Literal search and
  `unicode_casefold()` support full folding; regex uses simple folding.
- Undo/redo preserves complete byte sequences. Undo grouping still classifies
  single-character operations by byte count, so multibyte typing can create
  smaller undo groups than ASCII typing. This remains follow-on behavior.
- Custom byte-oriented macros, external filters and old clients publishing raw
  UTF-8 under `STRING` can produce unexpected or malformed text. The protocol
  now honors Latin-1 `STRING`; old mislabeled bytes are not guessed as UTF-8.
  Original malformed document bytes remain recoverable.

No installed editor or running server is replaced by these changes.
