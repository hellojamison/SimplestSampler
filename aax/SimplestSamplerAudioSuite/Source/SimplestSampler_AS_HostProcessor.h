#pragma once

#include "AAX_CHostProcessor.h"
#include "SimplestSampler_AS_Defs.h"

#include <cstdint>
#include <vector>

class SimplestSampler_AS_HostProcessor : public AAX_CHostProcessor {
public:
    static AAX_CHostProcessor* AAX_CALLBACK Create();

    SimplestSampler_AS_HostProcessor();
    ~SimplestSampler_AS_HostProcessor() override;

    AAX_Result RenderAudio(
        const float* const inAudioIns[],
        int32_t inAudioInCount,
        float* const inAudioOuts[],
        int32_t inAudioOutCount,
        int32_t* ioWindowSize) override;

    AAX_Result PreAnalyze(int32_t inAudioInCount, int32_t iWindowSize) override;
    AAX_Result AnalyzeAudio(
        const float* const inAudioIns[],
        int32_t inAudioInCount,
        int32_t* ioWindowSize) override;
    AAX_Result PostAnalyze() override;

private:
    std::vector<float> mCaptureBuffer;
    int32_t mChannelCount = 1;
    double mSampleRateHz = 48000.0;
    int32_t mTargetSlotIndex = 0;
    int32_t mActiveSlotCount = kSimplestSamplerDefaultActiveSlots;
};
