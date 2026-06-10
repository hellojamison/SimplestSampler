import SwiftUI
import UniformTypeIdentifiers

struct SlotRowView: View {
    @ObservedObject var viewModel: SamplerViewModel
    let index: Int
    let capture: SamplerCapture?
    let isStored: Bool

    @Environment(\.samplerThemeColors) private var theme
    @State private var draftName = ""
    @FocusState private var nameFocused: Bool

    private var isSelected: Bool {
        capture?.id == viewModel.selectedCaptureId
            && viewModel.selectedCaptureSource == (isStored ? "stored" : "recent")
    }

    private var isPlaying: Bool {
        guard let capture else { return false }
        return viewModel.audioPlayback.isPlaying && viewModel.audioPlayback.playingCaptureId == capture.id
    }

    private var primaryText: Color {
        isSelected ? theme.slotSelectedText : theme.text
    }

    private var secondaryText: Color {
        isSelected ? theme.slotSelectedText.opacity(0.72) : theme.muted
    }

    var body: some View {
        HStack(spacing: SamplerTheme.Layout.rowColumnGap) {
            Button(action: playAction) {
                Image(systemName: isPlaying ? "stop.fill" : "play.fill")
                    .font(.system(size: 9, weight: .bold))
                    .frame(width: SamplerTheme.Layout.playButtonSize, height: SamplerTheme.Layout.playButtonSize)
            }
            .buttonStyle(SlotPlayButtonStyle(isSelected: isSelected))
            .disabled(capture == nil)

            VStack(alignment: .leading, spacing: SamplerTheme.Layout.metaLineSpacing) {
                HStack(spacing: SamplerTheme.Layout.metaNameDurationGap) {
                    TextField(
                        isStored ? "Stored Sample" : "Empty Slot",
                        text: $draftName
                    )
                    .textFieldStyle(.plain)
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(primaryText)
                    .lineLimit(1)
                    .disabled(capture == nil)
                    .focused($nameFocused)
                    .onSubmit { commitRename() }
                    .onChange(of: nameFocused) { focused in
                        if focused {
                            draftName = capture?.slotLabel ?? ""
                        } else {
                            commitRename()
                        }
                    }
                    .onChange(of: capture?.id) { _ in
                        if !nameFocused {
                            draftName = capture?.slotLabel ?? ""
                        }
                    }
                    .onAppear {
                        draftName = capture?.slotLabel ?? ""
                    }

                    if let duration = capture?.formattedDuration {
                        Text(duration)
                            .font(.system(size: 10, weight: .medium, design: .monospaced))
                            .foregroundStyle(secondaryText)
                            .fixedSize()
                    }
                }

                if let capture {
                    Text(capture.fileName)
                        .font(.system(size: 10))
                        .foregroundStyle(secondaryText)
                        .lineLimit(1)
                } else if isStored {
                    Text("Click Store on an active sample to keep it here.")
                        .font(.system(size: 10))
                        .foregroundStyle(secondaryText)
                }
            }
            .frame(minWidth: 0, maxWidth: .infinity, alignment: .leading)

            if !isStored {
                HStack(spacing: SamplerTheme.Layout.actionGap) {
                    if ShortcutDefinitions.hasShortcutSupport(forSlotNumber: index + 1) {
                        ShortcutBindingButton(
                            viewModel: viewModel,
                            shortcutID: ShortcutDefinitions.captureShortcutID(for: index + 1),
                            prefix: "Cap",
                            compact: true
                        )
                    }
                    Button(capture?.saved == true ? "Stored" : "Store") {
                        viewModel.toggleStore(at: index)
                    }
                    .buttonStyle(SlotActionButtonStyle(isAccent: capture?.saved == true, isSelected: isSelected))
                    .disabled(capture == nil)

                    Button("Delete") {
                        viewModel.deleteActiveSlot(at: index)
                    }
                    .buttonStyle(SlotActionButtonStyle(isDestructive: true, isSelected: isSelected))
                    .disabled(capture == nil)
                }
                .fixedSize(horizontal: true, vertical: false)

                if ShortcutDefinitions.hasShortcutSupport(forSlotNumber: index + 1) {
                    ShortcutBindingButton(
                        viewModel: viewModel,
                        shortcutID: ShortcutDefinitions.playShortcutID(for: index + 1),
                        compact: true,
                        isTrigger: true
                    )
                    .fixedSize(horizontal: true, vertical: false)
                }
            } else if capture != nil {
                HStack(spacing: SamplerTheme.Layout.actionGap) {
                    Button("Finder") {
                        if let id = capture?.id {
                            viewModel.revealStoredCaptureInFinder(id: id)
                        }
                    }
                    .buttonStyle(SlotActionButtonStyle(isSelected: isSelected))
                    .fixedSize(horizontal: true, vertical: false)

                    Button("Delete") {
                        if let id = capture?.id {
                            viewModel.deleteStoredCapture(id: id)
                        }
                    }
                    .buttonStyle(SlotActionButtonStyle(isDestructive: true, isSelected: isSelected))
                    .fixedSize(horizontal: true, vertical: false)
                }
                .fixedSize(horizontal: true, vertical: false)
            }
        }
        .padding(.horizontal, SamplerTheme.Layout.rowPaddingH)
        .padding(.vertical, SamplerTheme.Layout.rowPaddingV)
        .opacity(capture == nil && !isStored ? 0.72 : 1)
        .background(rowBackground)
        .clipShape(RoundedRectangle(cornerRadius: SamplerTheme.Layout.rowCornerRadius, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: SamplerTheme.Layout.rowCornerRadius, style: .continuous)
                .stroke(borderColor, lineWidth: isSelected ? 1.5 : 1)
        )
        .contentShape(Rectangle())
        .onTapGesture {
            guard let capture else { return }
            viewModel.selectCapture(id: capture.id, source: isStored ? "stored" : "recent")
        }
        .onDrop(of: [.fileURL], isTargeted: dropTargetBinding) { providers in
            guard !isStored else { return false }
            return handleDrop(providers)
        }
    }

    private var rowBackground: some View {
        Group {
            if isSelected {
                theme.slotSelected
            } else if isPlaying {
                theme.slotPlaying
            } else {
                theme.slotBackground
            }
        }
    }

    private var borderColor: Color {
        isSelected ? theme.accent : theme.border
    }

    private var dropTargetBinding: Binding<Bool> {
        Binding(
            get: { viewModel.dropTargetIndex == index },
            set: { targeted in
                viewModel.dropTargetIndex = targeted ? index : (viewModel.dropTargetIndex == index ? -1 : viewModel.dropTargetIndex)
            }
        )
    }

    private func playAction() {
        guard let capture else { return }
        viewModel.playCapture(capture, source: isStored ? "stored" : "recent")
    }

    private func commitRename() {
        guard let capture else { return }
        let submitted = draftName.trimmingCharacters(in: .whitespacesAndNewlines)
        let hasCustom = !submitted.isEmpty
        let nextName = hasCustom ? submitted : capture.defaultDisplayName
        viewModel.renameCapture(id: capture.id, displayName: nextName, hasCustomDisplayName: hasCustom)
        draftName = capture.slotLabel
    }

    private func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first(where: { $0.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier) }) else {
            return false
        }

        provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, _ in
            guard let data = item as? Data, let url = URL(dataRepresentation: data, relativeTo: nil) else { return }
            Task { @MainActor in
                viewModel.loadDroppedFile(path: url.path, slotIndex: index)
                viewModel.dropTargetIndex = -1
            }
        }
        return true
    }
}

struct SlotPlayButtonStyle: ButtonStyle {
    var isSelected = false
    @Environment(\.samplerThemeColors) private var theme

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .foregroundStyle(isSelected ? theme.slotSelectedText : theme.text)
            .background(
                Circle()
                    .fill(
                        isSelected
                            ? Color.white.opacity(configuration.isPressed ? 0.12 : 0.14)
                            : (configuration.isPressed ? theme.playButtonFillPressed : theme.playButtonFill)
                    )
            )
            .overlay(
                Circle()
                    .stroke(isSelected ? Color.white.opacity(0.18) : theme.border, lineWidth: 1)
            )
    }
}

struct SlotActionButtonStyle: ButtonStyle {
    var isDestructive = false
    var isAccent = false
    var isSelected = false
    @Environment(\.samplerThemeColors) private var theme

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 10, weight: .semibold))
            .padding(.horizontal, SamplerTheme.Layout.actionPaddingH)
            .frame(minHeight: SamplerTheme.Layout.actionMinHeight)
            .foregroundStyle(actionForeground)
            .background(
                Capsule(style: .continuous)
                    .fill(actionBackground(configuration: configuration))
            )
            .overlay(
                Capsule(style: .continuous)
                    .stroke(isSelected ? Color.white.opacity(0.18) : theme.border, lineWidth: 1)
            )
    }

    private var actionForeground: Color {
        if isDestructive {
            return isSelected ? theme.slotSelectedText : theme.captureText
        }
        return isSelected ? theme.slotSelectedText : theme.muted
    }

    private func actionBackground(configuration: Configuration) -> Color {
        if isSelected {
            return Color.white.opacity(configuration.isPressed ? 0.12 : 0.14)
        }
        if isAccent {
            return theme.accentSoft
        }
        return configuration.isPressed
            ? theme.buttonFillPressed
            : theme.actionButtonFill
    }
}

struct SlotShortcutButtonStyle: ButtonStyle {
    var isCapturing: Bool
    var isTrigger = false
    @Environment(\.samplerThemeColors) private var theme

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .padding(.horizontal, isTrigger ? SamplerTheme.Layout.shortcutTriggerPaddingH : SamplerTheme.Layout.shortcutPaddingH)
            .frame(minWidth: isTrigger ? SamplerTheme.Layout.shortcutTriggerMinWidth : nil)
            .frame(minHeight: SamplerTheme.Layout.actionMinHeight)
            .foregroundStyle(isCapturing ? theme.captureText : theme.text)
            .background(
                Capsule(style: .continuous)
                    .fill(
                        isCapturing
                            ? theme.shortcutCapturingFill
                            : (isTrigger ? theme.shortcutTriggerFill : theme.shortcutButtonFill)
                    )
            )
            .overlay(
                Capsule(style: .continuous)
                    .stroke(theme.border, lineWidth: 1)
            )
    }
}
