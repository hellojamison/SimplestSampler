import Foundation

/// Logarithmic sampler volume curve ported from OverCue `getSamplerGainMultiplier`.
enum SamplerVolumeMath {
    static let unityVolume = SamplerConstants.defaultVolume
    static let minAttenuationDecibels: Double = -30

    static func normalizedVolume(_ volume: Int) -> Int {
        max(0, min(SamplerConstants.maxVolume, volume))
    }

    static func gainMultiplier(forVolume volume: Int) -> Float {
        let normalized = normalizedVolume(volume)
        if normalized <= 0 {
            return 0
        }

        let unityRatio = Double(normalized) / Double(unityVolume)
        if unityRatio <= 1 {
            let attenuationDecibels = (1 - unityRatio) * minAttenuationDecibels
            return Float(pow(10, attenuationDecibels / 20))
        }
        return Float(unityRatio)
    }

    /// Decibels relative to unity (80 = 0 dB). `nil` when muted (volume 0).
    static func decibelsRelativeToUnity(forVolume volume: Int) -> Double? {
        let normalized = normalizedVolume(volume)
        if normalized <= 0 {
            return nil
        }

        let unityRatio = Double(normalized) / Double(unityVolume)
        if unityRatio <= 1 {
            return (1 - unityRatio) * minAttenuationDecibels
        }
        return 20 * log10(unityRatio)
    }

    static func formattedDecibels(forVolume volume: Int) -> String {
        guard let decibels = decibelsRelativeToUnity(forVolume: volume) else {
            return "Mute"
        }
        if abs(decibels) < 0.05 {
            return "0 dB"
        }
        let sign = decibels > 0 ? "+" : ""
        return String(format: "%@%.1f dB", sign, decibels)
    }
}
