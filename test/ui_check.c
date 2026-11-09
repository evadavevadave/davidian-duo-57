/* SPDX-License-Identifier: GPL-3.0-or-later AND ISC
 * Copyright (C) 2026 Davidian
 * The LV2 interface declarations in this file follow the ISC-licensed LV2 headers;
 * see LICENSE-LV2 for their copyright and permission notice. */
/* Loads the built GUI library the way a Windows LV2 host does, embeds it in a window,
 * drives it with mouse messages and checks the values it sends back. Windows only.
 * Usage: ui_check <bundle folder> [screenshot.bmp] */
#define _CRT_SECURE_NO_WARNINGS
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "../src/lv2_core.h"
#include "../src/ports.h"

typedef void (*WriteFn)(void*, uint32_t, uint32_t, uint32_t, const void*);
typedef struct {
    const char* URI;
    void* (*instantiate)(const void*, const char*, const char*, WriteFn, void*, void**, const LV2_Feature* const*);
    void (*cleanup)(void*);
    void (*port_event)(void*, uint32_t, uint32_t, uint32_t, const void*);
    const void* (*extension_data)(const char*);
} UiDesc;

static int failures;
static int last_port = -1, writes;
static float last_value;

static void check(const char* what, int ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

static void on_write(void* controller, uint32_t port, uint32_t size, uint32_t format, const void* buf)
{
    (void)controller;
    if (size == sizeof(float) && format == 0) {
        last_port = (int)port;
        last_value = *(const float*)buf;
        ++writes;
    }
}

static void pump(void)
{
    MSG m;
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
}

static LPARAM xy(int x, int y) { return MAKELPARAM(x, y); }

static void click(HWND w, int x, int y)
{
    SendMessageA(w, WM_LBUTTONDOWN, MK_LBUTTON, xy(x, y));
    SendMessageA(w, WM_LBUTTONUP, 0, xy(x, y));
}

static void drag(HWND w, int x, int y, int dy)
{
    int i;
    SendMessageA(w, WM_LBUTTONDOWN, MK_LBUTTON, xy(x, y));
    for (i = 1; i <= 6; ++i)
        SendMessageA(w, WM_MOUSEMOVE, MK_LBUTTON, xy(x, y + dy * i / 6));
    SendMessageA(w, WM_LBUTTONUP, 0, xy(x, y + dy));
}

static int save_window(HWND w, const char* path)
{
    RECT r;
    HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
    BITMAPINFO bi;
    void* bits = NULL;
    HBITMAP bmp;
    FILE* f;
    BITMAPFILEHEADER fh;
    int width, height;
    GetClientRect(w, &r);
    width = r.right;
    height = r.bottom;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    SelectObject(mem, bmp);
    SendMessageA(w, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT);
    GdiFlush();
    f = fopen(path, "wb");
    if (f) {
        memset(&fh, 0, sizeof fh);
        fh.bfType = 0x4d42;
        fh.bfOffBits = sizeof fh + sizeof(BITMAPINFOHEADER);
        fh.bfSize = fh.bfOffBits + 4u * (unsigned)(width * height);
        fwrite(&fh, sizeof fh, 1, f);
        fwrite(&bi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f);
        fwrite(bits, 4, (size_t)(width * height), f);
        fclose(f);
    }
    DeleteDC(mem);
    DeleteObject(bmp);
    ReleaseDC(NULL, screen);
    return f != NULL;
}

int main(int argc, char** argv)
{
    char path[1024];
    HMODULE lib;
    const UiDesc* (*get)(uint32_t);
    const UiDesc* d;
    HWND parent, child = NULL;
    LV2_Feature parent_feature;
    const LV2_Feature* features[2];
    void* ui;
    void* widget = NULL;
    RECT rc;
    int i;
    char label[160];

    if (argc < 2) {
        printf("usage: ui_check <bundle folder> [screenshot.bmp]\n");
        return 2;
    }
    snprintf(path, sizeof path, "%s\\duo57_ui.dll", argv[1]);
    lib = LoadLibraryA(path);
    check("GUI library loads", lib != NULL);
    if (!lib)
        return 1;
    *(void**)&get = (void*)GetProcAddress(lib, "lv2ui_descriptor");
    check("exports lv2ui_descriptor", get != NULL);
    if (!get)
        return 1;
    d = get(0);
    check("descriptor has the GUI's URI", d && strcmp(d->URI, DUO57_UI_URI) == 0);
    check("only one GUI in the library", get(1) == NULL);

    parent = CreateWindowExA(0, "STATIC", "host", WS_OVERLAPPEDWINDOW, 0, 0, 1100, 400, NULL, NULL, NULL, NULL);
    parent_feature.URI = "http://lv2plug.in/ns/extensions/ui#parent";
    parent_feature.data = parent;
    features[0] = &parent_feature;
    features[1] = NULL;

    check("refuses another plugin's URI",
          d->instantiate(d, "urn:other", argv[1], on_write, NULL, &widget, features) == NULL);
    ui = d->instantiate(d, DUO57_URI, argv[1], on_write, NULL, &widget, features);
    check("instantiates inside the host's window", ui != NULL && widget != NULL);
    if (!ui)
        return 1;
    child = (HWND)widget;
    check("the widget is a child of the host's window", GetParent(child) == parent);
    GetClientRect(child, &rc);
    snprintf(label, sizeof label, "window is %ldx%ld", rc.right, rc.bottom);
    check(label, rc.right == 1040 && rc.bottom == 330);
    check("offers the idle interface",
          d->extension_data("http://lv2plug.in/ns/extensions/ui#idleInterface") != NULL);

    /* The host sends every control port's value first (the TTL defaults). */
    {
        static const float vals[P_COUNT] = { 0, 0, 2, 6, 6, 7, 5, 6, 1, 0, 0 };
        for (i = P_INPUT; i < P_COUNT; ++i)
            d->port_event(ui, (uint32_t)i, sizeof(float), 0, &vals[i]);
    }
    check("port updates from the host write nothing back", writes == 0);
    pump();

    /* 180 px of drag covers the whole 1-12 range, so 36 px down is 2.2 steps. */
    drag(child, 281, 190, 36);
    snprintf(label, sizeof label, "dragging Volume Bright down 36 px sends port %d = %.3f", last_port, last_value);
    check(label, last_port == P_VOL_BRIGHT && fabsf(last_value - 3.8f) < 0.01f);

    drag(child, 784, 190, -400);
    snprintf(label, sizeof label, "dragging Presence far up stops at %.3f", last_value);
    check(label, last_port == P_PRESENCE && last_value == 12.0f);

    drag(child, 538, 190, 50); /* move Treble off its default first */
    SendMessageA(child, WM_LBUTTONDBLCLK, MK_LBUTTON, xy(538, 190));
    SendMessageA(child, WM_LBUTTONUP, 0, xy(538, 190));
    snprintf(label, sizeof label, "double-click resets Treble to %.2f", last_value);
    check(label, last_port == P_TREBLE && fabsf(last_value - 7.0f) < 1e-4f);

    click(child, 151, 206);
    check("clicking the Normal 2 jack selects it", last_port == P_INPUT && last_value == IN_NORMAL2);
    click(child, 71, 140);
    check("clicking the Bright 1 jack selects it", last_port == P_INPUT && last_value == IN_BRIGHT1);
    click(child, 151, 140);
    check("clicking the Normal 1 jack selects it", last_port == P_INPUT && last_value == IN_NORMAL1);
    click(child, 120, 268);
    check("the Y cable button selects Bright 1 + Normal 1", last_port == P_INPUT && last_value == IN_BOTH);
    /* Cabinet selector rows sit at y = 120 + 19 * value. */
    click(child, 940, 120 + 19 * CAB_4X12_CLOSED);
    check("clicking the 4X12 CLOSED row selects it", last_port == P_CAB && last_value == CAB_4X12_CLOSED);
    click(child, 886, 120);
    check("clicking the OFF lamp turns the cabinet off", last_port == P_CAB && last_value == CAB_OFF);
    {
        POINT pt = { 910, 120 };
        ClientToScreen(child, &pt);
        SendMessageA(child, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)-WHEEL_DELTA), MAKELPARAM(pt.x, pt.y));
        check("wheel down on the selector steps to the 2X12 OPEN row",
              last_port == P_CAB && last_value == CAB_2X12_OPEN);
    }

    drag(child, 983, 262, 90);
    snprintf(label, sizeof label, "dragging Output down 90 px sends %.1f dB", last_value);
    check(label, last_port == P_OUT_LEVEL && fabsf(last_value - -36.0f) < 0.05f);

    {
        POINT pt = { 661, 190 };
        ClientToScreen(child, &pt);
        writes = 0;
        SendMessageA(child, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(pt.x, pt.y));
        snprintf(label, sizeof label, "mouse wheel on Bass sends %.3f", last_value);
        check(label, writes == 1 && last_port == P_BASS && fabsf(last_value - 5.275f) < 1e-3f);
    }
    click(child, 5, 5);
    check("clicking empty panel sends nothing new", last_port == P_BASS);

    pump();
    if (argc > 2) {
        check("screenshot written", save_window(child, argv[2]));
        printf("         %s\n", argv[2]);
    }

    d->cleanup(ui);
    check("cleanup destroys the window", !IsWindow(child));
    DestroyWindow(parent);
    FreeLibrary(lib);
    printf(failures ? "\n%d FAILED\n" : "\nAll passed\n", failures);
    return failures ? 1 : 0;
}
