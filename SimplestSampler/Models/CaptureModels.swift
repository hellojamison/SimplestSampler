import Foundation

struct SamplerCaptureSource: Sendable {
    var filePath: String
    var fileName: String
    var clipName: String
    var defaultDisplayName: String
    var playbackStartSeconds: Double?
    var playbackEndSeconds: Double?
    var sourceIdentity: String
    var managedByApp: Bool
    var source: String

    init(
        filePath: String,
        fileName: String = "",
        clipName: String = "",
        defaultDisplayName: String = "",
        playbackStartSeconds: Double? = nil,
        playbackEndSeconds: Double? = nil,
        sourceIdentity: String = "",
        managedByApp: Bool = false,
        source: String = "pt-direct"
    ) {
        self.filePath = filePath
        self.fileName = fileName.isEmpty ? URL(fileURLWithPath: filePath).lastPathComponent : fileName
        self.clipName = clipName
        self.defaultDisplayName = defaultDisplayName
        self.playbackStartSeconds = playbackStartSeconds
        self.playbackEndSeconds = playbackEndSeconds
        self.sourceIdentity = sourceIdentity
        self.managedByApp = managedByApp
        self.source = source
    }
}

struct PTSelectedClipFile: Sendable {
    var filePath: String
    var fileName: String
    var fileId: String
    var clipName: String
    var isOnline: Bool
    var clipStartTime: String
    var srcStartSeconds: Double?
    var clipId: String
    var sampleRateHz: Double?
    var sessionFps: Double?
}

struct PTClipSegment: Sendable {
    var filePath: String
    var fileName: String
    var fileId: String
    var clipName: String
    var clipId: String
    var resolutionSource: String
    var clipStartTime: String
    var segmentStartTime: String
    var segmentEndTime: String
    var srcStartSeconds: Double?
    var sourceStartSeconds: Double?
    var sourceEndSeconds: Double?
    var isOnline: Bool
    var sampleRateHz: Double?
    var sessionFps: Double?
}

struct PTClipSegmentsPayload: Sendable {
    var trackName: String
    var sessionFps: Double?
    var sampleRateHz: Double?
    var segments: [PTClipSegment]
}

struct PTTimelineSelection: Sendable {
    var playStartMarkerTime: String
    var inTime: String
    var outTime: String
    var preRollStartTime: String
    var postRollStopTime: String
    var preRollEnabled: Bool
    var postRollEnabled: Bool
    var hasSelection: Bool
    var sessionFps: Double?
    var sessionFeetFramesFps: Double?
}

struct PTSourceWindow: Sendable {
    var startSec: Double
    var endSec: Double
    var anchorFileSec: Double
}

struct PTMultichannelSegmentEntry: Sendable {
    var segment: PTClipSegment
    var index: Int
    var sourceWindow: PTSourceWindow
    var filePath: String
    var baseName: String
}

struct PTMultichannelPlan: Sendable {
    var baseName: String
    var channels: [PTMultichannelSegmentEntry]
}

struct SessionAudioFileEntry: Sendable {
    var filePath: String
    var fileName: String
    var size: Int64
    var mtimeMs: TimeInterval
}

struct SessionAudioFileSnapshot: Sendable {
    var audioFilesDirectory: String
    var files: [SessionAudioFileEntry]
}

struct ParsedPtslProtocol: Sendable {
    var major: Int
    var minor: Int
    var revision: Int

    var releaseMajor: Int {
        major >= 2000 && major < 2100 ? major - 2000 : major
    }
}

enum PTSLCaptureConstants {
    static let defaultTimeoutSeconds: TimeInterval = 30
    static let helperPollTimeoutSeconds: TimeInterval = 8
    static let captureMaxDurationSeconds: TimeInterval = 90
    static let consolidateTimeoutSeconds: TimeInterval = 8
    static let consolidateTimeoutOldPTSeconds: TimeInterval = 15
    static let consolidatePollIntervalSeconds: TimeInterval = 0.25
    static let consolidateTriggerSettleSeconds: TimeInterval = 0.45
    static let consolidateFrontmostTimeoutSeconds: TimeInterval = 3
    static let consolidateFrontmostPollSeconds: TimeInterval = 0.05
    static let generatedCapturesDirectoryName = "Generated Sampler Captures"
}

enum CaptureTimeout {
    static func run<T: Sendable>(
        seconds: TimeInterval,
        message: String,
        operation: @escaping @Sendable () async throws -> T
    ) async throws -> T {
        let timeoutNanoseconds = UInt64(max(1, seconds) * 1_000_000_000)
        return try await withThrowingTaskGroup(of: T.self) { group in
            group.addTask {
                try await operation()
            }
            group.addTask {
                try await Task.sleep(nanoseconds: timeoutNanoseconds)
                throw PTSLHelperError.helperTimedOut(message)
            }
            guard let result = try await group.next() else {
                throw PTSLHelperError.helperTimedOut(message)
            }
            group.cancelAll()
            return result
        }
    }
}

enum CaptureDebugLogger {
    private static let enabled = ProcessInfo.processInfo.environment["SIMPLESTSAMPLER_CAPTURE_DEBUG"] == "1"
    private static var ring: [String] = []
    private static let maxEntries = 200

    static func log(_ message: String, context: [String: Any] = [:]) {
        guard enabled else { return }
        let timestamp = ISO8601DateFormatter().string(from: Date())
        var line = "[\(timestamp)] \(message)"
        if !context.isEmpty {
            line += " \(context)"
        }
        ring.append(line)
        if ring.count > maxEntries {
            ring.removeFirst(ring.count - maxEntries)
        }
        fputs("\(line)\n", stderr)
    }
}
