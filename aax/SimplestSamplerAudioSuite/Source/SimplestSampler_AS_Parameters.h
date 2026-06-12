#pragma once

#include "AAX_CEffectParameters.h"
#include "SimplestSampler_AS_Defs.h"

#include <string>

class SimplestSampler_AS_Parameters : public AAX_CEffectParameters {
public:
    static AAX_CEffectParameters* AAX_CALLBACK Create();

    SimplestSampler_AS_Parameters();
    ~SimplestSampler_AS_Parameters() override;

    AAX_Result EffectInit() override;

    int32_t TargetSlotNumber() const;
    int32_t TargetSlotIndex() const;
    int32_t ActiveSlotCount() const;
    double SampleRateHz() const;
    std::string SlotDisplayLabel(int slotNumber) const;
    bool SetTargetSlotNumber(int32_t slot);
    bool SetActiveSlotCount(int32_t count);

private:
    AAX_Result AddTargetSlotParameter();
    AAX_Result AddActiveSlotCountParameter();

    double mSampleRateHz = 48000.0;
};
