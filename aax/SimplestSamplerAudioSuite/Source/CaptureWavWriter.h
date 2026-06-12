#pragma once

#include <cstdint>
#include <string>
#include <vector>

class CaptureWavWriter {
public:
    static bool WriteFloat32Wav(
        const std::string& filePath,
        const std::vector<float>& interleavedSamples,
        int32_t channelCount,
        double sampleRateHz
    );
};
