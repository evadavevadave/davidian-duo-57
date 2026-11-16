/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* The Duo '57 amp as a small C API, shared by the VST3 and Audio Unit builds. It is the LV2
 * plugin in src/duo57.c without the LV2 entry point: every port in ports.h is a float pointer
 * the wrapper connects, control ports are read once per block, and the audio ports may point
 * at the same buffer. The plugin has no latency. */
#ifndef DUO57_ENGINE_H
#define DUO57_ENGINE_H

#include <stdint.h>

#include "ports.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DuoEngine DuoEngine;

/* Allocates and builds the circuit tables; not real-time safe. */
DuoEngine* duo_engine_new(double sample_rate);
void duo_engine_connect(DuoEngine* e, uint32_t port, void* data);
/* Clears the circuit state; connect the controls first. */
void duo_engine_activate(DuoEngine* e);
void duo_engine_run(DuoEngine* e, uint32_t frames);
void duo_engine_free(DuoEngine* e);

#ifdef __cplusplus
}
#endif

#endif
