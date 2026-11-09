# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
# Linux / macOS / MinGW build of duo57.lv2 (Windows with MSVC: build.bat).
#   make            plugin + GUI into build/duo57.lv2
#   make test       also runs the DSP test and renders build/panel.bmp
#   make dist       zips the bundle as build/duo57-<platform>.zip
#   make install    copies the bundle to ~/.lv2 (LV2DIR=... to change)
# GUI: ui:CocoaUI on macOS, ui:X11UI on Linux (needs libx11-dev), ui:WindowsUI on MinGW.
# macOS builds are universal (x86_64 + arm64) by default; set ARCH= for the host arch only.
UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
  LIB_EXT  := .dylib
  ARCH     ?= -arch x86_64 -arch arm64
  PLAT     := $(ARCH) -mmacosx-version-min=10.13
  LDFLAGS  += -dynamiclib
  UI_CLASS := CocoaUI
  UI_LANG  := -x objective-c
  UI_LIBS  := -framework Cocoa
  DIST     := macos-universal
else ifneq (,$(findstring MINGW,$(UNAME)))
  LIB_EXT  := .dll
  LDFLAGS  += -shared -static-libgcc
  UI_CLASS := WindowsUI
  UI_LIBS  := -luser32 -lgdi32
  DIST     := windows-x64
else
  LIB_EXT  := .so
  LDFLAGS  += -shared -Wl,--no-undefined
  UI_CLASS := X11UI
  UI_LIBS  := -lX11
  DIST     := linux-$(shell uname -m)
endif

CC      ?= cc
# No -ffast-math: the plugin relies on NaN checks to recover from bad input.
CFLAGS  ?= -O2 -fno-math-errno -Wall -Wextra
CFLAGS  += $(PLAT) -fPIC -fvisibility=hidden -std=c99 -D_DEFAULT_SOURCE
UI_CFLAGS := -O2 -Wall -Wextra $(PLAT) -fPIC -fvisibility=hidden -std=c11 -D_DEFAULT_SOURCE
LDFLAGS += $(PLAT)
BUNDLE  := build/duo57.lv2
LV2DIR  ?= $(HOME)/.lv2
UI_SRC  := src/ui/ui_lv2.c src/ui/panel.h src/ui/canvas.h src/ui/font.h src/ui/style.h \
           src/ui/window_cocoa.h src/ports.h src/lv2_core.h

TARGETS := $(BUNDLE)/duo57$(LIB_EXT) $(BUNDLE)/duo57_ui$(LIB_EXT) $(BUNDLE)/manifest.ttl \
           $(BUNDLE)/duo57.ttl $(BUNDLE)/LICENSE $(BUNDLE)/LICENSE-LV2

all: $(TARGETS)

$(BUNDLE):
	mkdir -p $@

$(BUNDLE)/duo57$(LIB_EXT): src/duo57.c src/lv2_core.h src/ports.h | $(BUNDLE)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lm

$(BUNDLE)/duo57_ui$(LIB_EXT): $(UI_SRC) | $(BUNDLE)
	$(CC) $(UI_CFLAGS) $(UI_LANG) $< -o $@ $(LDFLAGS) $(UI_LIBS) -lm

$(BUNDLE)/manifest.ttl: duo57.lv2/manifest.ttl.in | $(BUNDLE)
	sed -e 's/@LIB_EXT@/$(LIB_EXT)/' -e 's/@UI_CLASS@/$(UI_CLASS)/' $< > $@

$(BUNDLE)/duo57.ttl: duo57.lv2/duo57.ttl | $(BUNDLE)
	cp $< $@

$(BUNDLE)/LICENSE $(BUNDLE)/LICENSE-LV2: $(BUNDLE)/%: % | $(BUNDLE)
	cp $< $@

test: build/render build/ui_render
	cd build && ./render && ./ui_render panel.bmp

build/render: test/render.c src/duo57.c src/ports.h | $(BUNDLE)
	$(CC) $(CFLAGS) $< -o $@ -lm

build/ui_render: test/ui_render.c $(UI_SRC) | $(BUNDLE)
	$(CC) $(UI_CFLAGS) $< -o $@ -lm

dist: all
	cd build && rm -f duo57-$(DIST).zip && zip -r -X duo57-$(DIST).zip duo57.lv2

install: all
	mkdir -p $(LV2DIR)
	cp -r $(BUNDLE) $(LV2DIR)/

clean:
	rm -rf build

.PHONY: all test dist install clean
