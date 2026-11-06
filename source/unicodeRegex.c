/* Copyright 2026. Distributed under the same GPL terms as NEdit. */
/* The established parser/VM is instantiated with scalar cells. Its arithmetic
   then counts characters, including bounded lookbehind and quantifier backtrack.
   The adapter maps captures and extents back to original UTF-8 byte addresses. */
#include "unicodeRegex.h"
#include "utf8.h"
#include "../util/nedit_malloc.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#ifdef HAVE_UTF8PROC
#include <utf8proc.h>
typedef unsigned int REChar;
typedef unsigned int REInputChar;
typedef unsigned int REContextChar;
typedef struct {
    REChar *startp[NSUBEXP], *endp[NSUBEXP], *extentpBW, *extentpFW;
    int top_branch;
    REChar match_start, anchor;
    REChar program[1];
} wideRegexp;

static int uLetter(REChar c)
{
    int k = utf8proc_category(c);
    return k >= UTF8PROC_CATEGORY_LU && k <= UTF8PROC_CATEGORY_LO;
}
static int uDigit(REChar c) { return utf8proc_category(c) == UTF8PROC_CATEGORY_ND; }
static int uWord(REChar c)
{
    int k = utf8proc_category(c);
    return uLetter(c) || (k >= UTF8PROC_CATEGORY_MN && k <= UTF8PROC_CATEGORY_ME) ||
           (k >= UTF8PROC_CATEGORY_ND && k <= UTF8PROC_CATEGORY_NO) ||
           k == UTF8PROC_CATEGORY_PC;
}
static int uSpace(REChar c)
{
    int k = utf8proc_category(c);
    return (c >= 9 && c <= 13) || c == 0x85 || k == UTF8PROC_CATEGORY_ZS ||
           k == UTF8PROC_CATEGORY_ZL || k == UTF8PROC_CATEGORY_ZP;
}
static REChar uFold(REChar c)
{
    utf8proc_int32_t out[4], lowerOut[4];
    utf8proc_ssize_t n = utf8proc_decompose_char(c, out, 4, UTF8PROC_CASEFOLD, NULL);
    REChar lower;
    if (n == 1)
        return out[0];
    lower = utf8proc_tolower(c);
    /* Capital sharp s has a simple lowercase pair with the same full fold.
       Dotted capital I does not: its two-scalar fold must stay distinct. */
    if (n > 1 && n <= 4 && lower != c &&
        utf8proc_decompose_char(lower, lowerOut, 4, UTF8PROC_CASEFOLD, NULL) == n &&
        !memcmp(out, lowerOut, n * sizeof(out[0])))
        return lower;
    return c;
}
static size_t reLength(const REChar *s)
{
    const REChar *p = s;
    while (*p)
        p++;
    return p - s;
}
static REChar *reFind(const REChar *s, REChar c)
{
    do {
        if (*s == c)
            return (REChar *)s;
    } while (*s++);
    return NULL;
}
static int reCompare(const REChar *a, const REChar *b, size_t n)
{
    while (n--) {
        if (*a != *b)
            return 1;
        if (!*a)
            return 0;
        a++;
        b++;
    }
    return 0;
}
/* Class property tokens are outside scalars and byte-recovery tokens. */
#define UCLASS_BASE 0x120000U
#define UCLASS_FOLD (UCLASS_BASE + 10)
#define UCLASS_RANGE (UCLASS_BASE + 11)
#define UCLASS_RANGE_IC (UCLASS_BASE + 12)
static int classProperty(REChar token, REChar c)
{
    int answer;
    switch ((token - UCLASS_BASE) / 2) {
    case 0:
        answer = uDigit(c);
        break;
    case 1:
        answer = uLetter(c);
        break;
    case 2:
        answer = uWord(c);
        break;
    case 3:
        answer = uSpace(c) && c != '\n';
        break;
    default:
        answer = uSpace(c) && c != '\n';
        break;
    }
    return ((token - UCLASS_BASE) & 1)
               ? !answer && (c != '\n' || (token - UCLASS_BASE) / 2 == 4)
               : answer;
}
/* A small inverse simple-fold table makes even very broad insensitive ranges
   constant/logarithmic work per subject scalar. Build it once, lazily. */
typedef struct {
    REChar fold, scalar;
} foldPair;
static foldPair *foldPairs;
static int nFoldPairs;
static int compareFoldPairs(const void *a, const void *b)
{
    const foldPair *left = a, *right = b;
    return left->fold < right->fold ? -1 : left->fold > right->fold;
}
static int insensitiveRange(REChar c, REChar low, REChar high)
{
    REChar folded = uFold(c), k, f;
    int lo = 0, hi, mid, capacity = 2048;
    if ((c >= low && c <= high) || (folded >= low && folded <= high))
        return 1;
    if (!foldPairs) {
        foldPairs = NEditMalloc(capacity * sizeof(foldPair));
        for (k = 1; k <= 0x10ffff; k++) {
            f = uFold(k);
            if (f == k)
                continue;
            if (nFoldPairs == capacity) {
                capacity *= 2;
                foldPairs = NEditRealloc(foldPairs, capacity * sizeof(foldPair));
            }
            foldPairs[nFoldPairs].fold = f;
            foldPairs[nFoldPairs++].scalar = k;
        }
        qsort(foldPairs, nFoldPairs, sizeof(foldPair), compareFoldPairs);
    }
    hi = nFoldPairs;
    while (lo < hi) {
        mid = (lo + hi) / 2;
        if (foldPairs[mid].fold < folded)
            lo = mid + 1;
        else
            hi = mid;
    }
    while (lo < nFoldPairs && foldPairs[lo].fold == folded) {
        if (foldPairs[lo].scalar >= low && foldPairs[lo].scalar <= high)
            return 1;
        lo++;
    }
    return 0;
}
static REChar *reClassFind(const REChar *s, REChar c)
{
    while (*s) {
        if (*s == UCLASS_FOLD) {
            if (uFold(c) == uFold(s[1]))
                return (REChar *)s;
            s += 2;
        } else if (*s == UCLASS_RANGE || *s == UCLASS_RANGE_IC) {
            if (*s == UCLASS_RANGE_IC ? insensitiveRange(c, s[1], s[2])
                                      : c >= s[1] && c <= s[2])
                return (REChar *)s;
            s += 3;
        } else {
            if ((*s >= UCLASS_BASE && *s < UCLASS_BASE + 10) ? classProperty(*s, c)
                                                             : *s == c)
                return (REChar *)s;
            s++;
        }
    }
    return NULL;
}
static const REChar *unicodeDelimiters;
static REChar *defaultUnicodeDelimiters;
static int reDelimiter(REChar c)
{
    return !c || uSpace(c) || (unicodeDelimiters && reFind(unicodeDelimiters, c)) ||
           (!unicodeDelimiters && !uWord(c));
}
#define NEDIT_WIDE_REGEX 1
#define regexp wideRegexp
#define CompileRE compileWideRE
#define ExecRE execWideRE
#define SetREDefaultWordDelimiters setWideDelimiters
#define isalpha(c) uLetter(c)
#define isalnum(c) uWord(c)
#define isdigit(c) uDigit(c)
#define isspace(c) uSpace(c)
#define tolower(c) uFold(c)
#define toupper(c) utf8proc_toupper(c)
#include "regularExp.c"
#undef regexp
#undef CompileRE
#undef ExecRE
#undef SetREDefaultWordDelimiters
#undef isalpha
#undef isalnum
#undef isdigit
#undef isspace
#undef tolower
#undef toupper

/* Invalid bytes get distinct out-of-Unicode cells and map back unchanged. */
static REChar *decode(const char *s, int length, int **offsets, int *count)
{
    int p = 0, n = 0, valid, step;
    unsigned long c;
    REChar *out = NEditMalloc(((size_t)length + 1) * sizeof(REChar));
    int *map = offsets ? NEditMalloc(((size_t)length + 1) * sizeof(int)) : NULL;
    while (p < length) {
        step = Utf8Decode(s + p, length - p, &c, &valid);
        if (map)
            map[n] = p;
        out[n++] = valid ? c : 0x110000U + (unsigned char)s[p];
        p += step;
    }
    out[n] = 0;
    if (map)
        map[n] = length;
    if (offsets)
        *offsets = map;
    if (count)
        *count = n;
    return out;
}
/* Only explicit immutable-subject scopes retain decoded text. In particular,
   syntax parsing shares one decode across all its pattern matches; no cache
   survives a mutation, a temporary terminator, or the scope's lifetime. */
typedef struct regexSubject {
    const char *base;
    REChar *cells;
    int *map, count, references;
    struct regexSubject *previous;
} regexSubject;
static regexSubject *subjects;
void BeginUtf8RegexSubject(const char *base)
{
    regexSubject *subject;
    if (subjects && subjects->base == base) {
        subjects->references++;
        return;
    }
    subject = NEditNew(regexSubject);
    subject->base = base;
    subject->cells = decode(base, strlen(base), &subject->map, &subject->count);
    subject->references = 1;
    subject->previous = subjects;
    subjects = subject;
}
void EndUtf8RegexSubject(void)
{
    regexSubject *subject = subjects;
    if (!subject || --subject->references)
        return;
    subjects = subject->previous;
    NEditFree(subject->cells);
    NEditFree(subject->map);
    NEditFree(subject);
}
void SetUtf8REDefaultWordDelimiters(const char *text)
{
    NEditFree(defaultUnicodeDelimiters);
    defaultUnicodeDelimiters = text ? decode(text, strlen(text), NULL, NULL) : NULL;
}
static int indexAt(int *map, int n, int offset, int roundUp)
{
    int lo = 0, hi = n, mid;
    if (offset <= 0)
        return 0;
    while (lo < hi) {
        mid = (lo + hi) / 2;
        if (map[mid] < offset)
            lo = mid + 1;
        else
            hi = mid;
    }
    return !roundUp && map[lo] > offset && lo ? lo - 1 : lo;
}
static wideRegexp *wideProgram(const regexp *p)
{
    size_t alignment = sizeof(void *),
           offset = (sizeof(regexp) + alignment - 1) & ~(alignment - 1);
    return (wideRegexp *)((char *)p + offset);
}
regexp *CompileUtf8RE(const char *pattern, char **error, int flags)
{
    REChar *cells;
    wideRegexp *wide;
    regexp *out;
    size_t alignment = sizeof(void *),
           offset = (sizeof(regexp) + alignment - 1) & ~(alignment - 1), size;
    if (!pattern) {
        *error = "NULL regular expression";
        return NULL;
    }
    cells = decode(pattern, strlen(pattern), NULL, NULL);
    wide = compileWideRE(cells, error, flags & ~REDFLT_UTF8);
    NEditFree(cells);
    if (!wide)
        return NULL;
    size = sizeof(wideRegexp) + Reg_Size * sizeof(REChar);
    out = NEditCalloc(1, offset + size);
    out->utf8 = 1;
    memcpy(wideProgram(out), wide, size);
    NEditFree(wide);
    return out;
}
int ExecUtf8RE(regexp *p, const char *string, const char *end, int reverse,
               unsigned int prev, unsigned int succ, const char *delimiters,
               const char *behind, const char *till)
{
    const char *base = behind ? behind : string;
    int *map, n, i, start, finish, limit, found;
    int cached = subjects && subjects->base == base;
    REChar *cells,
        *d = delimiters ? decode(delimiters, strlen(delimiters), NULL, NULL) : NULL;
    wideRegexp *wide = wideProgram(p);
    if (cached) {
        cells = subjects->cells;
        map = subjects->map;
        n = subjects->count;
    } else
        cells = decode(base, strlen(base), &map, &n);
    memset(wide->startp, 0, sizeof(wide->startp));
    memset(wide->endp, 0, sizeof(wide->endp));
    start = indexAt(map, n, string - base, 1);
    finish = end ? indexAt(map, n, end - base, reverse ? 0 : 1) : n;
    limit = till ? indexAt(map, n, till - base, 0) : n;
    unicodeDelimiters = d ? d : defaultUnicodeDelimiters;
    found = execWideRE(wide, cells + start, end ? cells + finish : NULL, reverse,
                       start ? cells[start - 1] : prev,
                       till && limit < n ? cells[limit] : succ, NULL, cells,
                       till ? cells + limit : NULL);
    if (found) {
        for (i = 0; i < NSUBEXP; i++) {
            p->startp[i] =
                wide->startp[i] ? (char *)base + map[wide->startp[i] - cells] : NULL;
            p->endp[i] =
                wide->endp[i] ? (char *)base + map[wide->endp[i] - cells] : NULL;
        }
        p->extentpBW = (char *)base + map[wide->extentpBW - cells];
        p->extentpFW = (char *)base + map[wide->extentpFW - cells];
        p->top_branch = wide->top_branch;
    }
    unicodeDelimiters = NULL;
    NEditFree(d);
    if (!cached) {
        NEditFree(cells);
        NEditFree(map);
    }
    return found;
}
Boolean SubstituteUtf8RE(const regexp *p, const char *source, char *dest, int max)
{
    const char *s = source, *part;
    char *converted;
    char numeric[5];
    int used = 0, len, capture, mode, step, valid;
    const char *sourceEnd = source ? source + strlen(source) : NULL;
    unsigned long scalar;
    if (!source || !dest || max <= 0)
        return False;
    while (*s) {
        capture = -1;
        mode = 0;
        if (*s == '\\' && strchr("uUlL", s[1]) && s[1]) {
            mode = s[1];
            s += 2;
            if (!*s)
                break;
        }
        if (*s == '&') {
            capture = 0;
            s++;
        } else if (*s == '\\') {
            s++;
            if (*s >= '1' && *s <= '9')
                capture = *s++ - '0';
            else {
                unsigned char c;
                if (*s == 'x' || *s == 'X' || *s == '0') {
                    REChar *tmp = decode(s, sourceEnd - s, NULL, NULL), *ptr = tmp;
                    c = numeric_escape(*ptr, &ptr);
                    if (c) {
                        s += ptr - tmp + 1;
                        len = Utf8Encode(c, numeric);
                    } else {
                        numeric[0] = *s++;
                        len = 1;
                    }
                    NEditFree(tmp);
                } else {
                    c = literal_escape((unsigned char)*s);
                    if (c) {
                        numeric[0] = c;
                        s++;
                        len = 1;
                    } else if (!*s) {
                        numeric[0] = '\\';
                        len = 1;
                    } else {
                        len = Utf8Decode(s, sourceEnd - s, &scalar, &valid);
                        memcpy(numeric, s, len);
                        s += len;
                    }
                }
                numeric[len] = 0;
                part = numeric;
            }
        } else {
            part = s;
            len = Utf8Decode(s, sourceEnd - s, &scalar, &valid);
            s += len;
        }
        if (capture >= 0) {
            if (!p->startp[capture] || !p->endp[capture])
                continue;
            part = p->startp[capture];
            len = p->endp[capture] - part;
        }
        converted = Utf8CaseMap(part, len, mode == 'u' || mode == 'U',
                                mode == 'u' || mode == 'l', capture >= 0 && mode != 0);
        len = strlen(converted);
        if (used + len >= max) {
            int pos = 0;
            while (pos < len) {
                step = Utf8Decode(converted + pos, len - pos, NULL, NULL);
                if (used + pos + step >= max)
                    break;
                pos += step;
            }
            memcpy(dest + used, converted, pos);
            dest[used + pos] = 0;
            NEditFree(converted);
            return False;
        }
        memcpy(dest + used, converted, len);
        used += len;
        NEditFree(converted);
    }
    dest[used] = 0;
    return True;
}
#else
/* Legacy-only builds never dispatch here. */
void SetUtf8REDefaultWordDelimiters(const char *s) { (void)s; }
void BeginUtf8RegexSubject(const char *s) { (void)s; }
void EndUtf8RegexSubject(void) {}
regexp *CompileUtf8RE(const char *s, char **e, int f)
{
    (void)s;
    (void)f;
    *e = "UTF-8 regex unavailable";
    return NULL;
}
int ExecUtf8RE(regexp *p, const char *s, const char *e, int r, unsigned int a,
               unsigned int b, const char *d, const char *l, const char *t)
{
    (void)p;
    (void)s;
    (void)e;
    (void)r;
    (void)a;
    (void)b;
    (void)d;
    (void)l;
    (void)t;
    return 0;
}
Boolean SubstituteUtf8RE(const regexp *p, const char *s, char *d, int n)
{
    (void)p;
    (void)s;
    (void)d;
    (void)n;
    return False;
}
#endif
