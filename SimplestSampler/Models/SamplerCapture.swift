import Foundation

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
        managedByApp: Bool = false
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

struct SamplerSessionState: Codable, Equatable {
    var recentCaptures: [SamplerCapture?]
    var storedCaptures: [SamplerCapture]
    var loadedCaptureId: String
    var activeTab: String
    var windowFrame: WindowFrame?
    /// Visible active slot rows; nil in older saved sessions defaults to 4 on load.
    var activeSlotCount: Int?

    static let empty = SamplerSessionState(
        recentCaptures: Array(repeating: nil, count: SamplerConstants.defaultActiveSlots),
        storedCaptures: [],
        loadedCaptureId: "",
        activeTab: "recent",
        windowFrame: nil,
        activeSlotCount: SamplerConstants.defaultActiveSlots
    )
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
    static let defaultVolume = 80
    static let maxVolume = 140
    static let minWindowWidth: CGFloat = 360
    static let minWindowHeight: CGFloat = 340
    static let defaultWindowWidth: CGFloat = 520
    static let defaultWindowHeight: CGFloat = 430
    static let slotRowStride: CGFloat = 42
    static let addSlotControlHeight: CGFloat = 30

    static var fixedWindowChromeHeight: CGFloat {
        defaultWindowHeight - CGFloat(defaultActiveSlots) * slotRowStride
    }

    static func contentHeight(forActiveSlotCount count: Int, showsSlotControls: Bool) -> CGFloat {
        let slots = max(defaultActiveSlots, count)
        var height = fixedWindowChromeHeight + CGFloat(slots) * slotRowStride
        if showsSlotControls {
            height += addSlotControlHeight + 8
        }
        return max(minWindowHeight, height)
    }

    static let supportedDropExtensions: Set<String> = [
        "wav", "aif", "aiff", "caf", "mp3", "m4a", "mp4", "aac", "ogg", "flac"
    ]

    static func isSupportedAudioFile(_ path: String) -> Bool {
        let ext = (path as NSString).pathExtension.lowercased()
        return supportedDropExtensions.contains(ext)
    }
}
