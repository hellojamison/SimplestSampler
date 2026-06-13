import Foundation
import UniformTypeIdentifiers

extension UTType {
    static let simplestSamplerStoredCapture = UTType(exportedAs: "com.hellojamison.simplestsampler.stored-capture-id")
    static let simplestSamplerActiveSlotIndex = UTType(exportedAs: "com.hellojamison.simplestsampler.active-slot-index")
    static let simplestSamplerActiveSlotDrag = UTType(exportedAs: "com.hellojamison.simplestsampler.active-slot-drag")
    static let simplestSamplerSoundboardPadIndex = UTType(exportedAs: "com.hellojamison.simplestsampler.soundboard-pad-index")
    static let simplestSamplerSoundboardPadDrag = UTType(exportedAs: "com.hellojamison.simplestsampler.soundboard-pad-drag")
}

struct ActiveSlotDragPayload: Codable, Sendable {
    var slotIndex: Int
    var captureId: String?

    var encodedData: Data? {
        try? JSONEncoder().encode(self)
    }

    static func decode(from data: Data) -> ActiveSlotDragPayload? {
        try? JSONDecoder().decode(ActiveSlotDragPayload.self, from: data)
    }

    static func decodeLegacyString(_ rawValue: String) -> ActiveSlotDragPayload? {
        let trimmed = rawValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let slotIndex = Int(trimmed) else { return nil }
        return ActiveSlotDragPayload(slotIndex: slotIndex, captureId: nil)
    }
}

struct SoundboardPadDragPayload: Codable, Sendable {
    var padIndex: Int
    var captureId: String?

    var encodedData: Data? {
        try? JSONEncoder().encode(self)
    }

    static func decode(from data: Data) -> SoundboardPadDragPayload? {
        try? JSONDecoder().decode(SoundboardPadDragPayload.self, from: data)
    }

    static func decodeLegacyString(_ rawValue: String) -> SoundboardPadDragPayload? {
        let trimmed = rawValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let padIndex = Int(trimmed) else { return nil }
        return SoundboardPadDragPayload(padIndex: padIndex, captureId: nil)
    }
}

extension NSItemProvider {
    var supportsActiveSlotDragPayload: Bool {
        hasItemConformingToTypeIdentifier(UTType.simplestSamplerActiveSlotDrag.identifier)
            || hasItemConformingToTypeIdentifier(UTType.simplestSamplerActiveSlotIndex.identifier)
    }

    func loadActiveSlotDragPayload(_ completion: @escaping (ActiveSlotDragPayload?) -> Void) {
        if hasItemConformingToTypeIdentifier(UTType.simplestSamplerActiveSlotDrag.identifier) {
            loadDataRepresentation(forTypeIdentifier: UTType.simplestSamplerActiveSlotDrag.identifier) { data, _ in
                completion(data.flatMap(ActiveSlotDragPayload.decode(from:)))
            }
            return
        }

        if hasItemConformingToTypeIdentifier(UTType.simplestSamplerActiveSlotIndex.identifier) {
            loadDataRepresentation(forTypeIdentifier: UTType.simplestSamplerActiveSlotIndex.identifier) { data, _ in
                guard let data else {
                    completion(nil)
                    return
                }
                completion(
                    ActiveSlotDragPayload.decode(from: data)
                        ?? String(data: data, encoding: .utf8).flatMap(ActiveSlotDragPayload.decodeLegacyString(_:))
                )
            }
            return
        }
        completion(nil)
    }

    var supportsSoundboardPadDragPayload: Bool {
        hasItemConformingToTypeIdentifier(UTType.simplestSamplerSoundboardPadDrag.identifier)
            || hasItemConformingToTypeIdentifier(UTType.simplestSamplerSoundboardPadIndex.identifier)
    }

    func loadSoundboardPadDragPayload(_ completion: @escaping (SoundboardPadDragPayload?) -> Void) {
        if hasItemConformingToTypeIdentifier(UTType.simplestSamplerSoundboardPadDrag.identifier) {
            loadDataRepresentation(forTypeIdentifier: UTType.simplestSamplerSoundboardPadDrag.identifier) { data, _ in
                completion(data.flatMap(SoundboardPadDragPayload.decode(from:)))
            }
            return
        }

        if hasItemConformingToTypeIdentifier(UTType.simplestSamplerSoundboardPadIndex.identifier) {
            loadDataRepresentation(forTypeIdentifier: UTType.simplestSamplerSoundboardPadIndex.identifier) { data, _ in
                guard let data else {
                    completion(nil)
                    return
                }
                completion(
                    SoundboardPadDragPayload.decode(from: data)
                        ?? String(data: data, encoding: .utf8).flatMap(SoundboardPadDragPayload.decodeLegacyString(_:))
                )
            }
            return
        }

        completion(nil)
    }

    func loadStoredCaptureID(_ completion: @escaping (String?) -> Void) {
        if hasItemConformingToTypeIdentifier(UTType.simplestSamplerStoredCapture.identifier) {
            loadDataRepresentation(forTypeIdentifier: UTType.simplestSamplerStoredCapture.identifier) { data, _ in
                guard let data else {
                    completion(nil)
                    return
                }
                completion(String(data: data, encoding: .utf8))
            }
            return
        }

        loadObject(ofClass: NSString.self) { item, _ in
            completion(item as? String)
        }
    }
}

struct SamplerCapture: Codable, Identifiable, Equatable, Sendable {
    var id: String
    var filePath: String
    var fileName: String
    var clipName: String
    var displayName: String
    var defaultDisplayName: String
    var hasCustomDisplayName: Bool
    var playbackStartSeconds: Double?
    var playbackEndSeconds: Double?
    var capturedAt: TimeInterval
    var saved: Bool
    var sourceIdentity: String
    var managedByApp: Bool
    var categoryId: String?

    init(
        id: String,
        filePath: String,
        fileName: String = "",
        clipName: String = "",
        displayName: String = "",
        defaultDisplayName: String = "",
        hasCustomDisplayName: Bool = false,
        playbackStartSeconds: Double? = nil,
        playbackEndSeconds: Double? = nil,
        capturedAt: TimeInterval = Date().timeIntervalSince1970 * 1000,
        saved: Bool = false,
        sourceIdentity: String = "",
        managedByApp: Bool = false,
        categoryId: String? = nil
    ) {
        self.id = id
        self.filePath = filePath
        self.fileName = fileName.isEmpty ? URL(fileURLWithPath: filePath).lastPathComponent : fileName
        self.clipName = clipName
        self.displayName = displayName
        self.defaultDisplayName = defaultDisplayName
        self.hasCustomDisplayName = hasCustomDisplayName
        self.playbackStartSeconds = playbackStartSeconds
        self.playbackEndSeconds = playbackEndSeconds
        self.capturedAt = capturedAt
        self.saved = saved
        self.sourceIdentity = sourceIdentity.isEmpty ? Self.buildSourceIdentity(filePath: filePath) : sourceIdentity
        self.managedByApp = managedByApp
        self.categoryId = categoryId
    }

    static func buildSourceIdentity(filePath: String) -> String {
        filePath.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    var slotLabel: String {
        if hasCustomDisplayName {
            let custom = displayName.trimmingCharacters(in: .whitespacesAndNewlines)
            if !custom.isEmpty { return custom }
        }
        let visible = displayName.trimmingCharacters(in: .whitespacesAndNewlines)
        if !visible.isEmpty { return visible }
        let fallback = defaultDisplayName.trimmingCharacters(in: .whitespacesAndNewlines)
        if !fallback.isEmpty { return fallback }
        let clip = clipName.trimmingCharacters(in: .whitespacesAndNewlines)
        if !clip.isEmpty { return clip }
        let name = fileName.trimmingCharacters(in: .whitespacesAndNewlines)
        if !name.isEmpty {
            let base = (name as NSString).deletingPathExtension
            return base.isEmpty ? name : base
        }
        return "Capture"
    }

    var durationSeconds: Double? {
        if let start = playbackStartSeconds, let end = playbackEndSeconds, end > start {
            return end - start
        }
        return nil
    }

    var formattedDuration: String? {
        guard let duration = durationSeconds, duration > 0 else { return nil }
        if duration < 60 {
            return String(format: "%.1fs", duration)
        }
        let minutes = Int(duration) / 60
        let seconds = duration.truncatingRemainder(dividingBy: 60)
        return String(format: "%d:%04.1f", minutes, seconds)
    }
}

struct StoredCategory: Codable, Identifiable, Equatable, Sendable {
    var id: String
    var name: String
}

enum StoredCategoryFilter {
    static let all = "__all__"
    static let uncategorized = "__uncategorized__"

    static func matches(capture: SamplerCapture, filter: String) -> Bool {
        switch filter {
        case all:
            return true
        case uncategorized:
            return (capture.categoryId ?? "").isEmpty
        default:
            return capture.categoryId == filter
        }
    }
}

struct SamplerSessionState: Codable, Equatable {
    var recentCaptures: [SamplerCapture?]
    var storedCaptures: [SamplerCapture]
    var soundboardPads: [SamplerCapture?]
    var loadedCaptureId: String
    var activeTab: String
    var windowFrame: WindowFrame?
    /// Visible active slot rows; nil in older saved sessions defaults to 4 on load.
    var activeSlotCount: Int?
    /// Stored tab category filter; older sessions may still decode from `simpleishCategoryFilter`.
    var storedCategoryFilter: String?

    static let empty = SamplerSessionState(
        recentCaptures: Array(repeating: nil, count: SamplerConstants.defaultActiveSlots),
        storedCaptures: [],
        soundboardPads: Array(repeating: nil, count: SamplerConstants.soundboardPadCount),
        loadedCaptureId: "",
        activeTab: "recent",
        windowFrame: nil,
        activeSlotCount: SamplerConstants.defaultActiveSlots,
        storedCategoryFilter: StoredCategoryFilter.all
    )

    enum CodingKeys: String, CodingKey {
        case recentCaptures
        case storedCaptures
        case soundboardPads
        case loadedCaptureId
        case activeTab
        case windowFrame
        case activeSlotCount
        case storedCategoryFilter
        case simpleishCategoryFilter
    }

    init(
        recentCaptures: [SamplerCapture?],
        storedCaptures: [SamplerCapture],
        soundboardPads: [SamplerCapture?],
        loadedCaptureId: String,
        activeTab: String,
        windowFrame: WindowFrame?,
        activeSlotCount: Int?,
        storedCategoryFilter: String?
    ) {
        self.recentCaptures = recentCaptures
        self.storedCaptures = storedCaptures
        self.soundboardPads = soundboardPads
        self.loadedCaptureId = loadedCaptureId
        self.activeTab = activeTab
        self.windowFrame = windowFrame
        self.activeSlotCount = activeSlotCount
        self.storedCategoryFilter = storedCategoryFilter
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        recentCaptures = try container.decode([SamplerCapture?].self, forKey: .recentCaptures)
        storedCaptures = try container.decodeIfPresent([SamplerCapture].self, forKey: .storedCaptures) ?? []
        soundboardPads = try container.decodeIfPresent([SamplerCapture?].self, forKey: .soundboardPads)
            ?? Array(repeating: nil, count: SamplerConstants.soundboardPadCount)
        loadedCaptureId = try container.decode(String.self, forKey: .loadedCaptureId)
        let decodedActiveTab = try container.decode(String.self, forKey: .activeTab)
        activeTab = decodedActiveTab == "simpleish" ? "stored" : decodedActiveTab
        windowFrame = try container.decodeIfPresent(WindowFrame.self, forKey: .windowFrame)
        activeSlotCount = try container.decodeIfPresent(Int.self, forKey: .activeSlotCount)
        if let storedFilter = try container.decodeIfPresent(String.self, forKey: .storedCategoryFilter) {
            storedCategoryFilter = storedFilter
        } else {
            storedCategoryFilter = try container.decodeIfPresent(String.self, forKey: .simpleishCategoryFilter)
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(recentCaptures, forKey: .recentCaptures)
        try container.encode(storedCaptures, forKey: .storedCaptures)
        try container.encode(soundboardPads, forKey: .soundboardPads)
        try container.encode(loadedCaptureId, forKey: .loadedCaptureId)
        try container.encode(activeTab, forKey: .activeTab)
        try container.encodeIfPresent(windowFrame, forKey: .windowFrame)
        try container.encodeIfPresent(activeSlotCount, forKey: .activeSlotCount)
        try container.encodeIfPresent(storedCategoryFilter, forKey: .storedCategoryFilter)
    }
}

struct WindowFrame: Codable, Equatable {
    var x: Double
    var y: Double
    var width: Double
    var height: Double
}

enum SamplerConstants {
    static let defaultActiveSlots = 4
    static let maxActiveSlots = 16
    static let soundboardPadCount = 16
    static let soundboardColumns = 4
    static let defaultVolume = 80
    static let maxVolume = 140
    static let minWindowWidth: CGFloat = 360
    static let minWindowHeight: CGFloat = 340
    static let defaultWindowWidth: CGFloat = 520
    static let slotRowHeight: CGFloat = 48
    static let slotRowSpacing: CGFloat = 8
    static let addSlotControlHeight: CGFloat = 30
    static let addSlotControlSpacing: CGFloat = 8
    static let soundboardPadSize: CGFloat = 70
    static let soundboardGap: CGFloat = 8
    static let storedCategoryBarHeight: CGFloat = 24
    static let storedCategoryBarSpacing: CGFloat = 8

    static let fixedWindowChromeHeight: CGFloat = 262

    static var defaultWindowHeight: CGFloat {
        contentHeight(
            forActiveSlotCount: defaultActiveSlots,
            showsSlotControls: true
        )
    }

    static var soundboardGridHeight: CGFloat {
        let rows = CGFloat(soundboardColumns)
        return rows * soundboardPadSize + (rows - 1) * soundboardGap
    }

    static func contentHeight(forActiveSlotCount count: Int, showsSlotControls: Bool) -> CGFloat {
        let slots = max(defaultActiveSlots, count)
        let rowStackHeight =
            CGFloat(slots) * slotRowHeight
            + CGFloat(max(0, slots - 1)) * slotRowSpacing
        var height = fixedWindowChromeHeight + rowStackHeight
        if showsSlotControls {
            height += addSlotControlHeight + addSlotControlSpacing
        }
        return max(minWindowHeight, height)
    }

    static func contentHeight(forActiveTab tab: String, activeSlotCount count: Int, showsSlotControls: Bool) -> CGFloat {
        if tab == "stored" {
            return max(
                minWindowHeight,
                contentHeight(
                    forActiveSlotCount: defaultActiveSlots,
                    showsSlotControls: false
                ) + storedCategoryBarHeight + storedCategoryBarSpacing
            )
        }
        if tab == "soundboard" {
            return max(minWindowHeight, fixedWindowChromeHeight + soundboardGridHeight)
        }
        return contentHeight(forActiveSlotCount: count, showsSlotControls: showsSlotControls)
    }

    static let supportedDropExtensions: Set<String> = [
        "wav", "aif", "aiff", "caf", "mp3", "m4a", "mp4", "aac", "ogg", "flac"
    ]

    static func isSupportedAudioFile(_ path: String) -> Bool {
        let ext = (path as NSString).pathExtension.lowercased()
        return supportedDropExtensions.contains(ext)
    }
}
