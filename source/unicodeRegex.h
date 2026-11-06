/* Copyright 2026. Distributed under the same GPL terms as NEdit. */
/* Unicode adapter for NEdit's regex dialect. Compiled data is one allocation. */
#ifndef NEDIT_UNICODE_REGEX_H
#define NEDIT_UNICODE_REGEX_H
#include "regularExp.h"
regexp *CompileUtf8RE(const char *, char **, int);
int ExecUtf8RE(regexp *, const char *, const char *, int, unsigned int, unsigned int,
               const char *, const char *, const char *);
Boolean SubstituteUtf8RE(const regexp *, const char *, char *, int);
void SetUtf8REDefaultWordDelimiters(const char *);
void BeginUtf8RegexSubject(const char *);
void EndUtf8RegexSubject(void);
#endif
