import AppKit
import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @ObservedObject var viewModel: SamplerViewModel
    @Environment(\.samplerThemeColors) private var theme
    @State private var isStoredTabDropTarget = false

    var body: some View {
        VStack(spacing: SamplerTheme.Layout.sectionSpacing) {
            outputBar
            toolbar
            tabBar
            captureList
            footer
            statusBar
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .samplerWindowChrome()
        .background(WindowFrameTracker(viewModel: viewModel))
        .background {
            if #available(macOS 14.0, *) {
                PreferencesOpenSettingsBridge()
            }
        }
    }

    private var outputBar: some View {
        HStack(spacing: SamplerTheme.Layout.rowColumnGap) {
            SamplerSectionLabel(title: "Output")
                .frame(width: SamplerTheme.Layout.labelWidth, alignment: .leading)

            Picker("Sampler audio output", selection: Binding(
                get: { viewModel.outputDeviceUID },
                set: { viewModel.setOutputDeviceUID($0) }
            )) {
                Text("System Default").tag("")
                ForEach(viewModel.outputDevices) { device in
                    Text(device.name).tag(device.uid)
                }
            }
            .labelsHidden()
            .tint(theme.accent)
            .frame(maxWidth: .infinity)
        }
        .padding(.horizontal, SamplerTheme.Layout.chipPaddingH)
        .padding(.vertical, SamplerTheme.Layout.chipPaddingV)
        .frame(minHeight: SamplerTheme.Layout.chipMinHeight)
        .samplerChipSurface()
    }

    private var toolbar: some View {
        HStack(spacing: SamplerTheme.Layout.toolbarGap) {
            HStack(spacing: SamplerTheme.Layout.toolbarGap) {
                Button {
                    viewModel.playToggleTapped()
                } label: {
                    Image(systemName: viewModel.audioPlayback.isPlaying ? "stop.fill" : "play.fill")
                        .font(.system(size: 13, weight: .bold))
                        .frame(maxWidth: .infinity)
                        .frame(minHeight: SamplerTheme.Layout.toolbarButtonHeight)
                }
                .buttonStyle(SamplerToolbarButtonStyle())
                .disabled(!viewModel.audioPlayback.isPlaying && viewModel.selectedCapture() == nil)
                .help(viewModel.audioPlayback.isPlaying ? "Stop playback" : "Play selected sample")

                Button("Capture") {
                    viewModel.captureButtonTapped()
                }
                .buttonStyle(SamplerCaptureButtonStyle())
                .disabled(viewModel.isCaptureInProgress)
                .help("Capture the selected Pro Tools clip or edit range into the Active sampler")
            }
            .frame(maxWidth: .infinity)

            PreferencesGearButton()
        }
    }

    private var tabBar: some View {
        HStack(spacing: SamplerTheme.Layout.tabBarPadding) {
            tabButton(title: "Active", tab: "recent")
            tabButton(title: "Stored", tab: "stored")
            tabButton(title: "Sound Board", tab: "soundboard")
        }
        .padding(SamplerTheme.Layout.tabBarPadding)
        .background(
            Capsule(style: .continuous)
                .fill(theme.tabBarBackground)
        )
        .overlay(
            Capsule(style: .continuous)
                .stroke(theme.tabBarBorder, lineWidth: 1)
        )
        .frame(maxWidth: .infinity, alignment: .center)
    }

    private func tabButton(title: String, tab: String) -> some View {
        SamplerTabDropButton(
            title: title,
            tab: tab,
            viewModel: viewModel,
            isActive: viewModel.activeTab == tab,
            onDropActiveSlot: handleTabDrop(slotIndex:tab:)
        )
    }

    @ViewBuilder
    private var captureList: some View {
        if viewModel.activeTab == "stored" {
            storedCaptureList
        } else if viewModel.activeTab == "soundboard" {
            soundboardCaptureList
        } else {
            activeCaptureList
        }
    }

    private var storedCaptureList: some View {
        ScrollView(.vertical, showsIndicators: true) {
            VStack(spacing: SamplerTheme.Layout.rowSpacing) {
                StoredCategoryBar(viewModel: viewModel)

                if viewModel.storedCaptures.isEmpty {
                    SlotRowView(viewModel: viewModel, index: 0, capture: nil, isStored: true)
                } else if viewModel.filteredStoredCaptures.isEmpty {
                    StoredEmptyFilterRow()
                } else {
                    ForEach(Array(viewModel.filteredStoredCaptures.enumerated()), id: \.element.id) { index, capture in
                        SlotRowView(
                            viewModel: viewModel,
                            index: index,
                            capture: capture,
                            isStored: true,
                            showsCategoryPicker: true
                        )
                    }
                }
            }
            .frame(maxWidth: .infinity, alignment: .topLeading)
            .padding(1)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .background(storedTabDropBackground)
        .onDrop(of: [.simplestSamplerActiveSlotDrag, .simplestSamplerActiveSlotIndex, .plainText], isTargeted: $isStoredTabDropTarget) { providers in
            handleStoredTabDrop(providers)
        }
    }

    private var soundboardCaptureList: some View {
        ScrollView(.vertical, showsIndicators: true) {
            SoundBoardView(viewModel: viewModel)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
    }

    private var activeCaptureList: some View {
        ScrollView(.vertical, showsIndicators: true) {
            VStack(spacing: SamplerTheme.Layout.rowSpacing) {
                ForEach(0..<viewModel.activeSlotCount, id: \.self) { index in
                    SlotRowView(
                        viewModel: viewModel,
                        index: index,
                        capture: viewModel.recentCaptures[index],
                        isStored: false
                    )
                }

                if viewModel.showsSlotCountControls {
                    HStack(spacing: 8) {
                        if viewModel.canRemoveActiveSlot {
                            Button(action: { viewModel.removeActiveSlot() }) {
                                slotCountControlLabel(systemImage: "minus", title: "Remove Slot")
                            }
                            .buttonStyle(AddSlotButtonStyle())
                        }

                        if viewModel.canAddActiveSlot {
                            Button(action: { viewModel.addActiveSlot() }) {
                                slotCountControlLabel(systemImage: "plus", title: "Add Slot")
                            }
                            .buttonStyle(AddSlotButtonStyle())
                        }
                    }
                }
            }
            .frame(maxWidth: .infinity, alignment: .topLeading)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
    }

    @ViewBuilder
    private var storedTabDropBackground: some View {
        if isStoredTabDropTarget {
            RoundedRectangle(cornerRadius: SamplerTheme.Layout.rowCornerRadius, style: .continuous)
                .stroke(theme.accent.opacity(0.48), style: StrokeStyle(lineWidth: 2, dash: [6, 4]))
                .background(
                    RoundedRectangle(cornerRadius: SamplerTheme.Layout.rowCornerRadius, style: .continuous)
                        .fill(theme.accentSoft.opacity(0.08))
                )
        }
    }

    private var footer: some View {
        VolumeControlView(viewModel: viewModel)
    }

    private var statusBar: some View {
        Text(viewModel.statusMessage)
            .font(.system(size: 11))
            .foregroundStyle(viewModel.statusIsError ? theme.error : theme.muted)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.top, SamplerTheme.Layout.statusBarTopPadding)
    }

    private func slotCountControlLabel(systemImage: String, title: String) -> some View {
        HStack(spacing: 6) {
            Image(systemName: systemImage)
                .font(.system(size: 11, weight: .semibold))
            Text(title)
                .font(.system(size: 12, weight: .semibold))
        }
        .frame(maxWidth: .infinity)
        .frame(minHeight: SamplerConstants.addSlotControlHeight)
        .foregroundStyle(theme.muted)
    }

    private func handleStoredTabDrop(_ providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first(where: \.supportsActiveSlotDragPayload) else {
            return false
        }
        provider.loadActiveSlotDragPayload { payload in
            guard let payload else { return }
            Task { @MainActor in
                viewModel.storeActiveCapture(from: payload.slotIndex)
            }
        }
        return true
    }

    private func handleTabDrop(slotIndex: Int, tab: String) {
        switch tab {
        case "stored":
            viewModel.setActiveTab("stored")
            viewModel.storeActiveCapture(from: slotIndex)
        case "soundboard":
            viewModel.setActiveTab("soundboard")
            viewModel.assignActiveCaptureToSoundboard(from: slotIndex)
        default:
            viewModel.setActiveTab("recent")
        }
    }

}

private struct SamplerTabDropButton: View {
    let title: String
    let tab: String
    @ObservedObject var viewModel: SamplerViewModel
    let isActive: Bool
    let onDropActiveSlot: (Int, String) -> Void

    @State private var isDropTargeted = false

    var body: some View {
        Button(title) {
            viewModel.setActiveTab(tab)
        }
        .buttonStyle(SamplerTabButtonStyle(isActive: isActive, isDropTarget: isDropTargeted))
        .onDrop(
            of: [.simplestSamplerActiveSlotDrag, .simplestSamplerActiveSlotIndex, .plainText],
            isTargeted: Binding(
                get: { isDropTargeted },
                set: { targeted in
                    isDropTargeted = targeted
                    if targeted, viewModel.activeTab != tab {
                        viewModel.setActiveTab(tab)
                    }
                }
            )
        ) { providers in
            handleDrop(providers)
        }
    }

    private func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first(where: \.supportsActiveSlotDragPayload) else {
            return false
        }
        provider.loadActiveSlotDragPayload { payload in
            guard let payload else { return }
            Task { @MainActor in
                onDropActiveSlot(payload.slotIndex, tab)
            }
        }
        return true
    }
}

@available(macOS 14.0, *)
private struct PreferencesOpenSettingsBridge: View {
    @Environment(\.openSettings) private var openSettings

    var body: some View {
        Color.clear
            .frame(width: 0, height: 0)
            .onAppear {
                PreferencesBridge.openHandler = { openSettings() }
            }
            .onDisappear {
                PreferencesBridge.openHandler = nil
            }
    }
}

struct PreferencesGearButton: View {
    var body: some View {
        if #available(macOS 14.0, *) {
            PreferencesGearButtonModern()
        } else {
            PreferencesGearButtonLegacy()
        }
    }
}

@available(macOS 14.0, *)
private struct PreferencesGearButtonModern: View {
    @Environment(\.openSettings) private var openSettings

    var body: some View {
        Button(action: { PreferencesOpener.toggle(openSettings: openSettings) }) {
            PreferencesGearButtonLabel()
        }
        .buttonStyle(SamplerIconButtonStyle())
        .help("Preferences (⌘,)")
    }
}

private struct PreferencesGearButtonLegacy: View {
    var body: some View {
        Button(action: PreferencesOpener.toggle) {
            PreferencesGearButtonLabel()
        }
        .buttonStyle(SamplerIconButtonStyle())
        .help("Preferences (⌘,)")
    }
}

private struct PreferencesGearButtonLabel: View {
    var body: some View {
        Image(systemName: "gearshape")
            .font(.system(size: 13, weight: .semibold))
            .frame(width: SamplerTheme.Layout.iconButtonSize, height: SamplerTheme.Layout.iconButtonSize)
    }
}

@MainActor
enum PreferencesBridge {
    static var openHandler: (() -> Void)?

    static func toggle() {
        if PreferencesOpener.closeIfOpen() { return }
        if let openHandler {
            openHandler()
        } else {
            PreferencesOpener.open()
        }
    }
}

enum PreferencesOpener {
    @MainActor
    static func toggle() {
        PreferencesBridge.toggle()
    }

    @available(macOS 14.0, *)
    @MainActor
    static func toggle(openSettings: OpenSettingsAction) {
        if closeIfOpen() { return }
        openSettings()
    }

    @MainActor
    static func open() {
        NSApp.activate(ignoringOtherApps: true)

        let selectors = ["showSettingsWindow:", "showPreferencesWindow:"]
        for name in selectors {
            let selector = Selector((name))
            if NSApp.sendAction(selector, to: nil, from: nil) { return }
            if NSApp.sendAction(selector, to: NSApp, from: nil) { return }
        }

        guard let appMenu = NSApp.mainMenu?.items.first?.submenu else { return }
        for item in appMenu.items where item.keyEquivalent == "," {
            guard let action = item.action else { return }
            NSApp.sendAction(action, to: item.target, from: item)
            return
        }
    }

    @MainActor
    static func closeIfOpen() -> Bool {
        guard let window = settingsWindow() else { return false }
        window.close()
        return true
    }

    @MainActor
    private static func settingsWindow() -> NSWindow? {
        NSApp.windows.first { window in
            guard window.isVisible else { return false }
            let title = window.title.lowercased()
            if title.contains("settings") || title.contains("preferences") {
                return true
            }
            return String(describing: type(of: window)).lowercased().contains("settings")
        }
    }
}

private extension SamplerViewModel {
    func selectedCapture() -> SamplerCapture? {
        if selectedCaptureSource == "stored" {
            return storedCaptures.first { $0.id == selectedCaptureId }
        }
        if selectedCaptureSource == "soundboard" {
            return soundboardPads.compactMap { $0 }.first { $0.id == selectedCaptureId }
        }
        return recentCaptures.compactMap { $0 }.first { $0.id == selectedCaptureId }
    }
}

struct SamplerIconButtonStyle: ButtonStyle {
    @Environment(\.samplerThemeColors) private var theme
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .foregroundStyle(theme.strongText)
            .background(
                SamplerControlSurface(
                    theme: theme,
                    cornerRadius: SamplerTheme.Layout.chipCornerRadius,
                    borderColor: theme.controlBorder
                ) {
                    RoundedRectangle(cornerRadius: SamplerTheme.Layout.chipCornerRadius, style: .continuous)
                        .fill(SamplerControlFill.control(isPressed: configuration.isPressed).gradient(theme: theme))
                }
            )
            .shadow(color: theme.panelShadow.opacity(0.35), radius: 4, y: 1)
            .samplerDisabledOpacity(!isEnabled, theme: theme)
    }
}

struct SamplerToolbarButtonStyle: ButtonStyle {
    @Environment(\.samplerThemeColors) private var theme
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .foregroundStyle(theme.selectedRowText)
            .background(
                SamplerControlSurface(
                    theme: theme,
                    cornerRadius: SamplerTheme.Layout.chipCornerRadius,
                    borderColor: toolbarBorder
                ) {
                    RoundedRectangle(cornerRadius: SamplerTheme.Layout.chipCornerRadius, style: .continuous)
                        .fill(toolbarFill(isPressed: configuration.isPressed))
                }
            )
            .shadow(color: theme.accentStrong.opacity(0.22), radius: 5, y: 2)
            .samplerDisabledOpacity(!isEnabled, theme: theme)
    }

    private var toolbarBorder: Color {
        theme.accentStrong.opacity(0.34)
    }

    private func toolbarFill(isPressed: Bool) -> LinearGradient {
        if isPressed {
            return LinearGradient(
                colors: [
                    theme.accentTop.opacity(0.86),
                    theme.accentStrong.opacity(0.86)
                ],
                startPoint: .top,
                endPoint: .bottom
            )
        }
        return theme.accentGradient
    }
}

struct SamplerCaptureButtonStyle: ButtonStyle {
    @Environment(\.samplerThemeColors) private var theme
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 13, weight: .semibold))
            .frame(maxWidth: .infinity)
            .frame(minHeight: SamplerTheme.Layout.toolbarButtonHeight)
            .foregroundStyle(theme.captureText)
            .background(
                SamplerControlSurface(
                    theme: theme,
                    cornerRadius: SamplerTheme.Layout.chipCornerRadius,
                    borderColor: theme.captureBorder
                ) {
                    RoundedRectangle(cornerRadius: SamplerTheme.Layout.chipCornerRadius, style: .continuous)
                        .fill(SamplerControlFill.capture(isPressed: configuration.isPressed).gradient(theme: theme))
                }
            )
            .shadow(color: theme.captureBottom.opacity(0.18), radius: 5, y: 2)
            .samplerDisabledOpacity(!isEnabled, theme: theme)
    }
}

struct AddSlotButtonStyle: ButtonStyle {
    @Environment(\.samplerThemeColors) private var theme

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(
                SamplerControlSurface(
                    theme: theme,
                    cornerRadius: SamplerTheme.Layout.rowCornerRadius,
                    borderColor: theme.border
                ) {
                    RoundedRectangle(cornerRadius: SamplerTheme.Layout.rowCornerRadius, style: .continuous)
                        .fill(
                            configuration.isPressed
                                ? theme.controlActiveGradient
                                : LinearGradient(
                                    colors: [theme.chipBackground, theme.chipBackground.opacity(0.92)],
                                    startPoint: .top,
                                    endPoint: .bottom
                                )
                        )
                }
            )
    }
}

struct SamplerTabButtonStyle: ButtonStyle {
    var isActive: Bool
    var isDropTarget: Bool = false
    @Environment(\.samplerThemeColors) private var theme

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 12, weight: .bold))
            .kerning(0.3)
            .padding(.horizontal, SamplerTheme.Layout.tabPaddingH)
            .frame(minHeight: SamplerTheme.Layout.tabMinHeight)
            .contentShape(Rectangle())
            .foregroundStyle((isActive || isDropTarget) ? theme.selectedRowText : theme.muted)
            .background {
                if isActive {
                    Capsule(style: .continuous)
                        .fill(theme.accentGradient)
                        .overlay(alignment: .top) {
                            Capsule(style: .continuous)
                                .stroke(theme.topHighlight, lineWidth: 1)
                                .padding(1)
                        }
                        .shadow(color: theme.accent.opacity(0.18), radius: 4, y: 2)
                } else if isDropTarget {
                    Capsule(style: .continuous)
                        .fill(theme.accentSoft.opacity(0.92))
                        .overlay(
                            Capsule(style: .continuous)
                                .stroke(theme.accent.opacity(0.45), lineWidth: 1.5)
                        )
                }
            }
            .opacity(configuration.isPressed && !isActive ? 0.85 : 1)
    }
}

struct WindowFrameTracker: NSViewRepresentable {
    @ObservedObject var viewModel: SamplerViewModel

    func makeCoordinator() -> Coordinator {
        Coordinator(
            preferencesStore: viewModel.preferencesStore,
            preferredHeight: viewModel.preferredContentHeight
        )
    }

    func makeNSView(context: Context) -> NSView {
        let view = NSView()
        DispatchQueue.main.async {
            Task { @MainActor in
                context.coordinator.attach(to: view)
            }
        }
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {
        Task { @MainActor in
            context.coordinator.attach(to: nsView)
        }
    }

    final class Coordinator: NSObject {
        private weak var observedWindow: NSWindow?
        private var hasAppliedInitialFrame = false
        private let preferencesStore: PreferencesStore
        private let preferredHeight: () -> CGFloat
        private var pendingSave: DispatchWorkItem?

        init(preferencesStore: PreferencesStore, preferredHeight: @escaping () -> CGFloat) {
            self.preferencesStore = preferencesStore
            self.preferredHeight = preferredHeight
        }

        deinit {
            teardownObservers()
        }

        @MainActor
        func attach(to view: NSView) {
            guard let window = view.window else { return }
            if observedWindow === window { return }

            teardownObservers()
            observedWindow = window
            applyInitialConfiguration(to: window)
            registerObservers(for: window)
        }

        @MainActor
        private func applyInitialConfiguration(to window: NSWindow) {
            window.minSize = NSSize(
                width: SamplerConstants.minWindowWidth,
                height: SamplerConstants.minWindowHeight
            )

            guard !hasAppliedInitialFrame else { return }
            hasAppliedInitialFrame = true

            if let frame = preferencesStore.preferredWindowFrame(),
               frame.width >= SamplerConstants.minWindowWidth,
               frame.height >= SamplerConstants.minWindowHeight {
                window.setFrame(
                    clampedInitialFrame(frame, minimumHeight: preferredHeight()),
                    display: true
                )
            } else {
                window.setContentSize(NSSize(
                    width: SamplerConstants.defaultWindowWidth,
                    height: preferredHeight()
                ))
            }
        }

        @MainActor
        private func clampedInitialFrame(_ frame: NSRect, minimumHeight: CGFloat) -> NSRect {
            let requiredHeight = max(SamplerConstants.minWindowHeight, minimumHeight)
            let currentContentHeight = observedWindow?.contentRect(forFrameRect: frame).height ?? frame.height
            guard currentContentHeight < requiredHeight else { return frame }

            var adjusted = frame
            let heightDelta = requiredHeight - currentContentHeight
            adjusted.origin.y -= heightDelta
            adjusted.size.height += heightDelta
            return adjusted
        }

        private func registerObservers(for window: NSWindow) {
            NotificationCenter.default.addObserver(
                self,
                selector: #selector(windowFrameDidChange(_:)),
                name: NSWindow.didMoveNotification,
                object: window
            )
            NotificationCenter.default.addObserver(
                self,
                selector: #selector(windowFrameDidChange(_:)),
                name: NSWindow.didResizeNotification,
                object: window
            )
        }

        private func teardownObservers() {
            guard let window = observedWindow else { return }
            NotificationCenter.default.removeObserver(self, name: NSWindow.didMoveNotification, object: window)
            NotificationCenter.default.removeObserver(self, name: NSWindow.didResizeNotification, object: window)
        }

        @objc private func windowFrameDidChange(_ notification: Notification) {
            guard let window = notification.object as? NSWindow else { return }
            Task { @MainActor in
                scheduleSave(window.frame)
            }
        }

        @MainActor
        private func scheduleSave(_ frame: NSRect) {
            pendingSave?.cancel()
            let work = DispatchWorkItem { [preferencesStore] in
                Task { @MainActor in
                    preferencesStore.updateWindowFrame(frame)
                }
            }
            pendingSave = work
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3, execute: work)
        }
    }
}
