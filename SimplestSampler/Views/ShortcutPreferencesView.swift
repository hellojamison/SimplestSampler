import SwiftUI

struct ShortcutPreferencesView: View {
    @ObservedObject var viewModel: SamplerViewModel
    @Environment(\.samplerThemeColors) private var theme

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            header
            appearanceSection
            captureSection
            playSection
            footer
        }
        .padding(20)
        .frame(minWidth: 460, minHeight: 440)
        .background(theme.backgroundGradient)
        .background(KeyboardHandlerView(viewModel: viewModel))
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Preferences")
                .font(.system(size: 16, weight: .semibold))
                .foregroundStyle(theme.text)
            Text(captureHint)
                .font(.system(size: 11))
                .foregroundStyle(theme.muted)
        }
    }

    private var captureHint: String {
        if viewModel.capturingShortcutId.isEmpty {
            return "Click a shortcut to change it. Press Escape to cancel capture."
        }
        return "Listening for a shortcut. Press Escape to cancel."
    }

    private var appearanceSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Appearance")
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(theme.muted)

            Picker("Appearance", selection: Binding(
                get: { viewModel.themeMode },
                set: { viewModel.setThemeMode($0) }
            )) {
                ForEach(SamplerThemeMode.allCases) { mode in
                    Text(mode.label).tag(mode)
                }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
        }
    }

    private var captureSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Capture")
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(theme.muted)

            VStack(spacing: SamplerTheme.Layout.rowSpacing) {
                ForEach(ShortcutDefinitions.captureSlotIDs, id: \.self) { shortcutID in
                    ShortcutPreferenceRow(viewModel: viewModel, shortcutID: shortcutID)
                }
            }
        }
    }

    private var playSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Play / Trigger")
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(theme.muted)

            VStack(spacing: SamplerTheme.Layout.rowSpacing) {
                ForEach(ShortcutDefinitions.playSlotIDs, id: \.self) { shortcutID in
                    ShortcutPreferenceRow(viewModel: viewModel, shortcutID: shortcutID)
                }
            }
        }
    }

    private var footer: some View {
        HStack {
            Button("Reset All to Defaults") {
                viewModel.resetAllShortcutsToDefaults()
            }
            .disabled(!viewModel.hasCustomShortcutBindings())

            Spacer()

            Text(viewModel.statusMessage)
                .font(.system(size: 11))
                .foregroundStyle(viewModel.statusIsError ? theme.error : theme.muted)
                .lineLimit(1)
        }
    }
}

private struct ShortcutPreferenceRow: View {
    @ObservedObject var viewModel: SamplerViewModel
    let shortcutID: String
    @Environment(\.samplerThemeColors) private var theme

    private var slotNumber: Int {
        ShortcutDefinitions.slotNumber(for: shortcutID) ?? 0
    }

    private var hasBinding: Bool {
        !viewModel.preferencesStore.shortcutAccelerator(for: shortcutID)
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .isEmpty
    }

    var body: some View {
        HStack(spacing: 10) {
            Text("Slot \(slotNumber)")
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(theme.text)
                .frame(width: SamplerTheme.Layout.labelWidth, alignment: .leading)

            Text(ShortcutDefinitions.preferencesActionLabel(for: shortcutID))
                .font(.system(size: 12))
                .foregroundStyle(theme.muted)
                .frame(width: 88, alignment: .leading)

            ShortcutBindingButton(viewModel: viewModel, shortcutID: shortcutID)
                .frame(minWidth: 100, alignment: .leading)

            Text("Default: \(viewModel.defaultShortcutLabel(for: shortcutID))")
                .font(.system(size: 10))
                .foregroundStyle(theme.muted)
                .frame(maxWidth: .infinity, alignment: .leading)

            Button("Clear") {
                viewModel.clearShortcut(shortcutID: shortcutID)
            }
            .buttonStyle(ShortcutPreferenceActionButtonStyle())
            .disabled(!hasBinding)

            Button("Reset") {
                viewModel.resetShortcutToDefault(shortcutID: shortcutID)
            }
            .buttonStyle(ShortcutPreferenceActionButtonStyle())
            .disabled(viewModel.isShortcutAtDefault(for: shortcutID))
        }
        .padding(.horizontal, SamplerTheme.Layout.chipPaddingH)
        .padding(.vertical, SamplerTheme.Layout.chipPaddingV)
        .samplerChipSurface()
    }
}

private struct ShortcutPreferenceActionButtonStyle: ButtonStyle {
    @Environment(\.samplerThemeColors) private var theme

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 10, weight: .semibold))
            .padding(.horizontal, SamplerTheme.Layout.actionPaddingH)
            .frame(minHeight: SamplerTheme.Layout.actionMinHeight)
            .foregroundStyle(theme.text)
            .background(
                Capsule(style: .continuous)
                    .fill(configuration.isPressed ? theme.buttonFillPressed : theme.buttonFill)
            )
            .overlay(
                Capsule(style: .continuous)
                    .stroke(theme.border, lineWidth: 1)
            )
    }
}
