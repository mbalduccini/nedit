/* Copyright 2026. Distributed under the same GPL terms as NEdit.
   Measurement and drawing share shaped grapheme glyphs and font choices.
   Shaping stays within an editing grapheme; display order remains left to right. */
#include "utf8Font.h"
#include "utf8.h"
#include "../util/nedit_malloc.h"
#include <string.h>
#include <limits.h>
#ifdef HAVE_HARFBUZZ
#include <hb.h>
#include <hb-ft.h>
#include <unicode/uchar.h>
#endif

#ifdef HAVE_XFT
#include <X11/Xft/Xft.h>
#define MAX_FALLBACK 32
#define GLYPH_CACHE 256
typedef struct {
    unsigned long scalar;
    int style;
    XftFont *font;
} glyphChoice;
#ifdef HAVE_HARFBUZZ
#define SHAPE_CACHE 256
#define SHAPE_TEXT_LIMIT 128
typedef struct { unsigned int glyph; int x, y; } shapedGlyph;
typedef struct {
    char *text;
    int length, style, width, count;
    XftFont *face;
    shapedGlyph *glyphs;
} shapedCluster;
#endif
struct _utf8Font {
    Widget w;
    Display *display;
    int screen, ascent, descent, nFallback, nextChoice;
    XftFont *base[4], *fallback[MAX_FALLBACK];
    FcPattern *request[4];
    int fallbackStyle[MAX_FALLBACK];
    XftColor colors[16];
    int nColors, nextColor;
    glyphChoice choices[GLYPH_CACHE];
    XftDraw *draw;
#ifdef HAVE_HARFBUZZ
    shapedCluster shapes[SHAPE_CACHE];
#endif
};

utf8Font *Utf8FontOpen(Widget w, const char *name)
{
    utf8Font *font = (utf8Font *)NEditMalloc(sizeof(utf8Font));
    FcPattern *pattern, *match;
    FcResult result;
    int i;
    memset(font, 0, sizeof(*font));
    font->w = w;
    font->display = XtDisplay(w);
    font->screen = XScreenNumberOfScreen(XtScreen(w));
    for (i = 0; i < GLYPH_CACHE; i++) font->choices[i].style = -1;
    for (i = 0; i < 4; i++) {
        pattern = FcNameParse((const FcChar8 *)(name && *name
                ? name : "monospace:size=12"));
        if (!pattern) break;
        /* The font name supplies the family/size; syntax styles supply face. */
        FcPatternDel(pattern, FC_WEIGHT);
        FcPatternDel(pattern, FC_SLANT);
        FcPatternAddInteger(pattern, FC_WEIGHT,
                i & 1 ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
        FcPatternAddInteger(pattern, FC_SLANT,
                i & 2 ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
        font->request[i] = FcPatternDuplicate(pattern);
        match = XftFontMatch(font->display, font->screen, pattern, &result);
        if (match) font->base[i] = XftFontOpenPattern(font->display, match);
        FcPatternDestroy(pattern);
        if (!font->base[i]) break;
        if (font->base[i]->ascent > font->ascent)
            font->ascent = font->base[i]->ascent;
        if (font->base[i]->descent > font->descent)
            font->descent = font->base[i]->descent;
    }
    if (i != 4) {
        Utf8FontClose(font);
        return NULL;
    }
    return font;
}

void Utf8FontClose(utf8Font *font)
{
    int i;
    if (!font) return;
    if (font->draw) XftDrawDestroy(font->draw);
#ifdef HAVE_HARFBUZZ
    for (i = 0; i < SHAPE_CACHE; i++) {
        NEditFree(font->shapes[i].text);
        NEditFree(font->shapes[i].glyphs);
    }
#endif
    for (i = 0; i < 4; i++) {
        if (font->base[i]) XftFontClose(font->display, font->base[i]);
        if (font->request[i]) FcPatternDestroy(font->request[i]);
    }
    for (i = 0; i < font->nFallback; i++)
        XftFontClose(font->display, font->fallback[i]);
    NEditFree(font);
}

/* Bounded caches prevent documents with many missing scalars from allocating
   a font (or cache record) per character. Fontconfig supplies installed fallback
   families; a genuinely absent glyph uses the base font's missing-glyph box. */
static XftFont *glyphFont(utf8Font *font, unsigned long scalar, int style)
{
    XftFont *chosen = font->base[style];
    FcPattern *pattern, *match;
    FcFontSet *matches;
    FcCharSet *charset, *coverage;
    FcResult result;
    int i;
    if (XftCharExists(font->display, chosen, (FcChar32)scalar)) return chosen;
    for (i = 0; i < GLYPH_CACHE; i++)
        if (font->choices[i].style == style &&
                font->choices[i].scalar == scalar) return font->choices[i].font;
    for (i = 0; i < font->nFallback; i++) {
        if (font->fallbackStyle[i] == style &&
                XftCharExists(font->display, font->fallback[i], (FcChar32)scalar)) {
            chosen = font->fallback[i];
            break;
        }
    }
    if (i == font->nFallback && font->nFallback < MAX_FALLBACK) {
        pattern = FcPatternDuplicate(font->request[style]);
        charset = FcCharSetCreate();
        FcCharSetAddChar(charset, (FcChar32)scalar);
        FcPatternDel(pattern, FC_FILE);
        FcPatternDel(pattern, FC_INDEX);
        FcPatternDel(pattern, FC_CHARSET);
        FcPatternAddCharSet(pattern, FC_CHARSET, charset);
        FcCharSetDestroy(charset);
        FcConfigSubstitute(NULL, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);
        matches = FcFontSort(NULL, pattern, FcFalse, NULL, &result);
        if (matches) {
            int j;
            for (j = 0; j < matches->nfont; j++) {
                XftFont *candidate;
                if (FcPatternGetCharSet(matches->fonts[j], FC_CHARSET, 0,
                        &coverage) != FcResultMatch ||
                        !FcCharSetHasChar(coverage, (FcChar32)scalar)) continue;
                match = FcFontRenderPrepare(NULL, pattern, matches->fonts[j]);
                candidate = match ? XftFontOpenPattern(font->display, match) : NULL;
                if (!candidate) continue;
                /* Keep the line baseline and height stable when fallback
                   families have larger ascenders/descenders at the same size. */
                if (candidate->ascent > font->ascent ||
                        candidate->descent > font->descent) {
                    double size, scale = 1.0;
                    if (candidate->ascent > font->ascent)
                        scale = (double)font->ascent / candidate->ascent;
                    if (candidate->descent > font->descent &&
                            (double)font->descent/candidate->descent < scale)
                        scale = (double)font->descent/candidate->descent;
                    match = FcPatternDuplicate(candidate->pattern);
                    if (FcPatternGetDouble(match, FC_PIXEL_SIZE, 0, &size) == FcResultMatch) {
                        FcPatternDel(match, FC_PIXEL_SIZE);
                        FcPatternAddDouble(match, FC_PIXEL_SIZE, size * scale);
                        XftFontClose(font->display, candidate);
                        candidate = XftFontOpenPattern(font->display, match);
                    } else FcPatternDestroy(match);
                    if (!candidate) continue;
                }
                if (XftCharExists(font->display, candidate, (FcChar32)scalar)) {
                    font->fallbackStyle[font->nFallback] = style;
                    font->fallback[font->nFallback++] = candidate;
                    chosen = candidate;
                    break;
                }
                XftFontClose(font->display, candidate);
            }
            FcFontSetDestroy(matches);
        }
        FcPatternDestroy(pattern);
    }
    i = font->nextChoice++ % GLYPH_CACHE;
    font->choices[i].scalar = scalar;
    font->choices[i].style = style;
    font->choices[i].font = chosen;
    if (font->nextChoice == GLYPH_CACHE) font->nextChoice = 0;
    return chosen;
}

#ifdef HAVE_HARFBUZZ
/* Select one face for the whole grapheme so OpenType mark positioning and
   emoji substitutions can see all its characters. Color emoji presentation
   requests a color face; text presentation keeps the normal face priority. */
static int coversCluster(utf8Font *font, XftFont *face, const char *text, int length)
{
    int p = 0, n;
    unsigned long scalar;
    while (p < length) {
        n = Utf8Decode(text + p, length - p, &scalar, NULL);
        if (!Utf8IsIgnorable(scalar) && !XftCharExists(font->display, face, scalar))
            return 0;
        p += n;
    }
    return 1;
}
static XftFont *clusterFont(utf8Font *font, const char *text, int length, int style)
{
    int p = 0, n, i, color = 0, textPresentation = 0;
    unsigned long scalar;
    FcPattern *pattern, *match;
    FcFontSet *matches;
    FcCharSet *charset = FcCharSetCreate();
    FcResult result;
    XftFont *face = NULL;
    for (p = 0; p < length; p += n) {
        n = Utf8Decode(text + p, length - p, &scalar, NULL);
        if (scalar == 0xfe0e)
            textPresentation = 1;
        if (scalar == 0xfe0f || u_hasBinaryProperty(scalar, UCHAR_EMOJI_PRESENTATION))
            color = 1;
        if (!Utf8IsIgnorable(scalar))
            FcCharSetAddChar(charset, scalar);
    }
    color = color && !textPresentation;
    if (!color && coversCluster(font, font->base[style], text, length)) {
        FcCharSetDestroy(charset);
        return font->base[style];
    }
    for (i = 0; i < font->nFallback; i++) {
        FcBool isColor = FcFalse;
        FcPatternGetBool(font->fallback[i]->pattern, FC_COLOR, 0, &isColor);
        if (font->fallbackStyle[i] == style && (!color || isColor) &&
            coversCluster(font, font->fallback[i], text, length)) {
            FcCharSetDestroy(charset);
            return font->fallback[i];
        }
    }
    if (font->nFallback < MAX_FALLBACK) {
        pattern = FcPatternDuplicate(font->request[style]);
        FcPatternDel(pattern, FC_CHARSET);
        FcPatternAddCharSet(pattern, FC_CHARSET, charset);
        if (color)
            FcPatternAddBool(pattern, FC_COLOR, FcTrue);
        FcConfigSubstitute(NULL, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);
        matches = FcFontSort(NULL, pattern, FcFalse, NULL, &result);
        if (matches) {
            for (i = 0; i < matches->nfont; i++) {
                FcCharSet *coverage;
                if (FcPatternGetCharSet(matches->fonts[i], FC_CHARSET, 0, &coverage) !=
                        FcResultMatch ||
                    !FcCharSetIsSubset(charset, coverage))
                    continue;
                match = FcFontRenderPrepare(NULL, pattern, matches->fonts[i]);
                face = match ? XftFontOpenPattern(font->display, match) : NULL;
                if (!face)
                    continue;
                /* Match fallback to the stable line height, as for scalar fonts. */
                if (face->ascent > font->ascent || face->descent > font->descent) {
                    double size, scale = 1.0;
                    if (face->ascent > font->ascent)
                        scale = (double)font->ascent / face->ascent;
                    if (face->descent > font->descent &&
                        (double)font->descent / face->descent < scale)
                        scale = (double)font->descent / face->descent;
                    match = FcPatternDuplicate(face->pattern);
                    if (FcPatternGetDouble(match, FC_PIXEL_SIZE, 0, &size) ==
                        FcResultMatch) {
                        FcPatternDel(match, FC_PIXEL_SIZE);
                        FcPatternAddDouble(match, FC_PIXEL_SIZE, size * scale);
                        XftFontClose(font->display, face);
                        face = XftFontOpenPattern(font->display, match);
                    } else
                        FcPatternDestroy(match);
                }
                if (face && coversCluster(font, face, text, length)) {
                    font->fallbackStyle[font->nFallback] = style;
                    font->fallback[font->nFallback++] = face;
                    break;
                }
                if (face)
                    XftFontClose(font->display, face);
                face = NULL;
            }
            FcFontSetDestroy(matches);
        }
        FcPatternDestroy(pattern);
    }
    FcCharSetDestroy(charset);
    return face;
}
static void freeShape(shapedCluster *shape)
{
    NEditFree(shape->text);
    NEditFree(shape->glyphs);
    memset(shape, 0, sizeof(*shape));
}
/* HarfBuzz positions use 26.6 units; keep rounding symmetric and bounded
   before converting to Xft's int-coordinate API. */
static int shapePixels(long long value)
{
    value = value >= 0 ? (value + 32) / 64 : -((-value + 32) / 64);
    return value > INT_MAX ? INT_MAX : value < INT_MIN ? INT_MIN : (int)value;
}

static shapedCluster *shapeCluster(utf8Font *font, const char *text, int length,
                                   int style, shapedCluster *temporary)
{
    shapedCluster *shape = temporary;
    hb_font_t *hbFont;
    hb_buffer_t *buffer;
    hb_glyph_info_t *info;
    hb_glyph_position_t *positions;
    unsigned int count, i;
    FT_Face ftFace;
    long long x = 0, y = 0;
    XftFont *face;
    int slot;
    if (length <= SHAPE_TEXT_LIMIT) {
        /* A direct mapped hash keeps width/draw lookups constant-time. */
        unsigned int hash = style;
        for (slot = 0; slot < length; slot++)
            hash = hash * 33 + (unsigned char)text[slot];
        slot = hash % SHAPE_CACHE;
        shape = &font->shapes[slot];
        if (shape->text && shape->style == style && shape->length == length &&
            !memcmp(shape->text, text, length))
            return shape;
    }
    face = clusterFont(font, text, length, style);
    if (!face)
        return NULL; /* Existing per-scalar fallback handles partial coverage. */
    ftFace = XftLockFace(face);
    if (!ftFace)
        return NULL;
    hbFont = hb_ft_font_create_referenced(ftFace);
    hb_ft_font_set_load_flags(hbFont, FT_LOAD_DEFAULT | FT_LOAD_COLOR);
    buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, text, length, 0, length);
    hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
    hb_buffer_guess_segment_properties(buffer);
    hb_buffer_set_flags(buffer, HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES);
    hb_shape(hbFont, buffer, NULL, 0);
    info = hb_buffer_get_glyph_infos(buffer, &count);
    positions = hb_buffer_get_glyph_positions(buffer, NULL);
    freeShape(shape);
    shape->glyphs = NEditMalloc((count ? count : 1) * sizeof(shapedGlyph));
    shape->face = face;
    shape->count = count;
    shape->style = style;
    shape->length = length;
    for (i = 0; i < count; i++) {
        shape->glyphs[i].glyph = info[i].codepoint;
        shape->glyphs[i].x = shapePixels(x + positions[i].x_offset);
        shape->glyphs[i].y = shapePixels(-y - positions[i].y_offset);
        x += positions[i].x_advance;
        y += positions[i].y_advance;
    }
    shape->width = x < 0 ? 0 : shapePixels(x);
    if (shape != temporary) {
        shape->text = NEditMalloc(length);
        memcpy(shape->text, text, length);
    }
    hb_buffer_destroy(buffer);
    hb_font_destroy(hbFont);
    XftUnlockFace(face);
    return shape;
}
#endif

int Utf8FontHasGlyph(utf8Font *font, unsigned long scalar, int style)
{
    return XftCharExists(font->display, glyphFont(font, scalar, style), (FcChar32)scalar);
}

int Utf8FontAscent(utf8Font *font) { return font->ascent; }
int Utf8FontDescent(utf8Font *font) { return font->descent; }

int Utf8FontWidth(utf8Font *font, const char *text, int length, int style)
{
    int pos, consumed, width = 0;
    unsigned long scalar;
    FcChar32 value;
    XGlyphInfo extents;
#ifdef HAVE_HARFBUZZ
    if (length > 1 || (length == 1 && (unsigned char)text[0] >= 128)) {
        shapedCluster temporary = {0};
        shapedCluster *shape = shapeCluster(font, text, length, style, &temporary);
        if (shape) {
            width = shape->width;
            freeShape(&temporary);
            return width;
        }
    }
#endif
    for (pos = 0; pos < length; pos += consumed) {
        consumed = Utf8Decode(text + pos, length - pos, &scalar, NULL);
        if (Utf8CharWidth(scalar) == 0) continue;
        value = (FcChar32)scalar;
        XftTextExtents32(font->display, glyphFont(font, scalar, style),
                &value, 1, &extents);
        if (extents.xOff > INT_MAX - width) return INT_MAX;
        width += extents.xOff;
    }
    return width;
}

void Utf8FontDraw(utf8Font *font, Pixel pixel, int x, int baseline,
        const char *text, int length, int style,
        int clipX, int clipY, int clipWidth, int clipHeight)
{
    int pos, consumed, i, baseX = x;
    unsigned long scalar;
    FcChar32 value;
    XGlyphInfo extents;
    XftFont *face;
    XftColor color;
    XColor rgb;
    XWindowAttributes attrs;
    XRectangle clip;
    if (!XtIsRealized(font->w) || clipWidth <= 0 || clipHeight <= 0) return;
    if (!font->draw) {
        XGetWindowAttributes(font->display, XtWindow(font->w), &attrs);
        font->draw = XftDrawCreate(font->display, XtWindow(font->w),
                attrs.visual, attrs.colormap);
    }
    for (i = 0; i < font->nColors; i++)
        if (font->colors[i].pixel == pixel) break;
    if (i == font->nColors) {
        if (font->nColors < 16) font->nColors++;
        else { i = font->nextColor; font->nextColor = (i + 1) % 16; }
        rgb.pixel = pixel;
        XQueryColor(font->display, XftDrawColormap(font->draw), &rgb);
        font->colors[i].pixel = pixel;
        font->colors[i].color.red = rgb.red;
        font->colors[i].color.green = rgb.green;
        font->colors[i].color.blue = rgb.blue;
        font->colors[i].color.alpha = 65535;
    }
    color = font->colors[i];
    clip.x = clipX; clip.y = clipY;
    clip.width = clipWidth; clip.height = clipHeight;
    XftDrawSetClipRectangles(font->draw, 0, 0, &clip, 1);
#ifdef HAVE_HARFBUZZ
    if (length > 1 || (length == 1 && (unsigned char)text[0] >= 128)) {
        shapedCluster temporary = {0};
        shapedCluster *shape = shapeCluster(font, text, length, style, &temporary);
        if (shape) {
            for (i = 0; i < shape->count; i++) {
                FT_UInt glyph = shape->glyphs[i].glyph;
                int glyphX = shapePixels(((long long)x + shape->glyphs[i].x) * 64);
                int glyphY = shapePixels(((long long)baseline + shape->glyphs[i].y) * 64);
                XftDrawGlyphs(font->draw, &color, shape->face, glyphX, glyphY, &glyph, 1);
            }
            freeShape(&temporary);
            return;
        }
    }
#endif
    for (pos = 0; pos < length; pos += consumed) {
        consumed = Utf8Decode(text + pos, length - pos, &scalar, NULL);
        value = (FcChar32)scalar;
        face = glyphFont(font, scalar, style);
        if (Utf8CharWidth(scalar) == 0) {
            /* Default-ignorable scalars affect boundaries, not visible ink. */
            if (!Utf8IsIgnorable(scalar))
                XftDrawString32(font->draw, &color, face, baseX, baseline, &value, 1);
        } else {
            baseX = x;
            XftDrawString32(font->draw, &color, face, x, baseline, &value, 1);
            XftTextExtents32(font->display, face, &value, 1, &extents);
            x += extents.xOff;
        }
    }
}
#else
/* Preserve builds on platforms without Xft; UTF-8 rendering stays opt-in. */
utf8Font *Utf8FontOpen(Widget w, const char *name)
    { (void)w; (void)name; return NULL; }
void Utf8FontClose(utf8Font *font) { (void)font; }
int Utf8FontHasGlyph(utf8Font *font, unsigned long scalar, int style)
    { (void)font; (void)scalar; (void)style; return 0; }
int Utf8FontAscent(utf8Font *font) { (void)font; return 0; }
int Utf8FontDescent(utf8Font *font) { (void)font; return 0; }
int Utf8FontWidth(utf8Font *font, const char *text, int length, int style)
    { (void)font; (void)text; (void)length; (void)style; return 0; }
void Utf8FontDraw(utf8Font *font, Pixel color, int x, int baseline,
        const char *text, int length, int style,
        int clipX, int clipY, int clipWidth, int clipHeight)
    { (void)font; (void)color; (void)x; (void)baseline; (void)text;
      (void)length; (void)style; (void)clipX; (void)clipY;
      (void)clipWidth; (void)clipHeight; }
#endif
