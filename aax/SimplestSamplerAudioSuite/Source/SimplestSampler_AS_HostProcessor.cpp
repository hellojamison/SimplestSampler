#include "SimplestSampler_AS_HostProcessor.h"
#include "SimplestSampler_AS_Parameters.h"
#include "CaptureWavWriter.h"
#include "PluginLibraryBridge.h"
#include "SimplestSampler_AS_Defs.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <sstream>

namespace {

int64_t CurrentEpochMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string RandomHex(int count) {
    static thread_local std::mt19937 rng{std::random_device{}()};
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(static_cast<size_t>(count));
    std::uniform_int_distribution<int> dist(0, 15);
    for (int i = 0; i < count; ++i) {
        out.push_back(digits[dist(rng)]);
    }
    return out;
}

std::string CaptureFileName() {
    std::ostringstream name;
    name << "AAX-Capture-" << CurrentEpochMs() << "-" << RandomHex(4) << ".wav";
    return name.str();
}

} // namespace

AAX_CHostProcessor* AAX_CALLBACK SimplestSampler_AS_HostProcessor::Create() {
    return new SimplestSampler_AS_HostProcessor();
}

SimplestSampler_AS_HostProcessor::SimplestSampler_AS_HostProcessor() = default;
SimplestSampler_AS_HostProcessor::~SimplestSampler_AS_HostProcessor() = default;

AAX_Result SimplestSampler_AS_HostProcessor::RenderAudio(
    const float* const inAudioIns[],
    int32_t inAudioInCount,
    float* const inAudioOuts[],
    int32_t inAudioOutCount,
    int32_t* ioWindowSize)
{
    if (!ioWindowSize || *ioWindowSize <= 0 || inAudioInCount != inAudioOutCount) {
        return AAX_ERROR_ARGUMENT_BUFFER_OVERFLOW;
    }

    for (int32_t channel = 0; channel < inAudioInCount; ++channel) {
        const float* input = inAudioIns[channel];
        float* output = inAudioOuts[channel];
        if (!input || !output) {
            continue;
        }
        std::memcpy(output, input, static_cast<size_t>(*ioWindowSize) * sizeof(float));
    }
    return AAX_SUCCESS;
}

AAX_Result SimplestSampler_AS_HostProcessor::PreAnalyze(int32_t inAudioInCount, int32_t /*iWindowSize*/) {
    mCaptureBuffer.clear();
    mChannelCount = std::max<int32_t>(1, inAudioInCount);

    if (auto* parameters = dynamic_cast<SimplestSampler_AS_Parameters*>(GetEffectParameters())) {
        mTargetSlotIndex = parameters->TargetSlotIndex();
        mActiveSlotCount = parameters->ActiveSlotCount();
        mSampleRateHz = parameters->SampleRateHz();
    } else {
        mTargetSlotIndex = 0;
        mActiveSlotCount = kSimplestSamplerDefaultActiveSlots;
    }

    return AAX_SUCCESS;
}

AAX_Result SimplestSampler_AS_HostProcessor::AnalyzeAudio(
    const float* const inAudioIns[],
    int32_t inAudioInCount,
    int32_t* ioWindowSize)
{
    if (!ioWindowSize || *ioWindowSize <= 0 || inAudioInCount <= 0) {
        return AAX_SUCCESS;
    }

    mChannelCount = std::max<int32_t>(1, inAudioInCount);
    const int32_t frameCount = *ioWindowSize;
    const size_t previousSize = mCaptureBuffer.size();
    mCaptureBuffer.resize(previousSize + static_cast<size_t>(frameCount * mChannelCount));

    if (mChannelCount == 1) {
        const float* input = inAudioIns[0];
        if (input) {
            std::memcpy(mCaptureBuffer.data() + previousSize, input, static_cast<size_t>(frameCount) * sizeof(float));
        }
        return AAX_SUCCESS;
    }

    float* dest = mCaptureBuffer.data() + previousSize;
    for (int32_t frame = 0; frame < frameCount; ++frame) {
        for (int32_t channel = 0; channel < mChannelCount; ++channel) {
            const float* input = inAudioIns[channel];
            dest[static_cast<size_t>(frame) * static_cast<size_t>(mChannelCount) + static_cast<size_t>(channel)]
                = input ? input[frame] : 0.0f;
        }
    }

    return AAX_SUCCESS;
}

AAX_Result SimplestSampler_AS_HostProcessor::PostAnalyze() {
    if (mCaptureBuffer.empty()) {
        return AAX_SUCCESS;
    }

    if (!PluginLibraryBridge::EnsureDirectories()) {
        return AAX_ERROR_INVALID_PATH;
    }

    const std::string fileName = CaptureFileName();
    const std::string filePath = PluginLibraryBridge::GeneratedCapturesDirectory() + "/" + fileName;
    if (!CaptureWavWriter::WriteFloat32Wav(filePath, mCaptureBuffer, mChannelCount, mSampleRateHz)) {
        return AAX_ERROR_INVALID_PATH;
    }

    PluginBridgeDocument document;
    PluginLibraryBridge::ReadDocument(document);
    document.activeSlotCount = std::max(document.activeSlotCount, mActiveSlotCount);

    const int64_t capturedAt = CurrentEpochMs();
    const std::string captureID = "sampler-capture-aax-" + std::to_string(capturedAt);
    const std::string displayName = "AAX Capture";

    bool replaced = false;
    for (PluginBridgeSlot& slot : document.slots) {
        if (slot.index == mTargetSlotIndex) {
            slot.id = captureID;
            slot.filePath = filePath;
            slot.displayName = displayName;
            slot.source = "aax-audiosuite-capture";
            slot.capturedAtMs = capturedAt;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        PluginBridgeSlot slot;
        slot.index = mTargetSlotIndex;
        slot.id = captureID;
        slot.filePath = filePath;
        slot.displayName = displayName;
        slot.source = "aax-audiosuite-capture";
        slot.capturedAtMs = capturedAt;
        document.slots.push_back(slot);
    }

    if (!PluginLibraryBridge::AtomicWriteDocument(document)) {
        return AAX_ERROR_INVALID_PATH;
    }

    mCaptureBuffer.clear();
    return AAX_SUCCESS;
}
