#include "SimplestSampler_AS_Defs.h"
#include "SimplestSampler_AS_GUI.h"
#include "SimplestSampler_AS_HostProcessor.h"
#include "SimplestSampler_AS_Parameters.h"

#include "AAX_ICollection.h"
#include "AAX_IEffectDescriptor.h"
#include "AAX_IPropertyMap.h"
#include "AAX_Exception.h"
#include "AAX_Assert.h"

static AAX_Result SimplestSampler_GetPlugInDescription(AAX_IEffectDescriptor* outDescriptor) {
    AAX_CheckedResult err;

    AAX_IPropertyMap* const properties = outDescriptor->NewPropertyMap();
    if (!properties) {
        err = AAX_ERROR_NULL_OBJECT;
    }

    err = properties->AddProperty(AAX_eProperty_ManufacturerID, cSimplestSampler_ManufacturerID);
    err = properties->AddProperty(AAX_eProperty_ProductID, cSimplestSampler_ProductID);
    err = properties->AddProperty(AAX_eProperty_PlugInID_AudioSuite, cSimplestSampler_PlugInID_AudioSuite);
    err = properties->AddProperty(AAX_eProperty_NumberOfInputs, AAX_eMaxAudioSuiteTracks);
    err = properties->AddProperty(AAX_eProperty_NumberOfOutputs, AAX_eMaxAudioSuiteTracks);
    err = properties->AddProperty(AAX_eProperty_RequiresAnalysis, true);

    err = outDescriptor->AddName("SimplestSampler");
    err = outDescriptor->AddName("Simplest Sampler");
    err = outDescriptor->AddName("SSmp");

    err = outDescriptor->AddCategory(AAX_ePlugInCategory_Example | AAX_ePlugInCategory_Effect);

    err = outDescriptor->AddProcPtr(
        reinterpret_cast<void*>(SimplestSampler_AS_Parameters::Create),
        kAAX_ProcPtrID_Create_EffectParameters);
    err = outDescriptor->AddProcPtr(
        reinterpret_cast<void*>(SimplestSampler_AS_HostProcessor::Create),
        kAAX_ProcPtrID_Create_HostProcessor);
    err = outDescriptor->AddProcPtr(
        reinterpret_cast<void*>(SimplestSampler_AS_GUI::Create),
        kAAX_ProcPtrID_Create_EffectGUI);

    err = outDescriptor->SetProperties(properties);
    return err;
}

AAX_Result GetEffectDescriptions(AAX_ICollection* outCollection) {
    AAX_CheckedResult err;
    static const AAX_CEffectID effectID = "com.hellojamison.simplestsampler.audiosuite";

    AAX_IEffectDescriptor* const plugInDescriptor = outCollection->NewDescriptor();
    if (plugInDescriptor) {
        AAX_SWALLOW_MULT(
            err = SimplestSampler_GetPlugInDescription(plugInDescriptor);
            err = outCollection->AddEffect(effectID, plugInDescriptor);
        );
    } else {
        err = AAX_ERROR_NULL_OBJECT;
    }

    err = outCollection->SetManufacturerName(kSimplestSampler_ManufacturerName);
    err = outCollection->AddPackageName("SimplestSampler AudioSuite Capture");
    err = outCollection->AddPackageName("SimplestSampler");
    err = outCollection->SetPackageVersion(1);

    return err;
}
