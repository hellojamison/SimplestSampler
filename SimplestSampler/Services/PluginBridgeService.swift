import Foundation

struct PluginBridgeSlot: Codable, Equatable, Sendable {
    var index: Int
    var id: String?
    var filePath: String
    var displayName: String
    var playbackStartSeconds: Double?
    var playbackEndSeconds: Double?
    var capturedAt: TimeInterval
    var source: String

    init(
        index: Int,
        id: String? = nil,
        filePath: String = "",
        displayName: String = "",
        playbackStartSeconds: Double? = nil,
        playbackEndSeconds: Double? = nil,
        capturedAt: TimeInterval = 0,
        source: String = "aax-audiosuite-capture"
    ) {
        self.index = index
        self.id = id
        self.filePath = filePath
        self.displayName = displayName
        self.playbackStartSeconds = playbackStartSeconds
        self.playbackEndSeconds = playbackEndSeconds
        self.capturedAt = capturedAt
        self.source = source
    }
}

struct PluginActiveSlotsDocument: Codable, Equatable, Sendable {
    var activeSlotCount: Int
    var slots: [PluginBridgeSlot]
    var shortcutBindings: [String: String]

    static let empty = PluginActiveSlotsDocument(
        activeSlotCount: SamplerConstants.defaultActiveSlots,
        slots: [],
        shortcutBindings: ShortcutDefinitions.defaultBindings
    )
}

enum PluginBridgeService {
    static let bridgeFileName = "plugin-active-slots.json"
    static let pluginCaptureSource = "aax-audiosuite-capture"

    static var bridgeFileURL: URL {
        StoredLibraryService.applicationSupportURL.appendingPathComponent(bridgeFileName)
    }

    static func ensureApplicationSupportDirectory() throws {
        try FileManager.default.createDirectory(
            at: StoredLibraryService.applicationSupportURL,
            withIntermediateDirectories: true
        )
    }

    static func loadDocument() -> PluginActiveSlotsDocument {
        guard let data = try? Data(contentsOf: bridgeFileURL),
              let decoded = try? JSONDecoder().decode(PluginActiveSlotsDocument.self, from: data) else {
            return .empty
        }
        return decoded
    }

    static func writeDocument(_ document: PluginActiveSlotsDocument) throws {
        try ensureApplicationSupportDirectory()
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        let data = try encoder.encode(document)
        let tempURL = bridgeFileURL.appendingPathExtension("tmp")
        try data.write(to: tempURL, options: .atomic)
        _ = try FileManager.default.replaceItemAt(bridgeFileURL, withItemAt: tempURL)
    }

    static func shortcutBindings(from preferences: SamplerPreferences) -> [String: String] {
        var bindings: [String: String] = [:]
        for shortcutID in ShortcutDefinitions.allShortcutIDs {
            bindings[shortcutID] = preferences.shortcutBindings[shortcutID]
                ?? ShortcutDefinitions.defaultBindings[shortcutID]
                ?? ""
        }
        for slot in (ShortcutDefinitions.defaultShortcutSlotCount + 1)...SamplerConstants.maxActiveSlots {
            bindings[ShortcutDefinitions.captureShortcutID(for: slot)] = preferences.shortcutBindings[
                ShortcutDefinitions.captureShortcutID(for: slot)
            ] ?? ""
            bindings[ShortcutDefinitions.playShortcutID(for: slot)] = preferences.shortcutBindings[
                ShortcutDefinitions.playShortcutID(for: slot)
            ] ?? ""
        }
        return bindings
    }

    static func makeDocument(
        recentCaptures: [SamplerCapture?],
        activeSlotCount: Int,
        shortcutBindings: [String: String]
    ) -> PluginActiveSlotsDocument {
        let clampedCount = min(SamplerConstants.maxActiveSlots, max(SamplerConstants.defaultActiveSlots, activeSlotCount))
        var slots: [PluginBridgeSlot] = []

        for index in 0..<clampedCount {
            guard let capture = index < recentCaptures.count ? recentCaptures[index] : nil else { continue }
            slots.append(PluginBridgeSlot(
                index: index,
                id: capture.id,
                filePath: capture.filePath,
                displayName: capture.slotLabel,
                playbackStartSeconds: capture.playbackStartSeconds,
                playbackEndSeconds: capture.playbackEndSeconds,
                capturedAt: capture.capturedAt,
                source: capture.managedByApp ? "app-managed-capture" : "app-capture"
            ))
        }

        return PluginActiveSlotsDocument(
            activeSlotCount: clampedCount,
            slots: slots,
            shortcutBindings: shortcutBindings
        )
    }

    static func syncFromApp(
        recentCaptures: [SamplerCapture?],
        activeSlotCount: Int,
        preferences: SamplerPreferences
    ) {
        let document = makeDocument(
            recentCaptures: recentCaptures,
            activeSlotCount: activeSlotCount,
            shortcutBindings: shortcutBindings(from: preferences)
        )
        try? writeDocument(document)
    }

    static func mergePluginCaptures(
        into recentCaptures: inout [SamplerCapture?],
        activeSlotCount: Int,
        storedCaptures: [SamplerCapture],
        document: PluginActiveSlotsDocument
    ) -> Bool {
        let clampedCount = min(
            SamplerConstants.maxActiveSlots,
            max(SamplerConstants.defaultActiveSlots, document.activeSlotCount, activeSlotCount)
        )

        while recentCaptures.count < clampedCount {
            recentCaptures.append(nil)
        }
        if recentCaptures.count > clampedCount {
            recentCaptures = Array(recentCaptures.prefix(clampedCount))
        }

        var changed = false
        for slot in document.slots where slot.source == pluginCaptureSource {
            guard slot.index >= 0, slot.index < recentCaptures.count else { continue }
            let normalizedPath = slot.filePath.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !normalizedPath.isEmpty, FileManager.default.fileExists(atPath: normalizedPath) else { continue }

            let fileName = URL(fileURLWithPath: normalizedPath).lastPathComponent
            let displayName = slot.displayName.trimmingCharacters(in: .whitespacesAndNewlines)
            let captureID = slot.id?.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty == false
                ? slot.id!
                : "sampler-capture-aax-\(Int(slot.capturedAt))"

            let existing = recentCaptures[slot.index]
            let nextCapture = SamplerCapture(
                id: captureID,
                filePath: normalizedPath,
                fileName: fileName,
                clipName: displayName.isEmpty ? fileName : displayName,
                displayName: displayName.isEmpty ? ((fileName as NSString).deletingPathExtension) : displayName,
                defaultDisplayName: displayName.isEmpty ? ((fileName as NSString).deletingPathExtension) : displayName,
                hasCustomDisplayName: existing?.hasCustomDisplayName ?? false,
                playbackStartSeconds: slot.playbackStartSeconds,
                playbackEndSeconds: slot.playbackEndSeconds,
                capturedAt: slot.capturedAt > 0 ? slot.capturedAt : Date().timeIntervalSince1970 * 1000,
                saved: storedCaptures.contains { $0.sourceIdentity == normalizedPath },
                sourceIdentity: normalizedPath,
                managedByApp: true
            )

            if existing != nextCapture {
                recentCaptures[slot.index] = nextCapture
                changed = true
            }
        }

        return changed
    }
}

final class PluginBridgeWatcher {
    private var source: DispatchSourceFileSystemObject?
    private var lastModificationTime: TimeInterval = 0
    private let queue = DispatchQueue(label: "com.hellojamison.simplestsampler.plugin-bridge")
    private let onChange: () -> Void

    init(onChange: @escaping () -> Void) {
        self.onChange = onChange
        start()
    }

    deinit {
        source?.cancel()
        source = nil
    }

    private func start() {
        let url = PluginBridgeService.bridgeFileURL
        try? PluginBridgeService.ensureApplicationSupportDirectory()
        if !FileManager.default.fileExists(atPath: url.path) {
            FileManager.default.createFile(atPath: url.path, contents: nil)
        }

        let descriptor = open(url.path, O_EVTONLY)
        guard descriptor >= 0 else { return }

        let dispatchSource = DispatchSource.makeFileSystemObjectSource(
            fileDescriptor: descriptor,
            eventMask: [.write, .rename, .delete],
            queue: queue
        )
        dispatchSource.setEventHandler { [weak self] in
            guard let self else { return }
            let attributes = try? FileManager.default.attributesOfItem(atPath: url.path)
            let mtime = (attributes?[.modificationDate] as? Date)?.timeIntervalSince1970 ?? 0
            guard mtime > self.lastModificationTime else { return }
            self.lastModificationTime = mtime
            DispatchQueue.main.async {
                self.onChange()
            }
        }
        dispatchSource.setCancelHandler {
            close(descriptor)
        }
        dispatchSource.resume()
        source = dispatchSource
        lastModificationTime = (try? FileManager.default.attributesOfItem(atPath: url.path)[.modificationDate] as? Date)?
            .timeIntervalSince1970 ?? 0
    }
}
