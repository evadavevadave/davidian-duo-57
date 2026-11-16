/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. */
/* The Duo '57 panel in a native window: a Win32 child window, a Cocoa NSView or an X11 window.
 * The LV2 UI, the VST3 editor and the Audio Unit view all use this, each with its own host
 * callbacks. view.c is compiled as Objective-C on macOS. */
#ifndef DUO_VIEW_H_INCLUDED
#define DUO_VIEW_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#define DUO_VIEW_W 1040
#define DUO_VIEW_H 330

typedef struct DuoView DuoView;

typedef struct {
    void* host;
    /* A plain value (as in ports.h) the user set on the panel. */
    void (*write)(void* host, int port, float value);
    /* Optional: brackets each edit, begin = 1 then begin = 0. */
    void (*gesture)(void* host, int port, int begin);
    /* Optional: called about 30 times a second on the UI thread, before the panel redraws. */
    void (*tick)(void* host);
} DuoViewHost;

/* Opens the panel inside parent (an HWND, NSView* or X11 Window, cast to void*), or as its own
 * view or window when parent is NULL. Returns NULL on failure. */
DuoView* duo_view_open(void* parent, const DuoViewHost* host);
/* The HWND, NSView* or X11 Window. */
void* duo_view_widget(DuoView* v);
/* A value from the host; it shows on the next redraw. */
void duo_view_set(DuoView* v, int port, float value);
/* X11 has no event loop of its own here, so the host's idle call pumps it. Returns 0. */
int duo_view_idle(DuoView* v);
void duo_view_close(DuoView* v);

#ifdef __cplusplus
}
#endif

#endif
