#pragma once

#include "AAX_CEffectGUI_Cocoa.h"

class SimplestSampler_AS_GUI;

#ifdef __OBJC__
@class SimplestSamplerPanelView;
#else
class SimplestSamplerPanelView;
#endif

class SimplestSampler_AS_GUI : public AAX_CEffectGUI_Cocoa {
public:
    static AAX_CEffectGUI* AAX_CALLBACK Create();

    ~SimplestSampler_AS_GUI() override;

    AAX_Result GetViewSize(AAX_Point* oEffectViewSize) const override;
    AAX_Result GetCustomLabel(AAX_EPlugInStrings iSelector, AAX_IString* oString) const override;
    AAX_Result ParameterUpdated(AAX_CParamID iParameterID) override;
    AAX_Result TimerWakeup() override;

    void RefreshPanel();
    bool SetTargetSlot(int32_t slot);
    bool SetActiveSlotCount(int32_t count);
    int32_t TargetSlot() const;
    int32_t ActiveSlotCount() const;

protected:
    void CreateViewContents() override;
    void CreateViewContainer() override;
    void DeleteViewContainer() override;

private:
    SimplestSamplerPanelView* PanelView() const;
    void DetachPanelView();
    void SchedulePanelRefresh();

    bool mPanelRefreshEnabled = false;
};
