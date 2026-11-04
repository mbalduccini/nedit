#include "utf8.h"
#include "textBuf.h"
#include "utf8Layout.h"
#include "../util/nedit_malloc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

static int encode(unsigned long scalar, char bytes[4])
{
    if (scalar < 0x80) {
        bytes[0] = scalar;
        return 1;
    }
    if (scalar < 0x800) {
        bytes[0] = 0xc0 | (scalar >> 6);
        bytes[1] = 0x80 | (scalar & 0x3f);
        return 2;
    }
    if (scalar < 0x10000) {
        bytes[0] = 0xe0 | (scalar >> 12);
        bytes[1] = 0x80 | ((scalar >> 6) & 0x3f);
        bytes[2] = 0x80 | (scalar & 0x3f);
        return 3;
    }
    bytes[0] = 0xf0 | (scalar >> 18);
    bytes[1] = 0x80 | ((scalar >> 12) & 0x3f);
    bytes[2] = 0x80 | ((scalar >> 6) & 0x3f);
    bytes[3] = 0x80 | (scalar & 0x3f);
    return 4;
}

static void testDecoder(void)
{
    unsigned long scalar, decoded;
    char bytes[4];
    int length, valid, i;
    const char *invalid[] = {
        "\x80", "\xbf", "\xc0\xaf", "\xc1\xbf", "\xc2", "\xc2X",
        "\xe0\x80\x80", "\xed\xa0\x80", "\xed\xbf\xbf",
        "\xe2\x82", "\xe2X\xac", "\xe2\x82X", "\xf0\x80\x80\x80",
        "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xff",
        "\xf0\x90\x80", "\xf0\x90X\x80", "\xf0\x90\x80X"
    };

    /* Exercise every Unicode scalar, not a handful of punctuation samples. */
    for (scalar = 0; scalar <= 0x10ffff; scalar++) {
        if (scalar >= 0xd800 && scalar <= 0xdfff) continue;
        length = encode(scalar, bytes);
        assert(Utf8Decode(bytes, length, &decoded, &valid) == length);
        assert(valid && decoded == scalar);
        for (i = 1; i < length; i++) {
            assert(Utf8Decode(bytes, i, &decoded, &valid) == 1);
            assert(!valid && decoded == 0xfffd);
        }
    }
    for (i = 0; i < (int)(sizeof(invalid) / sizeof(invalid[0])); i++) {
        assert(Utf8Decode(invalid[i], strlen(invalid[i]), &decoded, &valid) == 1);
        assert(!valid && decoded == 0xfffd);
    }
    assert(Utf8Decode(NULL, 0, &decoded, &valid) == 0);
    assert(decoded == 0 && !valid);
}

static void assertContents(textBuffer *buf, const char *expected)
{
    char *actual = BufGetAll(buf);
    if (buf->length != (int)strlen(expected) || strcmp(actual, expected))
        fprintf(stderr, "Actual: [%s] (%d), expected: [%s] (%lu)\n", actual, buf->length, expected, (unsigned long)strlen(expected));
    assert(buf->length == (int)strlen(expected));
    assert(strcmp(actual, expected) == 0);
    NEditFree(actual);
}

static void testBoundaries(void)
{
    const char text[] = "A\xc3\xa9\xe4\xb8\xad\xf0\x90\x90\x80Z";
    const int boundaries[] = {0, 1, 3, 6, 10, 11};
    const int columns[] = {0, 1, 2, 4, 5, 6};
    const unsigned long scalars[] = {'A', 0xe9, 0x4e2d, 0x10400, 'Z'};
    textBuffer *buf = BufCreate();
    unsigned long scalar;
    int i, p, gap, valid;
    char *selected;

    assert(buf->encoding == BUF_ENCODING_8BIT);
    BufSetAll(buf, text);
    assert(BufNextCharacter(buf, 1) == 2);
    assert(BufPrevCharacter(buf, 3) == 2);
    assert(BufCountDispChars(buf, 0, 11) == 11);
    BufSetEncoding(buf, BUF_ENCODING_UTF8);
    /* Empty insertion moves the gap; every physical gap position is tested. */
    for (gap = 0; gap <= buf->length; gap++) {
        BufInsert(buf, gap, "");
        assertContents(buf, text);
        for (i = 0; i < 5; i++) {
            assert(BufGetCodepoint(buf, boundaries[i], &scalar, &valid)
                    == boundaries[i + 1] - boundaries[i]);
            assert(valid && scalar == scalars[i]);
            assert(BufNextCharacter(buf, boundaries[i]) == boundaries[i + 1]);
            assert(BufPrevCharacter(buf, boundaries[i + 1]) == boundaries[i]);
            for (p = boundaries[i]; p < boundaries[i + 1]; p++) {
                assert(BufStartOfCharacter(buf, p) == boundaries[i]);
                assert(BufEndOfCharacter(buf, p)
                        == (p == boundaries[i] ? p : boundaries[i + 1]));
                if (p > boundaries[i])
                    assert(BufPrevCharacter(buf, p) == boundaries[i]);
            }
            assert(BufCountDispChars(buf, 0, boundaries[i + 1]) == columns[i + 1]);
            assert(BufCountForwardDispChars(buf, 0, columns[i + 1]) == boundaries[i + 1]);
        }
    }
    assert(BufPrevCharacter(buf, 0) == 0);
    assert(BufNextCharacter(buf, buf->length) == buf->length);
    assert(BufStartOfCharacter(buf, -5) == 0);
    assert(BufEndOfCharacter(buf, 100) == buf->length);
    BufSelect(buf, 2, 8);
    assert(buf->primary.start == 1 && buf->primary.end == 10);
    selected = BufGetSelectionText(buf);
    assert(strcmp(selected, "\xc3\xa9\xe4\xb8\xad\xf0\x90\x90\x80") == 0);
    NEditFree(selected);
    BufReplaceSelected(buf, "\xce\xb2");
    assertContents(buf, "A\xce\xb2Z");
    BufSecondarySelect(buf, 2, 2);
    assert(!buf->secondary.selected && buf->secondary.start == 1);
    BufSecondarySelect(buf, 3, 2);
    assert(buf->secondary.start == 1 && buf->secondary.end == 3);
    BufFree(buf);
}

static void testMalformedAndColumns(void)
{
    textBuffer *buf = BufCreate();
    const char invalid[] = "\x80\xc0\xaf\xed\xa0\x80\xf4\x90\x80\x80\xe2\x82";
    const char binary[] = {'A', (char)0xc3, (char)0xa9, '\0', (char)0x80};
    char withNull[sizeof(binary) + 1], *restored;
    int p;

    BufSetEncoding(buf, BUF_ENCODING_UTF8);
    BufSetAll(buf, invalid);
    for (p = 0; p < buf->length; p++) {
        assert(BufStartOfCharacter(buf, p) == p);
        assert(BufNextCharacter(buf, p) == p + 1);
        assert(BufPrevCharacter(buf, p + 1) == p);
    }
    assertContents(buf, invalid);
    /* A bad prefix must not prevent finding a subsequent valid scalar. */
    BufSetAll(buf, "\xe2\xc3\xa9\x80");
    assert(BufNextCharacter(buf, 0) == 1);
    assert(BufNextCharacter(buf, 1) == 3);
    assert(BufStartOfCharacter(buf, 2) == 1);
    assert(BufPrevCharacter(buf, 3) == 1);
    assert(BufNextCharacter(buf, 3) == 4);
    assert(BufPrevCharacter(buf, 4) == 3);
    BufSetAll(buf, "\xc3\xa9\t\xe4\xb8\xad\nZ\x01");
    assert(BufCountDispChars(buf, 0, 6) == 10);
    assert(BufCountForwardDispChars(buf, 0, 2) == 3);
    assert(BufCountForwardDispChars(buf, 0, 10) == 6);
    assert(BufCountForwardDispChars(buf, 0, 100) == 6);
    assert(BufCountDispChars(buf, 7, 9) == 6); /* Z + <soh> */
    /* UTF-8 interpretation must not lose the existing binary/NUL round trip. */
    memcpy(withNull, binary, sizeof(binary));
    withNull[sizeof(binary)] = '\0';
    assert(BufSubstituteNullChars(withNull, sizeof(binary), buf));
    BufSetAll(buf, withNull);
    assert(buf->length == sizeof(binary));
    assert(BufCodepointWidth(buf, 3, 2) == 5); /* <nul> */
    restored = BufGetAll(buf);
    BufUnsubstituteNullChars(restored, buf);
    assert(memcmp(restored, binary, sizeof(binary)) == 0);
    NEditFree(restored);
    BufFree(buf);
}

/* Deliberately different advances catch byte-count-as-width mistakes. */
static int measure(const char *text, int length, int byteStart, void *arg)
{
    unsigned long scalar;
    int pos = 0, width = 0;
    (void)byteStart;
    if (arg != NULL) return *(int *)arg;
    while (pos < length) {
        pos += Utf8Decode(text + pos, length - pos, &scalar, NULL);
        width += scalar < 0x80 ? 7 : scalar == 0x4e2d ? 14 : 10;
    }
    return width;
}

static void testLayout(void)
{
    const char text[] = "A\xc3\xa9\t\xe4\xb8\xad\xf0\x90\x90\x80\x80";
    utf8LineLayout *layout = Utf8LayoutCreate(text, strlen(text), 8, '\0',
            measure, NULL);
    const int bytes[] = {0, 1, 3, 4, 7, 11, 12};
    const int pixels[] = {0, 7, 17, 59, 73, 83, 93};
    int i, p, overflow = INT_MAX;

    assert(layout != NULL);
    assert(layout->nCells == 6 && layout->columns == 12 && layout->width == 93);
    for (i = 0; i < 6; i++) {
        assert(layout->cells[i].byteStart == bytes[i]);
        assert(layout->cells[i].byteEnd == bytes[i + 1]);
        for (p = bytes[i]; p < bytes[i + 1]; p++)
            assert(Utf8LayoutByteToX(layout, p) == pixels[i]);
        assert(Utf8LayoutXToByte(layout, pixels[i], 1) == bytes[i]);
        assert(Utf8LayoutXToByte(layout, pixels[i + 1] - 1, 0) == bytes[i]);
    }
    assert(Utf8LayoutByteToX(layout, 100) == 93);
    assert(Utf8LayoutXToByte(layout, -1, 1) == 0);
    assert(Utf8LayoutXToByte(layout, 200, 1) == 12);
    assert(layout->cells[5].renderLength == 3);
    assert(memcmp(layout->cells[5].renderText, "\xef\xbf\xbd", 3) == 0);
    Utf8LayoutFree(layout);
    assert(Utf8LayoutCreate("ab", 2, 8, '\0', measure, &overflow) == NULL);
    assert(Utf8LayoutCreate("a\nb", 3, 8, '\0', measure, NULL) == NULL);
    assert(Utf8LayoutCreate("a", 1, 0, '\0', measure, NULL) == NULL);
    layout = Utf8LayoutCreate("a\tb", 3, 100, '\0', measure, NULL);
    assert(layout && layout->columns == 101 && layout->width == 707);
    assert(layout->cells[1].renderLength == 1);
    Utf8LayoutFree(layout);
    layout = Utf8LayoutCreate(NULL, 0, 8, '\0', measure, NULL);
    assert(layout != NULL && layout->width == 0 && layout->nCells == 0);
    assert(Utf8LayoutXToByte(layout, 0, 1) == 0);
    Utf8LayoutFree(layout);
}

static void testGraphemesAndRectangles(void)
{
    const char *text = "Ae\xcc\x81\xe4\xb8\xad\xf0\x9f\x87\xba\xf0\x9f\x87\xb8\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbbZ";
    const int boundaries[] = {0, 1, 4, 7, 15, 26, 27};
    textBuffer *buf = BufCreate();
    char *copy;
    int i, gap, p, valid;
    unsigned long scalar;
    BufSetEncoding(buf, BUF_ENCODING_UTF8);
    BufSetAll(buf, text);
    for (gap = 0; gap <= buf->length; gap++) {
        BufInsert(buf, gap, "");
        for (i = 0; i < 6; i++) {
            assert(BufNextGrapheme(buf, boundaries[i]) == boundaries[i + 1]);
            assert(BufPrevGrapheme(buf, boundaries[i + 1]) == boundaries[i]);
            for (p = boundaries[i]; p < boundaries[i + 1]; p++)
                assert(BufStartOfGrapheme(buf, p) == boundaries[i]);
        }
    }
    assert(BufCountDispChars(buf, 0, 7) == 4);
    BufSelect(buf, 2, 3);
    assert(buf->primary.start == 1 && buf->primary.end == 4);
    BufSetAll(buf, "\xe4\xb8\xad\tX\ne\xcc\x81\tY");
    buf->useTabs = 0;
    copy = BufGetTextInRect(buf, 0, buf->length, 0, 2);
    assert(!strcmp(copy, "\xe4\xb8\xad\ne\xcc\x81"));
    NEditFree(copy);
    BufRemoveRect(buf, 0, buf->length, 0, 2);
    assertContents(buf, "      X\n      Y");
    BufSetAll(buf, "A\xe4\xb8\xadZ\nAe\xcc\x81Z");
    BufInsertCol(buf, 1, 0, "\xc3\xa9\n\xce\xb2", NULL, NULL);
    assertContents(buf, "A\xc3\xa9\xe4\xb8\xadZ\nA\xce\xb2" "e\xcc\x81Z");
    BufReplaceRect(buf, 0, buf->length, 1, 2, "Q\nR");
    assertContents(buf, "AQ\xe4\xb8\xadZ\nAR" "e\xcc\x81Z");
    BufOverlayRect(buf, 0, 1, 3, "\xc3\xa9\n\xce\xb2", NULL, NULL);
    assertContents(buf, "A\xc3\xa9  Z\nA\xce\xb2 Z");
    /* Test every rectangle edge on mixed wide and combining text. */
    for (i = 0; i < 12; i++) for (gap = i; gap < 12; gap++) {
        BufSetAll(buf, "e\xcc\x81\xe4\xb8\xad\tZ\n\xe4\xb8\xad" "e\xcc\x81\tQ");
        BufRemoveRect(buf, 0, buf->length, i, gap);
        for (p = 0; p < buf->length;) {
            p += BufGetCodepoint(buf, p, &scalar, &valid);
            assert(valid);
        }
    }
    BufSetAll(buf, "A\nB\n");
    assert(BufNextGrapheme(buf, 1) == 2);
    assert(BufPrevGrapheme(buf, 2) == 1);
    assert(BufNextGrapheme(buf, 3) == 4);
    BufSetAll(buf, "a\x80\xcc\x81");
    assert(BufNextGrapheme(buf, 1) == 2);
    assert(BufNextGrapheme(buf, 2) == 4);
    BufSetAll(buf, "");
    copy = BufGetTextInRect(buf, 0, 0, 0, 0);
    assert(!strcmp(copy, "")); NEditFree(copy);
    BufSetAll(buf, "e\xcc\x81\tZ"); BufSetTabDistance(buf, 100);
    BufInsertCol(buf, 50, 0, "中", NULL, NULL);
    copy = BufGetAll(buf);
    assert(strstr(copy, "中") != NULL);
    NEditFree(copy); BufSetTabDistance(buf, 8);
    BufSetAll(buf, "e\xcc\x81\x80\xcc\x81");
    BufInsertCol(buf, 1, 0, "Q", NULL, NULL);
    assertContents(buf, "e\xcc\x81Q\x80\xcc\x81");
    {
        utf8LineLayout *map = Utf8LayoutCreate("e\xcc\x81\xe4\xb8\xad", 6, 8, 0, measure, NULL);
        assert(map && map->nCells == 2 && map->columns == 3);
        assert(Utf8LayoutXToByte(map, 1, 0) == 0);
        assert(Utf8LayoutByteToX(map, 2) == 0);
        Utf8LayoutFree(map);
    }
    /* Graphemes have no fixed byte limit; render storage must grow with them. */
    {
        char longCluster[603];
        utf8LineLayout *map;
        longCluster[0] = 'e';
        for (i = 0; i < 300; i++) memcpy(longCluster + 1 + 2*i, "\xcc\x81", 2);
        longCluster[601] = 'Z'; longCluster[602] = '\0';
        BufSetAll(buf, longCluster);
        assert(BufNextGrapheme(buf, 0) == 601 && BufPrevGrapheme(buf, 601) == 0);
        map = Utf8LayoutCreate(longCluster, 602, 8, 0, measure, NULL);
        assert(map && map->nCells == 2 && map->cells[0].renderLength == 601);
        assert(!memcmp(map->cells[0].renderText, longCluster, 601));
        Utf8LayoutFree(map);
    }
    BufFree(buf);
}

int main(void)
{
    testDecoder();
    testBoundaries();
    testMalformedAndColumns();
    testLayout();
    testGraphemesAndRectangles();
    puts("UTF-8 decoder, gap-buffer, and line-layout tests passed (all Unicode scalars).");
    return 0;
}
