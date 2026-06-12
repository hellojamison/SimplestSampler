#include "SimplestSampler_AS_Parameters.h"
#include "PluginLibraryBridge.h"

#include "AAX_CBinaryTaperDelegate.h"
#include "AAX_CBinaryDisplayDelegate.h"
#include "AAX_CLinearTaperDelegate.h"
#include "AAX_CNumberDisplayDelegate.h"
#include "AAX_IController.h"

#include <algorithm>
#include <sstream>

AAX_CEffectParameters* AAX_CALLBACK SimplestSampler_AS_Parameters::Create() {
    return new SimplestSampler_AS_Parameters();
}

SimplestSampler_AS_Parameters::SimplestSampler_AS_Parameters() = default;
SimplestSampler_AS_Parameters::~SimplestSampler_AS_Parameters() = default;

AAX_Result SimplestSampler_AS_Parameters::EffectInit() {
    AAX_CString bypassID = cDefaultMasterBypassID;
    AAX_IParameter* masterBypass = new AAX_CParameter<bool>(
        bypassID.CString(), AAX_CString("Bypass"), false,
        AAX_CBinaryTaperDelegate<bool>(),
        AAX_CBinaryDisplayDelegate<bool>("bypass", "on"), true);
    masterBypass->SetNumberOfSteps(2);
    masterBypass->SetType(AAX_eParameterType_Discrete);
    mParameterManager.AddParameter(masterBypass);

    AAX_Result result = AddTargetSlotParameter();
    result = AddActiveSlotCountParameter();

    if (AAX_IController* controller = Controller()) {
        AAX_CSampleRate sampleRate = 48000.0;
        if (controller->GetSampleRate(&sampleRate) == AAX_SUCCESS && sampleRate > 0.0) {
            mSampleRateHz = sampleRate;
        }
    }

    return result;
}

AAX_Result SimplestSampler_AS_Parameters::AddTargetSlotParameter() {
    AAX_CParameter<int32_t>* targetSlot = new AAX_CParameter<int32_t>(
        kSimplestSamplerParamTargetSlot, AAX_CString("Slot"), 1,
        AAX_CLinearTaperDelegate<int32_t>(1, kSimplestSamplerMaxActiveSlots),
        AAX_CNumberDisplayDelegate<int32_t>(), false);
    targetSlot->SetNumberOfSteps(kSimplestSamplerMaxActiveSlots);
    targetSlot->SetType(AAX_eParameterType_Discrete);
    mParameterManager.AddParameter(targetSlot);
    return AAX_SUCCESS;
}

AAX_Result SimplestSampler_AS_Parameters::AddActiveSlotCountParameter() {
    AAX_CParameter<int32_t>* activeSlots = new AAX_CParameter<int32_t>(
        kSimplestSamplerParamActiveSlotCount, AAX_CString("Slots"), kSimplestSamplerDefaultActiveSlots,
        AAX_CLinearTaperDelegate<int32_t>(kSimplestSamplerDefaultActiveSlots, kSimplestSamplerMaxActiveSlots),
        AAX_CNumberDisplayDelegate<int32_t>(), false);
    activeSlots->SetNumberOfSteps(kSimplestSamplerMaxActiveSlots - kSimplestSamplerDefaultActiveSlots + 1);
    activeSlots->SetType(AAX_eParameterType_Discrete);
    mParameterManager.AddParameter(activeSlots);
    return AAX_SUCCESS;
}

int32_t SimplestSampler_AS_Parameters::TargetSlotNumber() const {
    const AAX_IParameter* parameter = mParameterManager.GetParameterByID(kSimplestSamplerParamTargetSlot);
    int32_t slot = 1;
    if (parameter) {
        parameter->GetValueAsInt32(&slot);
    }
    return std::max<int32_t>(1, std::min<int32_t>(kSimplestSamplerMaxActiveSlots, slot));
}

int32_t SimplestSampler_AS_Parameters::TargetSlotIndex() const {
    return TargetSlotNumber() - 1;
}

bool SimplestSampler_AS_Parameters::SetTargetSlotNumber(int32_t slot) {
    AAX_IParameter* parameter = mParameterManager.GetParameterByID(kSimplestSamplerParamTargetSlot);
    if (!parameter) {
        return false;
    }
    slot = std::max<int32_t>(1, std::min<int32_t>(kSimplestSamplerMaxActiveSlots, slot));
    return parameter->SetValueWithInt32(slot) == AAX_SUCCESS;
}

bool SimplestSampler_AS_Parameters::SetActiveSlotCount(int32_t count) {
    AAX_IParameter* parameter = mParameterManager.GetParameterByID(kSimplestSamplerParamActiveSlotCount);
    if (!parameter) {
        return false;
    }
    count = std::max<int32_t>(kSimplestSamplerDefaultActiveSlots, std::min<int32_t>(kSimplestSamplerMaxActiveSlots, count));
    return parameter->SetValueWithInt32(count) == AAX_SUCCESS;
}

int32_t SimplestSampler_AS_Parameters::ActiveSlotCount() const {
    const AAX_IParameter* parameter = mParameterManager.GetParameterByID(kSimplestSamplerParamActiveSlotCount);
    int32_t count = kSimplestSamplerDefaultActiveSlots;
    if (parameter) {
        parameter->GetValueAsInt32(&count);
    }
    return std::max<int32_t>(kSimplestSamplerDefaultActiveSlots, std::min<int32_t>(kSimplestSamplerMaxActiveSlots, count));
}

double SimplestSampler_AS_Parameters::SampleRateHz() const {
    return mSampleRateHz;
}

std::string SimplestSampler_AS_Parameters::SlotDisplayLabel(int slotNumber) const {
    PluginBridgeDocument document;
    PluginLibraryBridge::ReadDocument(document);

    std::ostringstream label;
    const int slotIndex = slotNumber - 1;
    const PluginBridgeSlot* slot = nullptr;
    for (const PluginBridgeSlot& candidate : document.slots) {
        if (candidate.index == slotIndex) {
            slot = &candidate;
            break;
        }
    }

    if (slot && !slot->filePath.empty()) {
        label << (slot->displayName.empty() ? "Captured" : slot->displayName);
    } else {
        label << "Empty";
    }

    const std::string captureID = "samplerCaptureSlot" + std::to_string(slotNumber);
    const std::string playID = "samplerSlot" + std::to_string(slotNumber);
    const auto captureBinding = document.shortcutBindings.find(captureID);
    const auto playBinding = document.shortcutBindings.find(playID);

    label << " | Cap ";
    label << (captureBinding != document.shortcutBindings.end() && !captureBinding->second.empty()
        ? captureBinding->second : "None");
    label << " | Play ";
    label << (playBinding != document.shortcutBindings.end() && !playBinding->second.empty()
        ? playBinding->second : "None");

    return label.str();
}
