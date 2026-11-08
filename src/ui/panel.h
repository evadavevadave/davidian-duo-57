/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* The GUI panel: layout, drawing and mouse handling, independent of any windowing system.
 *
 * House style (style.h, font.h, canvas.h, shared with the other LV2 GUIs): graphite panel,
 * cream lettering, amber accents, red pilot light, black knurled knobs, outlined sections.
 * The panel name "Duo '57" is original; the layout follows the amp's functional
 * grouping (four input jacks, two channel volumes, tone stack, presence) with no branding
 * or artwork from the original hardware. */
#ifndef UI_PANEL_H
#define UI_PANEL_H

#include <stdio.h>
#include <stdlib.h>

#include "../ports.h"
#include "canvas.h"
#include "font.h"
#include "style.h"

#define PANEL_W 1040
#define PANEL_H 330

enum { W_KNOB, W_TRIM, W_SWITCH, W_JACK, W_YBTN };

typedef struct {
    int kind, port;
    float x, y, r;
    float min, max, def; /* for W_JACK / W_YBTN, def is the value the jack selects */
    const char* label;   /* on the panel */
    const char* name;    /* in the display */
    const char* opts[2];
    int nopts;
} Widget;

/* Section boxes: x0, y0, x1, y1. */
#define SEC_Y0 80
#define SEC_Y1 312
#define KNOB_Y 190

static const Widget WIDGETS[] = {
    /* Input jacks: one port, each jack selects a value. */
    { W_JACK, P_INPUT, 71, 140, 14, 0, 4, IN_BRIGHT1, "1", "BRIGHT 1", {0}, 0 },
    { W_JACK, P_INPUT, 151, 140, 14, 0, 4, IN_NORMAL1, "1", "NORMAL 1", {0}, 0 },
    { W_JACK, P_INPUT, 71, 206, 14, 0, 4, IN_BRIGHT2, "2", "BRIGHT 2", {0}, 0 },
    { W_JACK, P_INPUT, 151, 206, 14, 0, 4, IN_NORMAL2, "2", "NORMAL 2", {0}, 0 },
    { W_YBTN, P_INPUT, 111, 268, 0, 0, 4, IN_BOTH, "Y CABLE", "BRIGHT 1 + NORMAL 1", {0}, 0 },

    { W_KNOB, P_VOL_BRIGHT, 281, KNOB_Y, 30, 1, 12, 6, "BRIGHT", "VOLUME BRIGHT", {0}, 0 },
    { W_KNOB, P_VOL_NORMAL, 401, KNOB_Y, 30, 1, 12, 6, "NORMAL", "VOLUME NORMAL", {0}, 0 },
    { W_KNOB, P_TREBLE, 538, KNOB_Y, 30, 1, 12, 7, "TREBLE", "TREBLE", {0}, 0 },
    { W_KNOB, P_BASS, 661, KNOB_Y, 30, 1, 12, 5, "BASS", "BASS", {0}, 0 },
    { W_KNOB, P_PRESENCE, 784, KNOB_Y, 30, 1, 12, 6, "PRESENCE", "PRESENCE", {0}, 0 },

    { W_SWITCH, P_CAB, 912, 160, 0, 0, 1, 1, "CABINET", "CABINET SIM", { "OFF", "ON" }, 2 },
    { W_TRIM, P_IN_TRIM, 893, 262, 17, -24, 24, 0, "INPUT", "INPUT TRIM", {0}, 0 },
    { W_TRIM, P_OUT_LEVEL, 983, 262, 17, -60, 12, 0, "OUTPUT", "OUTPUT LEVEL", {0}, 0 },
};
#define N_WIDGETS ((int)(sizeof WIDGETS / sizeof WIDGETS[0]))

#define PILOT_X 604.0f
#define PILOT_Y 35.0f

#define KNOB_SPAN (0.75f * CV_PI) /* +/-135 degrees */

typedef void (*PanelWrite)(void* host, int port, float value);

typedef struct {
    uint32_t* px; /* what the window shows */
    uint32_t* bg; /* static artwork */
    Canvas c;
    float val[P_COUNT];
    int drag;      /* widget being dragged, or -1 */
    float drag_y, drag_norm;
    int shown;     /* widget in the display, or -1 */
    int dirty;
    PanelWrite write;
    void* host;
} Panel;

/* ---- Values ---- */

static float w_norm(const Widget* w, float v)
{
    float n = (v - w->min) / (w->max - w->min);
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}

static float w_value(const Widget* w, float n)
{
    n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    return w->min + n * (w->max - w->min);
}

static int input_sel(const Panel* p)
{
    int i = (int)floorf(p->val[P_INPUT] + 0.5f);
    return i < 0 ? 0 : (i > IN_BOTH ? IN_BOTH : i);
}

static int switch_index(const Panel* p, const Widget* w)
{
    int i = (int)floorf(p->val[w->port] + 0.5f);
    return i < 0 ? 0 : (i >= w->nopts ? w->nopts - 1 : i);
}

/* Is this jack plugged? The Y cable fills Bright 1 and Normal 1. */
static int jack_lit(const Panel* p, const Widget* w)
{
    const int sel = input_sel(p), v = (int)w->def;
    if (w->kind == W_YBTN)
        return sel == IN_BOTH;
    return sel == v || (sel == IN_BOTH && (v == IN_BRIGHT1 || v == IN_NORMAL1));
}

static void w_format(const Panel* p, const Widget* w, char* buf, size_t n)
{
    static const char* const INPUTS[] = { "NORMAL 1", "NORMAL 2", "BRIGHT 1", "BRIGHT 2", "BRIGHT 1 + NORMAL 1" };
    const float v = p->val[w->port];
    switch (w->kind) {
    case W_JACK:
    case W_YBTN: snprintf(buf, n, "%s", INPUTS[input_sel(p)]); break;
    case W_SWITCH: snprintf(buf, n, "%s", w->opts[switch_index(p, w)]); break;
    case W_TRIM: snprintf(buf, n, "%+.1f DB", v); break;
    default: snprintf(buf, n, "%.1f", v); break;
    }
}

/* ---- Static artwork ---- */

static void draw_screw(Canvas* c, float x, float y)
{
    cv_circle(c, x + 1, y + 1.5f, 6, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, 6, 0xb8bbc2, 0x4a4d54, -0.4f, -0.5f, 1);
    cv_line(c, x - 4, y + 2, x + 4, y - 2, 1.6f, 0x2a2c30, 0.9f);
}

static void draw_section(Canvas* c, float x0, float y0, float x1, float y1, const char* title)
{
    const float tw = text_width(title, TXT_SECTION, 2.5f);
    const float cx = 0.5f * (x0 + x1);
    cv_rrect(c, x0, y0, x1, y1, 10, 0x000000, 0.18f);
    cv_rrect_line(c, x0, y0, x1, y1, 10, 1.4f, COL_LINE, 0.9f);
    /* Title sits in a gap in the top edge. */
    cv_rrect(c, cx - 0.5f * tw - 8, y0 - 6, cx + 0.5f * tw + 8, y0 + 6, 3,
             mix(COL_PANEL_TOP, COL_PANEL_BOT, y0 / PANEL_H), 1);
    text_center(c, cx, y0 - 4.5f, title, TXT_SECTION, 2.5f, 1.5f, COL_AMBER, 1);
}

/* Main knobs carry a 1-12 scale, as on a tweed amp; trims carry plain ticks. */
static void draw_knob_scale(Canvas* c, const Widget* w)
{
    int i;
    if (w->kind == W_KNOB) {
        for (i = 0; i < 12; ++i) {
            char num[4];
            const float a = -KNOB_SPAN + 2.0f * KNOB_SPAN * i / 11.0f;
            const float r0 = w->r + 9.0f, r1 = r0 + 4.0f, rn = w->r + 24.0f;
            cv_line(c, cv_ax(w->x, r0, a), cv_ay(w->y, r0, a), cv_ax(w->x, r1, a), cv_ay(w->y, r1, a),
                    1.3f, COL_INK, 0.8f);
            snprintf(num, sizeof num, "%d", i + 1);
            text_center(c, cv_ax(w->x, rn, a), cv_ay(w->y, rn, a) - 3.5f, num, TXT_SMALL, 1.0f, 1.1f,
                        COL_DIM, 1);
        }
    } else {
        for (i = 0; i < 7; ++i) {
            const float a = -KNOB_SPAN + 2.0f * KNOB_SPAN * i / 6.0f;
            const float r0 = w->r + 6.0f, r1 = r0 + 6.0f;
            cv_line(c, cv_ax(w->x, r0, a), cv_ay(w->y, r0, a), cv_ax(w->x, r1, a), cv_ay(w->y, r1, a),
                    1.3f, COL_INK, 0.8f);
        }
    }
    /* Dim track under the value arc. */
    cv_arc(c, w->x, w->y, w->r + 4.5f, 2.2f, -KNOB_SPAN, KNOB_SPAN, 0x000000, 0.55f);
    if (w->kind == W_KNOB)
        text_center(c, w->x, w->y + w->r + 32.0f, w->label, TXT_LABEL, 2.0f, 1.7f, COL_INK, 1);
    else
        text_center(c, w->x, w->y + w->r + 14.0f, w->label, 8.0f, 2.0f, 1.4f, COL_INK, 1);
}

static void draw_switch_static(Canvas* c, const Widget* w)
{
    text_center(c, w->x + 14, w->y - 44, w->label, 8, 2.0f, 1.4f, COL_INK, 1);
    /* Mounting plate and nut. */
    cv_rrect(c, w->x - 9, w->y - 25, w->x + 9, w->y + 25, 4, 0x000000, 0.35f);
    cv_rrect_line(c, w->x - 9, w->y - 25, w->x + 9, w->y + 25, 4, 1, 0x71757d, 0.7f);
    cv_circle(c, w->x + 1, w->y + 2, 8.5f, 0x000000, 0.5f);
    cv_shaded_circle(c, w->x, w->y, 8.5f, COL_METAL_LIGHT, COL_METAL_DARK, -0.4f, -0.6f, 1);
    cv_circle(c, w->x, w->y, 5, 0x2d2f33, 1);
}

/* Input jack: chrome nut around a black socket. */
static void draw_jack_static(Canvas* c, const Widget* w)
{
    const float x = w->x, y = w->y, r = w->r;
    cv_circle(c, x + 1.5f, y + 2.5f, r + 1, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, r, COL_METAL_LIGHT, COL_METAL_DARK, -0.4f, -0.6f, 1);
    cv_ring(c, x, y, r * 0.78f, 1.0f, 0x3a3c41, 0.7f);
    cv_ring(c, x, y, r, 1.0f, 0x2a2c30, 0.8f);
}

static void draw_ybtn_static(Canvas* c, const Widget* w)
{
    const float x0 = w->x - 52, x1 = w->x + 52, y0 = w->y - 13, y1 = w->y + 13;
    cv_rrect(c, x0 + 1, y0 + 2, x1 + 1, y1 + 2, 5, 0x000000, 0.45f);
    cv_rrect_vgrad(c, x0, y0, x1, y1, 5, 0x3b3e44, 0x1d1f23, 1);
    cv_rrect_line(c, x0, y0, x1, y1, 5, 1.0f, 0x71757d, 0.7f);
    text_draw(c, x0 + 30, w->y - 4.0f, w->label, 8, 2.0f, 1.4f, COL_INK, 1);
}

static void draw_pilot_static(Canvas* c, float x, float y)
{
    /* Chrome bezel around a faceted jewel. */
    cv_circle(c, x + 1, y + 2, 13, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, 13, 0xc4c7cd, 0x45484e, -0.4f, -0.6f, 1);
    cv_ring(c, x, y, 13, 1.0f, 0x202226, 0.8f);
    cv_glow(c, x, y, 7, 18, COL_RED, 0.35f);
    cv_shaded_circle(c, x, y, 8.5f, 0xffc4b8, COL_RED, -0.3f, -0.4f, 1);
    cv_ring(c, x, y, 4.5f, 1.0f, 0xffffff, 0.18f);
    cv_line(c, x - 8, y, x + 8, y, 0.8f, 0x7a1510, 0.35f);
    cv_line(c, x, y - 8, x, y + 8, 0.8f, 0x7a1510, 0.35f);
    cv_circle(c, x - 3, y - 3, 2.2f, 0xffffff, 0.75f);
    text_center(c, x, y + 19, "PILOT", TXT_SMALL, 2.0f, 1.1f, COL_DIM, 1);
}

static void draw_background(Panel* p)
{
    Canvas c;
    int i, y;
    cv_init(&c, p->bg, PANEL_W, PANEL_H);
    for (y = 0; y < PANEL_H; ++y) {
        uint32_t col = mix(COL_PANEL_TOP, COL_PANEL_BOT, (float)y / PANEL_H);
        int x;
        for (x = 0; x < PANEL_W; ++x)
            p->bg[y * PANEL_W + x] = col;
    }
    cv_texture(&c, PANEL_TEXTURE);
    /* Bevelled edge */
    cv_rrect_line(&c, 1.5f, 1.5f, PANEL_W - 1.5f, PANEL_H - 1.5f, 6, 1.5f, 0x55595f, 0.8f);
    cv_rrect_line(&c, 3.5f, 3.5f, PANEL_W - 3.5f, PANEL_H - 3.5f, 5, 1.5f, 0x000000, 0.4f);

    /* Header */
    text_draw(&c, 30, 18, "DAVIDIAN DUO '57", TXT_TITLE, 3.2f, 2.8f, COL_INK, 1);
    text_draw(&c, 32, 46, "FOUR-INPUT 12AX7 PREAMP - 6L6 PUSH-PULL - PRESENCE FEEDBACK", 7, 2.4f, 1.1f, COL_DIM, 1);
    cv_line(&c, 30, 60, 570, 60, 1.2f, COL_AMBER, 0.55f);
    draw_pilot_static(&c, PILOT_X, PILOT_Y);
    /* Readout window */
    cv_rrect(&c, 640, 14, 1024, 56, 6, 0x000000, 0.6f);
    cv_rrect(&c, 642, 16, 1022, 54, 5, 0x0d0a06, 1);
    cv_rrect_line(&c, 640, 14, 1024, 56, 6, 1.2f, 0x6d7179, 0.8f);

    draw_section(&c, 16, SEC_Y0, 206, SEC_Y1, "INPUTS");
    draw_section(&c, 216, SEC_Y0, 466, SEC_Y1, "VOLUME");
    draw_section(&c, 476, SEC_Y0, 846, SEC_Y1, "TONE");
    draw_section(&c, 856, SEC_Y0, 1024, SEC_Y1, "STUDIO");

    /* Jack column titles and row numbers. */
    text_center(&c, 71, 102, "BRIGHT", 8, 2.0f, 1.4f, COL_INK, 1);
    text_center(&c, 151, 102, "NORMAL", 8, 2.0f, 1.4f, COL_INK, 1);

    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM: draw_knob_scale(&c, w); break;
        case W_SWITCH: draw_switch_static(&c, w); break;
        case W_JACK:
            draw_jack_static(&c, w);
            text_center(&c, 111, w->y - 4.5f, w->label, 9, 1.0f, 1.4f, COL_DIM, 1);
            break;
        case W_YBTN: draw_ybtn_static(&c, w); break;
        }
    }

    draw_screw(&c, 14, 14);
    draw_screw(&c, PANEL_W - 14, 14);
    draw_screw(&c, 14, PANEL_H - 14);
    draw_screw(&c, PANEL_W - 14, PANEL_H - 14);
}

/* ---- Moving parts ---- */

static void draw_knob(Canvas* c, const Widget* w, float n, int active)
{
    const float r = w->r, x = w->x, y = w->y;
    const float ang = -KNOB_SPAN + 2.0f * KNOB_SPAN * n;
    const int big = w->kind == W_KNOB;
    const int knurls = big ? 36 : 20;
    int i;
    cv_arc(c, x, y, r + 4.5f, 2.2f, -KNOB_SPAN, ang, active ? 0xffd27f : COL_AMBER, 0.95f);
    cv_circle(c, x + 2, y + 4, r + 1, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, r, COL_KNOB_LIGHT, COL_KNOB_DARK, -0.45f, -0.65f, 1);
    for (i = 0; i < knurls; ++i) {
        const float a = ang + 2.0f * CV_PI * i / knurls;
        cv_line(c, cv_ax(x, r * 0.84f, a), cv_ay(y, r * 0.84f, a), cv_ax(x, r * 0.98f, a),
                cv_ay(y, r * 0.98f, a), big ? 1.6f : 1.2f, 0x000000, 0.55f);
    }
    cv_shaded_circle(c, x, y, r * 0.74f, 0x3c3e44, 0x18191c, -0.35f, -0.55f, 1);
    cv_ring(c, x, y, r * 0.74f, 1.0f, 0x6a6d74, 0.5f);
    cv_line(c, cv_ax(x, r * 0.2f, ang), cv_ay(y, r * 0.2f, ang), cv_ax(x, r * 0.93f, ang),
            cv_ay(y, r * 0.93f, ang), big ? 3.0f : 2.2f, COL_INK, 1);
}

static void draw_switch(Canvas* c, const Widget* w, int idx)
{
    const float x = w->x, y = w->y;
    const float ey = (idx == 0) ? y - 19.0f : y + 19.0f;
    int i;
    for (i = 0; i < w->nopts; ++i) {
        const float oy = y - 20.0f + 40.0f * i;
        text_draw(c, x + 15, oy - 3.5f, w->opts[i], 7, 1.6f, 1.25f, i == idx ? COL_AMBER : COL_DIM, 1);
    }
    cv_line(c, x + 1.5f, y + 2, x + 1.5f, ey + 2, 6, 0x000000, 0.45f);
    cv_line(c, x, y, x, ey, 5.5f, 0xb6b9bf, 1);
    cv_line(c, x - 1.2f, y, x - 1.2f, ey, 1.6f, 0xf0f1f3, 0.7f);
    cv_shaded_circle(c, x, ey, 4.8f, 0xf4f5f7, 0x7c7f86, -0.35f, -0.45f, 1);
}

/* Socket: empty is a dark hole; plugged shows a black plug sleeve with an amber ring. */
static void draw_jack(Canvas* c, const Widget* w, int lit)
{
    const float x = w->x, y = w->y, r = w->r;
    if (lit) {
        cv_glow(c, x, y, r * 0.7f, 10, COL_AMBER, 0.3f);
        cv_shaded_circle(c, x, y, r * 0.72f, 0x45474d, 0x0c0c0e, -0.4f, -0.5f, 1);
        cv_ring(c, x, y, r * 0.72f, 1.6f, COL_AMBER, 0.95f);
        cv_circle(c, x, y, r * 0.22f, 0x8d9097, 1);
    } else {
        cv_shaded_circle(c, x, y, r * 0.55f, 0x26282c, 0x050506, 0.3f, 0.4f, 1);
    }
}

static void draw_ybtn(Canvas* c, const Widget* w, int lit)
{
    const float lx = w->x - 36, ly = w->y;
    if (lit) {
        cv_glow(c, lx, ly, 3.5f, 10, COL_AMBER, 0.4f);
        cv_shaded_circle(c, lx, ly, 4.2f, 0xffe6b8, COL_AMBER, -0.3f, -0.4f, 1);
    } else {
        cv_shaded_circle(c, lx, ly, 4.2f, 0x4a3410, 0x1c1206, -0.3f, -0.4f, 1);
    }
}

static void draw_display(Panel* p)
{
    char val[64];
    Canvas* c = &p->c;
    if (p->shown < 0) {
        text_draw(c, 656, 22, "DUO '57 TWEED", 8, 2.2f, 1.3f, COL_AMBER_DK, 1);
        text_draw(c, 656, 36, "DRAG KNOBS - DOUBLE-CLICK RESETS - CLICK A JACK", 7, 1.6f, 1.2f, 0x9c6a24, 1);
        return;
    }
    w_format(p, &WIDGETS[p->shown], val, sizeof val);
    text_draw(c, 656, 22, WIDGETS[p->shown].kind == W_JACK || WIDGETS[p->shown].kind == W_YBTN
                              ? "INPUT JACK" : WIDGETS[p->shown].name,
              8, 2.2f, 1.3f, 0xc58b33, 1);
    text_draw(c, 656, 35, val, 13, 2.0f, 2.0f, COL_AMBER, 1);
}

static void panel_draw(Panel* p)
{
    int i;
    memcpy(p->px, p->bg, sizeof(uint32_t) * PANEL_W * PANEL_H);
    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM: draw_knob(&p->c, w, w_norm(w, p->val[w->port]), p->drag == i); break;
        case W_SWITCH: draw_switch(&p->c, w, switch_index(p, w)); break;
        case W_JACK: draw_jack(&p->c, w, jack_lit(p, w)); break;
        case W_YBTN: draw_ybtn(&p->c, w, jack_lit(p, w)); break;
        }
    }
    draw_display(p);
    p->dirty = 0;
}

/* ---- Setup and host values ---- */

static int panel_init(Panel* p, PanelWrite write, void* host)
{
    int i;
    memset(p, 0, sizeof *p);
    p->px = (uint32_t*)malloc(sizeof(uint32_t) * PANEL_W * PANEL_H);
    p->bg = (uint32_t*)malloc(sizeof(uint32_t) * PANEL_W * PANEL_H);
    if (!p->px || !p->bg) {
        free(p->px);
        free(p->bg);
        return 0;
    }
    cv_init(&p->c, p->px, PANEL_W, PANEL_H);
    for (i = 0; i < N_WIDGETS; ++i)
        if (WIDGETS[i].kind != W_JACK && WIDGETS[i].kind != W_YBTN)
            p->val[WIDGETS[i].port] = WIDGETS[i].def;
    p->val[P_INPUT] = IN_BRIGHT1;
    p->drag = -1;
    p->shown = -1;
    p->write = write;
    p->host = host;
    draw_background(p);
    panel_draw(p);
    return 1;
}

static void panel_free(Panel* p)
{
    free(p->px);
    free(p->bg);
    p->px = p->bg = NULL;
}

/* A value from the host. */
static void panel_set(Panel* p, int port, float v)
{
    if (port < 0 || port >= P_COUNT || !isfinite(v))
        return;
    if (p->val[port] != v) {
        p->val[port] = v;
        p->dirty = 1;
    }
}

/* ---- Mouse ---- */

static void set_value(Panel* p, int wi, float v)
{
    const Widget* w = &WIDGETS[wi];
    if (w->kind != W_KNOB && w->kind != W_TRIM)
        v = floorf(v + 0.5f);
    if (v < w->min) v = w->min;
    if (v > w->max) v = w->max;
    p->shown = wi;
    p->dirty = 1;
    if (p->val[w->port] != v) {
        p->val[w->port] = v;
        if (p->write)
            p->write(p->host, w->port, v);
    }
}

static int hit(float x, float y)
{
    int i;
    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        const float dx = x - w->x, dy = y - w->y;
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM:
        case W_JACK:
            if (dx * dx + dy * dy <= (w->r + 8) * (w->r + 8))
                return i;
            break;
        case W_SWITCH:
            if (dx >= -14 && dx <= 50 && dy >= -30 && dy <= 30)
                return i;
            break;
        case W_YBTN:
            if (fabsf(dx) <= 52 && fabsf(dy) <= 13)
                return i;
            break;
        }
    }
    return -1;
}

static void panel_mouse_down(Panel* p, float x, float y, int dbl)
{
    const int i = hit(x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &WIDGETS[i];
    switch (w->kind) {
    case W_KNOB:
    case W_TRIM:
        if (dbl) {
            set_value(p, i, w->def);
            return;
        }
        p->drag = i;
        p->drag_y = y;
        p->drag_norm = w_norm(w, p->val[w->port]);
        p->shown = i;
        p->dirty = 1;
        break;
    case W_SWITCH:
        set_value(p, i, y < w->y ? 0.0f : 1.0f);
        break;
    case W_JACK:
    case W_YBTN:
        set_value(p, i, w->def);
        break;
    }
}

static void panel_mouse_move(Panel* p, float x, float y, int fine)
{
    const Widget* w;
    float n;
    (void)x;
    if (p->drag < 0)
        return;
    w = &WIDGETS[p->drag];
    n = p->drag_norm + (p->drag_y - y) / (fine ? 900.0f : 180.0f);
    /* Fresh reference each move, so switching fine mode mid-drag doesn't jump. */
    set_value(p, p->drag, w_value(w, n));
    p->drag_norm = w_norm(w, p->val[w->port]);
    p->drag_y = y;
}

static void panel_mouse_up(Panel* p)
{
    if (p->drag >= 0)
        p->dirty = 1;
    p->drag = -1;
}

static void panel_wheel(Panel* p, float x, float y, float steps, int fine)
{
    const int i = hit(x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &WIDGETS[i];
    if (w->kind == W_SWITCH)
        set_value(p, i, steps > 0 ? 0.0f : 1.0f);
    else if (w->kind == W_KNOB || w->kind == W_TRIM)
        set_value(p, i, w_value(w, w_norm(w, p->val[w->port]) + steps * (fine ? 0.004f : 0.025f)));
}

#endif
