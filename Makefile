# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
# Linux / macOS / MinGW build of duo57.lv2
# The GUI is built for Windows (ui:WindowsUI) and X11 (ui:X11UI, needs libx11-dev).
# macOS has no window layer yet, so the bundle there has no GUI and hosts show generic controls.
UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
  LIB_EXT  := .dylib
  LDFLAGS  += -dynamiclib
  UI_CLASS :=
else ifneq (,$(findstring MINGW,$(UNAME)))
  LIB_EXT  := .dll
  LDFLAGS  += -shared -static-libgcc
  UI_CLASS := WindowsUI
  UI_LIBS  := -luser32 -lgdi32
else
  LIB_EXT  := .so
  LDFLAGS  += -shared -Wl,--no-undefined
  UI_CLASS := X11UI
  UI_LIBS  := -lX11
endif

CC      ?= cc
CFLAGS  ?= -O2 -ffast-math -Wall -Wextra
CFLAGS  += -fPIC -fvisibility=hidden -std=c99 -D_DEFAULT_SOURCE
UI_CFLAGS := -O2 -Wall -Wextra -fPIC -fvisibility=hidden -std=c11 -D_DEFAULT_SOURCE
BUNDLE  := build/duo57.lv2
LV2DIR  ?= $(HOME)/.lv2
UI_SRC  := src/ui/ui_lv2.c src/ui/panel.h src/ui/canvas.h src/ui/font.h src/ui/style.h src/ports.h

TARGETS := $(BUNDLE)/duo57$(LIB_EXT) $(BUNDLE)/manifest.ttl $(BUNDLE)/duo57.ttl
ifneq ($(UI_CLASS),)
  TARGETS += $(BUNDLE)/duo57_ui$(LIB_EXT)
endif

all: $(TARGETS)

$(BUNDLE):
	mkdir -p $@

$(BUNDLE)/duo57$(LIB_EXT): src/duo57.c src/lv2_core.h src/ports.h | $(BUNDLE)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lm

$(BUNDLE)/duo57_ui$(LIB_EXT): $(UI_SRC) | $(BUNDLE)
	$(CC) $(UI_CFLAGS) $< -o $@ $(LDFLAGS) $(UI_LIBS) -lm

$(BUNDLE)/manifest.ttl: duo57.lv2/manifest.ttl.in | $(BUNDLE)
ifeq ($(UI_CLASS),)
	sed -e 's/@LIB_EXT@/$(LIB_EXT)/' -e '/# UI-BEGIN/,/# UI-END/d' $< > $@
else
	sed -e 's/@LIB_EXT@/$(LIB_EXT)/' -e 's/@UI_CLASS@/$(UI_CLASS)/' $< > $@
endif

$(BUNDLE)/duo57.ttl: duo57.lv2/duo57.ttl | $(BUNDLE)
	cp $< $@

test: build/render build/ui_render
	cd build && ./render && ./ui_render panel.bmp

build/render: test/render.c src/duo57.c src/ports.h | $(BUNDLE)
	$(CC) $(CFLAGS) $< -o $@ -lm

build/ui_render: test/ui_render.c $(UI_SRC) | $(BUNDLE)
	$(CC) $(UI_CFLAGS) $< -o $@ -lm

install: all
	mkdir -p $(LV2DIR)
	cp -r $(BUNDLE) $(LV2DIR)/

clean:
	rm -rf build

.PHONY: all test install clean
