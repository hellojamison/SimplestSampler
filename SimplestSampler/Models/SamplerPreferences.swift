import Foundation

struct SamplerPreferences: Codable, Equatable {
    var samplerAudioOutputDeviceUID: String
    var samplerVolume: Int
    var shortcutBindings: [String: String]
    var themeMode: SamplerThemeMode

    static let defaults = SamplerPreferences(
        samplerAudioOutputDeviceUID: "",
        samplerVolume: SamplerConstants.defaultVolume,
        shortcutBindings: ShortcutDefinitions.defaultBindings,
        themeMode: .system
    )

    enum CodingKeys: String, CodingKey {
        case samplerAudioOutputDeviceUID
        case samplerVolume
        case shortcutBindings
        case themeMode
        case darkModeEnabled
    }

    init(
        samplerAudioOutputDeviceUID: String,
        samplerVolume: Int,
        shortcutBindings: [String: String],
        themeMode: SamplerThemeMode
    ) {
        self.samplerAudioOutputDeviceUID = samplerAudioOutputDeviceUID
        self.samplerVolume = samplerVolume
        self.shortcutBindings = shortcutBindings
        self.themeMode = themeMode
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        samplerAudioOutputDeviceUID = try container.decode(String.self, forKey: .samplerAudioOutputDeviceUID)
        samplerVolume = try container.decode(Int.self, forKey: .samplerVolume)
        shortcutBindings = try container.decode([String: String].self, forKey: .shortcutBindings)

        if let mode = try container.decodeIfPresent(SamplerThemeMode.self, forKey: .themeMode) {
            themeMode = mode
        } else if let legacyDark = try container.decodeIfPresent(Bool.self, forKey: .darkModeEnabled) {
            themeMode = legacyDark ? .dark : .light
        } else {
            themeMode = .system
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(samplerAudioOutputDeviceUID, forKey: .samplerAudioOutputDeviceUID)
        try container.encode(samplerVolume, forKey: .samplerVolume)
        try container.encode(shortcutBindings, forKey: .shortcutBindings)
        try container.encode(themeMode, forKey: .themeMode)
    }
}

enum ShortcutDefinitions {
    static let defaultShortcutSlotCount = 4

    static let captureSlotIDs = (1...defaultShortcutSlotCount).map { "samplerCaptureSlot\($0)" }
    static let playSlotIDs = (1...defaultShortcutSlotCount).map { "samplerSlot\($0)" }

    static func hasShortcutSupport(forSlotNumber slot: Int) -> Bool {
        slot >= 1 && slot <= defaultShortcutSlotCount
    }

    static let defaultBindings: [String: String] = [
        "samplerCaptureSlot1": "Command+F13",
        "samplerCaptureSlot2": "Command+F14",
        "samplerCaptureSlot3": "Command+F15",
        "samplerCaptureSlot4": "",
        "samplerSlot1": "F13",
        "samplerSlot2": "F14",
        "samplerSlot3": "F15",
        "samplerSlot4": ""
    ]

    static let defaultPlayKeys = ["F13", "F14", "F15", ""]

    static func captureShortcutID(for slot: Int) -> String {
        "samplerCaptureSlot\(slot)"
    }

    static func playShortcutID(for slot: Int) -> String {
        "samplerSlot\(slot)"
    }

    static func actionLabel(for shortcutID: String) -> String {
        if shortcutID.hasPrefix("samplerCaptureSlot") { return "Capture" }
        if shortcutID.hasPrefix("samplerSlot") { return "Trigger" }
        return "Shortcut"
    }

    static func slotNumber(for shortcutID: String) -> Int? {
        let digits = shortcutID.filter(\.isNumber)
        return Int(digits)
    }

    static let allShortcutIDs: [String] = captureSlotIDs + playSlotIDs

    static func preferencesActionLabel(for shortcutID: String) -> String {
        if shortcutID.hasPrefix("samplerCaptureSlot") { return "Capture slot" }
        if shortcutID.hasPrefix("samplerSlot") { return "Play slot" }
        return "Shortcut"
    }

    static func formattedDefaultLabel(for shortcutID: String) -> String {
        let accelerator = defaultBindings[shortcutID] ?? ""
        if accelerator.isEmpty { return "None" }
        return ParsedShortcut.parse(accelerator)?.formattedLabel() ?? accelerator
    }
}

struct ParsedShortcut: Equatable {
    var command: Bool
    var control: Bool
    var option: Bool
    var shift: Bool
    var keyToken: String

    var accelerator: String {
        var parts: [String] = []
        if command { parts.append("Command") }
        if control { parts.append("Control") }
        if option { parts.append("Alt") }
        if shift { parts.append("Shift") }
        parts.append(keyToken)
        return parts.joined(separator: "+")
    }

    static func parse(_ accelerator: String) -> ParsedShortcut? {
        let parts = accelerator.split(separator: "+").map { String($0).trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
        guard !parts.isEmpty else { return nil }

        var command = false
        var control = false
        var option = false
        var shift = false
        var keyToken = ""

        for part in parts {
            let lower = part.lowercased()
            switch lower {
            case "command", "cmd":
                command = true
            case "control", "ctrl":
                control = true
            case "commandorcontrol", "cmdorctrl":
                command = true
            case "alt", "opt", "option":
                option = true
            case "shift":
                shift = true
            default:
                if !keyToken.isEmpty { return nil }
                keyToken = normalizeKeyToken(part)
                if keyToken.isEmpty { return nil }
            }
        }

        guard !keyToken.isEmpty else { return nil }
        return ParsedShortcut(command: command, control: control, option: option, shift: shift, keyToken: keyToken)
    }

    private static func normalizeKeyToken(_ token: String) -> String {
        let trimmed = token.trimmingCharacters(in: .whitespaces)
        if trimmed.count == 1, trimmed.first?.isLetter == true {
            return trimmed.uppercased()
        }
        if trimmed.count == 1, trimmed.first?.isNumber == true {
            return trimmed
        }
        let upper = trimmed.uppercased()
        if upper.range(of: #"^F([1-9]|1[0-9]|2[0-4])$"#, options: .regularExpression) != nil {
            return upper
        }
        let aliases: [String: String] = [
            "space": "Space", "spacebar": "Space", "escape": "Escape", "esc": "Escape",
            "return": "Enter", "enter": "Enter", "delete": "Delete", "backspace": "Delete",
            "tab": "Tab", "left": "Left", "right": "Right", "up": "Up", "down": "Down"
        ]
        return aliases[trimmed.lowercased()] ?? ""
    }

    func formattedLabel() -> String {
        var parts: [String] = []
        if command { parts.append("Cmd") }
        if control { parts.append("Ctrl") }
        if option { parts.append("Opt") }
        if shift { parts.append("Shift") }
        parts.append(keyToken)
        return parts.joined(separator: "+")
    }
}
