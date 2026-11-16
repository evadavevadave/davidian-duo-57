/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Audio Unit (AUv2 effect) build of the Davidian Duo '57 for macOS: the engine in duo57.c behind
 * Apple's AudioUnitSDK (Apache 2.0 licence; see licenses/Apache-2.0.txt), with parameters from
 * params.h and a Cocoa view from au_view.m. Adapted from the Davidian Treble Booster 30. */
#include <AudioUnitSDK/AUEffectBase.h>
#include <AudioToolbox/AudioUnitProperties.h>

#include <cstring>
#include <vector>

#include "../engine.h"
#include "../params.h"

/* Both set by CMakeLists.txt, which also puts them in Info.plist and au_view.m. */
#ifndef DUO_AU_BUNDLE_ID
#    error "DUO_AU_BUNDLE_ID must name the component's bundle identifier"
#endif
#ifndef DUO_AU_VIEW_FACTORY
#    error "DUO_AU_VIEW_FACTORY must name the Cocoa view factory class"
#endif
#define DUO_STR2(x) #x
#define DUO_STR(x) DUO_STR2(x)

class DavidianDuo57AU : public ausdk::AUEffectBase {
public:
    explicit DavidianDuo57AU(AudioComponentInstance ci) : AUEffectBase(ci, false)
    {
        /* Parameter IDs are the LV2 port numbers. The host's bypass is AUEffectBase's own. */
        for (int i = 0; i < DUO_NUM_PARAMS; ++i) {
            values[DUO_PARAMS[i].port] = DUO_PARAMS[i].def;
            Globals()->SetParameter((AudioUnitParameterID)DUO_PARAMS[i].port, DUO_PARAMS[i].def);
        }
    }

    ~DavidianDuo57AU() override { duo_engine_free(engine); }

    OSStatus Initialize() override
    {
        const OSStatus r = AUEffectBase::Initialize();
        if (r != noErr)
            return r;
        duo_engine_free(engine);
        engine = duo_engine_new(GetSampleRate());
        if (!engine)
            return kAudioUnitErr_FailedInitialization;
        mono.assign(GetMaxFramesPerSlice() > 0 ? GetMaxFramesPerSlice() : 4096, 0.0f);
        for (int port = DUO_FIRST_CONTROL; port < P_COUNT; ++port)
            duo_engine_connect(engine, (uint32_t)port, &values[port]);
        pull();
        duo_engine_activate(engine);
        return noErr;
    }

    void Cleanup() override
    {
        duo_engine_free(engine);
        engine = nullptr;
        AUEffectBase::Cleanup();
    }

    OSStatus Reset(AudioUnitScope scope, AudioUnitElement element) override
    {
        if (engine) {
            pull();
            duo_engine_activate(engine);
        }
        return AUEffectBase::Reset(scope, element);
    }

    /* The amplifier is mono: mono or stereo in (mixed to mono), mono or stereo out. */
    UInt32 SupportedNumChannels(const AUChannelInfo** out) override
    {
        static const AUChannelInfo configs[] = { { 1, 1 }, { 1, 2 }, { 2, 2 } };
        if (out)
            *out = configs;
        return 3;
    }

    bool SupportsTail() override { return true; }
    Float64 GetTailTime() override { return 0.25; }
    Float64 GetLatency() override { return 0.0; }

    OSStatus GetParameterInfo(AudioUnitScope scope, AudioUnitParameterID id, AudioUnitParameterInfo& info) override
    {
        const DuoParam* p = duo_param((int)id);
        if (scope != kAudioUnitScope_Global)
            return kAudioUnitErr_InvalidScope;
        if (!p)
            return kAudioUnitErr_InvalidParameter;
        std::memset(&info, 0, sizeof info);
        FillInParameterName(info, CFStringCreateWithCString(nullptr, p->name, kCFStringEncodingUTF8), true);
        info.flags |= kAudioUnitParameterFlag_IsReadable | kAudioUnitParameterFlag_IsWritable;
        if (p->steps)
            info.unit = kAudioUnitParameterUnit_Indexed;
        else if (!std::strcmp(p->unit, "dB"))
            info.unit = kAudioUnitParameterUnit_Decibels;
        else
            info.unit = kAudioUnitParameterUnit_Generic;
        info.minValue = p->min;
        info.maxValue = p->max;
        info.defaultValue = p->def;
        return noErr;
    }

    OSStatus GetParameterValueStrings(AudioUnitScope scope, AudioUnitParameterID id, CFArrayRef* out) override
    {
        const DuoParam* p = duo_param((int)id);
        CFStringRef names[6];
        if (scope != kAudioUnitScope_Global || !p || !p->steps)
            return kAudioUnitErr_InvalidProperty;
        if (!out) /* the SDK asks whether there are any */
            return noErr;
        for (int i = 0; i <= p->steps; ++i)
            names[i] = CFStringCreateWithCString(nullptr, p->opts[i], kCFStringEncodingUTF8);
        *out = CFArrayCreate(nullptr, (const void**)names, p->steps + 1, &kCFTypeArrayCallBacks);
        for (int i = 0; i <= p->steps; ++i)
            CFRelease(names[i]);
        return noErr;
    }

    OSStatus GetPropertyInfo(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element,
                             UInt32& size, bool& writable) override
    {
        if (scope == kAudioUnitScope_Global && id == kAudioUnitProperty_CocoaUI) {
            size = sizeof(AudioUnitCocoaViewInfo);
            writable = false;
            return noErr;
        }
        return AUEffectBase::GetPropertyInfo(id, scope, element, size, writable);
    }

    OSStatus GetProperty(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element,
                         void* data) override
    {
        if (scope == kAudioUnitScope_Global && id == kAudioUnitProperty_CocoaUI) {
            CFBundleRef bundle = CFBundleGetBundleWithIdentifier(CFSTR(DUO_AU_BUNDLE_ID));
            CFURLRef url = bundle ? CFBundleCopyBundleURL(bundle) : nullptr;
            AudioUnitCocoaViewInfo info;
            if (!url)
                return kAudioUnitErr_InvalidProperty;
            /* The host releases both. */
            info.mCocoaAUViewBundleLocation = url;
            info.mCocoaAUViewClass[0] = CFStringCreateWithCString(nullptr, DUO_STR(DUO_AU_VIEW_FACTORY),
                                                                  kCFStringEncodingUTF8);
            std::memcpy(data, &info, sizeof info);
            return noErr;
        }
        return AUEffectBase::GetProperty(id, scope, element, data);
    }

    OSStatus ProcessBufferLists(AudioUnitRenderActionFlags& flags, const AudioBufferList& in,
                                AudioBufferList& out, UInt32 frames) override
    {
        if (!engine || in.mNumberBuffers < 1 || out.mNumberBuffers < 1)
            return kAudioUnitErr_FormatNotSupported;
        pull();
        const float* l = static_cast<const float*>(in.mBuffers[0].mData);
        const float* r = in.mNumberBuffers > 1 ? static_cast<const float*>(in.mBuffers[1].mData) : l;
        for (UInt32 done = 0; done < frames;) {
            const UInt32 n = frames - done < mono.size() ? frames - done : (UInt32)mono.size();
            for (UInt32 i = 0; i < n; ++i)
                mono[i] = in.mNumberBuffers > 1 ? 0.5f * (l[done + i] + r[done + i]) : l[done + i];
            duo_engine_connect(engine, P_IN, mono.data());
            duo_engine_connect(engine, P_OUT, mono.data());
            duo_engine_run(engine, n);
            for (UInt32 c = 0; c < out.mNumberBuffers; ++c)
                std::memcpy(static_cast<float*>(out.mBuffers[c].mData) + done, mono.data(), sizeof(float) * n);
            done += n;
        }
        flags &= ~kAudioUnitRenderAction_OutputIsSilence;
        return noErr;
    }

private:
    void pull()
    {
        for (int i = 0; i < DUO_NUM_PARAMS; ++i)
            values[DUO_PARAMS[i].port] = duo_clamp(&DUO_PARAMS[i], GetParameter((AudioUnitParameterID)DUO_PARAMS[i].port));
    }

    DuoEngine* engine = nullptr;
    std::vector<float> mono;
    float values[P_COUNT] = {};
};

AUSDK_COMPONENT_ENTRY(ausdk::AUBaseFactory, DavidianDuo57AU)
