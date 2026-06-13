import SwiftUI

private enum ShortcutPreferencesLayout {
    static let sectionSpacing: CGFloat = 14
    static let contentPadding: CGFloat = 18
    static let windowMinWidth: CGFloat = 392
    static let windowIdealWidth: CGFloat = 404
    static let appearanceLabelWidth: CGFloat = 76
    static let appearanceMenuWidth: CGFloat = 152
    static let shortcutInfoWidth: CGFloat = 78
    static let bindingMinWidth: CGFloat = 94
    static let buttonGap: CGFloat = 6
}

struct ShortcutPreferencesView: View {
    @ObservedObject var viewModel: SamplerViewModel
    @Environment(\.samplerThemeColors) private var theme

    var body: some View {
        VStack(alignment: .leading, spacing: ShortcutPreferencesLayout.sectionSpacing) {
            header
            appearanceSection
            captureSection
            playSection
            footer
        }
        .padding(ShortcutPreferencesLayout.contentPadding)
        .frame(
            minWidth: ShortcutPreferencesLayout.windowMinWidth,
            idealWidth: ShortcutPreferencesLayout.windowIdealWidth,
            alignment: .topLeading
        )
        .fixedSize(horizontal: false, vertical: true)
        .samplerWindowChrome()
        .background(KeyboardHandlerView(viewModel: viewModel))
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Preferences")
                .font(.system(size: 16, weight: .semibold))
                .foregroundStyle(theme.text)
        }
    }

    private var appearanceSection: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Appearance")
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(theme.muted)

            VStack(alignment: .leading, spacing: 8) {
                appearancePickerRow(title: "Theme pack") {
                    Picker("Theme pack", selection: Binding(
                        get: { viewModel.appTheme },
                        set: { viewModel.setAppTheme($0) }
                    )) {
                        ForEach(SamplerAppTheme.allCases) { appTheme in
                            Text(appTheme.label).tag(appTheme)
                        }
                    }
                    .labelsHidden()
                    .pickerStyle(.menu)
                    .frame(width: ShortcutPreferencesLayout.appearanceMenuWidth, alignment: .leading)
                }

                appearancePickerRow(title: "Color scheme") {
                    Picker("Color scheme", selection: Binding(
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
        HStack(alignment: .top, spacing: 10) {
            Button("Reset All to Defaults") {
                viewModel.resetAllShortcutsToDefaults()
            }
            .disabled(!viewModel.hasCustomShortcutBindings())

            Spacer()

            Text(viewModel.statusMessage)
                .font(.system(size: 11))
                .foregroundStyle(viewModel.statusIsError ? theme.error : theme.muted)
                .multilineTextAlignment(.trailing)
                .lineLimit(2)
                .frame(maxWidth: 160, alignment: .trailing)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private func appearancePickerRow<Content: View>(title: String, @ViewBuilder content: () -> Content) -> some View {
        HStack(alignment: .center, spacing: 12) {
            Text(title)
                .font(.system(size: 11, weight: .semibold))
                .foregroundStyle(theme.text)
                .frame(width: ShortcutPreferencesLayout.appearanceLabelWidth, alignment: .leading)

            content()
                .frame(maxWidth: .infinity, alignment: .leading)
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
        HStack(alignment: .center, spacing: 10) {
            VStack(alignment: .leading, spacing: 2) {
                Text("Slot \(slotNumber)")
                    .font(.system(size: 12, weight: .semibold))
                    .foregroundStyle(theme.text)

                Text(ShortcutDefinitions.preferencesActionLabel(for: shortcutID))
                    .font(.system(size: 11))
                    .foregroundStyle(theme.muted)
            }
            .frame(width: ShortcutPreferencesLayout.shortcutInfoWidth, alignment: .leading)

            VStack(alignment: .leading, spacing: 4) {
                ShortcutBindingButton(viewModel: viewModel, shortcutID: shortcutID)
                    .frame(minWidth: ShortcutPreferencesLayout.bindingMinWidth, alignment: .leading)

                Text("Default: \(viewModel.defaultShortcutLabel(for: shortcutID))")
                    .font(.system(size: 10))
                    .foregroundStyle(theme.muted)
                    .lineLimit(1)
            }
            .frame(maxWidth: .infinity, alignment: .leading)

            HStack(spacing: ShortcutPreferencesLayout.buttonGap) {
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
            .fixedSize(horizontal: true, vertical: false)
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
