import AppKit
import SwiftUI
import UniformTypeIdentifiers

struct SlotRowView: View {
    @ObservedObject var viewModel: SamplerViewModel
    let index: Int
    let capture: SamplerCapture?
    let isStored: Bool
    var showsCategoryPicker: Bool = false

    @Environment(\.samplerThemeColors) private var theme
    @State private var draftName = ""
    @State private var isCommittingRename = false
    @FocusState private var nameFocused: Bool

    private var isSelected: Bool {
        capture?.id == viewModel.selectedCaptureId
            && viewModel.selectedCaptureSource == (isStored ? "stored" : "recent")
    }

    private var isPlaying: Bool {
        guard let capture else { return false }
        return viewModel.audioPlayback.isPlaying && viewModel.audioPlayback.playingCaptureId == capture.id
    }

    private var playbackProgress: CGFloat {
        guard isPlaying else { return 0 }
        return CGFloat(viewModel.audioPlayback.playbackProgress)
    }

    private var primaryText: Color {
        isSelected ? theme.selectedRowText : theme.text
    }

    private var secondaryText: Color {
        isSelected ? theme.selectedMutedText : theme.muted
    }

    private var renameFieldForeground: Color {
        nameFocused ? theme.renameFieldText : primaryText
    }

    private var namePlaceholder: String {
        isStored ? "Stored Sample" : "Empty Slot"
    }

    private var nameLayoutText: String {
        let trimmedDraft = draftName.trimmingCharacters(in: .whitespacesAndNewlines)
        if !trimmedDraft.isEmpty {
            return trimmedDraft
        }
        if let capture {
            return capture.slotLabel
        }
        return namePlaceholder
    }

    @ViewBuilder
    private var renameFieldBackground: some View {
        if nameFocused {
            RoundedRectangle(cornerRadius: SamplerTheme.Layout.renameFieldCornerRadius, style: .continuous)
                .fill(theme.renameFieldFill)
                .shadow(color: theme.renameFieldBorder.opacity(0.28), radius: 4, y: 1)
                .shadow(color: theme.shadow.opacity(0.32), radius: 2, y: 1)
        }
    }

    @ViewBuilder
    private var renameFieldBorder: some View {
        if nameFocused {
            RoundedRectangle(cornerRadius: SamplerTheme.Layout.renameFieldCornerRadius, style: .continuous)
                .strokeBorder(theme.renameFieldBorder, lineWidth: 2)
        }
    }

    private var nameBinding: Binding<String> {
        Binding(
            get: { draftName },
            set: { draftName = $0 }
        )
    }

    private var isActiveRowDragEnabled: Bool {
        !isStored && capture != nil
    }

    var body: some View {
        HStack(spacing: SamplerTheme.Layout.rowColumnGap) {
            Button(action: playAction) {
                Image(systemName: isPlaying ? "stop.fill" : "play.fill")
                    .symbolRenderingMode(.monochrome)
                    .font(.system(size: 10, weight: .black))
                    .frame(width: SamplerTheme.Layout.playButtonSize, height: SamplerTheme.Layout.playButtonSize)
            }
            .buttonStyle(SlotPlayButtonStyle(isSelected: isSelected))
            .disabled(capture == nil)

            VStack(alignment: .leading, spacing: SamplerTheme.Layout.metaLineSpacing) {
                HStack(spacing: SamplerTheme.Layout.metaNameDurationGap) {
                    Text(nameLayoutText)
                        .font(.system(size: 13, weight: .semibold))
                        .lineLimit(1)
                        .foregroundStyle(.clear)
                        .overlay(alignment: .leading) {
                            TextField(namePlaceholder, text: nameBinding)
                                .textFieldStyle(.plain)
                                .font(.system(size: 13, weight: .semibold))
                                .foregroundStyle(renameFieldForeground)
                                .lineLimit(1)
                                .background {
                                    renameFieldBackground
                                        .padding(.horizontal, -SamplerTheme.Layout.renameFieldPaddingH)
                                        .padding(.vertical, -SamplerTheme.Layout.renameFieldPaddingV)
                                }
                                .overlay {
                                    renameFieldBorder
                                        .padding(.horizontal, -SamplerTheme.Layout.renameFieldPaddingH)
                                        .padding(.vertical, -SamplerTheme.Layout.renameFieldPaddingV)
                                }
                                .animation(.easeOut(duration: 0.12), value: nameFocused)
                                .modifier(RenameFocusEffectModifier(disabled: nameFocused))
                                .disabled(capture == nil)
                                .focused($nameFocused)
                                .id(displayNameFieldIdentity)
                                .onSubmit {
                                    commitRename()
                                    nameFocused = false
                                }
                                .onChange(of: nameFocused) { focused in
                                    if focused {
                                        draftName = capture?.slotLabel ?? ""
                                        selectRenameFieldText()
                                    } else {
                                        commitRename()
                                    }
                                }
                                .onChange(of: capture?.id) { _ in syncDraftNameFromCapture() }
                                .onChange(of: capture?.displayName) { _ in syncDraftNameFromCapture() }
                                .onChange(of: capture?.hasCustomDisplayName) { _ in syncDraftNameFromCapture() }
                                .onChange(of: capture?.fileName) { _ in syncDraftNameFromCapture() }
                                .onAppear { syncDraftNameFromCapture() }
                                .modifier(StoredDoubleClickRenameModifier(enabled: isStored && capture != nil) {
                                    beginRename()
                                })
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
                        dismissRenameField()
                        viewModel.toggleStore(at: index)
                    }
                    .buttonStyle(SlotActionButtonStyle(isAccent: capture?.saved == true, isSelected: isSelected))
                    .disabled(capture == nil)

                    Button("Delete") {
                        dismissRenameField()
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
                    if showsCategoryPicker, let capture {
                        categoryMenu(for: capture)
                    }

                    Button("Finder") {
                        dismissRenameField()
                        if let id = capture?.id {
                            viewModel.revealStoredCaptureInFinder(id: id)
                        }
                    }
                    .buttonStyle(SlotActionButtonStyle(isSelected: isSelected))
                    .fixedSize(horizontal: true, vertical: false)

                    Button("Delete") {
                        dismissRenameField()
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
        .shadow(color: isSelected ? theme.accent.opacity(0.08) : .clear, radius: 6, y: 2)
        .contentShape(Rectangle())
        .onTapGesture {
            if nameFocused {
                dismissRenameField()
                return
            }
            guard let capture else { return }
            viewModel.selectCapture(id: capture.id, source: isStored ? "stored" : "recent")
        }
        .onChange(of: viewModel.selectedCaptureId) { _ in
            resignRenameIfSelectionMoved()
        }
        .onChange(of: viewModel.selectedCaptureSource) { _ in
            resignRenameIfSelectionMoved()
        }
        .onDrop(of: [.fileURL, .simplestSamplerActiveSlotDrag, .simplestSamplerActiveSlotIndex, .plainText], isTargeted: dropTargetBinding) { providers in
            guard !isStored else { return false }
            return handleRowDrop(providers)
        }
        .modifier(ActiveCaptureRowDragModifier(enabled: isActiveRowDragEnabled, dragProvider: activeSlotDragProvider))
        .modifier(StoredCaptureRowDragModifier(capture: capture, enabled: showsCategoryPicker))
    }

    private var rowBackground: some View {
        GeometryReader { geometry in
            ZStack(alignment: .leading) {
                rowBaseBackground
                if playbackProgress > 0 {
                    rowPlaybackProgressFill
                        .frame(width: geometry.size.width * playbackProgress)
                }
            }
        }
    }

    private var rowBaseBackground: Color {
        if isSelected { return theme.selectedRow }
        if isPlaying { return theme.slotPlaying }
        return theme.slotBackground
    }

    private var rowPlaybackProgressFill: Color {
        if isSelected {
            return theme.rowPlaybackOverlay
        }
        return theme.accentSoft.opacity(0.62)
    }

    private var borderColor: Color {
        if isSelected { return theme.selectedRowBorder }
        if isPlaying { return theme.borderStrong }
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

    private func categoryMenu(for capture: SamplerCapture) -> some View {
        Menu {
            Button {
                viewModel.setCaptureCategory(captureId: capture.id, categoryId: nil)
            } label: {
                categoryMenuLabel(title: "Uncategorized", isSelected: (capture.categoryId ?? "").isEmpty)
            }

            ForEach(viewModel.storedCategories) { category in
                Button {
                    viewModel.setCaptureCategory(captureId: capture.id, categoryId: category.id)
                } label: {
                    categoryMenuLabel(title: category.name, isSelected: capture.categoryId == category.id)
                }
            }
        } label: {
            Text(viewModel.categoryLabel(for: capture))
                .font(.system(size: 11, weight: .semibold))
                .lineLimit(1)
        }
        .buttonStyle(SlotActionButtonStyle(isSelected: isSelected))
        .fixedSize(horizontal: true, vertical: false)
    }

    private func categoryMenuLabel(title: String, isSelected: Bool) -> some View {
        HStack {
            Text(title)
            if isSelected {
                Image(systemName: "checkmark")
            }
        }
    }

    private func playAction() {
        dismissRenameField()
        guard let capture else { return }
        viewModel.playCapture(capture, source: isStored ? "stored" : "recent")
    }

    private var displayNameFieldIdentity: String {
        let id = capture?.id ?? "empty-\(index)"
        let fileName = capture?.fileName ?? ""
        return "\(id)|\(fileName)"
    }

    private func syncDraftNameFromCapture() {
        guard !nameFocused else { return }
        draftName = capture?.slotLabel ?? ""
    }

    private func dismissRenameField() {
        guard nameFocused else { return }
        nameFocused = false
    }

    private func resignRenameIfSelectionMoved() {
        guard nameFocused, let capture else { return }
        let source = isStored ? "stored" : "recent"
        guard viewModel.selectedCaptureId != capture.id || viewModel.selectedCaptureSource != source else { return }
        dismissRenameField()
    }

    private func beginRename() {
        guard capture != nil else { return }
        draftName = capture?.slotLabel ?? ""
        nameFocused = true
        selectRenameFieldText()
    }

    private func selectRenameFieldText() {
        DispatchQueue.main.async {
            NSApp.sendAction(#selector(NSText.selectAll(_:)), to: nil, from: nil)
        }
    }

    private func commitRename() {
        guard !isCommittingRename else { return }
        guard let capture else {
            draftName = ""
            return
        }

        let submitted = draftName.trimmingCharacters(in: .whitespacesAndNewlines)
        let hasCustom = !submitted.isEmpty
        let nextName = hasCustom ? submitted : capture.defaultDisplayName
        let currentLabel = capture.slotLabel

        if hasCustom {
            guard submitted != currentLabel || !capture.hasCustomDisplayName else { return }
        } else {
            guard capture.hasCustomDisplayName else { return }
        }

        isCommittingRename = true
        viewModel.renameCapture(id: capture.id, displayName: nextName, hasCustomDisplayName: hasCustom)
        draftName = hasCustom ? submitted : fallbackLabel(for: capture)
        isCommittingRename = false
    }

    private func fallbackLabel(for capture: SamplerCapture) -> String {
        let fallback = capture.defaultDisplayName.trimmingCharacters(in: .whitespacesAndNewlines)
        return fallback.isEmpty ? capture.slotLabel : fallback
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

    private func handleRowDrop(_ providers: [NSItemProvider]) -> Bool {
        if providers.contains(where: { $0.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier) }) {
            return handleDrop(providers)
        }
        return handleReorderDrop(providers)
    }

    private func activeSlotDragProvider() -> NSItemProvider {
        let provider = NSItemProvider(object: "\(index)" as NSString)
        let payload = viewModel.activeSlotDragPayload(for: index)
        provider.registerDataRepresentation(
            forTypeIdentifier: UTType.simplestSamplerActiveSlotDrag.identifier,
            visibility: .all
        ) { completion in
            completion(payload?.encodedData, nil)
            return nil
        }
        provider.registerDataRepresentation(
            forTypeIdentifier: UTType.simplestSamplerActiveSlotIndex.identifier,
            visibility: .all
        ) { completion in
            completion(payload?.encodedData ?? "\(self.index)".data(using: .utf8), nil)
            return nil
        }
        return provider
    }

    private func handleReorderDrop(_ providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first(where: \.supportsActiveSlotDragPayload) else {
            return false
        }
        provider.loadActiveSlotDragPayload { payload in
            guard let payload, payload.slotIndex != index else { return }
            Task { @MainActor in
                viewModel.dropTargetIndex = -1
                viewModel.moveActiveSlot(from: payload.slotIndex, to: index)
            }
        }
        return true
    }
}

private struct RenameFocusEffectModifier: ViewModifier {
    var disabled: Bool

    func body(content: Content) -> some View {
        if #available(macOS 14.0, *) {
            content.focusEffectDisabled(disabled)
        } else {
            content
        }
    }
}

private struct HoverCursorModifier: ViewModifier {
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

private struct ActiveCaptureRowDragModifier: ViewModifier {
    let enabled: Bool
    let dragProvider: () -> NSItemProvider

    func body(content: Content) -> some View {
        if enabled {
            content
                .onDrag { dragProvider() }
                .modifier(HoverCursorModifier(cursor: .openHand))
                .help("Drag sample to reorder or move it to another tab")
        } else {
            content
        }
    }
}

private struct StoredCaptureRowDragModifier: ViewModifier {
    let capture: SamplerCapture?
    let enabled: Bool

    func body(content: Content) -> some View {
        if enabled, let capture {
            content.onDrag {
                let provider = NSItemProvider(object: capture.id as NSString)
                provider.registerDataRepresentation(
                    forTypeIdentifier: UTType.simplestSamplerStoredCapture.identifier,
                    visibility: .all
                ) { completion in
                    completion(capture.id.data(using: .utf8), nil)
                    return nil
                }
                return provider
            }
        } else {
            content
        }
    }
}

private struct StoredDoubleClickRenameModifier: ViewModifier {
    var enabled: Bool
    var onRename: () -> Void

    func body(content: Content) -> some View {
        if enabled {
            content.highPriorityGesture(
                TapGesture(count: 2).onEnded { onRename() }
            )
        } else {
            content
        }
    }
}

struct SlotPlayButtonStyle: ButtonStyle {
    var isSelected = false
    @Environment(\.samplerThemeColors) private var theme
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .foregroundStyle(playForeground)
            .background {
                Circle()
                    .fill(playFill(isPressed: configuration.isPressed))
            }
            .overlay(
                Circle()
                    .stroke(playBorder, lineWidth: 1)
            )
            .shadow(color: playShadow, radius: 2, y: 1)
            .samplerDisabledOpacity(!isEnabled, theme: theme)
    }

    private var playForeground: Color {
        theme.selectedRowText
    }

    private var playBorder: Color {
        if isSelected {
            return theme.selectedChromeBorder
        }
        return theme.accentStrong.opacity(0.34)
    }

    private var playShadow: Color {
        isSelected ? .clear : theme.accentStrong.opacity(0.18)
    }

    private func playFill(isPressed: Bool) -> AnyShapeStyle {
        if isSelected {
            return AnyShapeStyle(theme.selectedChromeFill.opacity(isPressed ? 0.86 : 1))
        }
        let gradient = isPressed
            ? LinearGradient(
                colors: [
                    theme.accentTop.opacity(0.86),
                    theme.accentStrong.opacity(0.86)
                ],
                startPoint: .top,
                endPoint: .bottom
            )
            : theme.accentGradient
        return AnyShapeStyle(gradient)
    }
}

struct SlotActionButtonStyle: ButtonStyle {
    var isDestructive = false
    var isAccent = false
    var isSelected = false
    @Environment(\.samplerThemeColors) private var theme
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 10, weight: .bold))
            .kerning(0.2)
            .padding(.horizontal, SamplerTheme.Layout.actionPaddingH)
            .frame(minHeight: SamplerTheme.Layout.actionMinHeight)
            .foregroundStyle(actionForeground)
            .background(
                Capsule(style: .continuous)
                    .fill(actionBackground(configuration: configuration))
            )
            .overlay(
                Capsule(style: .continuous)
                    .stroke(actionBorder, lineWidth: 1)
            )
            .samplerDisabledOpacity(!isEnabled, theme: theme)
    }

    private var actionForeground: Color {
        if isDestructive {
            return isSelected ? theme.selectedRowText : theme.captureText
        }
        if isAccent {
            return theme.strongText
        }
        return isSelected ? theme.selectedRowText : theme.muted
    }

    private var actionBorder: Color {
        if isSelected { return theme.selectedChromeBorder }
        if isDestructive { return theme.captureBorder.opacity(0.75) }
        return theme.controlBorder
    }

    private func actionBackground(configuration: Configuration) -> some ShapeStyle {
        if isSelected {
            return AnyShapeStyle(theme.selectedChromeFill.opacity(configuration.isPressed ? 0.86 : 1))
        }
        if isAccent {
            return AnyShapeStyle(theme.accentSoft.opacity(configuration.isPressed ? 0.88 : 1))
        }
        if isDestructive {
            return AnyShapeStyle(
                LinearGradient(
                    colors: [
                        theme.captureTop.opacity(configuration.isPressed ? 0.78 : 0.92),
                        theme.captureBottom.opacity(configuration.isPressed ? 0.78 : 0.92)
                    ],
                    startPoint: .top,
                    endPoint: .bottom
                )
            )
        }
        return AnyShapeStyle(
            SamplerControlFill.control(isPressed: configuration.isPressed).gradient(theme: theme)
        )
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
            .foregroundStyle(isCapturing ? theme.accent : theme.strongText)
            .background(
                Capsule(style: .continuous)
                    .fill(shortcutBackground(configuration: configuration))
            )
            .overlay(
                Capsule(style: .continuous)
                    .stroke(isCapturing ? theme.accent.opacity(0.58) : theme.controlBorder, lineWidth: 1)
            )
            .shadow(color: isCapturing ? theme.focusRing : .clear, radius: 0, y: 0)
            .overlay {
                if isCapturing {
                    Capsule(style: .continuous)
                        .stroke(theme.focusRing, lineWidth: 3)
                }
            }
    }

    private func shortcutBackground(configuration: Configuration) -> some ShapeStyle {
        if isCapturing {
            return AnyShapeStyle(theme.shortcutCapturingFill)
        }
        if isTrigger {
            return AnyShapeStyle(theme.shortcutTriggerFill)
        }
        return AnyShapeStyle(
            SamplerControlFill.control(isPressed: configuration.isPressed).gradient(theme: theme)
        )
    }
}
