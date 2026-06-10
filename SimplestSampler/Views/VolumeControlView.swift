import SwiftUI

struct VolumeControlView: View {
    @ObservedObject var viewModel: SamplerViewModel
    @Environment(\.samplerThemeColors) private var theme

    var body: some View {
        VStack(alignment: .leading, spacing: SamplerTheme.Layout.volumeMeterSpacing) {
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

            HStack(spacing: SamplerTheme.Layout.volumeSliderGap) {
                Spacer()
                    .frame(width: SamplerTheme.Layout.labelWidth)

                VolumeMeterView(level: viewModel.audioPlayback.outputLevel)
                    .frame(maxWidth: .infinity)

                Spacer()
                    .frame(width: SamplerTheme.Layout.volumeValueWidth)
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

private struct VolumeMeterView: View {
    var level: Double
    @Environment(\.samplerThemeColors) private var theme

    var body: some View {
        GeometryReader { geometry in
            ZStack(alignment: .leading) {
                Capsule(style: .continuous)
                    .fill(theme.border.opacity(0.35))

                Capsule(style: .continuous)
                    .fill(
                        LinearGradient(
                            colors: [theme.accentSoft, theme.accent],
                            startPoint: .leading,
                            endPoint: .trailing
                        )
                    )
                    .frame(width: max(0, geometry.size.width * CGFloat(level)))
            }
        }
        .frame(height: SamplerTheme.Layout.volumeMeterHeight)
        .animation(.linear(duration: 0.05), value: level)
    }
}
