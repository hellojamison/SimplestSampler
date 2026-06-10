import AppKit
import SwiftUI

@main
struct SimplestSamplerApp: App {
    @StateObject private var preferencesStore = PreferencesStore()
    @StateObject private var audioPlayback = AudioPlaybackService()
    @StateObject private var viewModel: SamplerViewModel

    init() {
        let preferences = PreferencesStore()
        let playback = AudioPlaybackService()
        _preferencesStore = StateObject(wrappedValue: preferences)
        _audioPlayback = StateObject(wrappedValue: playback)
        _viewModel = StateObject(wrappedValue: SamplerViewModel(preferencesStore: preferences, audioPlayback: playback))
    }

    var body: some Scene {
        WindowGroup {
            SamplerThemedRoot(viewModel: viewModel) {
                ContentView(viewModel: viewModel)
                    .frame(
                        minWidth: SamplerConstants.minWindowWidth,
                        minHeight: SamplerConstants.minWindowHeight
                    )
                    .background(KeyboardHandlerView(viewModel: viewModel))
            }
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(
            width: SamplerConstants.defaultWindowWidth,
            height: SamplerConstants.defaultWindowHeight
        )
        .commands {
            CommandGroup(replacing: .newItem) {}
            CommandGroup(replacing: .appSettings) {
                Button("Settings…") {
                    PreferencesBridge.toggle()
                }
                .keyboardShortcut(",", modifiers: .command)
            }
        }

        Settings {
            SamplerThemedRoot(viewModel: viewModel) {
                ShortcutPreferencesView(viewModel: viewModel)
            }
        }
    }
}

struct KeyboardHandlerView: NSViewRepresentable {
    @ObservedObject var viewModel: SamplerViewModel

    func makeNSView(context: Context) -> KeyboardHandlerNSView {
        let view = KeyboardHandlerNSView()
        view.viewModel = viewModel
        return view
    }

    func updateNSView(_ nsView: KeyboardHandlerNSView, context: Context) {
        nsView.viewModel = viewModel
    }
}

final class KeyboardHandlerNSView: NSView {
    weak var viewModel: SamplerViewModel?

    override var acceptsFirstResponder: Bool { true }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        window?.makeFirstResponder(self)
    }

    override func keyDown(with event: NSEvent) {
        guard let viewModel else {
            super.keyDown(with: event)
            return
        }

        if viewModel.capturingShortcutId.isEmpty == false {
            if viewModel.handleShortcutKeyEvent(event) {
                return
            }
        }

        if event.keyCode == 49, !isEditingTarget(event) {
            _ = viewModel.handleSpaceKey()
            return
        }

        super.keyDown(with: event)
    }

    private func isEditingTarget(_ event: NSEvent) -> Bool {
        guard let responder = window?.firstResponder else { return false }
        return responder is NSTextView || responder is NSTextField
    }
}
