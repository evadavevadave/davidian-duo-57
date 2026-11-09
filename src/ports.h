/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Port indices, shared by the plugin and its GUI. They must match duo57.lv2/duo57.ttl. */
#ifndef DUO57_PORTS_H
#define DUO57_PORTS_H

#define DUO57_URI    "urn:davidian:duo-57"
#define DUO57_UI_URI "urn:davidian:duo-57#ui"

typedef enum {
    P_IN = 0,
    P_OUT,
    P_INPUT,        /* 0 Normal 1, 1 Normal 2, 2 Bright 1, 3 Bright 2, 4 Normal 1 + Bright 1 */
    P_VOL_NORMAL,
    P_VOL_BRIGHT,
    P_TREBLE,
    P_BASS,
    P_PRESENCE,
    P_CAB,
    P_IN_TRIM,
    P_OUT_LEVEL,
    P_COUNT
} PortIndex;

enum { IN_NORMAL1 = 0, IN_NORMAL2, IN_BRIGHT1, IN_BRIGHT2, IN_BOTH };

/* Cabinet port values. 0 and 1 keep their meaning from the old on/off switch,
 * so sessions saved with it load unchanged. */
enum {
    CAB_OFF = 0,
    CAB_2X12_OPEN,    /* the amp's own speakers (default) */
    CAB_1X12_OPEN,
    CAB_4X10_OPEN,
    CAB_2X12_CLOSED,
    CAB_4X12_CLOSED,
    CAB_COUNT
};

#endif
