import AppKit
import SwiftUI
import UniformTypeIdentifiers

struct SoundBoardView: View {
    @ObservedObject var viewModel: SamplerViewModel
    @Environment(\.samplerThemeColors) private var theme

    private let columns = Array(
        repeating: GridItem(.flexible(), spacing: SamplerConstants.soundboardGap),
        count: SamplerConstants.soundboardColumns
    )

    var body: some View {
        LazyVGrid(columns: columns, spacing: SamplerConstants.soundboardGap) {
            ForEach(0..<SamplerConstants.soundboardPadCount, id: \.self) { index in
                SoundBoardPadView(
                    viewModel: viewModel,
                    index: index,
                    capture: viewModel.soundboardPads[index]
                )
            }
        }
        .frame(maxWidth: .infinity)
    }
}

private struct SoundBoardPadView: View {
    @ObservedObject var viewModel: SamplerViewModel
    let index: Int
    let capture: SamplerCapture?
    @Environment(\.samplerThemeColors) private var theme

    private var isSelected: Bool {
        if viewModel.selectedSoundboardPadIndex == index {
            return true
        }
        guard let capture else { return false }
        return viewModel.selectedCaptureId == capture.id && viewModel.selectedCaptureSource == "soundboard"
    }

    private var isPlaying: Bool {
        guard let capture else { return false }
        return viewModel.audioPlayback.isPlaying && viewModel.audioPlayback.playingCaptureId == capture.id
    }

    private var isDropTarget: Bool {
        viewModel.dropTargetIndex == index
    }

    var body: some View {
        Button(action: padTapped) {
            VStack(alignment: .leading, spacing: 4) {
                Text(capture?.slotLabel ?? "Pad \(index + 1)")
                    .font(.system(size: 11, weight: .bold))
                    .foregroundStyle(labelColor)
                    .lineLimit(2)
                    .multilineTextAlignment(.leading)
                    .frame(maxWidth: .infinity, alignment: .leading)

                if let duration = capture?.formattedDuration {
                    Text(duration)
                        .font(.system(size: 10, weight: .semibold, design: .monospaced))
                        .foregroundStyle(secondaryLabelColor)
                        .lineLimit(1)
                } else {
                    Spacer(minLength: 0)
                }
            }
            .padding(8)
            .frame(maxWidth: .infinity, minHeight: SamplerConstants.soundboardPadSize, maxHeight: SamplerConstants.soundboardPadSize, alignment: .topLeading)
            .background(padBackground)
            .clipShape(RoundedRectangle(cornerRadius: SamplerTheme.Layout.rowCornerRadius, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: SamplerTheme.Layout.rowCornerRadius, style: .continuous)
                    .stroke(borderColor, lineWidth: isSelected ? 1.5 : 1)
            )
            .shadow(color: isSelected ? theme.accent.opacity(0.08) : .clear, radius: 6, y: 2)
        }
        .buttonStyle(.plain)
        .contentShape(RoundedRectangle(cornerRadius: SamplerTheme.Layout.rowCornerRadius, style: .continuous))
        .onDrop(
            of: [
                .fileURL,
                .simplestSamplerSoundboardPadDrag,
                .simplestSamplerSoundboardPadIndex,
                .simplestSamplerActiveSlotDrag,
                .simplestSamplerActiveSlotIndex,
                .plainText
            ],
            isTargeted: dropTargetBinding
        ) { providers in
            handleDrop(providers)
        }
        .modifier(SoundBoardPadDragModifier(index: index, capture: capture, viewModel: viewModel))
        .contextMenu {
            if capture != nil {
                Button("Clear Pad") {
                    viewModel.clearSoundboardPad(at: index)
                }
            }
        }
    }

    private var labelColor: Color {
        if isSelected { return theme.selectedRowText }
        if capture == nil { return theme.muted }
        return theme.textStrong
    }

    private var secondaryLabelColor: Color {
        isSelected ? theme.selectedMutedText : theme.muted
    }

    private var padBackground: Color {
        if isDropTarget {
            return theme.accentSoft.opacity(0.55)
        }
        if isSelected {
            return theme.selectedRow
        }
        if isPlaying {
            return theme.slotPlaying
        }
        return theme.slotBackground
    }

    private var borderColor: Color {
        if isDropTarget {
            return theme.accent.opacity(0.55)
        }
        if isSelected {
            return theme.selectedRowBorder
        }
        if isPlaying {
            return theme.borderStrong
        }
        return theme.border
    }

    private var dropTargetBinding: Binding<Bool> {
        Binding(
            get: { viewModel.dropTargetIndex == index },
            set: { targeted in
                viewModel.dropTargetIndex = targeted ? index : (viewModel.dropTargetIndex == index ? -1 : viewModel.dropTargetIndex)
            }
        )
    }

    private func padTapped() {
        if let capture {
            viewModel.playSoundboardPad(at: index)
            return
        }
        viewModel.selectSoundboardPad(at: index)
        viewModel.dropTargetIndex = -1
    }

    private func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        if let provider = providers.first(where: { $0.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier) }) {
            provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, _ in
                guard let data = item as? Data, let url = URL(dataRepresentation: data, relativeTo: nil) else { return }
                Task { @MainActor in
                    viewModel.loadDroppedFileOnSoundboard(path: url.path, padIndex: index)
                    viewModel.dropTargetIndex = -1
                }
            }
            return true
        }

        if let provider = providers.first(where: \.supportsSoundboardPadDragPayload) {
            provider.loadSoundboardPadDragPayload { payload in
                guard let payload, payload.padIndex != index else { return }
                Task { @MainActor in
                    viewModel.moveSoundboardPad(from: payload.padIndex, to: index)
                    viewModel.dropTargetIndex = -1
                }
            }
            return true
        }

        guard let provider = providers.first(where: \.supportsActiveSlotDragPayload) else {
            return false
        }
        provider.loadActiveSlotDragPayload { payload in
            guard let payload else { return }
            Task { @MainActor in
                viewModel.assignActiveCaptureToSoundboard(from: payload.slotIndex, padIndex: index)
                viewModel.dropTargetIndex = -1
            }
        }
        return true
    }
}

private struct SoundBoardPadDragModifier: ViewModifier {
    let index: Int
    let capture: SamplerCapture?
    @ObservedObject var viewModel: SamplerViewModel

    func body(content: Content) -> some View {
        if capture != nil {
            content
                .onDrag { soundboardPadDragProvider() }
                .modifier(SoundBoardHoverCursorModifier(cursor: .openHand))
                .help("Drag to move or swap pads")
        } else {
            content
        }
    }

    private func soundboardPadDragProvider() -> NSItemProvider {
        let provider = NSItemProvider()
        let payload = viewModel.soundboardPadDragPayload(for: index)
        provider.registerDataRepresentation(
            forTypeIdentifier: UTType.simplestSamplerSoundboardPadDrag.identifier,
            visibility: .all
        ) { completion in
            completion(payload?.encodedData, nil)
            return nil
        }
        provider.registerDataRepresentation(
            forTypeIdentifier: UTType.simplestSamplerSoundboardPadIndex.identifier,
            visibility: .all
        ) { completion in
            completion(payload?.encodedData ?? "\(self.index)".data(using: .utf8), nil)
            return nil
        }
        return provider
    }
}

private struct SoundBoardHoverCursorModifier: ViewModifier {
    let cursor: NSCursor

    @State private var isHovering = false

    func body(content: Content) -> some View {
        content.onHover { hovering in
            guard hovering != isHovering else { return }
            isHovering = hovering
            if hovering {
                cursor.push()
            } else {
                NSCursor.pop()
            }
        }
    }
}
