import AppKit
import SwiftUI

struct ContentView: View {
    @ObservedObject var viewModel: SamplerViewModel
    @Environment(\.samplerThemeColors) private var theme

    var body: some View {
        VStack(spacing: SamplerTheme.Layout.sectionSpacing) {
            outputBar
            toolbar
            tabBar
            captureList
            footer
            statusBar
        }
        .padding(SamplerTheme.Layout.windowPadding)
        .background(theme.backgroundGradient)
        .background(WindowFrameTracker(viewModel: viewModel))
        .background {
            if #available(macOS 14.0, *) {
                PreferencesOpenSettingsBridge()
            }
        }
    }

    private var outputBar: some View {
        HStack(spacing: SamplerTheme.Layout.rowColumnGap) {
            Text("Output")
                .font(.system(size: 11, weight: .semibold))
                .foregroundStyle(theme.muted)
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
                Button(viewModel.audioPlayback.isPlaying ? "Stop" : "Play") {
                    viewModel.playToggleTapped()
                }
                .buttonStyle(SamplerToolbarButtonStyle())
                .disabled(!viewModel.audioPlayback.isPlaying && viewModel.selectedCapture() == nil)

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
        }
        .padding(SamplerTheme.Layout.tabBarPadding)
        .background(theme.tabBarBackground)
        .clipShape(Capsule(style: .continuous))
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private func tabButton(title: String, tab: String) -> some View {
        Button(title) {
            viewModel.setActiveTab(tab)
        }
        .buttonStyle(SamplerTabButtonStyle(isActive: viewModel.activeTab == tab))
    }

    private var captureList: some View {
        ScrollView {
            VStack(spacing: SamplerTheme.Layout.rowSpacing) {
                if viewModel.activeTab == "stored" {
                    if viewModel.storedCaptures.isEmpty {
                        SlotRowView(viewModel: viewModel, index: 0, capture: nil, isStored: true)
                    } else {
                        ForEach(Array(viewModel.storedCaptures.enumerated()), id: \.element.id) { index, capture in
                            SlotRowView(viewModel: viewModel, index: index, capture: capture, isStored: true)
                        }
                    }
                } else {
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
            }
        }
        .frame(maxHeight: .infinity)
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
        return recentCaptures.compactMap { $0 }.first { $0.id == selectedCaptureId }
    }
}

struct SamplerIconButtonStyle: ButtonStyle {
    @Environment(\.samplerThemeColors) private var theme

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .foregroundStyle(theme.text)
            .background(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .fill(configuration.isPressed ? theme.buttonFillPressed : theme.buttonFill)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .stroke(theme.border, lineWidth: 1)
            )
    }
}

struct SamplerToolbarButtonStyle: ButtonStyle {
    @Environment(\.samplerThemeColors) private var theme

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 13, weight: .semibold))
            .frame(maxWidth: .infinity)
            .frame(minHeight: SamplerTheme.Layout.toolbarButtonHeight)
            .foregroundStyle(theme.text)
            .background(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .fill(configuration.isPressed ? theme.buttonFillPressed : theme.buttonFill)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .stroke(theme.border, lineWidth: 1)
            )
    }
}

struct SamplerCaptureButtonStyle: ButtonStyle {
    @Environment(\.samplerThemeColors) private var theme

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 13, weight: .semibold))
            .frame(maxWidth: .infinity)
            .frame(minHeight: SamplerTheme.Layout.toolbarButtonHeight)
            .foregroundStyle(theme.captureText)
            .background(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .fill(
                        LinearGradient(
                            colors: [
                                theme.captureTop.opacity(configuration.isPressed ? 0.8 : 1),
                                theme.captureBottom.opacity(configuration.isPressed ? 0.8 : 1)
                            ],
                            startPoint: .top,
                            endPoint: .bottom
                        )
                    )
            )
            .overlay(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .stroke(theme.captureBottom.opacity(0.35), lineWidth: 1)
            )
    }
}

struct AddSlotButtonStyle: ButtonStyle {
    @Environment(\.samplerThemeColors) private var theme

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(
                RoundedRectangle(cornerRadius: SamplerTheme.Layout.rowCornerRadius, style: .continuous)
                    .fill(configuration.isPressed ? theme.buttonFillPressed : theme.chipBackground)
            )
            .overlay(
                RoundedRectangle(cornerRadius: SamplerTheme.Layout.rowCornerRadius, style: .continuous)
                    .stroke(theme.border, lineWidth: 1)
            )
    }
}

struct SamplerTabButtonStyle: ButtonStyle {
    var isActive: Bool
    @Environment(\.samplerThemeColors) private var theme

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 12, weight: .semibold))
            .padding(.horizontal, SamplerTheme.Layout.tabPaddingH)
            .frame(minHeight: SamplerTheme.Layout.tabMinHeight)
            .foregroundStyle(isActive ? theme.slotSelectedText : theme.muted)
            .background(
                Capsule(style: .continuous)
                    .fill(isActive ? theme.accent : Color.clear)
            )
    }
}

struct WindowFrameTracker: NSViewRepresentable {
    @ObservedObject var viewModel: SamplerViewModel

    func makeNSView(context: Context) -> NSView {
        let view = NSView()
        DispatchQueue.main.async {
            if let window = view.window {
                configureWindow(window)
            }
        }
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {
        if let window = nsView.window {
            configureWindow(window)
        }
    }

    private func configureWindow(_ window: NSWindow) {
        window.minSize = NSSize(
            width: SamplerConstants.minWindowWidth,
            height: SamplerConstants.minWindowHeight
        )
        if let frame = viewModel.preferencesStore.preferredWindowFrame(), frame.width >= SamplerConstants.minWindowWidth {
            window.setFrame(frame, display: true)
        } else {
            window.setContentSize(NSSize(
                width: SamplerConstants.defaultWindowWidth,
                height: viewModel.preferredContentHeight()
            ))
        }

        let preferencesStore = viewModel.preferencesStore
        NotificationCenter.default.removeObserver(window, name: NSWindow.didMoveNotification, object: window)
        NotificationCenter.default.removeObserver(window, name: NSWindow.didResizeNotification, object: window)
        NotificationCenter.default.addObserver(forName: NSWindow.didMoveNotification, object: window, queue: .main) { notification in
            guard let trackedWindow = notification.object as? NSWindow else { return }
            Task { @MainActor in
                preferencesStore.updateWindowFrame(trackedWindow.frame)
            }
        }
        NotificationCenter.default.addObserver(forName: NSWindow.didResizeNotification, object: window, queue: .main) { notification in
            guard let trackedWindow = notification.object as? NSWindow else { return }
            Task { @MainActor in
                preferencesStore.updateWindowFrame(trackedWindow.frame)
            }
        }
    }
}
