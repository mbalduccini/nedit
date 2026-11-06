.SUFFIXES:

CC = cc
CFLAGS = -O2 -Wall -Wextra
CPPFLAGS = -I../source -DHAVE_UTF8PROC $(shell pkg-config --cflags libutf8proc)

all: test_utf8 test_unicode_regex

test_utf8: test_utf8.c ../source/utf8.c ../source/utf8.h \
        ../source/utf8Layout.c ../source/utf8Layout.h \
        ../source/textBuf.c ../source/textBuf.h ../source/rangeset.c \
        ../util/nedit_malloc.c
	$(CC) $(CPPFLAGS) $(CFLAGS) test_utf8.c ../source/utf8.c ../source/utf8Layout.c \
	    ../source/textBuf.c ../source/rangeset.c ../util/nedit_malloc.c \
	    $(shell pkg-config --libs libutf8proc) -o $@

test_unicode_regex: test_unicode_regex.c ../source/regularExp.c ../source/regularExp.h \
        ../source/unicodeRegex.c ../source/unicodeRegex.h ../source/utf8.c \
        ../source/utf8.h ../util/nedit_malloc.c ../util/nedit_malloc.h
	$(CC) $(CPPFLAGS) $(CFLAGS) test_unicode_regex.c ../source/regularExp.c \
            ../source/unicodeRegex.c ../source/utf8.c ../util/nedit_malloc.c \
            $(shell pkg-config --libs libutf8proc) -lXt -lX11 -o $@

check: test_utf8 test_unicode_regex
	./test_utf8
	./test_unicode_regex

WIDGET_OBJECTS = ../source/text.o ../source/textDisp.o ../source/textSel.o \
    ../source/selectionCodec.o ../source/textDrag.o ../source/textBuf.o ../source/utf8.o \
    ../source/rangeset.o ../source/undo.o ../source/utf8Layout.o ../source/utf8Font.o

widget-objects:
	$(MAKE) -C ../source -f Makefile.linux nedit nc

test_utf8_widget: test_utf8_widget.c widget-objects
	$(CC) $(CPPFLAGS) $(CFLAGS) test_utf8_widget.c $(WIDGET_OBJECTS) -Wl,--wrap=XmImMbLookupString \
	    ../util/libNUtil.a -lXm -lXt -lX11 -lm $(shell pkg-config --libs xft fontconfig libutf8proc) -o $@

APPLICATION_OBJECTS = $(filter-out ../source/nc.o,$(wildcard ../source/*.o))

test_utf8_application: test_utf8_application.c widget-objects
	$(CC) $(CPPFLAGS) $(CFLAGS) test_utf8_application.c $(APPLICATION_OBJECTS) \
	    -Wl,--wrap=XtAppMainLoop ../Microline/XmL/libXmL.a ../Xlt/libXlt.a \
	    ../util/libNUtil.a -lXm -lXt -lX11 -lm \
	    $(shell pkg-config --libs xft fontconfig libutf8proc) -o $@

check-widget: test_utf8_widget test_utf8_application
	python run_widget_tests.py

check-sanitize:
	$(CC) $(CPPFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
	    test_utf8.c ../source/utf8.c ../source/utf8Layout.c ../source/textBuf.c \
	    ../source/rangeset.c ../util/nedit_malloc.c \
	    $(shell pkg-config --libs libutf8proc) -o test_utf8_sanitize
	./test_utf8_sanitize
	$(CC) $(CPPFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
            test_unicode_regex.c ../source/regularExp.c ../source/unicodeRegex.c \
            ../source/utf8.c ../util/nedit_malloc.c \
            $(shell pkg-config --libs libutf8proc) -lXt -lX11 -o test_unicode_regex_sanitize
	./test_unicode_regex_sanitize

.PHONY: all check check-widget check-sanitize widget-objects
