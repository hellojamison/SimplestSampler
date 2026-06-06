import SwiftUI
import UniformTypeIdentifiers

struct SlotRowView: View {
    @ObservedObject var viewModel: SamplerViewModel
    let index: Int
    let capture: SamplerCapture?
    let isStored: Bool

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

    var body: some View {
        HStack(spacing: 8) {
            Button(action: playAction) {
                Image(systemName: isPlaying ? "stop.fill" : "play.fill")
                    .font(.system(size: 11, weight: .bold))
                    .frame(width: 28, height: 28)
            }
            .buttonStyle(SlotPlayButtonStyle())
            .disabled(capture == nil)

            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 6) {
                    TextField(
                        isStored ? "Stored Sample" : "Empty Slot",
                        text: $draftName
                    )
                    .textFieldStyle(.plain)
                    .font(.system(size: 13, weight: .semibold))
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
                            .foregroundStyle(SamplerTheme.muted)
                    }
                }

                if let capture {
                    Text(capture.fileName)
                        .font(.system(size: 10))
                        .foregroundStyle(SamplerTheme.muted)
                        .lineLimit(1)
                } else if isStored {
                    Text("Click Store on an active sample to keep it here.")
                        .font(.system(size: 10))
                        .foregroundStyle(SamplerTheme.muted)
                }
            }

            Spacer(minLength: 4)

            if !isStored {
                HStack(spacing: 4) {
                    shortcutButton(
                        id: ShortcutDefinitions.captureShortcutID(for: index + 1),
                        prefix: "Cap"
                    )
                    Button(capture?.saved == true ? "Stored" : "Store") {
                        viewModel.toggleStore(at: index)
                    }
                    .buttonStyle(SlotActionButtonStyle(isAccent: capture?.saved == true))
                    .disabled(capture == nil)

                    Button("Delete") {
                        viewModel.deleteActiveSlot(at: index)
                    }
                    .buttonStyle(SlotActionButtonStyle(isDestructive: true))
                    .disabled(capture == nil)

                    shortcutButton(
                        id: ShortcutDefinitions.playShortcutID(for: index + 1),
                        prefix: nil
                    )
                }
            } else if capture != nil {
                Button("Delete") {
                    if let id = capture?.id {
                        viewModel.deleteStoredCapture(id: id)
                    }
                }
                .buttonStyle(SlotActionButtonStyle(isDestructive: true))
            }
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 8)
        .background(rowBackground)
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
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
                SamplerTheme.slotSelected
            } else if isPlaying {
                SamplerTheme.accentSoft.opacity(0.55)
            } else {
                SamplerTheme.slotBackground
            }
        }
    }

    private var borderColor: Color {
        isSelected ? SamplerTheme.accent : SamplerTheme.border
    }

    private var dropTargetBinding: Binding<Bool> {
        Binding(
            get: { viewModel.dropTargetIndex == index },
            set: { targeted in
                viewModel.dropTargetIndex = targeted ? index : (viewModel.dropTargetIndex == index ? -1 : viewModel.dropTargetIndex)
            }
        )
    }

    @ViewBuilder
    private func shortcutButton(id: String, prefix: String?) -> some View {
        let isCapturing = viewModel.capturingShortcutId == id
        Button(action: {
            if isCapturing {
                viewModel.cancelShortcutCapture()
            } else {
                viewModel.beginShortcutCapture(shortcutID: id)
            }
        }) {
            if let prefix {
                HStack(spacing: 2) {
                    Text(prefix).font(.system(size: 9, weight: .bold))
                    Text(isCapturing ? "Press Shortcut" : viewModel.shortcutLabel(for: id))
                        .font(.system(size: 9, weight: .medium))
                }
            } else {
                Text(isCapturing ? "Press Shortcut" : viewModel.shortcutLabel(for: id))
                    .font(.system(size: 9, weight: .medium))
            }
        }
        .buttonStyle(SlotShortcutButtonStyle(isCapturing: isCapturing))
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
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .foregroundStyle(.white)
            .background(
                Circle()
                    .fill(SamplerTheme.accent.opacity(configuration.isPressed ? 0.75 : 1.0))
            )
    }
}

struct SlotActionButtonStyle: ButtonStyle {
    var isDestructive = false
    var isAccent = false

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 10, weight: .semibold))
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .foregroundStyle(isDestructive ? SamplerTheme.captureText : SamplerTheme.text)
            .background(
                RoundedRectangle(cornerRadius: 6, style: .continuous)
                    .fill(
                        isAccent
                            ? SamplerTheme.accentSoft
                            : Color.white.opacity(configuration.isPressed ? 0.65 : 0.9)
                    )
            )
            .overlay(
                RoundedRectangle(cornerRadius: 6, style: .continuous)
                    .stroke(SamplerTheme.border, lineWidth: 1)
            )
    }
}

struct SlotShortcutButtonStyle: ButtonStyle {
    var isCapturing: Bool

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .padding(.horizontal, 6)
            .padding(.vertical, 4)
            .foregroundStyle(isCapturing ? SamplerTheme.captureText : SamplerTheme.text)
            .background(
                RoundedRectangle(cornerRadius: 6, style: .continuous)
                    .fill(isCapturing ? SamplerTheme.captureTop.opacity(0.35) : Color.white.opacity(0.85))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 6, style: .continuous)
                    .stroke(SamplerTheme.border, lineWidth: 1)
            )
    }
}
