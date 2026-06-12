#pragma once

#include "AAX.h"

// Dev-only placeholder IDs — register real Manufacturer/Product IDs with Avid before release.
static constexpr const char* kSimplestSampler_ManufacturerName = "Take One Audio";
const AAX_CTypeID cSimplestSampler_ManufacturerID = 'JMRB';
const AAX_CTypeID cSimplestSampler_ProductID = 'SSmp';
const AAX_CTypeID cSimplestSampler_PlugInID_AudioSuite = 'ASmp';

constexpr int kSimplestSamplerDefaultActiveSlots = 4;
constexpr int kSimplestSamplerMaxActiveSlots = 16;

static const AAX_CParamID kSimplestSamplerParamTargetSlot = "TargetSlot";
static const AAX_CParamID kSimplestSamplerParamActiveSlotCount = "ActiveSlotCount";
