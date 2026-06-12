#include "CaptureWavWriter.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

struct WavHeader {
    char riff[4] = { 'R', 'I', 'F', 'F' };
    uint32_t chunkSize = 0;
    char wave[4] = { 'W', 'A', 'V', 'E' };
    char fmt[4] = { 'f', 'm', 't', ' ' };
    uint32_t fmtChunkSize = 16;
    uint16_t audioFormat = 3; // IEEE float
    uint16_t numChannels = 1;
    uint32_t sampleRate = 48000;
    uint32_t byteRate = 0;
    uint16_t blockAlign = 0;
    uint16_t bitsPerSample = 32;
    char data[4] = { 'd', 'a', 't', 'a' };
    uint32_t dataChunkSize = 0;
};

void WriteU32LE(FILE* file, uint32_t value) {
    const unsigned char bytes[4] = {
        static_cast<unsigned char>(value & 0xff),
        static_cast<unsigned char>((value >> 8) & 0xff),
        static_cast<unsigned char>((value >> 16) & 0xff),
        static_cast<unsigned char>((value >> 24) & 0xff),
    };
    std::fwrite(bytes, 1, 4, file);
}

void WriteU16LE(FILE* file, uint16_t value) {
    const unsigned char bytes[2] = {
        static_cast<unsigned char>(value & 0xff),
        static_cast<unsigned char>((value >> 8) & 0xff),
    };
    std::fwrite(bytes, 1, 2, file);
}

} // namespace

bool CaptureWavWriter::WriteFloat32Wav(
    const std::string& filePath,
    const std::vector<float>& interleavedSamples,
    int32_t channelCount,
    double sampleRateHz)
{
    if (filePath.empty() || channelCount <= 0 || sampleRateHz <= 0.0) {
        return false;
    }

    const uint32_t channels = static_cast<uint32_t>(channelCount);
    const uint32_t sampleRate = static_cast<uint32_t>(sampleRateHz + 0.5);
    const uint32_t frameCount = channels > 0
        ? static_cast<uint32_t>(interleavedSamples.size() / channels)
        : 0;
    if (frameCount == 0) {
        return false;
    }

    WavHeader header;
    header.numChannels = static_cast<uint16_t>(channels);
    header.sampleRate = sampleRate;
    header.bitsPerSample = 32;
    header.blockAlign = static_cast<uint16_t>(channels * (header.bitsPerSample / 8));
    header.byteRate = sampleRate * header.blockAlign;
    header.dataChunkSize = frameCount * header.blockAlign;
    header.chunkSize = 36 + header.dataChunkSize;

    FILE* file = std::fopen(filePath.c_str(), "wb");
    if (!file) {
        return false;
    }

    std::fwrite(header.riff, 1, 4, file);
    WriteU32LE(file, header.chunkSize);
    std::fwrite(header.wave, 1, 4, file);
    std::fwrite(header.fmt, 1, 4, file);
    WriteU32LE(file, header.fmtChunkSize);
    WriteU16LE(file, header.audioFormat);
    WriteU16LE(file, header.numChannels);
    WriteU32LE(file, header.sampleRate);
    WriteU32LE(file, header.byteRate);
    WriteU16LE(file, header.blockAlign);
    WriteU16LE(file, header.bitsPerSample);
    std::fwrite(header.data, 1, 4, file);
    WriteU32LE(file, header.dataChunkSize);

    const size_t bytesToWrite = static_cast<size_t>(header.dataChunkSize);
    const size_t written = std::fwrite(interleavedSamples.data(), 1, bytesToWrite, file);
    std::fclose(file);
    return written == bytesToWrite;
}
