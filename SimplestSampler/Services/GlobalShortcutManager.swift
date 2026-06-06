import AppKit
import Carbon
import Foundation

@MainActor
final class GlobalShortcutManager {
    private var hotKeyRefs: [EventHotKeyRef?] = []
    private var hotKeyIDs: [UInt32: String] = [:]
    private var eventHandler: EventHandlerRef?
    private var nextHotKeyID: UInt32 = 1
    private var onShortcut: ((String) -> Void)?

    func start(onShortcut: @escaping (String) -> Void) {
        self.onShortcut = onShortcut
        installEventHandler()
    }

    func stop() {
        unregisterHotKeys()
        if let eventHandler {
            RemoveEventHandler(eventHandler)
            self.eventHandler = nil
        }
    }

    private func unregisterHotKeys() {
        for ref in hotKeyRefs {
            if let ref {
                UnregisterEventHotKey(ref)
            }
        }
        hotKeyRefs.removeAll()
        hotKeyIDs.removeAll()
    }

    func updateBindings(_ bindings: [String: String]) {
        unregisterHotKeys()

        for (shortcutID, accelerator) in bindings {
            let trimmed = accelerator.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !trimmed.isEmpty, let parsed = ParsedShortcut.parse(trimmed) else { continue }
            register(shortcutID: shortcutID, parsed: parsed)
        }
    }

    private func installEventHandler() {
        var eventType = EventTypeSpec(eventClass: OSType(kEventClassKeyboard), eventKind: UInt32(kEventHotKeyPressed))
        let handler: EventHandlerUPP = { _, event, userData -> OSStatus in
            guard let userData else { return OSStatus(eventNotHandledErr) }
            let manager = Unmanaged<GlobalShortcutManager>.fromOpaque(userData).takeUnretainedValue()

            var hotKeyID = EventHotKeyID()
            let status = GetEventParameter(
                event,
                EventParamName(kEventParamDirectObject),
                EventParamType(typeEventHotKeyID),
                nil,
                MemoryLayout<EventHotKeyID>.size,
                nil,
                &hotKeyID
            )
            guard status == noErr else { return status }

            Task { @MainActor in
                if let shortcutID = manager.hotKeyIDs[hotKeyID.id] {
                    manager.onShortcut?(shortcutID)
                }
            }
            return noErr
        }

        let selfPtr = Unmanaged.passUnretained(self).toOpaque()
        InstallEventHandler(GetApplicationEventTarget(), handler, 1, &eventType, selfPtr, &eventHandler)
    }

    private func register(shortcutID: String, parsed: ParsedShortcut) {
        guard let keyCode = keyCode(for: parsed.keyToken) else { return }

        var modifiers: UInt32 = 0
        if parsed.command { modifiers |= UInt32(cmdKey) }
        if parsed.control { modifiers |= UInt32(controlKey) }
        if parsed.option { modifiers |= UInt32(optionKey) }
        if parsed.shift { modifiers |= UInt32(shiftKey) }

        let hotKeyID = EventHotKeyID(signature: OSType(0x534D504C), id: nextHotKeyID)
        var ref: EventHotKeyRef?
        let status = RegisterEventHotKey(keyCode, modifiers, hotKeyID, GetApplicationEventTarget(), 0, &ref)
        guard status == noErr, let ref else { return }

        hotKeyIDs[nextHotKeyID] = shortcutID
        hotKeyRefs.append(ref)
        nextHotKeyID += 1
    }

    private func keyCode(for token: String) -> UInt32? {
        let map: [String: UInt32] = [
            "A": 0, "B": 11, "C": 8, "D": 2, "E": 14, "F": 3, "G": 5, "H": 4, "I": 34,
            "J": 38, "K": 40, "L": 37, "M": 46, "N": 45, "O": 31, "P": 35, "Q": 12, "R": 15,
            "S": 1, "T": 17, "U": 32, "V": 9, "W": 13, "X": 7, "Y": 16, "Z": 6,
            "0": 29, "1": 18, "2": 19, "3": 20, "4": 21, "5": 23, "6": 22, "7": 26, "8": 28, "9": 25,
            "F1": 122, "F2": 120, "F3": 99, "F4": 118, "F5": 96, "F6": 97, "F7": 98, "F8": 100,
            "F9": 101, "F10": 109, "F11": 103, "F12": 111, "F13": 105, "F14": 107, "F15": 113, "F16": 106,
            "F17": 64, "F18": 79, "F19": 80, "F20": 90,
            "Space": 49, "Enter": 36, "Delete": 51, "Escape": 53, "Tab": 48,
            "Left": 123, "Right": 124, "Down": 125, "Up": 126
        ]
        return map[token]
    }
}
