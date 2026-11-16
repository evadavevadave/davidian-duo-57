/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Hosts the built VST3 the way a Windows DAW does and checks what Steinberg's validator does
 * not: that the editor turns mouse clicks and drags into begin/perform/end edits, that the
 * parameters read as on the panel, and that the VST3 sounds the same as the LV2 build, sample
 * for sample, with the same settings, and passes the input through when bypassed. Windows only.
 * Adapted from the Davidian Treble Booster 30.
 * Usage: vst3_check <Davidian Duo 57.vst3 folder> <duo57.lv2 folder> */
#ifndef _CRT_SECURE_NO_WARNINGS
#    define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include "../src/lv2_core.h"
#include "../src/params.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

static int failures;

static void check(const char* what, bool ok)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

/* Records the edits the editor sends to the host. */
struct Handler : IComponentHandler {
    struct Edit { char kind; ParamID id; ParamValue v; };
    std::vector<Edit> log;
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API beginEdit(ParamID id) override { log.push_back({ 'b', id, 0 }); return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue v) override { log.push_back({ 'p', id, v }); return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID id) override { log.push_back({ 'e', id, 0 }); return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
};

/* An edit of one parameter: begin, at least one perform, end; returns the last value. */
static bool one_edit(const std::vector<Handler::Edit>& log, ParamID id, ParamValue* last)
{
    if (log.size() < 3 || log.front().kind != 'b' || log.back().kind != 'e')
        return false;
    for (const auto& e : log) {
        if (e.id != id)
            return false;
        if (e.kind == 'p')
            *last = e.v;
    }
    return true;
}

static void click(HWND w, int x, int y)
{
    SendMessageA(w, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    SendMessageA(w, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
}

static LRESULT CALLBACK frame_proc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h, m, w, l); }

static void test_editor(IEditController* ctrl, Handler& handler)
{
    WNDCLASSA wc = {};
    wc.lpfnWndProc = frame_proc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "Duo57TestFrame";
    RegisterClassA(&wc);
    HWND frame = CreateWindowA("Duo57TestFrame", "host", WS_OVERLAPPEDWINDOW, 0, 0, 1100, 400, nullptr,
                               nullptr, wc.hInstance, nullptr);
    IPlugView* view = ctrl->createView(ViewType::kEditor);
    check("controller creates an editor", view != nullptr);
    if (!view)
        return;
    ViewRect r;
    check("editor is 1040 x 330", view->getSize(&r) == kResultOk && r.getWidth() == 1040 && r.getHeight() == 330);
    check("editor supports HWND", view->isPlatformTypeSupported(kPlatformTypeHWND) == kResultTrue);
    check("editor attaches to a host window", view->attached(frame, kPlatformTypeHWND) == kResultOk);
    HWND panel = FindWindowExA(frame, nullptr, "DavidianDuo57VST3", nullptr);
    check("editor window is a child of the host window", panel != nullptr);
    if (panel) {
        ParamValue v = -1;
        /* The Normal 2 jack at (151, 206). */
        handler.log.clear();
        click(panel, 151, 206);
        check("jack click is one begin/perform/end edit", one_edit(handler.log, P_INPUT, &v)
                                                             && std::fabs(v - duo_to_norm(duo_param(P_INPUT), IN_NORMAL2)) < 1e-9);
        check("controller follows the click",
              std::fabs(ctrl->getParamNormalized(P_INPUT) - duo_to_norm(duo_param(P_INPUT), IN_NORMAL2)) < 1e-9);

        /* Treble at (538, 190): dragging up 18 px adds a tenth of the 1-12 range, 7.0 to 8.1. */
        handler.log.clear();
        SendMessageA(panel, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(538, 190));
        SendMessageA(panel, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(538, 181));
        SendMessageA(panel, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(538, 172));
        SendMessageA(panel, WM_LBUTTONUP, 0, MAKELPARAM(538, 172));
        check("knob drag is one edit with several performs", one_edit(handler.log, P_TREBLE, &v) && handler.log.size() >= 4);
        check("knob drag ends at 8.1", std::fabs(duo_from_norm(duo_param(P_TREBLE), v) - 8.1f) < 1e-4);

        /* The 4X12 CLOSED row of the cabinet selector. */
        handler.log.clear();
        click(panel, 940, 120 + 19 * CAB_4X12_CLOSED);
        check("selector click picks 4x12 Closed", one_edit(handler.log, P_CAB, &v) && v == 1.0);

        handler.log.clear();
        click(panel, 5, 5);
        check("clicking empty panel sends nothing", handler.log.empty());

        check("host values reach the open editor", ctrl->setParamNormalized(P_BASS, 0.25) == kResultOk);
    }
    check("editor detaches", view->removed() == kResultOk);
    check("editor window is gone", !panel || !IsWindow(panel));
    view->release();
    DestroyWindow(frame);
}

/* ---- Sound: the VST3 against the LV2 build ---- */

const double kRate = 48000.0;
const int kBlock = 256, kBlocks = 160, kBypassBlock = 120;
const int kFrames = kBlock * kBlocks;

/* A strummed chord, loud enough to drive the amp into some distortion. */
static float input(int i)
{
    const double t = i / kRate, env = std::exp(-3.0 * std::fmod(t, 0.4));
    return (float)(0.25 * env * (std::sin(2 * M_PI * 82.4 * t) + std::sin(2 * M_PI * 123.5 * t)
                                 + std::sin(2 * M_PI * 164.8 * t)));
}

/* The settings both formats get, as plain values that survive the trip through 0-1. */
static const struct { int port; float plain; } kSettings[] = {
    { P_INPUT, IN_BRIGHT1 }, { P_VOL_BRIGHT, 9.0f }, { P_TREBLE, 8.0f }, { P_BASS, 4.0f },
    { P_PRESENCE, 7.0f }, { P_CAB, CAB_4X10_OPEN }, { P_OUT_LEVEL, -6.0f },
};

static std::vector<float> run_lv2(const char* bundle)
{
    std::vector<float> out;
    char path[MAX_PATH];
    std::snprintf(path, sizeof path, "%s\\duo57.dll", bundle);
    HMODULE lib = LoadLibraryA(path);
    if (!lib)
        return out;
    auto get = (const LV2_Descriptor* (*)(uint32_t))GetProcAddress(lib, "lv2_descriptor");
    const LV2_Descriptor* d = get ? get(0) : nullptr;
    LV2_Handle h = d ? d->instantiate(d, kRate, bundle, nullptr) : nullptr;
    if (h) {
        float ctl[P_COUNT];
        std::vector<float> in(kBlock), buf(kBlock);
        for (int i = 0; i < DUO_NUM_PARAMS; ++i)
            ctl[DUO_PARAMS[i].port] = DUO_PARAMS[i].def;
        for (const auto& s : kSettings)
            ctl[s.port] = duo_from_norm(duo_param(s.port), duo_to_norm(duo_param(s.port), s.plain));
        for (int p = P_INPUT; p < P_COUNT; ++p)
            d->connect_port(h, (uint32_t)p, &ctl[p]);
        d->connect_port(h, P_IN, in.data());
        d->connect_port(h, P_OUT, buf.data());
        d->activate(h);
        for (int b = 0; b < kBlocks; ++b) {
            for (int i = 0; i < kBlock; ++i)
                in[i] = input(b * kBlock + i);
            d->run(h, kBlock);
            out.insert(out.end(), buf.begin(), buf.end());
        }
        d->cleanup(h);
    }
    FreeLibrary(lib);
    return out;
}

/* Stereo in with the same signal on both sides, so the mono mix equals the LV2 input. */
static std::vector<float> run_vst3(IComponent* comp)
{
    std::vector<float> out;
    FUnknownPtr<IAudioProcessor> proc(comp);
    if (!proc)
        return out;
    SpeakerArrangement stereo = SpeakerArr::kStereo, mono = SpeakerArr::kMono, s51 = SpeakerArr::k51;
    ProcessSetup setup { kRealtime, kSample32, kBlock, kRate };
    check("processor takes mono in, stereo out", proc->setBusArrangements(&mono, 1, &stereo, 1) == kResultTrue);
    check("processor refuses 5.1", proc->setBusArrangements(&s51, 1, &stereo, 1) == kResultFalse);
    check("processor takes stereo in and out", proc->setBusArrangements(&stereo, 1, &stereo, 1) == kResultTrue);
    check("processor sets up at 48 kHz", proc->setupProcessing(setup) == kResultOk);
    comp->activateBus(kAudio, kInput, 0, true);
    comp->activateBus(kAudio, kOutput, 0, true);
    check("processor activates", comp->setActive(true) == kResultOk);
    check("no latency", proc->getLatencySamples() == 0);
    proc->setProcessing(true);

    HostProcessData data;
    ParameterChanges changes(16);
    data.prepare(*comp, kBlock, kSample32);
    data.numSamples = kBlock;
    data.inputParameterChanges = &changes;
    for (int b = 0; b < kBlocks; ++b) {
        int32 index = 0;
        changes.clearQueue();
        if (b == 0)
            for (const auto& s : kSettings)
                if (IParamValueQueue* q = changes.addParameterData(s.port, index))
                    q->addPoint(0, duo_to_norm(duo_param(s.port), s.plain), index);
        if (b == kBypassBlock)
            if (IParamValueQueue* q = changes.addParameterData(100, index)) /* the bypass parameter */
                q->addPoint(0, 1.0, index);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < kBlock; ++i)
                data.inputs[0].channelBuffers32[c][i] = input(b * kBlock + i);
        if (proc->process(data) != kResultOk)
            break;
        for (int i = 0; i < kBlock; ++i) {
            out.push_back(data.outputs[0].channelBuffers32[0][i]);
            if (data.outputs[0].channelBuffers32[1][i] != data.outputs[0].channelBuffers32[0][i])
                out.back() = NAN; /* both outputs must carry the same signal */
        }
    }
    proc->setProcessing(false);
    comp->setActive(false);
    data.unprepare();
    return out;
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: vst3_check <Davidian Duo 57.vst3> <duo57.lv2>\n");
        return 2;
    }
    HostApplication host_app;
    PluginContextFactory::instance().setPluginContext(&host_app);
    std::string error;
    auto module = VST3::Hosting::Module::create(argv[1], error);
    check("loads the VST3 bundle", module != nullptr);
    if (!module) {
        std::printf("  %s\n", error.c_str());
        return 1;
    }
    const auto& factory = module->getFactory();
    check("factory vendor is Davidian", factory.info().vendor() == "Davidian");
    IPtr<PlugProvider> provider;
    for (const auto& ci : factory.classInfos())
        if (ci.category() == kVstAudioEffectClass) {
            check("one effect class named Duo '57", ci.name() == "Duo '57" && !provider);
            provider = owned(new PlugProvider(factory, ci, true));
        }
    if (!provider || !provider->initialize()) {
        check("component and controller start", false);
        return 1;
    }
    IPtr<IComponent> comp = provider->getComponentPtr();
    IPtr<IEditController> ctrl = provider->getControllerPtr();
    Handler handler;
    ctrl->setComponentHandler(&handler);

    {
        int bypass = 0;
        ParameterInfo info;
        for (int32 i = 0; i < ctrl->getParameterCount(); ++i)
            if (ctrl->getParameterInfo(i, info) == kResultOk)
                bypass += (info.flags & ParameterInfo::kIsBypass) != 0;
        check("10 parameters: 9 controls and bypass", ctrl->getParameterCount() == 10);
        check("one bypass parameter", bypass == 1);
        String128 text;
        char a[64];
        ctrl->getParamStringByValue(P_CAB, duo_to_norm(duo_param(P_CAB), CAB_4X12_CLOSED), text);
        for (int i = 0; i < 64; ++i) a[i] = (char)text[i];
        check("cabinet shows as 4x12 Closed", std::strcmp(a, "4x12 Closed") == 0);
        ctrl->getParamStringByValue(P_TREBLE, duo_to_norm(duo_param(P_TREBLE), 7.0f), text);
        for (int i = 0; i < 64; ++i) a[i] = (char)text[i];
        check("treble shows as 7.0", std::strcmp(a, "7.0") == 0);
    }

    test_editor(ctrl, handler);

    const std::vector<float> vst3 = run_vst3(comp);
    const std::vector<float> lv2 = run_lv2(argv[2]);
    check("both formats produce a full run of audio", vst3.size() == (size_t)kFrames && lv2.size() == (size_t)kFrames);
    if (vst3.size() == (size_t)kFrames && lv2.size() == (size_t)kFrames) {
        double peak = 0, diff = 0, dry = 0;
        const int fade_end = kBypassBlock * kBlock + (int)(0.011 * kRate);
        for (int i = 0; i < kBypassBlock * kBlock; ++i) {
            peak = std::fmax(peak, std::fabs(lv2[i]));
            diff = std::fmax(diff, std::isnan(vst3[i]) ? 1.0 : std::fabs(vst3[i] - lv2[i]));
        }
        for (int i = fade_end; i < kFrames; ++i)
            dry = std::fmax(dry, std::isnan(vst3[i]) ? 1.0 : std::fabs(vst3[i] - input(i)));
        std::printf("  peak %.3f, largest VST3/LV2 difference %.2e, bypassed difference from input %.2e\n",
                    peak, diff, dry);
        check("VST3 output matches the LV2 build on both channels", peak > 0.05 && diff < 1e-6);
        check("bypass passes the input through after its 10 ms fade", dry < 1e-6);
    }

    ctrl->setComponentHandler(nullptr);
    provider = nullptr;
    std::printf(failures ? "\n%d failed\n" : "\nAll passed\n", failures);
    return failures ? 1 : 0;
}
