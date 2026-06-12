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
        isSelected ? theme.slotSelectedText : theme.text
    }

    private var secondaryText: Color {
        isSelected ? theme.slotSelectedText.opacity(0.72) : theme.muted
    }

    private var renameFieldForeground: Color {
        nameFocused ? theme.renameFieldText : primaryText
    }

    @ViewBuilder
    private var renameFieldBackground: some View {
        if nameFocused {
            RoundedRectangle(cornerRadius: SamplerTheme.Layout.renameFieldCornerRadius, style: .continuous)
                .fill(theme.renameFieldFill)
                .shadow(color: theme.renameFieldBorder.opacity(0.28), radius: 4, y: 1)
                .shadow(color: Color.black.opacity(0.14), radius: 2, y: 1)
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

    var body: some View {
        HStack(spacing: SamplerTheme.Layout.rowColumnGap) {
            if !isStored {
                Image(systemName: "line.3.horizontal")
                    .font(.system(size: 9, weight: .bold))
                    .foregroundStyle(secondaryText.opacity(0.85))
                    .frame(width: 10)
                    .contentShape(Rectangle())
                    .onDrag { activeSlotDragProvider() }
                    .help("Drag to reorder")
            }

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
                        text: nameBinding
                    )
                    .textFieldStyle(.plain)
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(renameFieldForeground)
                    .lineLimit(1)
                    .padding(.horizontal, nameFocused ? SamplerTheme.Layout.renameFieldPaddingH : 0)
                    .padding(.vertical, nameFocused ? SamplerTheme.Layout.renameFieldPaddingV : 0)
                    .background(renameFieldBackground)
                    .overlay(renameFieldBorder)
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
        .onDrop(of: [.fileURL], isTargeted: dropTargetBinding) { providers in
            guard !isStored else { return false }
            return handleDrop(providers)
        }
        .onDrop(of: [.simplestSamplerActiveSlotIndex], isTargeted: nil) { providers in
            guard !isStored else { return false }
            return handleReorderDrop(providers)
        }
        .onDrag {
            guard let capture, showsCategoryPicker else { return NSItemProvider() }
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
        isSelected ? theme.slotSelected : theme.slotBackground
    }

    private var rowPlaybackProgressFill: Color {
        if isSelected {
            return Color.white.opacity(0.16)
        }
        return theme.accentSoft.opacity(0.62)
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

    private func activeSlotDragProvider() -> NSItemProvider {
        let provider = NSItemProvider(object: "\(index)" as NSString)
        provider.registerDataRepresentation(
            forTypeIdentifier: UTType.simplestSamplerActiveSlotIndex.identifier,
            visibility: .all
        ) { completion in
            completion("\(self.index)".data(using: .utf8), nil)
            return nil
        }
        return provider
    }

    private func handleReorderDrop(_ providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first(where: {
            $0.hasItemConformingToTypeIdentifier(UTType.simplestSamplerActiveSlotIndex.identifier)
        }) else {
            return false
        }

        provider.loadItem(forTypeIdentifier: UTType.simplestSamplerActiveSlotIndex.identifier, options: nil) { item, _ in
            guard let sourceIndex = slotIndex(from: item) else { return }
            Task { @MainActor in
                viewModel.moveActiveSlot(from: sourceIndex, to: index)
            }
        }
        return true
    }

    private func slotIndex(from item: NSSecureCoding?) -> Int? {
        let raw: String?
        if let string = item as? String {
            raw = string
        } else if let data = item as? Data {
            raw = String(data: data, encoding: .utf8)
        } else {
            raw = nil
        }
        guard let raw, let value = Int(raw) else { return nil }
        return value
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
