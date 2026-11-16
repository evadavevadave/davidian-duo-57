/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* VST3 build of the Davidian Duo '57: an audio processor around the engine in duo57.c, an edit
 * controller whose parameters come from params.h, and an editor that hosts the same panel as the
 * LV2 GUI. Built on the Steinberg VST3 SDK (MIT licence; see licenses/VST3-SDK-MIT.txt).
 * Adapted from the Davidian Treble Booster 30. */
#include <cstring>
#include <new>

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/common/pluginview.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include "../engine.h"
#include "../params.h"
#include "../ui/view.h"

#ifndef DUO_VERSION
#    define DUO_VERSION "0.0.0"
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

/* The VST3 class IDs. They must never change, or hosts lose saved projects. */
const FUID kProcessorUID(0xB00A2636, 0xCFDC48F2, 0x8DBB7958, 0x5B0D6EE6);
const FUID kControllerUID(0x73DB4BAD, 0x34AD444A, 0xAD6EE9C7, 0x97C35A70);

/* State: a tag, a count, every control's plain value in port order, then the bypass (0 or 1). */
const int32 kStateTag = 0x44353720; /* "D57 " */

/* VST3's own bypass parameter. The amp has no LV2 bypass port, so the processor crossfades to
 * the dry input itself. Its ID is past the port numbers. */
const ParamID kBypassID = 100;
const double kBypassFade = 0.01; /* seconds */

void ascii_to_tchar(const char* s, TChar* dst, int max)
{
    int i = 0;
    for (; s && s[i] && i < max - 1; ++i)
        dst[i] = (TChar)(unsigned char)s[i];
    dst[i] = 0;
}

void tchar_to_ascii(const TChar* s, char* dst, int max)
{
    int i = 0;
    for (; s && s[i] && i < max - 1; ++i)
        dst[i] = s[i] < 128 ? (char)s[i] : '?';
    dst[i] = 0;
}

/* Reads a saved state into plain values; leaves them alone if the state isn't ours. */
bool read_state(IBStream* state, float* values, bool* bypass)
{
    IBStreamer s(state, kLittleEndian);
    int32 tag = 0, n = 0;
    float v[DUO_NUM_INPUTS], b = 0.0f;
    if (!s.readInt32(tag) || tag != kStateTag || !s.readInt32(n) || n < 1)
        return false;
    for (int32 i = 0; i < DUO_NUM_INPUTS; ++i) {
        const DuoParam* p = duo_param(DUO_FIRST_CONTROL + i);
        v[i] = p->def; /* controls added in later versions keep their defaults */
        if (i < n && !s.readFloat(v[i]))
            return false;
        v[i] = duo_clamp(p, v[i]);
    }
    for (int32 i = DUO_NUM_INPUTS; i < n; ++i) /* values from later versions */
        if (!s.readFloat(b))
            return false;
    if (!s.readFloat(b))
        b = 0.0f;
    std::memcpy(values + DUO_FIRST_CONTROL, v, sizeof v);
    *bypass = b >= 0.5f;
    return true;
}

/* ---- Processor ---- */

class Processor : public AudioEffect {
public:
    Processor()
    {
        setControllerClass(kControllerUID);
        for (int i = 0; i < DUO_NUM_PARAMS; ++i)
            values[DUO_PARAMS[i].port] = DUO_PARAMS[i].def;
    }
    ~Processor() override
    {
        duo_engine_free(engine);
        delete[] mono;
        delete[] dry;
    }

    static FUnknown* createInstance(void*) { return (IAudioProcessor*)new Processor; }

    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const tresult r = AudioEffect::initialize(context);
        if (r != kResultOk)
            return r;
        addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        return kResultOk;
    }

    /* The amplifier is mono: a stereo input is mixed to mono, and the output goes to every
     * output channel. Mono or stereo on either side. */
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                          SpeakerArrangement* outputs, int32 numOuts) override
    {
        if (numIns != 1 || numOuts != 1)
            return kResultFalse;
        if (inputs[0] != SpeakerArr::kStereo && inputs[0] != SpeakerArr::kMono)
            return kResultFalse;
        if (outputs[0] != SpeakerArr::kStereo && outputs[0] != SpeakerArr::kMono)
            return kResultFalse;
        if (AudioBus* bus = getAudioInput(0)) {
            bus->setArrangement(inputs[0]);
            bus->setName(inputs[0] == SpeakerArr::kMono ? STR16("Mono In") : STR16("Stereo In"));
        }
        if (AudioBus* bus = getAudioOutput(0)) {
            bus->setArrangement(outputs[0]);
            bus->setName(outputs[0] == SpeakerArr::kMono ? STR16("Mono Out") : STR16("Stereo Out"));
        }
        return kResultTrue;
    }

    tresult PLUGIN_API canProcessSampleSize(int32 size) override
    {
        return size == kSample32 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override
    {
        sample_rate = setup.sampleRate;
        max_block = setup.maxSamplesPerBlock > 0 ? setup.maxSamplesPerBlock : 4096;
        return AudioEffect::setupProcessing(setup);
    }

    tresult PLUGIN_API setActive(TBool state) override
    {
        if (state) {
            duo_engine_free(engine);
            engine = duo_engine_new(sample_rate);
            if (!engine)
                return kOutOfMemory;
            delete[] mono;
            delete[] dry;
            mono = new (std::nothrow) float[max_block];
            dry = new (std::nothrow) float[max_block];
            if (!mono || !dry)
                return kOutOfMemory;
            for (int port = DUO_FIRST_CONTROL; port < P_COUNT; ++port)
                duo_engine_connect(engine, (uint32_t)port, &values[port]);
            duo_engine_activate(engine);
            wet = bypass ? 0.0 : 1.0;
        }
        return AudioEffect::setActive(state);
    }

    uint32 PLUGIN_API getLatencySamples() override { return 0; }
    uint32 PLUGIN_API getTailSamples() override { return (uint32)(0.25 * sample_rate); }

    tresult PLUGIN_API process(ProcessData& data) override
    {
        if (IParameterChanges* changes = data.inputParameterChanges) {
            const int32 n = changes->getParameterCount();
            for (int32 i = 0; i < n; ++i) {
                IParamValueQueue* q = changes->getParameterData(i);
                int32 offset = 0, points = q ? q->getPointCount() : 0;
                ParamValue v = 0.0;
                /* The knobs glide in the engine, so the block's last value is enough. */
                if (points <= 0 || q->getPoint(points - 1, offset, v) != kResultTrue)
                    continue;
                apply(q->getParameterId(), v);
            }
        }
        if (!engine || !mono || data.numSamples <= 0 || data.numInputs < 1 || data.numOutputs < 1
            || data.symbolicSampleSize != kSample32)
            return kResultOk;

        AudioBusBuffers& in = data.inputs[0];
        AudioBusBuffers& out = data.outputs[0];
        if (in.numChannels < 1 || out.numChannels < 1)
            return kResultOk;
        const double step = 1.0 / (kBypassFade * sample_rate);
        const double target = bypass ? 0.0 : 1.0;
        for (int32 done = 0; done < data.numSamples;) {
            const int32 n = data.numSamples - done < max_block ? data.numSamples - done : max_block;
            const Sample32* l = in.channelBuffers32[0] + done;
            const Sample32* r = in.numChannels > 1 ? in.channelBuffers32[1] + done : l;
            for (int32 i = 0; i < n; ++i)
                dry[i] = mono[i] = in.numChannels > 1 ? 0.5f * (l[i] + r[i]) : l[i];
            /* The amp keeps running while bypassed, so it comes back without a jump. */
            duo_engine_connect(engine, P_IN, mono);
            duo_engine_connect(engine, P_OUT, mono);
            duo_engine_run(engine, (uint32_t)n);
            if (wet != 1.0 || target != 1.0)
                for (int32 i = 0; i < n; ++i) {
                    wet += wet < target ? (wet + step > target ? target - wet : step)
                                        : (wet - step < target ? target - wet : -step);
                    mono[i] = (float)(wet * mono[i] + (1.0 - wet) * dry[i]);
                }
            for (int32 c = 0; c < out.numChannels; ++c)
                std::memcpy(out.channelBuffers32[c] + done, mono, sizeof(float) * (size_t)n);
            done += n;
        }
        out.silenceFlags = 0;
        return kResultOk;
    }

    tresult PLUGIN_API setState(IBStream* state) override
    {
        float v[P_COUNT];
        bool b = false;
        std::memcpy(v, values, sizeof v);
        if (!read_state(state, v, &b))
            return kResultFalse;
        bypass = b;
        std::memcpy(values + DUO_FIRST_CONTROL, v + DUO_FIRST_CONTROL, DUO_NUM_INPUTS * sizeof(float));
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream* state) override
    {
        IBStreamer s(state, kLittleEndian);
        if (!s.writeInt32(kStateTag) || !s.writeInt32(DUO_NUM_INPUTS))
            return kResultFalse;
        for (int i = 0; i < DUO_NUM_INPUTS; ++i)
            if (!s.writeFloat(values[DUO_FIRST_CONTROL + i]))
                return kResultFalse;
        return s.writeFloat(bypass ? 1.0f : 0.0f) ? kResultOk : kResultFalse;
    }

private:
    void apply(ParamID id, ParamValue v)
    {
        if (id == kBypassID) {
            bypass = v >= 0.5;
            return;
        }
        if (const DuoParam* p = duo_param((int)id))
            values[id] = duo_from_norm(p, v);
    }

    DuoEngine* engine = nullptr;
    float* mono = nullptr;
    float* dry = nullptr;
    double sample_rate = 48000.0;
    int32 max_block = 4096;
    float values[P_COUNT] = {};
    bool bypass = false;
    double wet = 1.0;
};

/* ---- Controller and editor ---- */

class DuoParameter : public Parameter {
public:
    explicit DuoParameter(const DuoParam* dp) : p(dp)
    {
        info.id = (ParamID)p->port;
        ascii_to_tchar(p->name, info.title, 128);
        ascii_to_tchar(p->short_name, info.shortTitle, 128);
        ascii_to_tchar(p->unit, info.units, 128);
        info.stepCount = p->steps;
        info.defaultNormalizedValue = duo_to_norm(p, p->def);
        info.unitId = kRootUnitId;
        info.flags = ParameterInfo::kCanAutomate | (p->steps ? ParameterInfo::kIsList : 0);
        valueNormalized = info.defaultNormalizedValue;
    }

    void toString(ParamValue norm, String128 string) const override
    {
        char buf[64];
        duo_format(p, duo_from_norm(p, norm), buf, sizeof buf);
        ascii_to_tchar(buf, string, 128);
    }

    bool fromString(const TChar* string, ParamValue& norm) const override
    {
        char buf[64];
        float v = 0.0f;
        tchar_to_ascii(string, buf, sizeof buf);
        if (!duo_parse(p, buf, &v))
            return false;
        norm = duo_to_norm(p, v);
        return true;
    }

    ParamValue toPlain(ParamValue norm) const override { return duo_from_norm(p, norm); }
    ParamValue toNormalized(ParamValue plain) const override { return duo_to_norm(p, (float)plain); }

private:
    const DuoParam* p;
};

class Editor;

class Controller : public EditController {
public:
    static FUnknown* createInstance(void*) { return (IEditController*)new Controller; }

    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const tresult r = EditController::initialize(context);
        if (r != kResultOk)
            return r;
        for (int i = 0; i < DUO_NUM_PARAMS; ++i)
            parameters.addParameter(new DuoParameter(&DUO_PARAMS[i]));
        parameters.addParameter(STR16("Bypass"), nullptr, 1, 0.0,
                                ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, kBypassID);
        return kResultOk;
    }

    tresult PLUGIN_API setComponentState(IBStream* state) override
    {
        float v[P_COUNT];
        bool b = false;
        if (!state || !read_state(state, v, &b))
            return kResultFalse;
        setParamNormalized(kBypassID, b ? 1.0 : 0.0);
        for (int i = 0; i < DUO_NUM_INPUTS; ++i) {
            const int port = DUO_FIRST_CONTROL + i;
            setParamNormalized((ParamID)port, duo_to_norm(duo_param(port), v[port]));
        }
        return kResultOk;
    }

    tresult PLUGIN_API setParamNormalized(ParamID tag, ParamValue value) override;
    IPlugView* PLUGIN_API createView(FIDString name) override;

    /* The panel's view of a parameter, as a plain value. */
    float plain(int port)
    {
        const DuoParam* p = duo_param(port);
        return p ? duo_from_norm(p, getParamNormalized((ParamID)port)) : 0.0f;
    }

    Editor* editor = nullptr;
};

class Editor : public CPluginView {
public:
    explicit Editor(Controller* c) : CPluginView(nullptr), controller(c)
    {
        ViewRect r(0, 0, DUO_VIEW_W, DUO_VIEW_H);
        setRect(r);
    }
    ~Editor() override { close(); }

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
    {
#if defined(_WIN32)
        return std::strcmp(type, kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
#elif defined(__APPLE__)
        return std::strcmp(type, kPlatformTypeNSView) == 0 ? kResultTrue : kResultFalse;
#else
        /* X11. The host's run loop (from the plug frame, once attached) drives events and redraws. */
        return std::strcmp(type, kPlatformTypeX11EmbedWindowID) == 0 ? kResultTrue : kResultFalse;
#endif
    }

    tresult PLUGIN_API attached(void* parent, FIDString type) override
    {
        DuoViewHost host = {};
        if (isPlatformTypeSupported(type) != kResultTrue || view)
            return kResultFalse;
        host.host = this;
        host.write = on_write;
        host.gesture = on_gesture;
        view = duo_view_open(parent, &host);
        if (!view)
            return kResultFalse;
        for (int i = 0; i < DUO_NUM_PARAMS; ++i)
            duo_view_set(view, DUO_PARAMS[i].port, controller->plain(DUO_PARAMS[i].port));
        controller->editor = this;
#if !defined(_WIN32) && !defined(__APPLE__)
        timer.editor = this;
        if (FUnknownPtr<Linux::IRunLoop> loop = FUnknownPtr<Linux::IRunLoop>(plugFrame))
            loop->registerTimer(&timer, 30);
#endif
        return CPluginView::attached(parent, type);
    }

    tresult PLUGIN_API removed() override
    {
        close();
        return CPluginView::removed();
    }

    tresult PLUGIN_API canResize() override { return kResultFalse; }

    void set(int port, float value)
    {
        if (view)
            duo_view_set(view, port, value);
    }

private:
#if !defined(_WIN32) && !defined(__APPLE__)
    /* The host's run loop calls this about 30 times a second; the X11 panel has no thread of
     * its own, so this is where it reads its events and redraws. */
    struct Timer : Linux::ITimerHandler {
        Editor* editor = nullptr;
        void PLUGIN_API onTimer() override
        {
            if (editor && editor->view)
                duo_view_idle(editor->view);
        }
        tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
        {
            if (FUnknownPrivate::iidEqual(iid, Linux::ITimerHandler::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
                *obj = this;
                return kResultOk;
            }
            *obj = nullptr;
            return kNoInterface;
        }
        /* Owned by the editor, which unregisters it before it goes. */
        uint32 PLUGIN_API addRef() override { return 1; }
        uint32 PLUGIN_API release() override { return 1; }
    } timer;
#endif

    void close()
    {
#if !defined(_WIN32) && !defined(__APPLE__)
        if (view)
            if (FUnknownPtr<Linux::IRunLoop> loop = FUnknownPtr<Linux::IRunLoop>(plugFrame))
                loop->unregisterTimer(&timer);
#endif
        if (controller->editor == this)
            controller->editor = nullptr;
        duo_view_close(view);
        view = nullptr;
    }

    static void on_write(void* host, int port, float value)
    {
        Editor* e = (Editor*)host;
        const ParamValue norm = duo_to_norm(duo_param(port), value);
        e->controller->setParamNormalized((ParamID)port, norm);
        e->controller->performEdit((ParamID)port, norm);
    }

    static void on_gesture(void* host, int port, int begin)
    {
        Editor* e = (Editor*)host;
        if (begin)
            e->controller->beginEdit((ParamID)port);
        else
            e->controller->endEdit((ParamID)port);
    }

    IPtr<Controller> controller;
    DuoView* view = nullptr;
};

tresult PLUGIN_API Controller::setParamNormalized(ParamID tag, ParamValue value)
{
    const tresult r = EditController::setParamNormalized(tag, value);
    if (editor && tag != kBypassID)
        editor->set((int)tag, plain((int)tag));
    return r;
}

IPlugView* PLUGIN_API Controller::createView(FIDString name)
{
    if (name && std::strcmp(name, ViewType::kEditor) == 0)
        return new Editor(this);
    return nullptr;
}

} // namespace

/* ---- Factory ---- */

BEGIN_FACTORY_DEF("Davidian", "https://github.com/evadavevadave/davidian-duo-57", "")

DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "Duo '57", Vst::kDistributable, PlugType::kFxDistortion, DUO_VERSION, kVstVersionString,
           Processor::createInstance)

DEF_CLASS2(INLINE_UID_FROM_FUID(kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
           "Duo '57 Controller", 0, "", DUO_VERSION, kVstVersionString, Controller::createInstance)

END_FACTORY
