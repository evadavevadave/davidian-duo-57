/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/*
 * Davidian Duo '57 — LV2 amplifier simulation derived from a 1957 tweed 2x12 combo service
 * diagram (drawing 0064417000 rev A, 05-MAR-04).
 *
 * Signal chain (component designators refer to the schematic):
 *
 *   J1/J2 Normal, J3/J4 Bright. Each jack drives its own 12AX7 triode
 *   (R5..R8 1.5k grid stoppers, R1..R4 1M grid leaks):
 *     Normal: V1-A / V1-B,  Bright: V2-A / V2-B
 *   All four triodes share R9 470R || C1 220uF; plate loads R10/R11, R14/R15 100k.
 *   Each pair is mixed through R12/R13 (R16/R17) 100k into C2 (C3) .022.
 *   Input 1 of a channel drives both triodes in parallel; Input 2 drives one triode
 *   while the other grid is grounded (about 6 dB less). The schematic's TP14 level
 *   (48.8x at the C3 node from J3) is only reachable with both triodes driven.
 *   -> R18 1M Vol Normal / R19 1M Vol Bright (+ C4 100pF bright cap)
 *   -> R20 / R21 270k mixer -> V3-A (R23 100k, R24 1.5k || C5 22uF)
 *      with R22 10M shunt feedback from the cathode-follower output
 *   -> V3-B DC-coupled cathode follower (R25 100k) -> C6 .1
 *   -> tweed tone stack: R27 220k, R28/R29 220k, R30 1M bass + C10 .0047,
 *      C7 250pF, R26 1M treble, C8 47pF, C9 .01
 *   -> V4-A (R34 100k, R33 1.5k unbypassed) with negative feedback R32 56k from
 *      the speaker into its cathode; presence R31 5k + C11 .1 on the cathode
 *   -> V4-B cathodyne phase inverter (R38 56k, R36 1.5k + R37 56k, R35 1M)
 *   -> C13/C14 .1, R39/R40 220k bias feeds, R41/R42 1.5k
 *   -> 2x 6L6GC push-pull, fixed bias -50.2 V (33.3 mA each), 455 V plates
 *   -> output transformer -> 4 ohm (2x 8 ohm 12" speakers).
 *   Power supply: 2x 5U4 rectifiers, choke, 22uF reservoirs (modelled as sag).
 *
 * Triodes use the Koren 12AX7 model solved on each stage's AC load line into lookup
 * tables at instantiate time. The V3-A / cathode-follower pair is one table; its
 * R22 feedback loop and the power-amp NFB loop are solved implicitly every sample.
 * The tone stack is the exact nodal network (trapezoidal companion models).
 * Everything runs at 4x oversampling.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "lv2_core.h"
#include "ports.h"

#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE__)
#  include <xmmintrin.h>
#  define TW_HAVE_SSE 1
#endif

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

#define OS        4      /* oversampling factor */
#define TABLE_N   4096   /* triode transfer table size */
#define AA_STAGES 6      /* biquads in each anti-alias filter (12th order) */

/* ------------------------------------------------------------------ ports */

/* Port indices and URIs live in ports.h, shared with the GUI. */

/* --------------------------------------------------------------- helpers */

static double clampd(double x, double lo, double hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

/* Front-panel knobs are numbered 1..12. */
static double knob01(double k)
{
    return clampd((k - 1.0) / 11.0, 0.0, 1.0);
}

/* Audio ("A") taper: 10% resistance at mid travel (treble, bass). */
static double taper_audio(double p)
{
    p = clampd(p, 0.0, 1.0);
    return (pow(81.0, p) - 1.0) / 80.0;
}

/* Volume pots ("SPL. D"): the schematic test-point levels (TP14 -> TP15 at 50%
 * rotation) are reproduced with a linear law, so that is what is used. */
static double taper_volume(double p)
{
    return clampd(p, 0.0, 1.0);
}

/* ---------------------------------------------------- TPT one-pole filter */

typedef struct {
    double G; /* g / (1 + g) */
    double s;
} OnePole;

static void op_set(OnePole* f, double fc, double fs)
{
    double g = tan(M_PI * clampd(fc, 0.5, 0.45 * fs) / fs);
    f->G = g / (1.0 + g);
}

static inline double op_lp(OnePole* f, double x)
{
    double v = (x - f->s) * f->G;
    double y = v + f->s;
    f->s     = y + v;
    return y;
}

static inline double op_hp(OnePole* f, double x)
{
    return x - op_lp(f, x);
}

/* Low shelf: gain g at DC, unity at HF; pole set in f. */
static inline double shelf_low(OnePole* f, double g, double x)
{
    return x + (g - 1.0) * op_lp(f, x);
}

/* High shelf: unity at DC, gain g at HF; pole set in f. */
static inline double shelf_high(OnePole* f, double g, double x)
{
    return x + (g - 1.0) * op_hp(f, x);
}

/* Affine form of shelf_high for the current state: y = a x + b. */
static inline void shelf_high_affine(const OnePole* f, double g, double* a, double* b)
{
    double k = (g - 1.0) * (1.0 - f->G);
    *a = 1.0 + k;
    *b = -k * f->s;
}

/* ------------------------------------------------------------ biquad DF2T */

typedef struct {
    double b0, b1, b2, a1, a2;
    double z1, z2;
} Biquad;

static inline double bq_run(Biquad* q, double x)
{
    double y = q->b0 * x + q->z1;
    q->z1    = q->b1 * x - q->a1 * y + q->z2;
    q->z2    = q->b2 * x - q->a2 * y;
    return y;
}

static void bq_lowpass(Biquad* q, double fc, double Q, double fs)
{
    double w = 2.0 * M_PI * fc / fs, c = cos(w), a = sin(w) / (2.0 * Q);
    double a0 = 1.0 + a;
    q->b0 = (1.0 - c) / 2.0 / a0;
    q->b1 = (1.0 - c) / a0;
    q->b2 = q->b0;
    q->a1 = -2.0 * c / a0;
    q->a2 = (1.0 - a) / a0;
}

static void bq_highpass(Biquad* q, double fc, double Q, double fs)
{
    double w = 2.0 * M_PI * fc / fs, c = cos(w), a = sin(w) / (2.0 * Q);
    double a0 = 1.0 + a;
    q->b0 = (1.0 + c) / 2.0 / a0;
    q->b1 = -(1.0 + c) / a0;
    q->b2 = q->b0;
    q->a1 = -2.0 * c / a0;
    q->a2 = (1.0 - a) / a0;
}

static void bq_peak(Biquad* q, double fc, double Q, double gain_db, double fs)
{
    double A = pow(10.0, gain_db / 40.0);
    double w = 2.0 * M_PI * fc / fs, c = cos(w), a = sin(w) / (2.0 * Q);
    double a0 = 1.0 + a / A;
    q->b0 = (1.0 + a * A) / a0;
    q->b1 = -2.0 * c / a0;
    q->b2 = (1.0 - a * A) / a0;
    q->a1 = -2.0 * c / a0;
    q->a2 = (1.0 - a / A) / a0;
}

/* ---------------------------------------------------- 12AX7 (Koren model) */

static double koren_12ax7(double vpk, double vgk)
{
    const double mu = 100.0, ex = 1.4, kg1 = 1060.0, kp = 600.0, kvb = 300.0;
    if (vpk <= 0.0) return 0.0;
    double a  = kp * (1.0 / mu + vgk / sqrt(kvb + vpk * vpk));
    double sp = a > 30.0 ? a : log1p(exp(a));
    double e1 = vpk / kp * sp;
    return e1 > 0.0 ? 2.0 * pow(e1, ex) / kg1 : 0.0;
}

/* Grid current of a 12AX7 for positive grid-cathode voltage. */
static double grid_current(double vgk)
{
    return vgk > 0.0 ? vgk * sqrt(vgk) / 2000.0 : 0.0;
}

typedef struct {
    double B;     /* supply */
    double Ra;    /* DC plate resistor */
    double Rac;   /* AC load: Ra || next stage */
    double Rk;    /* cathode resistor */
    double ip0;   /* quiescent current */
    double vp0;   /* quiescent plate voltage (to ground) */
    double vk0;   /* quiescent cathode voltage */
} StageDC;

/* Plate current for grid voltage vin (to ground), on the AC load line.
 * rk_dyn: cathode resistance seen by the signal (0 when bypassed). */
static double solve_ip(const StageDC* d, double vin, double rk_dyn)
{
    double lo = 0.0, hi = d->ip0 + d->vp0 / d->Rac + 0.01;
    for (int i = 0; i < 64; ++i) {
        double ip = 0.5 * (lo + hi);
        double vk = rk_dyn > 0.0 ? ip * rk_dyn : d->vk0;
        double vp = d->vp0 - (ip - d->ip0) * d->Rac;
        double f  = ip - koren_12ax7(vp - vk, vin - vk);
        if (f > 0.0) hi = ip; else lo = ip;
    }
    return 0.5 * (lo + hi);
}

static void solve_dc(StageDC* d)
{
    double lo = 0.0, hi = d->B / (d->Ra + d->Rk);
    for (int i = 0; i < 64; ++i) {
        double ip = 0.5 * (lo + hi);
        double vk = ip * d->Rk;
        double f  = ip - koren_12ax7(d->B - ip * d->Ra - vk, -vk);
        if (f > 0.0) hi = ip; else lo = ip;
    }
    d->ip0 = 0.5 * (lo + hi);
    d->vk0 = d->ip0 * d->Rk;
    d->vp0 = d->B - d->ip0 * d->Ra;
}

/* A memoryless transfer table y = f(x) with linear interpolation. */
typedef struct {
    float  y[TABLE_N];
    double lo, hi, inv_step;
} Table;

static inline double table_eval(const Table* t, double x)
{
    double pos = (x - t->lo) * t->inv_step;
    if (pos <= 0.0) return t->y[0];
    if (pos >= TABLE_N - 1) return t->y[TABLE_N - 1];
    int    i  = (int)pos;
    double fr = pos - i;
    return t->y[i] + fr * (t->y[i + 1] - t->y[i]);
}

/* Value and slope (for Newton iterations). */
static inline double table_eval_d(const Table* t, double x, double* dy)
{
    double pos = (x - t->lo) * t->inv_step;
    if (pos <= 0.0)          { *dy = 0.0; return t->y[0]; }
    if (pos >= TABLE_N - 1)  { *dy = 0.0; return t->y[TABLE_N - 1]; }
    int    i  = (int)pos;
    double fr = pos - i;
    double d  = t->y[i + 1] - t->y[i];
    *dy = d * t->inv_step;
    return t->y[i] + fr * d;
}

/* A common-cathode triode gain stage. */
typedef struct {
    Table   tab;            /* AC plate voltage (inverting) vs grid voltage */
    double  grid_thr;       /* grid voltage where grid conduction starts */
    double  grid_knee;      /* softness of grid-conduction limiting */
    int     bypassed;
    double  cath_g;         /* low-frequency gain ratio for a bypassed cathode */
    double  Rk;
    OnePole cath;           /* cathode bypass shelf */
    StageDC dc;
} Triode;

static inline double grid_limit(double x, double thr, double k)
{
    return x > thr ? thr + k * tanh((x - thr) / k) : x;
}

static void triode_build(Triode* t, double B, double Ra, double Rnext, double Rk,
                         int bypassed, double knee)
{
    StageDC d;
    d.B   = B;
    d.Ra  = Ra;
    d.Rk  = Rk;
    d.Rac = Ra * Rnext / (Ra + Rnext);
    solve_dc(&d);
    t->dc = d;

    double rk_dyn = bypassed ? 0.0 : Rk;
    t->bypassed   = bypassed;
    t->Rk         = Rk;
    t->grid_knee  = knee;

    if (bypassed) {
        t->grid_thr = d.vk0;
    } else {
        double lo = 0.0, hi = 400.0;
        for (int i = 0; i < 60; ++i) {
            double v = 0.5 * (lo + hi);
            if (v - solve_ip(&d, v, Rk) * Rk > 0.0) hi = v; else lo = v;
        }
        t->grid_thr = 0.5 * (lo + hi);
    }

    Table* tb    = &t->tab;
    tb->lo       = fmin(t->grid_thr, 0.0) - 60.0;
    tb->hi       = t->grid_thr + 4.0 * knee + 0.5;
    tb->inv_step = (TABLE_N - 1) / (tb->hi - tb->lo);

    double off = -(solve_ip(&d, 0.0, rk_dyn) - d.ip0) * d.Rac;
    for (int i = 0; i < TABLE_N; ++i) {
        double vin = tb->lo + i / tb->inv_step;
        double ip  = solve_ip(&d, vin, rk_dyn);
        tb->y[i]   = (float)(-(ip - d.ip0) * d.Rac - off);
    }

    t->cath_g = 1.0;
    if (bypassed) {
        const double dv = 0.01;
        double gb = (solve_ip(&d, dv, 0.0) - solve_ip(&d, -dv, 0.0));
        double gu = (solve_ip(&d, dv, Rk) - solve_ip(&d, -dv, Rk));
        if (gb > 0.0) t->cath_g = gu / gb;
    }
    t->cath.s = 0.0;
}

static void triode_set_cathode_cap(Triode* t, double Ck, double fs)
{
    if (!t->bypassed) return;
    double fz = 1.0 / (2.0 * M_PI * t->Rk * Ck);
    op_set(&t->cath, fz / t->cath_g, fs);
}

/* Static part of the stage (grid limiting + load line), no filters. */
static inline double triode_static(const Triode* t, double x)
{
    return table_eval(&t->tab, grid_limit(x, t->grid_thr, t->grid_knee));
}

static inline double triode_run(Triode* t, double x)
{
    if (t->bypassed) x = shelf_low(&t->cath, t->cath_g, x);
    return triode_static(t, x);
}

/* ------------------------------------- V3-A + V3-B cathode follower table */

/* Cathode follower V3-B: plate on B+, cathode R25 100k to ground, grid DC-coupled
 * to the V3-A plate. Input: V3-A plate voltage (Thevenin, absolute) with source
 * resistance Rs. Returns the cathode voltage. Grid current through Rs limits the
 * positive swing. */
static double cf_solve(double vth, double B, double Rk, double Rs)
{
    double lo = 0.0, hi = B;
    for (int i = 0; i < 60; ++i) {
        double vk = 0.5 * (lo + hi);
        /* grid voltage with grid current: vg = vth - Rs Ig(vg - vk) */
        double glo = vk - 50.0, ghi = fmax(vth, vk);
        if (glo > ghi) glo = ghi - 50.0;
        for (int j = 0; j < 50; ++j) {
            double vg = 0.5 * (glo + ghi);
            if (vg - vth + Rs * grid_current(vg - vk) > 0.0) ghi = vg; else glo = vg;
        }
        double vg = 0.5 * (glo + ghi);
        double ik = koren_12ax7(B - vk, vg - vk) + grid_current(vg - vk);
        if (ik - vk / Rk > 0.0) lo = vk; else hi = vk;
    }
    return 0.5 * (lo + hi);
}

/* Combined table: V3-A grid voltage (AC) -> cathode-follower output (AC). */
static void build_v3_table(Table* t, double* thr_out)
{
    Triode v3a;
    triode_build(&v3a, 302.0, 100e3, 10e6, 1.5e3, 1, 0.5);

    const double B = 302.0, Rk = 100e3, Rs = 70e3;
    t->lo       = v3a.tab.lo;
    t->hi       = v3a.tab.hi;
    t->inv_step = v3a.tab.inv_step;
    double vk0 = cf_solve(v3a.dc.vp0, B, Rk, Rs);
    for (int i = 0; i < TABLE_N; ++i) {
        double vin = t->lo + i / t->inv_step;
        double vp  = v3a.dc.vp0 + triode_static(&v3a, vin);
        t->y[i]    = (float)(cf_solve(vp, B, Rk, Rs) - vk0);
    }
    *thr_out = v3a.grid_thr;
}

/* ---------------------------------------------------- tone stack (nodal) */

/* Nodes */
enum { N_S = 0, N_A, N_X, N_W, N_Y, N_B, N_Z, TS_N };
/* Capacitors */
enum { C_6 = 0, C_7, C_8, C_9, C_10, C_IN, TS_NC };

typedef struct {
    double Ginv[TS_N][TS_N];
    double geq[TS_NC];
    int    cp[TS_NC], cn[TS_NC];   /* nodes (-1 = ground) */
    double cv[TS_NC], ci[TS_NC];   /* last branch voltage / current */
    double gs;                     /* source conductance (CF output) */
    double cur_t, cur_b;
    int    valid;
} ToneStack;

static void stamp_g(double G[TS_N][TS_N], int a, int b, double g)
{
    if (a >= 0) G[a][a] += g;
    if (b >= 0) G[b][b] += g;
    if (a >= 0 && b >= 0) { G[a][b] -= g; G[b][a] -= g; }
}

static int invert(double A[TS_N][TS_N], double R[TS_N][TS_N])
{
    double M[TS_N][2 * TS_N];
    for (int i = 0; i < TS_N; ++i)
        for (int j = 0; j < TS_N; ++j) {
            M[i][j] = A[i][j];
            M[i][j + TS_N] = (i == j) ? 1.0 : 0.0;
        }
    for (int c = 0; c < TS_N; ++c) {
        int p = c;
        for (int r = c + 1; r < TS_N; ++r)
            if (fabs(M[r][c]) > fabs(M[p][c])) p = r;
        if (fabs(M[p][c]) < 1e-300) return 0;
        if (p != c)
            for (int j = 0; j < 2 * TS_N; ++j) { double t = M[c][j]; M[c][j] = M[p][j]; M[p][j] = t; }
        double inv = 1.0 / M[c][c];
        for (int j = 0; j < 2 * TS_N; ++j) M[c][j] *= inv;
        for (int r = 0; r < TS_N; ++r) {
            if (r == c) continue;
            double f = M[r][c];
            if (f == 0.0) continue;
            for (int j = 0; j < 2 * TS_N; ++j) M[r][j] -= f * M[c][j];
        }
    }
    for (int i = 0; i < TS_N; ++i)
        for (int j = 0; j < TS_N; ++j) R[i][j] = M[i][j + TS_N];
    return 1;
}

static void tonestack_init(ToneStack* ts, double fs)
{
    static const double C[TS_NC]  = { 0.1e-6, 250e-12, 47e-12, 0.01e-6, 0.0047e-6, 60e-12 };
    static const int    cp[TS_NC] = { N_S, N_S, N_Y, N_B, N_W, N_Z };
    static const int    cn[TS_NC] = { N_A, N_Y, N_Z, -1,  -1,  -1  };
    for (int k = 0; k < TS_NC; ++k) {
        ts->geq[k] = 2.0 * C[k] * fs;
        ts->cp[k]  = cp[k];
        ts->cn[k]  = cn[k];
        ts->cv[k]  = ts->ci[k] = 0.0;
    }
    ts->gs    = 1.0 / 650.0; /* V3-B output impedance ~ 1/gm */
    ts->valid = 0;
}

/* treble, bass: knob position 0..1 */
static void tonestack_design(ToneStack* ts, double treble, double bass)
{
    const double Rpot = 1e6, Rmin = 100.0;
    double tt = taper_audio(treble);
    double tb = taper_audio(bass);
    double G[TS_N][TS_N];
    memset(G, 0, sizeof G);

    stamp_g(G, N_S, -1, ts->gs);
    stamp_g(G, N_A, -1, 1.0 / 220e3);                             /* R27 */
    stamp_g(G, N_A, N_X, 1.0 / 220e3);                            /* R28 */
    stamp_g(G, N_X, N_Z, 1.0 / 220e3);                            /* R29 */
    stamp_g(G, N_X, -1,  1.0 / fmax(tb * Rpot, Rmin));            /* R30 wiper-ground */
    stamp_g(G, N_X, N_W, 1.0 / fmax((1.0 - tb) * Rpot, Rmin));    /* R30 wiper-C10 */
    stamp_g(G, N_Y, N_Z, 1.0 / fmax((1.0 - tt) * Rpot, Rmin));    /* R26 top-wiper */
    stamp_g(G, N_Z, N_B, 1.0 / fmax(tt * Rpot, Rmin));            /* R26 wiper-bottom */
    stamp_g(G, N_Z, -1,  1.0 / 10e6);                             /* V4-A grid leakage */
    for (int k = 0; k < TS_NC; ++k) stamp_g(G, ts->cp[k], ts->cn[k], ts->geq[k]);

    invert(G, ts->Ginv);
}

/* Affine response of node A to the source u this sample: vA = kA u + cA. */
static inline void tonestack_node_affine(const ToneStack* ts, int node, double* k, double* c)
{
    double b[TS_N] = { 0 };
    for (int m = 0; m < TS_NC; ++m) {
        double J = -ts->geq[m] * ts->cv[m] - ts->ci[m];
        if (ts->cp[m] >= 0) b[ts->cp[m]] -= J;
        if (ts->cn[m] >= 0) b[ts->cn[m]] += J;
    }
    double s = 0.0;
    for (int j = 0; j < TS_N; ++j) s += ts->Ginv[node][j] * b[j];
    *c = s;
    *k = ts->Ginv[node][N_S] * ts->gs;
}

static inline double tonestack_run(ToneStack* ts, double u)
{
    double b[TS_N] = { 0 };
    double J[TS_NC];
    b[N_S] = u * ts->gs;
    for (int m = 0; m < TS_NC; ++m) {
        J[m] = -ts->geq[m] * ts->cv[m] - ts->ci[m];
        if (ts->cp[m] >= 0) b[ts->cp[m]] -= J[m];
        if (ts->cn[m] >= 0) b[ts->cn[m]] += J[m];
    }
    double v[TS_N];
    for (int i = 0; i < TS_N; ++i) {
        double s = 0.0;
        for (int j = 0; j < TS_N; ++j) s += ts->Ginv[i][j] * b[j];
        v[i] = s;
    }
    for (int m = 0; m < TS_NC; ++m) {
        double vb = v[ts->cp[m]] - (ts->cn[m] >= 0 ? v[ts->cn[m]] : 0.0);
        ts->ci[m] = ts->geq[m] * vb + J[m];
        ts->cv[m] = vb;
    }
    return v[N_Z];
}

/* ------------------------------------------------------------ power amp */

#define PA_BIAS     50.2    /* 6L6GC grid bias (V), TP2                    */
#define PA_VCO      63.0    /* grid cutoff distance at 455 V screens       */
#define PA_K        0.727e-3/* A / V^1.5 -> 33.3 mA at idle                */
#define PA_RL       53.7    /* speaker volts per amp of (Ia - Ib)          */
#define PA_VSAT     21.0    /* speaker voltage where the plates bottom out */
#define PA_B0       475.0   /* unloaded B+                                  */
#define PA_RSAG     300.0   /* rectifier + transformer resistance           */
#define PA_BNOM     455.0
#define PI_GAIN     0.95    /* cathodyne gain per side                       */
#define PI_POS      80.0    /* cathodyne swing limits                        */
#define PI_NEG      64.0
#define G_RSRC      8e3     /* PI source impedance into a conducting grid    */
#define G_RON       2.5e3   /* R41/R42 + grid diode                          */
#define G_RLEAK     220e3   /* R39 / R40                                     */
#define G_CC        0.1e-6  /* C13 / C14                                     */
#define NFB_RK      1.5e3   /* R33                                           */
#define NFB_R       56e3    /* R32                                           */

typedef struct {
    Triode  v4a;
    OnePole d_shelf;        /* V4-A cathode degeneration vs presence      */
    OnePole l_shelf;        /* feedback shunted by presence (R31 + C11)    */
    double  gD, gL;
    OnePole ot_hp, ot_lp;   /* output transformer bandwidth                */
    double  capA, capB;     /* coupling-cap voltages (C13, C14)            */
    double  i_avg;          /* supply current (for sag)                    */
    OnePole sag_lp;
    double  bplus;
    double  w;              /* last loop solution                          */
    double  T;              /* oversampled period                          */
    double  beta;
    double  ia, ib, iga, igb, na, nb; /* last evaluated internals */
} PowerAmp;

static inline double relu_smooth(double e)
{
    return 0.5 * (e + sqrt(e * e + 1.0));
}

static inline double pentode_i(double vg, double s)
{
    double z = vg + PA_VCO * s - PA_BIAS;
    return z > 0.0 ? PA_K * z * sqrt(z) : 0.0;
}

/* V4-A + cathodyne + 6L6 pair: speaker voltage (before OT filtering). */
static inline double power_h(PowerAmp* p, double w)
{
    double s   = p->bplus / PA_BNOM;
    double v4a = triode_static(&p->v4a, w);
    double vd  = PI_GAIN * v4a;
    vd = vd > 0.0 ? PI_POS * tanh(vd / PI_POS) : PI_NEG * tanh(vd / PI_NEG);

    /* plate side (inverted) -> V5, cathode side -> V6 */
    double ra = -vd - p->capA, rb = vd - p->capB;
    const double share = G_RSRC / (G_RSRC + G_RON);
    double ea = relu_smooth(ra - PA_BIAS), eb = relu_smooth(rb - PA_BIAS);
    p->na  = ra - share * ea;
    p->nb  = rb - share * eb;
    p->iga = ea / (G_RSRC + G_RON);
    p->igb = eb / (G_RSRC + G_RON);

    p->ia = pentode_i(p->na, s);
    p->ib = pentode_i(p->nb, s);
    double vsat = PA_VSAT * s;
    return vsat * tanh((p->ia - p->ib) * PA_RL / vsat);
}

static void power_set_presence(PowerAmp* p, double pres, double fs)
{
    /* R31 5k rheostat in series with C11; fully clockwise shorts it. */
    const double Rk = NFB_RK, C = 0.1e-6;
    double Rp = fmax(5e3 * (1.0 - pres), 10.0);
    double k  = 0.93; /* gm Rk of V4-A */
    p->gL = Rp / (Rk + Rp);
    op_set(&p->l_shelf, 1.0 / (2.0 * M_PI * (Rk + Rp) * C), fs);
    /* D(s) = (1 + k)(1 + s (Rk+Rp) C) / ((1 + k) + s C (Rk + Rp + k Rp)) */
    p->gD = (1.0 + k) * (Rk + Rp) / (Rk + Rp + k * Rp);
    op_set(&p->d_shelf, (1.0 + k) / (2.0 * M_PI * C * (Rk + Rp + k * Rp)), fs);
}

static double power_run(PowerAmp* p, double u)
{
    /* Affine responses of the linear parts for this sample. */
    double dA, dB, lA, lB;
    shelf_high_affine(&p->d_shelf, p->gD, &dA, &dB);

    /* feedback: L shelf is a high shelf with gain gL (< 1) */
    shelf_high_affine(&p->l_shelf, p->gL, &lA, &lB);

    /* OT: high-pass then low-pass */
    double G1 = p->ot_hp.G, s1 = p->ot_hp.s;
    double G2 = p->ot_lp.G, s2 = p->ot_lp.s;
    double oA = 1.0 - G1, oB = -(1.0 - G1) * s1;
    oA = G2 * oA; oB = G2 * oB + (1.0 - G2) * s2;

    /* w = dA (u - beta (lA (oA h + oB) + lB)) + dB */
    double L      = dA * p->beta * lA * oA;
    double target = dA * (u - p->beta * (lA * oB + lB)) + dB;

    double w = p->w;
    for (int it = 0; it < 8; ++it) {
        const double d = 1e-3;
        double h  = power_h(p, w);
        double g  = w + L * h - target;
        double dg = 1.0 + L * (power_h(p, w + d) - power_h(p, w - d)) / (2.0 * d);
        if (dg < 0.05) dg = 0.05;
        double step = g / dg;
        w -= step;
        if (fabs(step) < 1e-7) break;
    }
    p->w = w;

    /* Commit the solved sample. */
    double h  = power_h(p, w);
    double y  = op_lp(&p->ot_lp, op_hp(&p->ot_hp, h));
    (void)shelf_high(&p->l_shelf, p->gL, y);
    (void)shelf_high(&p->d_shelf, p->gD, u - p->beta * (lA * y + lB));

    /* coupling caps charge through grid conduction and discharge via R39/R40 */
    p->capA += p->T / G_CC * (p->na / G_RLEAK + p->iga);
    p->capB += p->T / G_CC * (p->nb / G_RLEAK + p->igb);

    /* rectifier sag */
    double itot = op_lp(&p->sag_lp, p->ia + p->ib + 0.012);
    p->bplus = PA_B0 - PA_RSAG * itot;
    return y;
}

/* ----------------------------------------------------------- plugin */

typedef struct {
    const float* port[P_COUNT];
    float*       out;

    double fs, fso;

    Biquad up[AA_STAGES], down[AA_STAGES];

    OnePole in_hp;

    Triode  v_single;   /* Input 2: one triode driven, the other idle   */
    Triode  v_dual;     /* Input 1: both triodes of the pair driven     */
    double  div_single, div_dual;
    OnePole cath_n, cath_b; /* per-channel cathode bypass shelf state */

    OnePole cn_hp, cb_hp;   /* C2 / C3 into the volume pots */
    OnePole bright;         /* C4 across the upper part of R19 */
    OnePole miller;         /* mixer impedance x V3-A input capacitance */

    Table   v3;             /* V3-A + V3-B */
    double  v3_g;           /* last loop solution */

    ToneStack ts;
    PowerAmp  pa;

    Biquad cab[6];

    double sm_vn, sm_vb, sm_in, sm_out;
    double sm_coef;
    double last_pres;
    int    input;
    int    primed;
} Duo57;

static void build_aa(Biquad* f, double fc, double fs)
{
    for (int k = 0; k < AA_STAGES; ++k) {
        double Q = 1.0 / (2.0 * sin((2.0 * (k + 1) - 1.0) * M_PI / (4.0 * AA_STAGES)));
        bq_lowpass(&f[k], fc, Q, fs);
        f[k].z1 = f[k].z2 = 0.0;
    }
}

static LV2_Handle instantiate(const LV2_Descriptor* desc, double rate,
                              const char* path, const LV2_Feature* const* features)
{
    (void)desc; (void)path; (void)features;
    Duo57* s = (Duo57*)calloc(1, sizeof(Duo57));
    if (!s) return NULL;

    s->fs  = rate;
    s->fso = rate * OS;
    double fso = s->fso;

    build_aa(s->up,   fmin(0.45 * rate, 20000.0), fso);
    build_aa(s->down, fmin(0.45 * rate, 20000.0), fso);

    op_set(&s->in_hp, 5.0, fso);

    /* Input triodes: shared R9 470R across four triodes acts as 1.88k each;
     * C1 220uF shared -> 55uF each.
     * Single: AC load R10 || (R12 + R13 + idle triode rp || R11, || 1M pot).
     * Dual:   both triodes drive the C2 node together. */
    triode_build(&s->v_single, 302.0, 100e3, 221e3, 1.88e3, 1, 0.5);
    triode_build(&s->v_dual,   302.0, 100e3, 2.1e6, 1.88e3, 1, 0.5);
    triode_set_cathode_cap(&s->v_single, 55e-6, fso);
    triode_set_cathode_cap(&s->v_dual,   55e-6, fso);
    s->div_single = 121e3 / 221e3;
    s->div_dual   = 2.0e6 / 2.1e6;

    op_set(&s->cn_hp, 1.0 / (2.0 * M_PI * 1e6 * 0.022e-6), fso);
    op_set(&s->cb_hp, 1.0 / (2.0 * M_PI * 1e6 * 0.022e-6), fso);

    {
        double thr;
        build_v3_table(&s->v3, &thr);
        (void)thr;
    }

    tonestack_init(&s->ts, fso);

    /* V4-A: R34 100k from 370 V, R33 1.5k || R32 56k unbypassed, PI grid next. */
    triode_build(&s->pa.v4a, 370.0, 100e3, 1e6, 1.46e3, 0, 0.5);
    s->pa.beta = NFB_RK / (NFB_RK + NFB_R);
    s->pa.T    = 1.0 / fso;
    op_set(&s->pa.ot_hp, 45.0, fso);
    op_set(&s->pa.ot_lp, 11000.0, fso);
    op_set(&s->pa.sag_lp, 1.0 / (2.0 * M_PI * 0.04), fso);
    power_set_presence(&s->pa, 0.5, fso);
    s->last_pres = -1.0;

    /* Cabinet: open-back 2x12 alnico, approximate, at base rate. */
    bq_highpass(&s->cab[0], 80.0, 0.7, rate);
    bq_peak(&s->cab[1], 120.0, 1.2, 2.5, rate);
    bq_peak(&s->cab[2], 500.0, 0.9, -2.5, rate);
    bq_peak(&s->cab[3], 2300.0, 1.4, 4.0, rate);
    bq_lowpass(&s->cab[4], fmin(5500.0, 0.45 * rate), 0.6, rate);
    bq_lowpass(&s->cab[5], fmin(8000.0, 0.45 * rate), 0.707, rate);

    s->sm_coef = 1.0 - exp(-1.0 / (0.015 * rate));
    s->input   = -1;
    return (LV2_Handle)s;
}

static void connect_port(LV2_Handle h, uint32_t port, void* data)
{
    Duo57* s = (Duo57*)h;
    if (port == P_OUT) s->out = (float*)data;
    else if (port < P_COUNT) s->port[port] = (const float*)data;
}

static double port_val(const Duo57* s, int p, double def)
{
    return s->port[p] ? (double)*s->port[p] : def;
}

static void reset_state(Duo57* s)
{
    for (int k = 0; k < AA_STAGES; ++k) {
        s->up[k].z1 = s->up[k].z2 = 0.0;
        s->down[k].z1 = s->down[k].z2 = 0.0;
    }
    for (int k = 0; k < 6; ++k) s->cab[k].z1 = s->cab[k].z2 = 0.0;
    s->cath_n.s = s->cath_b.s = 0.0;
    s->input = -1;
    OnePole* op[] = { &s->in_hp, &s->cn_hp, &s->cb_hp, &s->bright, &s->miller,
                      &s->pa.d_shelf, &s->pa.l_shelf, &s->pa.ot_hp, &s->pa.ot_lp };
    for (size_t k = 0; k < sizeof(op) / sizeof(op[0]); ++k) op[k]->s = 0.0;
    for (int k = 0; k < TS_NC; ++k) s->ts.cv[k] = s->ts.ci[k] = 0.0;
    s->v3_g = 0.0;
    s->pa.w = 0.0;
    s->pa.capA = s->pa.capB = 0.0;
    s->pa.bplus = PA_BNOM;
    s->pa.sag_lp.s = (PA_B0 - PA_BNOM) / PA_RSAG;
    s->primed = 0;
    s->ts.valid = 0;
}

static void activate(LV2_Handle h)
{
    reset_state((Duo57*)h);
}

#ifndef TW_PROBE
#  define TW_PROBE(i, v) ((void)0)
#endif

/* Per-block mixer constants. */
typedef struct {
    double tn, tb;          /* volume tapers */
    int    use_n, use_b;
    int    dual_n, dual_b;
} BlockCfg;

static inline double amp_tick(Duo57* s, double x, const BlockCfg* c,
                              double tn, double tb)
{
    x = op_hp(&s->in_hp, x);
    TW_PROBE(0, x);

    /* Input triodes -> C2 / C3 */
    double vn = 0.0, vb = 0.0;
    if (c->use_n) {
        const Triode* t = c->dual_n ? &s->v_dual : &s->v_single;
        vn = triode_static(t, shelf_low(&s->cath_n, t->cath_g, x))
           * (c->dual_n ? s->div_dual : s->div_single);
    }
    if (c->use_b) {
        const Triode* t = c->dual_b ? &s->v_dual : &s->v_single;
        vb = triode_static(t, shelf_low(&s->cath_b, t->cath_g, x))
           * (c->dual_b ? s->div_dual : s->div_single);
    }
    vn = op_hp(&s->cn_hp, vn);
    vb = op_hp(&s->cb_hp, vb);
    TW_PROBE(1, vb != 0.0 ? vb : vn);

    /* Volume pots: wiper Thevenin sources into R20 / R21 270k */
    const double Rp = 1e6, Rm = 270e3;
    double vnw = vn * tn;
    /* C4 bright cap across the upper part of R19; the shelf cannot represent
     * the pot at its ground end (pole beyond Nyquist), so fade it out there. */
    double vbw = shelf_low(&s->bright, tb, vb) * (tb / (tb + 1e-3));
    double Rn  = Rm + tn * (1.0 - tn) * Rp;
    double Rb  = Rm + tb * (1.0 - tb) * Rp;
    double gN  = 1.0 / Rn, gB = 1.0 / Rb;
    double Rth = 1.0 / (gN + gB);
    double vth = (vnw * gN + vbw * gB) * Rth;
    vth = op_lp(&s->miller, vth);

    /* V3-A grid: vg = a vth + b vA,  vA = kA F(vg) + cA  (R22 10M feedback) */
    const double Rf = 10e6;
    double a = Rf / (Rf + Rth), b = Rth / (Rf + Rth);
    double kA, cA;
    tonestack_node_affine(&s->ts, N_A, &kA, &cA);
    double vg = s->v3_g;
    for (int it = 0; it < 6; ++it) {
        double dF;
        double F  = table_eval_d(&s->v3, vg, &dF);
        double g  = vg - a * vth - b * (kA * F + cA);
        double dg = 1.0 - b * kA * dF;
        double st = g / dg;
        vg -= st;
        if (fabs(st) < 1e-7) break;
    }
    s->v3_g = vg;
    TW_PROBE(2, vg);
    double cf = table_eval(&s->v3, vg);
    TW_PROBE(3, cf);

    double z = tonestack_run(&s->ts, cf);
    TW_PROBE(4, z);

    double y = power_run(&s->pa, z);
    TW_PROBE(5, y);
    return y;
}

static void run(LV2_Handle h, uint32_t n)
{
    Duo57* s = (Duo57*)h;
    const float* in = s->port[P_IN];
    float* out = s->out;
    if (!in || !out) return;

#ifdef TW_HAVE_SSE
    unsigned int csr = _mm_getcsr();
    _mm_setcsr(csr | 0x8040); /* FTZ | DAZ */
#endif

    int input = (int)lrint(clampd(port_val(s, P_INPUT, IN_BRIGHT1), 0.0, 4.0));
    BlockCfg cfg;
    cfg.use_n  = input == IN_NORMAL1 || input == IN_NORMAL2 || input == IN_BOTH;
    cfg.use_b  = input == IN_BRIGHT1 || input == IN_BRIGHT2 || input == IN_BOTH;
    /* Input 1 of a channel drives both of its triodes, Input 2 only one. */
    cfg.dual_n = input == IN_NORMAL1 || input == IN_BOTH;
    cfg.dual_b = input == IN_BRIGHT1 || input == IN_BOTH;
    if (input != s->input) {
        /* re-plugging: clear the coupling states of the input section */
        s->cn_hp.s = s->cb_hp.s = s->bright.s = 0.0;
        s->cath_n.G = (cfg.dual_n ? &s->v_dual : &s->v_single)->cath.G;
        s->cath_b.G = (cfg.dual_b ? &s->v_dual : &s->v_single)->cath.G;
        s->cath_n.s = s->cath_b.s = 0.0;
        s->input = input;
    }

    double vn_t  = taper_volume(knob01(port_val(s, P_VOL_NORMAL, 6.0)));
    double vb_t  = taper_volume(knob01(port_val(s, P_VOL_BRIGHT, 6.0)));
    double in_t  = pow(10.0, clampd(port_val(s, P_IN_TRIM, 0.0), -24.0, 24.0) / 20.0);
    double out_t = pow(10.0, clampd(port_val(s, P_OUT_LEVEL, 0.0), -60.0, 12.0) / 20.0);
    int    cab   = port_val(s, P_CAB, 1.0) > 0.5;

    if (!s->primed) {
        s->sm_vn = vn_t; s->sm_vb = vb_t; s->sm_in = in_t; s->sm_out = out_t;
        s->primed = 1;
    }

    /* Bright cap and Miller corner follow the (block-start) pot positions. */
    {
        double rl = fmax(s->sm_vb * 1e6, 1.0), ru = fmax((1.0 - s->sm_vb) * 1e6, 1.0);
        op_set(&s->bright, 1.0 / (2.0 * M_PI * (rl * ru / (rl + ru)) * 100e-12), s->fso);
        double Rn  = 270e3 + s->sm_vn * (1.0 - s->sm_vn) * 1e6;
        double Rb  = 270e3 + s->sm_vb * (1.0 - s->sm_vb) * 1e6;
        double Rth = 1.0 / (1.0 / Rn + 1.0 / Rb);
        op_set(&s->miller, 1.0 / (2.0 * M_PI * Rth * 100e-12), s->fso);
    }

    /* Tone stack: glide toward targets once per block. */
    {
        double tt = knob01(port_val(s, P_TREBLE, 7.0));
        double tl = knob01(port_val(s, P_BASS, 5.0));
        if (!s->ts.valid) {
            s->ts.cur_t = tt; s->ts.cur_b = tl;
            tonestack_design(&s->ts, tt, tl);
            s->ts.valid = 1;
        } else if (fabs(tt - s->ts.cur_t) + fabs(tl - s->ts.cur_b) > 1e-5) {
            double k = fmin(1.0, n / (0.03 * s->fs));
            s->ts.cur_t += (tt - s->ts.cur_t) * k;
            s->ts.cur_b += (tl - s->ts.cur_b) * k;
            tonestack_design(&s->ts, s->ts.cur_t, s->ts.cur_b);
        }
    }

    {
        double pres = knob01(port_val(s, P_PRESENCE, 6.0));
        if (pres != s->last_pres) {
            power_set_presence(&s->pa, pres, s->fso);
            s->last_pres = pres;
        }
    }

    const double c = s->sm_coef;
    for (uint32_t i = 0; i < n; ++i) {
        s->sm_vn  += (vn_t - s->sm_vn) * c;
        s->sm_vb  += (vb_t - s->sm_vb) * c;
        s->sm_in  += (in_t - s->sm_in) * c;
        s->sm_out += (out_t - s->sm_out) * c;

        double x = (double)in[i] * s->sm_in;
        double acc = 0.0;
        for (int o = 0; o < OS; ++o) {
            double u = (o == 0) ? x * OS : 0.0;
            for (int k = 0; k < AA_STAGES; ++k) u = bq_run(&s->up[k], u);
            double y = amp_tick(s, u, &cfg, s->sm_vn, s->sm_vb);
            for (int k = 0; k < AA_STAGES; ++k) y = bq_run(&s->down[k], y);
            if (o == OS - 1) acc = y;
        }

        /* speaker volts -> digital: full power (~17 V peak) is about -4 dBFS */
        double y = acc * 0.035;
        if (cab) {
            for (int k = 0; k < 6; ++k) y = bq_run(&s->cab[k], y);
        }
        y *= s->sm_out;
        if (!(y == y) || fabs(y) > 1e6) { reset_state(s); y = 0.0; }
        out[i] = (float)y;
    }

#ifdef TW_HAVE_SSE
    _mm_setcsr(csr);
#endif
}

static void deactivate(LV2_Handle h) { (void)h; }

static void cleanup(LV2_Handle h) { free(h); }

static const void* extension_data(const char* uri)
{
    (void)uri;
    return NULL;
}

static const LV2_Descriptor descriptor = {
    DUO57_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}
