import SwiftUI

struct SamplerVolumeSlider: View {
    @Binding var value: Int
    var maxValue: Int
    var theme: SamplerThemeColors

    var body: some View {
        Slider(
            value: Binding(
                get: { Double(value) },
                set: { value = Int($0.rounded()) }
            ),
            in: 0...Double(maxValue)
        )
        .tint(theme.sliderThumb)
        .controlSize(.small)
        .padding(.vertical, 2)
        .background {
            Capsule(style: .continuous)
                .fill(theme.sliderTrack)
                .overlay(
                    Capsule(style: .continuous)
                        .stroke(theme.border.opacity(0.9), lineWidth: 1)
                )
                .frame(height: 6)
        }
    }
}
