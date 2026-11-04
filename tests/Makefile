.SUFFIXES:

CC = cc
CFLAGS = -O2 -Wall -Wextra
CPPFLAGS = -I../source

all: test_utf8

test_utf8: test_utf8.c ../source/utf8.c ../source/utf8.h \
        ../source/utf8Layout.c ../source/utf8Layout.h \
        ../source/textBuf.c ../source/textBuf.h ../source/rangeset.c \
        ../util/nedit_malloc.c
	$(CC) $(CPPFLAGS) $(CFLAGS) test_utf8.c ../source/utf8.c ../source/utf8Layout.c \
	    ../source/textBuf.c ../source/rangeset.c ../util/nedit_malloc.c \
	    -o $@

check: test_utf8
	./test_utf8

WIDGET_OBJECTS = ../source/text.o ../source/textDisp.o ../source/textSel.o \
    ../source/textDrag.o ../source/textBuf.o ../source/utf8.o \
    ../source/rangeset.o ../source/undo.o ../source/utf8Layout.o ../source/utf8Font.o

widget-objects:
	$(MAKE) -C ../source -f Makefile.linux nedit nc

test_utf8_widget: test_utf8_widget.c widget-objects
	$(CC) $(CPPFLAGS) $(CFLAGS) test_utf8_widget.c $(WIDGET_OBJECTS) \
	    ../util/libNUtil.a -lXm -lXt -lX11 -lm $(shell pkg-config --libs xft fontconfig) -o $@

check-widget: test_utf8_widget
	python run_widget_tests.py

.PHONY: all check check-widget widget-objects
