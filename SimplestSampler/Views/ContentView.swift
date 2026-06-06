import SwiftUI

struct ContentView: View {
    @ObservedObject var viewModel: SamplerViewModel

    var body: some View {
        VStack(spacing: 8) {
            outputBar
            toolbar
            tabBar
            captureList
            footer
            statusBar
        }
        .padding(8)
        .background(SamplerTheme.backgroundGradient)
        .background(WindowFrameTracker(viewModel: viewModel))
    }

    private var outputBar: some View {
        HStack {
            Text("Output")
                .font(.system(size: 11, weight: .semibold))
                .foregroundStyle(SamplerTheme.muted)

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
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
        .background(Color.white.opacity(0.55))
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .stroke(SamplerTheme.border, lineWidth: 1)
        )
    }

    private var toolbar: some View {
        HStack(spacing: 8) {
            Button(viewModel.audioPlayback.isPlaying ? "Stop" : "Play") {
                viewModel.playToggleTapped()
            }
            .buttonStyle(SamplerToolbarButtonStyle())
            .disabled(!viewModel.audioPlayback.isPlaying && viewModel.selectedCapture() == nil)

            Button("Capture") {
                viewModel.captureButtonTapped()
            }
            .buttonStyle(SamplerCaptureButtonStyle())
            .help("Pro Tools capture arrives in Phase 2")
        }
    }

    private var tabBar: some View {
        HStack(spacing: 0) {
            tabButton(title: "Active", tab: "recent")
            tabButton(title: "Stored", tab: "stored")
        }
        .padding(3)
        .background(Color.white.opacity(0.45))
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
    }

    private func tabButton(title: String, tab: String) -> some View {
        Button(title) {
            viewModel.setActiveTab(tab)
        }
        .buttonStyle(SamplerTabButtonStyle(isActive: viewModel.activeTab == tab))
        .frame(maxWidth: .infinity)
    }

    private var captureList: some View {
        ScrollView {
            VStack(spacing: 6) {
                if viewModel.activeTab == "stored" {
                    if viewModel.storedCaptures.isEmpty {
                        SlotRowView(viewModel: viewModel, index: 0, capture: nil, isStored: true)
                    } else {
                        ForEach(Array(viewModel.storedCaptures.enumerated()), id: \.element.id) { index, capture in
                            SlotRowView(viewModel: viewModel, index: index, capture: capture, isStored: true)
                        }
                    }
                } else {
                    ForEach(0..<SamplerConstants.maxActiveSlots, id: \.self) { index in
                        SlotRowView(
                            viewModel: viewModel,
                            index: index,
                            capture: viewModel.recentCaptures[index],
                            isStored: false
                        )
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
            .foregroundStyle(viewModel.statusIsError ? SamplerTheme.error : SamplerTheme.muted)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal, 4)
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

struct SamplerToolbarButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 13, weight: .semibold))
            .frame(maxWidth: .infinity)
            .padding(.vertical, 8)
            .foregroundStyle(SamplerTheme.text)
            .background(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .fill(Color.white.opacity(configuration.isPressed ? 0.7 : 0.92))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .stroke(SamplerTheme.border, lineWidth: 1)
            )
    }
}

struct SamplerCaptureButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 13, weight: .semibold))
            .frame(maxWidth: .infinity)
            .padding(.vertical, 8)
            .foregroundStyle(SamplerTheme.captureText)
            .background(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .fill(
                        LinearGradient(
                            colors: [
                                SamplerTheme.captureTop.opacity(configuration.isPressed ? 0.8 : 1),
                                SamplerTheme.captureBottom.opacity(configuration.isPressed ? 0.8 : 1)
                            ],
                            startPoint: .top,
                            endPoint: .bottom
                        )
                    )
            )
            .overlay(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .stroke(SamplerTheme.captureBottom.opacity(0.35), lineWidth: 1)
            )
    }
}

struct SamplerTabButtonStyle: ButtonStyle {
    var isActive: Bool

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 12, weight: .semibold))
            .padding(.vertical, 6)
            .foregroundStyle(isActive ? SamplerTheme.slotSelectedText : SamplerTheme.text)
            .background(
                RoundedRectangle(cornerRadius: 6, style: .continuous)
                    .fill(isActive ? SamplerTheme.slotSelected : Color.clear)
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
                height: SamplerConstants.defaultWindowHeight
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
