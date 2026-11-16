/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Control metadata for the hosts that don't read duo57.lv2/duo57.ttl: the VST3 and Audio Unit
 * builds. Values are the same plain values as the LV2 ports, so a setting means the same thing in
 * every format. test/params_check.c checks this table against the TTL. */
#ifndef DUO57_PARAMS_H
#define DUO57_PARAMS_H

#include <math.h>
#include <stdio.h>

#include "ports.h"

typedef struct {
    int port;          /* also the parameter ID in VST3 and the Audio Unit */
    const char* name;  /* as in the TTL */
    const char* short_name;
    float min, max, def;
    int steps;         /* 0: continuous; otherwise a switch with steps + 1 positions */
    const char* unit;  /* "dB", or "" for the 1-12 knobs and switches */
    const char* opts[6];
} DuoParam;

static const DuoParam DUO_PARAMS[] = {
    { P_INPUT, "Input Jack", "Input", 0, 4, IN_BRIGHT1, 4, "",
      { "Normal 1", "Normal 2", "Bright 1", "Bright 2", "Normal 1 + Bright 1 (Y cable)", 0 } },
    { P_VOL_NORMAL, "Volume Normal", "Vol Norm", 1, 12, 6, 0, "", { 0 } },
    { P_VOL_BRIGHT, "Volume Bright", "Vol Brt", 1, 12, 6, 0, "", { 0 } },
    { P_TREBLE, "Treble", "Treble", 1, 12, 7, 0, "", { 0 } },
    { P_BASS, "Bass", "Bass", 1, 12, 5, 0, "", { 0 } },
    { P_PRESENCE, "Presence", "Presence", 1, 12, 6, 0, "", { 0 } },
    { P_CAB, "Cabinet", "Cabinet", 0, CAB_COUNT - 1, CAB_2X12_OPEN, CAB_COUNT - 1, "",
      { "Off", "2x12 Open (stock)", "1x12 Open", "4x10 Open", "2x12 Closed", "4x12 Closed" } },
    { P_IN_TRIM, "Input Trim", "In Trim", -24, 24, 0, 0, "dB", { 0 } },
    { P_OUT_LEVEL, "Output Level", "Output", -60, 12, 0, 0, "dB", { 0 } },
};
#define DUO_NUM_PARAMS ((int)(sizeof DUO_PARAMS / sizeof DUO_PARAMS[0]))

/* The first control port and the number of saved values: every control, in port order. */
#define DUO_FIRST_CONTROL P_INPUT
#define DUO_NUM_INPUTS (P_COUNT - P_INPUT)

static inline const DuoParam* duo_param(int port)
{
    int i;
    for (i = 0; i < DUO_NUM_PARAMS; ++i)
        if (DUO_PARAMS[i].port == port)
            return &DUO_PARAMS[i];
    return NULL;
}

static inline float duo_clamp(const DuoParam* p, float v)
{
    if (!(v >= p->min)) /* also catches NaN */
        v = p->min;
    if (v > p->max)
        v = p->max;
    return p->steps ? floorf(v + 0.5f) : v;
}

/* 0-1 host value to plain value. Switches split 0-1 into equal parts, as the VST3 SDK does. */
static inline float duo_from_norm(const DuoParam* p, double n)
{
    n = n < 0.0 ? 0.0 : (n > 1.0 ? 1.0 : n);
    if (p->steps) {
        int i = (int)(n * (p->steps + 1));
        return p->min + (float)(i > p->steps ? p->steps : i);
    }
    return (float)(p->min + n * (p->max - p->min));
}

static inline double duo_to_norm(const DuoParam* p, float v)
{
    v = duo_clamp(p, v);
    if (p->steps)
        return (double)(v - p->min) / p->steps;
    return (double)(v - p->min) / (p->max - p->min);
}

/* Text for a value, matching the panel's readout. */
static inline void duo_format(const DuoParam* p, float v, char* buf, size_t n)
{
    v = duo_clamp(p, v);
    if (p->steps)
        snprintf(buf, n, "%s", p->opts[(int)(v - p->min)]);
    else if (p->unit[0])
        snprintf(buf, n, "%+.1f", v);
    else
        snprintf(buf, n, "%.1f", v);
}

/* The reverse of duo_format; returns 0 if the text means nothing. */
static inline int duo_parse(const DuoParam* p, const char* s, float* out)
{
    double d;
    int i;
    for (i = 0; p->steps && i <= p->steps; ++i) {
        const char *a = p->opts[i], *b = s;
        while (*a && *b && (*a | 32) == (*b | 32))
            ++a, ++b;
        if (!*a && !*b) {
            *out = p->min + (float)i;
            return 1;
        }
    }
#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4996) /* sscanf_s is not portable */
#endif
    if (sscanf(s, "%lf", &d) != 1)
        return 0;
#ifdef _MSC_VER
#    pragma warning(pop)
#endif
    *out = duo_clamp(p, (float)d);
    return 1;
}

#endif
