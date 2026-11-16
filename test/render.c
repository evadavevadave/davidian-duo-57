/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Offline test harness: runs the plugin through its LV2 descriptor on synthetic
 * guitar signals, checks for NaN/instability, compares internal levels with the
 * schematic's test-point voltages, reports levels and speed, and writes WAV files. */
#include <stdio.h>
#include <time.h>

/* Probe RMS accumulators for the calibration run (oversampled-rate values). */
static double g_probe_sum[8];
static long   g_probe_n[8];
static int    g_probe_on;
#define TW_PROBE(i, v) do { if (g_probe_on) { g_probe_sum[i] += (v) * (v); g_probe_n[i]++; } } while (0)

#include "../src/duo57.c"

#define SR 48000.0

static void write_wav(const char* path, const float* x, int n)
{
    FILE* f = fopen(path, "wb");
    if (!f) return;
    int sr = (int)SR, bytes = n * 2;
    fwrite("RIFF", 1, 4, f);
    int v = 36 + bytes; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f);
    short s = 1; fwrite(&s, 2, 1, f); fwrite(&s, 2, 1, f);
    fwrite(&sr, 4, 1, f);
    v = sr * 2; fwrite(&v, 4, 1, f);
    s = 2; fwrite(&s, 2, 1, f);
    s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (int i = 0; i < n; ++i) {
        double y = x[i] < -1 ? -1 : (x[i] > 1 ? 1 : x[i]);
        short q = (short)lrint(y * 32767.0);
        fwrite(&q, 2, 1, f);
    }
    fclose(f);
}

/* Karplus-Strong open E major chord, repeated strums. */
static void make_guitar(float* x, int n, double level)
{
    double freqs[4] = { 82.41, 123.47, 164.81, 207.65 };
    unsigned seed = 1234;
    for (int i = 0; i < n; ++i) x[i] = 0.0f;
    for (int strum = 0; strum * (int)SR < n; ++strum) {
        int start = strum * (int)SR;
        for (int s = 0; s < 4; ++s) {
            int L = (int)(SR / freqs[s]);
            float* buf = (float*)malloc(sizeof(float) * L);
            for (int k = 0; k < L; ++k) {
                seed = seed * 1103515245u + 12345u;
                buf[k] = (float)(((seed >> 9) & 0xFFFF) / 32768.0 - 1.0);
            }
            int p = 0;
            for (int i = start + s * 250; i < n && i < start + (int)SR; ++i) {
                int q = (p + 1) % L;
                float out = buf[p];
                buf[p] = 0.4985f * (buf[p] + buf[q]);
                p = q;
                x[i] += (float)(out * level / 4.0);
            }
            free(buf);
        }
    }
}

static LV2_Handle open_plugin(const LV2_Descriptor* d, float* ctl, float* in, float* out)
{
    LV2_Handle h = d->instantiate(d, SR, "", NULL);
    d->connect_port(h, P_IN, in);
    d->connect_port(h, P_OUT, out);
    for (int p = 2; p < P_COUNT; ++p) d->connect_port(h, p, &ctl[p]);
    d->activate(h);
    return h;
}

int main(void)
{
    const LV2_Descriptor* d = lv2_descriptor(0);
    if (!d || lv2_descriptor(1) != NULL) { printf("descriptor FAIL\n"); return 1; }

    const int N = (int)SR * 4;
    float* in  = (float*)malloc(sizeof(float) * N);
    float* out = (float*)malloc(sizeof(float) * N);
    int fail = 0;

    /* --- Schematic test conditions: 10 mV rms 1 kHz at J3, controls at 50%. */
    {
        float ctl[P_COUNT] = { 0 };
        ctl[P_INPUT] = IN_BRIGHT1;
        ctl[P_VOL_NORMAL] = ctl[P_VOL_BRIGHT] = 6.5f;
        ctl[P_TREBLE] = ctl[P_BASS] = ctl[P_PRESENCE] = 6.5f;
        ctl[P_CAB] = 0; ctl[P_IN_TRIM] = 0; ctl[P_OUT_LEVEL] = 0;
        for (int i = 0; i < N; ++i) in[i] = (float)(0.01 * sqrt(2.0) * sin(2.0 * M_PI * 1000.0 * i / SR));
        LV2_Handle h = open_plugin(d, ctl, in, out);
        d->run(h, (uint32_t)SR);            /* settle */
        g_probe_on = 1;
        d->run(h, (uint32_t)SR);
        g_probe_on = 0;
        const char* names[6] = { "J3 input   (TP13  10 mV)", "C3 node    (TP14 488 mV)",
                                 "V3-A grid  (TP15  50 mV)", "CF output  (TP16 2.51 V)",
                                 "V4-A grid  (TP17 261 mV)", "speaker    (TP23  2.6 V)" };
        const double sch[6] = { 0.010, 0.488, 0.050, 2.51, 0.261, 2.6 };
        double v[6];
        printf("Test-point levels, 10 mV 1 kHz at Bright 1, controls at 50%%:\n");
        for (int k = 0; k < 6; ++k) {
            v[k] = sqrt(g_probe_sum[k] / g_probe_n[k]);
            printf("  %-26s model %8.3f V rms  %+4.0f%%\n", names[k], v[k], 100.0 * (v[k] / sch[k] - 1.0));
            if (k > 0 && fabs(v[k] / sch[k] - 1.0) > 0.25) fail = 1;
        }
        /* The two pot tapers are fitted to these stage ratios. */
        double r_vol = (v[2] / v[1]) / (sch[2] / sch[1]), r_tone = (v[4] / v[3]) / (sch[4] / sch[3]);
        printf("  volume stage TP15/TP14 %+.1f%%, tone stack TP17/TP16 %+.1f%% vs schematic  %s\n",
               100.0 * (r_vol - 1.0), 100.0 * (r_tone - 1.0),
               fabs(r_vol - 1.0) < 0.02 && fabs(r_tone - 1.0) < 0.02 ? "ok" : "OFF");
        if (fabs(r_vol - 1.0) >= 0.02 || fabs(r_tone - 1.0) >= 0.02) fail = 1;
        d->cleanup(h);
    }

    /* --- DC operating points against the schematic's DC test points. */
    {
        Triode t;
        double dc[4], ref[4] = { 1.73, 1.55, 197.0, 1.93 };
        const char* nm[4] = { "input cathodes (TP8)", "V3-A cathode (TP9)", "CF cathode (TP10)", "V4-A cathode (TP11)" };
        triode_build(&t, 302.0, 100e3, 2.1e6, 1.88e3, 1, 0.5);  dc[0] = t.dc.vk0;
        triode_build(&t, 302.0, 100e3, 10e6, 1.5e3, 1, 0.5);    dc[1] = t.dc.vk0;
        dc[2] = cf_solve(t.dc.vp0, 302.0, 100e3, 70e3);
        triode_build(&t, 370.0, 100e3, 1e6, 1.46e3, 0, 0.5);    dc[3] = t.dc.vk0;
        printf("DC operating points:\n");
        for (int k = 0; k < 4; ++k) {
            int ok = fabs(dc[k] / ref[k] - 1.0) < 0.10;
            printf("  %-22s model %7.2f V  schematic %7.2f V  %+4.0f%%  %s\n", nm[k], dc[k], ref[k],
                   100.0 * (dc[k] / ref[k] - 1.0), ok ? "ok" : "OFF");
            if (!ok) fail = 1;
        }
    }

    /* --- Output test from the schematic: raise a 1 kHz sine at J3 until the speaker
     * THD reaches 5 %; the schematic calls for 30 W RMS into 4 ohms there. The
     * volume is set low so the power amp, not the preamp, sets the distortion. */
    {
        float ctl[P_COUNT] = { 0 };
        ctl[P_INPUT] = IN_BRIGHT1; ctl[P_VOL_BRIGHT] = 2; ctl[P_VOL_NORMAL] = 6.5f;
        ctl[P_TREBLE] = 6.5f; ctl[P_BASS] = 6.5f; ctl[P_PRESENCE] = 6.5f; ctl[P_CAB] = 0;
        LV2_Handle h = open_plugin(d, ctl, in, out);
        double prevW = 0.0, prevT = 0.0, w5 = -1.0;
        const int n = (int)SR / 2;
        for (double lvl = 0.01; lvl < 3.0 && w5 < 0.0; lvl *= 1.06) {
            for (int i = 0; i < (int)SR; ++i) in[i] = (float)(lvl * sqrt(2.0) * sin(2.0 * M_PI * 1000.0 * i / SR));
            d->activate(h);
            d->run(h, (uint32_t)SR);
            double mean = 0, sum = 0, c = 0, s = 0;
            for (int i = n; i < 2 * n; ++i) mean += out[i];
            mean /= n;
            for (int i = n; i < 2 * n; ++i) {
                double y = out[i] - mean, wt = 2.0 * M_PI * 1000.0 * i / SR;
                sum += y * y; c += y * cos(wt); s += y * sin(wt);
            }
            double tot = sum / n, fund = 2.0 * (c * c + s * s) / ((double)n * n);
            double thd = sqrt(fmax(tot - fund, 0.0) / fund);
            double vs = sqrt(tot) / 0.035, W = vs * vs / 4.0;
            if (thd >= 0.05) w5 = prevW + (0.05 - prevT) / (thd - prevT) * (W - prevW);
            prevW = W; prevT = thd;
        }
        for (int i = 0; i < (int)SR; ++i) in[i] = (float)(2.0 * sin(2.0 * M_PI * 1000.0 * i / SR));
        d->activate(h);
        d->run(h, (uint32_t)SR);
        double sum = 0;
        for (int i = n; i < 2 * n; ++i) sum += (double)out[i] * out[i];
        double vclip = sqrt(sum / n) / 0.035;
        int ok = w5 > 25.5 && w5 < 34.5;
        printf("Output test: %.1f W at 5%% THD into 4 ohm (schematic: 30 W)  %s\n", w5, ok ? "ok" : "OFF SPEC");
        printf("Full clipping: %.1f W (rated 40 W)\n", vclip * vclip / 4.0);
        if (!ok) fail = 1;
        d->cleanup(h);
    }

    make_guitar(in, N, 0.3);
    struct { const char* name; float input, vn, vb, tr, bs, pr; } cases[] = {
        { "bright_clean",  IN_BRIGHT1, 1,  3,  7, 5, 6 },
        { "normal_clean",  IN_NORMAL1, 3,  1,  6, 6, 6 },
        { "bright_crunch", IN_BRIGHT1, 1,  7,  7, 5, 6 },
        { "normal2_drive", IN_NORMAL2, 9,  1,  7, 6, 7 },
        { "ycable_max",    IN_BOTH,    12, 12, 8, 6, 9 },
    };
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        float ctl[P_COUNT] = { 0 };
        ctl[P_INPUT] = cases[c].input;
        ctl[P_VOL_NORMAL] = cases[c].vn; ctl[P_VOL_BRIGHT] = cases[c].vb;
        ctl[P_TREBLE] = cases[c].tr; ctl[P_BASS] = cases[c].bs; ctl[P_PRESENCE] = cases[c].pr;
        ctl[P_CAB] = 1; ctl[P_IN_TRIM] = 0; ctl[P_OUT_LEVEL] = 0;

        LV2_Handle h = open_plugin(d, ctl, in, out);
        clock_t t0 = clock();
        for (int i = 0; i < N; i += 256) {
            int m = N - i < 256 ? N - i : 256;
            d->connect_port(h, P_IN, in + i);
            d->connect_port(h, P_OUT, out + i);
            /* sweep the tone controls in the last second */
            if (i > 3 * SR) {
                ctl[P_TREBLE] = (float)(1.0 + 11.0 * (i - 3 * SR) / SR);
                ctl[P_BASS] = 13.0f - ctl[P_TREBLE];
                ctl[P_PRESENCE] = ctl[P_TREBLE];
            }
            d->run(h, m);
        }
        double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;

        double peak = 0, sum = 0; int bad = 0;
        for (int i = 0; i < N; ++i) {
            if (!isfinite(out[i])) bad = 1;
            peak = fmax(peak, fabs(out[i]));
            sum += (double)out[i] * out[i];
        }
        double rms = sqrt(sum / N);
        printf("%-14s peak %6.3f (%6.1f dBFS)  rms %6.1f dBFS  realtime x%.0f  %s\n",
               cases[c].name, peak, 20 * log10(peak + 1e-12), 20 * log10(rms + 1e-12),
               (N / SR) / (secs > 0 ? secs : 1e-9), bad ? "NaN/INF!" : "ok");
        if (bad || peak < 1e-4 || peak > 4.0) fail = 1;

        char path[128];
        snprintf(path, sizeof path, "test_%s.wav", cases[c].name);
        write_wav(path, out, N);
        d->deactivate(h);
        d->cleanup(h);
    }
    write_wav("test_input_di.wav", in, N);

    /* Switching the input jack while playing must not blow up. */
    {
        float ctl[P_COUNT] = { 0 };
        ctl[P_VOL_NORMAL] = 8; ctl[P_VOL_BRIGHT] = 8; ctl[P_TREBLE] = 7; ctl[P_BASS] = 6;
        ctl[P_PRESENCE] = 6; ctl[P_CAB] = 1;
        LV2_Handle h = open_plugin(d, ctl, in, out);
        int bad = 0;
        for (int i = 0; i < N; i += 512) {
            ctl[P_INPUT] = (float)((i / 512) % 5);
            d->connect_port(h, P_IN, in + i);
            d->connect_port(h, P_OUT, out + i);
            d->run(h, N - i < 512 ? N - i : 512);
        }
        for (int i = 0; i < N; ++i) if (!isfinite(out[i]) || fabs(out[i]) > 4.0) bad = 1;
        printf("input switching: %s\n", bad ? "FAIL" : "ok");
        if (bad) fail = 1;
        d->cleanup(h);
    }

    /* Cabinet models: each is level-matched to the stock 2x12, and switching
     * between them while playing must stay clean. */
    {
        static const char* names[CAB_COUNT] = { "off", "2x12 open", "1x12 open", "4x10 open",
                                                "2x12 closed", "4x12 closed" };
        double ref = 0.0;
        for (int m = CAB_2X12_OPEN; m < CAB_COUNT; ++m) {
            float ctl[P_COUNT] = { 0 };
            ctl[P_INPUT] = IN_BRIGHT1; ctl[P_VOL_NORMAL] = 1; ctl[P_VOL_BRIGHT] = 5;
            ctl[P_TREBLE] = 7; ctl[P_BASS] = 5; ctl[P_PRESENCE] = 6; ctl[P_CAB] = (float)m;
            LV2_Handle h = open_plugin(d, ctl, in, out);
            d->run(h, N);
            double sum = 0.0;
            for (int i = 0; i < N; ++i) sum += (double)out[i] * out[i];
            double db = 10.0 * log10(sum / N + 1e-30);
            if (m == CAB_2X12_OPEN) ref = db;
            int ok = isfinite(db) && fabs(db - ref) < 3.0;
            printf("cabinet %-12s rms %6.1f dBFS (%+.1f dB vs stock)  %s\n", names[m], db, db - ref,
                   ok ? "ok" : "LEVEL MISMATCH");
            if (!ok) fail = 1;
            char path[64];
            snprintf(path, sizeof path, "test_cab%d.wav", m);
            write_wav(path, out, N);
            d->cleanup(h);
        }

        float ctl[P_COUNT] = { 0 };
        ctl[P_INPUT] = IN_BRIGHT1; ctl[P_VOL_BRIGHT] = 8; ctl[P_VOL_NORMAL] = 1;
        ctl[P_TREBLE] = 7; ctl[P_BASS] = 6; ctl[P_PRESENCE] = 6;
        LV2_Handle h = open_plugin(d, ctl, in, out);
        int bad = 0;
        for (int i = 0; i < N; i += 384) {
            ctl[P_CAB] = (float)((i / 384) % CAB_COUNT);
            d->connect_port(h, P_IN, in + i);
            d->connect_port(h, P_OUT, out + i);
            d->run(h, N - i < 384 ? N - i : 384);
        }
        for (int i = 0; i < N; ++i) if (!isfinite(out[i]) || fabs(out[i]) > 4.0) bad = 1;
        printf("cabinet switching: %s\n", bad ? "FAIL" : "ok");
        if (bad) fail = 1;
        d->cleanup(h);
    }

    /* Silence must stay silent (no self-oscillation in the feedback loops). */
    {
        float ctl[P_COUNT] = { 0 };
        ctl[P_INPUT] = IN_BOTH; ctl[P_VOL_NORMAL] = 12; ctl[P_VOL_BRIGHT] = 12;
        ctl[P_TREBLE] = 12; ctl[P_BASS] = 12; ctl[P_PRESENCE] = 12; ctl[P_CAB] = 0;
        for (int i = 0; i < N; ++i) in[i] = i < 10 ? 0.5f : 0.0f;
        LV2_Handle h = open_plugin(d, ctl, in, out);
        d->run(h, N);
        double tail = 0;
        for (int i = N - (int)SR; i < N; ++i) tail = fmax(tail, fabs(out[i]));
        printf("impulse tail after 3 s: %g  %s\n", tail, tail < 1e-4 ? "ok" : "UNSTABLE");
        if (!(tail < 1e-4)) fail = 1;
        d->cleanup(h);
    }

    printf(fail ? "FAIL\n" : "PASS\n");
    return fail;
}
