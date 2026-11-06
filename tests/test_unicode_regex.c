#include "regularExp.h"
#include "unicodeRegex.h"
#include "utf8.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void check(const char *pattern, const char *text, int start, int end, int flags)
{
    char *error;
    regexp *r = CompileRE(pattern, &error, flags | REDFLT_UTF8);
    if (!r) {
        fprintf(stderr, "%s: %s\n", pattern, error);
        abort();
    }
    assert(ExecRE(r, text, NULL, 0, 0, 0, NULL, text, NULL));
    if (r->startp[0] - text != start || r->endp[0] - text != end) {
        fprintf(stderr, "%s: got %ld..%ld expected %d..%d\n", pattern,
                (long)(r->startp[0] - text), (long)(r->endp[0] - text), start, end);
        abort();
    }
    free(r);
}
int main(void)
{
    char *error, out[100];
    regexp *r;
    check(".", "é中", 0, 2, 0);
    check("é+", "ééé中", 0, 6, 0);
    check("[α-ω]+", " βγδ ", 1, 7, 0);
    check("[^é]+", "é中βé", 2, 7, 0);
    check("(?<=中)β", "中β", 3, 5, 0);
    check("(?iΣ)", "ς", 0, 2, 0);
    check("\\w+", "é中é", 0, 8, 0);
    check("[\\w]+", "é中", 0, 5, 0);
    check("\\d+", "١٢", 0, 4, 0);
    check("(.+)\\1", "中é中é", 0, 10, 0);
    check(".{2,3}?Z", "é中βZ", 0, 8, 0);
    check("(?n.)+", "中\nβ", 0, 6, 0);
    check("(?i[Α-Ω])", "ς", 0, 2, 0);
    check("(?iẞ)", "ß", 0, 2, 0);
    check("(?i[Σ])", "ς", 0, 2, 0);
    check("[中-𐐀]", "語", 0, 3, 0);
    check("^β", "é\nβ", 3, 5, 0);
    check("[\\s]+", "\n\u2003", 1, 4, 0);
    check("(?n[\\s])+", "\n\u2003", 0, 4, 0);
    check("\\S+", "\n中", 1, 4, 0);
    check("(?n[\\S])+", "\n中", 0, 4, 0);
    check(".", "\x80é", 0, 1, 0);
    check("\\xE9", "é", 0, 2, 0);
    check("(?<=é{1,2})中", "éé中", 4, 7, 0);
    check("(中|β)+Z", "中βZ", 0, 6, 0);
    check("é(?=中)", "é中", 0, 2, 0);
    r = CompileRE(".", &error, REDFLT_UTF8);
    {
        const char *t = "é中";
        assert(r && ExecRE(r, t, t + 5, 1, 0, 0, NULL, NULL, NULL));
        assert(r->startp[0] == t + 2);
        free(r);
    }
    r = CompileRE("^β", &error, REDFLT_UTF8);
    {
        const char *t = "é\nβ";
        assert(r && ExecRE(r, t, t + 5, 1, 0, 0, NULL, t, NULL));
        assert(r->startp[0] == t + 3);
        free(r);
    }
    r = CompileRE("^Z", &error, REDFLT_UTF8);
    assert(r && !ExecRE(r, "", "", 1, 0, 0, NULL, NULL, NULL));
    free(r);
    r = CompileRE("<β>", &error, REDFLT_UTF8);
    assert(r && !ExecRE(r, "β", NULL, 0, 0x4e2d, 0, " ", NULL, NULL));
    free(r);
    r = CompileRE("é(?=中)", &error, REDFLT_UTF8);
    {
        const char *t = "é中";
        assert(r && ExecRE(r, t, t + 2, 0, 0, 0, NULL, t, t + 2));
        assert(r->endp[0] == t + 2 && r->extentpFW == t + 5);
        free(r);
    }
    r = CompileRE("(?iİ)", &error, REDFLT_UTF8);
    assert(r && !ExecRE(r, "i", NULL, 0, 0, 0, NULL, NULL, NULL));
    free(r);
    assert(!CompileRE(NULL, &error, REDFLT_UTF8));
    assert(!CompileRE("[ω-α]", &error, REDFLT_UTF8));
    r = CompileRE("(é)", &error, REDFLT_UTF8);
    assert(r && ExecRE(r, "é", NULL, 0, 0, 0, NULL, NULL, NULL));
    assert(SubstituteRE(r, "\\U\\1", out, sizeof(out)) && !strcmp(out, "É"));
    assert(SubstituteRE(r, "\\ué-\\U&", out, sizeof(out)) && !strcmp(out, "é-É"));
    assert(SubstituteRE(r, "\\1-\\xE9-\\0", out, sizeof(out)) && !strcmp(out, "é-é-0"));
    assert(!SubstituteRE(r, "中中", out, 5) && !strcmp(out, "中"));
    assert(SubstituteRE(r, "\\9", out, sizeof(out)) && !strcmp(out, ""));
    free(r);
    {
        char *mapped = Utf8CaseFold("Straße Σς İ ﬃ", strlen("Straße Σς İ ﬃ"));
        assert(!strcmp(mapped, "strasse σσ i̇ ffi"));
        free(mapped);
        assert(Utf8Previous("é中", 5, 99) == 2);
        assert(Utf8Previous("é中", 5, 1) == 0);
        assert(Utf8ScalarCount("é中\x80", 6) == 3);
    }
    r = CompileRE(".", &error, 0);
    assert(r && ExecRE(r, "é", NULL, 0, 0, 0, NULL, NULL, NULL));
    assert(r->endp[0] - r->startp[0] == 1);
    free(r);
    {
        char pattern[200];
        int i;
        for (i = 0; i < 40; i++) {
            pattern[i] = '(';
            pattern[40 + i] = ')';
        }
        pattern[80] = 0;
        r = CompileRE(pattern, &error, REDFLT_UTF8);
        assert(r && ExecRE(r, "", NULL, 0, 0, 0, NULL, NULL, NULL));
        free(r);
    }
    puts("Unicode regex dialect, scalar "
         "quantifiers/classes/lookbehind/backrefs/case/replacement and legacy bytes "
         "passed.");
    return 0;
}
