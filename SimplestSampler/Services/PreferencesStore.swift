import AppKit
import Foundation

@MainActor
final class PreferencesStore: ObservableObject {
    @Published var preferences: SamplerPreferences
    @Published var sessionState: SamplerSessionState

    private let preferencesKey = "samplerPreferences"
    private let sessionKey = "samplerSessionState"

    init() {
        preferences = Self.loadPreferences()
        sessionState = Self.loadSessionState()
    }

    func savePreferences() {
        guard let data = try? JSONEncoder().encode(preferences) else { return }
        UserDefaults.standard.set(data, forKey: preferencesKey)
    }

    func saveSessionState() {
        guard let data = try? JSONEncoder().encode(sessionState) else { return }
        UserDefaults.standard.set(data, forKey: sessionKey)
    }

    func setVolume(_ volume: Int) {
        let clamped = max(0, min(SamplerConstants.maxVolume, volume))
        preferences.samplerVolume = clamped
        savePreferences()
    }

    func setOutputDeviceUID(_ uid: String) {
        preferences.samplerAudioOutputDeviceUID = uid
        savePreferences()
    }

    func setShortcutBinding(id: String, accelerator: String) {
        preferences.shortcutBindings[id] = accelerator
        savePreferences()
    }

    func shortcutAccelerator(for id: String) -> String {
        if let binding = preferences.shortcutBindings[id] {
            return binding
        }
        return ShortcutDefinitions.defaultBindings[id] ?? ""
    }

    func updateWindowFrame(_ frame: NSRect) {
        sessionState.windowFrame = WindowFrame(
            x: frame.origin.x,
            y: frame.origin.y,
            width: frame.size.width,
            height: frame.size.height
        )
        saveSessionState()
    }

    func preferredWindowFrame() -> NSRect? {
        guard let frame = sessionState.windowFrame else { return nil }
        return NSRect(x: frame.x, y: frame.y, width: frame.width, height: frame.height)
    }

    private static func loadPreferences() -> SamplerPreferences {
        guard let data = UserDefaults.standard.data(forKey: "samplerPreferences"),
              let decoded = try? JSONDecoder().decode(SamplerPreferences.self, from: data) else {
            return .defaults
        }
        return decoded
    }

    private static func loadSessionState() -> SamplerSessionState {
        guard let data = UserDefaults.standard.data(forKey: "samplerSessionState"),
              let decoded = try? JSONDecoder().decode(SamplerSessionState.self, from: data) else {
            return .empty
        }
        var state = decoded
        while state.recentCaptures.count < SamplerConstants.maxActiveSlots {
            state.recentCaptures.append(nil)
        }
        if state.recentCaptures.count > SamplerConstants.maxActiveSlots {
            state.recentCaptures = Array(state.recentCaptures.prefix(SamplerConstants.maxActiveSlots))
        }
        return state
    }
}
