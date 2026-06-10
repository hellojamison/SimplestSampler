import SwiftUI

struct VolumeControlView: View {
    @ObservedObject var viewModel: SamplerViewModel
    @Environment(\.samplerThemeColors) private var theme

    var body: some View {
        HStack(spacing: SamplerTheme.Layout.rowColumnGap) {
            Text("Volume")
                .font(.system(size: 11, weight: .semibold))
                .foregroundStyle(theme.muted)
                .frame(width: SamplerTheme.Layout.labelWidth, alignment: .leading)

            HStack(spacing: SamplerTheme.Layout.volumeSliderGap) {
                Slider(
                    value: Binding(
                        get: { Double(viewModel.volume) },
                        set: { viewModel.setVolume(Int($0.rounded())) }
                    ),
                    in: 0...Double(SamplerConstants.maxVolume),
                    step: 1
                )

                Text(SamplerVolumeMath.formattedDecibels(forVolume: viewModel.volume))
                    .font(.system(size: 11, weight: .semibold, design: .monospaced))
                    .foregroundStyle(theme.text)
                    .frame(width: SamplerTheme.Layout.volumeValueWidth, alignment: .trailing)
                    .lineLimit(1)
                    .minimumScaleFactor(0.85)
            }
        }
        .padding(.horizontal, SamplerTheme.Layout.chipPaddingH)
        .padding(.vertical, SamplerTheme.Layout.chipPaddingV)
        .frame(minHeight: SamplerTheme.Layout.chipMinHeight)
        .samplerChipSurface(cornerRadius: SamplerTheme.Layout.volumeCornerRadius)
        .contentShape(Rectangle())
        .gesture(
            TapGesture().modifiers(.option).onEnded {
                viewModel.resetVolumeToDefault()
            }
        )
    }
}
