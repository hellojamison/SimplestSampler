import SwiftUI

struct VolumeControlView: View {
    @ObservedObject var viewModel: SamplerViewModel

    var body: some View {
        HStack(spacing: 8) {
            Text("Volume")
                .font(.system(size: 11, weight: .semibold))
                .foregroundStyle(SamplerTheme.muted)

            Slider(
                value: Binding(
                    get: { Double(viewModel.volume) },
                    set: { viewModel.setVolume(Int($0.rounded())) }
                ),
                in: 0...Double(SamplerConstants.maxVolume),
                step: 1
            )
            .frame(maxWidth: 180)

            Text("\(viewModel.volume)%")
                .font(.system(size: 11, weight: .medium, design: .monospaced))
                .foregroundStyle(SamplerTheme.text)
                .frame(width: 40, alignment: .trailing)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
        .background(Color.white.opacity(0.55))
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .stroke(SamplerTheme.border, lineWidth: 1)
        )
        .contentShape(Rectangle())
        .gesture(
            TapGesture().modifiers(.option).onEnded {
                viewModel.resetVolumeToDefault()
            }
        )
    }
}
