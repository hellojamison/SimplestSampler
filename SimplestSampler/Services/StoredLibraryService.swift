import Foundation

struct StoredLibraryMetadata: Codable {
    var captures: [SamplerCapture]
    var nextSequence: Int
}

enum StoredLibraryService {
    static let libraryFileName = ".simplestsampler-library.json"
    static let samplesDirectoryName = "Samples"

    static var applicationSupportURL: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        return base.appendingPathComponent("SimplestSampler", isDirectory: true)
    }

    static var samplesDirectoryURL: URL {
        applicationSupportURL.appendingPathComponent(samplesDirectoryName, isDirectory: true)
    }

    static var libraryFileURL: URL {
        samplesDirectoryURL.appendingPathComponent(libraryFileName)
    }

    static func ensureDirectories() throws {
        try FileManager.default.createDirectory(at: samplesDirectoryURL, withIntermediateDirectories: true)
    }

    static func loadLibrary() -> StoredLibraryMetadata {
        guard FileManager.default.fileExists(atPath: libraryFileURL.path),
              let data = try? Data(contentsOf: libraryFileURL),
              let decoded = try? JSONDecoder().decode(StoredLibraryMetadata.self, from: data) else {
            return StoredLibraryMetadata(captures: [], nextSequence: 1)
        }
        return decoded
    }

    static func saveLibrary(_ metadata: StoredLibraryMetadata) throws {
        try ensureDirectories()
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        let data = try encoder.encode(metadata)
        try data.write(to: libraryFileURL, options: .atomic)
    }

    static func storeCapture(from recent: SamplerCapture, displayNameOverride: String? = nil) throws -> SamplerCapture {
        try ensureDirectories()

        var metadata = loadLibrary()
        let baseName = buildStoredBaseName(recent, displayNameOverride: displayNameOverride)
        let hasTrimmedRange = recent.playbackStartSeconds != nil && recent.playbackEndSeconds != nil

        var targetURL: URL?
        var nextStart: Double? = recent.playbackStartSeconds
        var nextEnd: Double? = recent.playbackEndSeconds

        if hasTrimmedRange {
            let wavURL = uniqueTargetURL(baseName: baseName, extension: "wav")
            do {
                try renderTrimmedWaveFile(recent, targetURL: wavURL)
                targetURL = wavURL
                nextStart = nil
                nextEnd = nil
            } catch {
                targetURL = nil
            }
        }

        if targetURL == nil {
            let ext = (recent.fileName as NSString).pathExtension.isEmpty ? "wav" : (recent.fileName as NSString).pathExtension
            let copyURL = uniqueTargetURL(baseName: baseName, extension: ext)
            try copySourceFile(recent.filePath, to: copyURL)
            targetURL = copyURL
        }

        guard let finalURL = targetURL else {
            throw NSError(domain: "SimplestSampler", code: 1, userInfo: [NSLocalizedDescriptionKey: "Could not store sample."])
        }

        let storedID = "sampler-stored-\(metadata.nextSequence)"
        metadata.nextSequence += 1

        let stored = SamplerCapture(
            id: storedID,
            filePath: finalURL.path,
            fileName: finalURL.lastPathComponent,
            clipName: recent.clipName,
            displayName: recent.displayName,
            defaultDisplayName: recent.defaultDisplayName,
            hasCustomDisplayName: recent.hasCustomDisplayName,
            playbackStartSeconds: nextStart,
            playbackEndSeconds: nextEnd,
            capturedAt: Date().timeIntervalSince1970 * 1000,
            saved: true,
            sourceIdentity: recent.sourceIdentity,
            managedByApp: true
        )

        metadata.captures.append(stored)
        try saveLibrary(metadata)
        return stored
    }

    static func deleteStoredCapture(id: String) throws -> SamplerCapture? {
        var metadata = loadLibrary()
        guard let index = metadata.captures.firstIndex(where: { $0.id == id }) else {
            return nil
        }

        let removed = metadata.captures.remove(at: index)
        try saveLibrary(metadata)

        if removed.managedByApp {
            let path = removed.filePath
            let stillReferenced = metadata.captures.contains { $0.filePath == path }
            if !stillReferenced {
                try? FileManager.default.removeItem(atPath: path)
            }
        }

        return removed
    }

    static func renameStoredCapture(id: String, displayName: String, hasCustomDisplayName: Bool) throws {
        var metadata = loadLibrary()
        guard let index = metadata.captures.firstIndex(where: { $0.id == id }) else { return }
        metadata.captures[index].displayName = displayName
        metadata.captures[index].hasCustomDisplayName = hasCustomDisplayName
        try saveLibrary(metadata)
    }

    private static func buildStoredBaseName(_ capture: SamplerCapture, displayNameOverride: String?) -> String {
        let raw = (displayNameOverride ?? capture.slotLabel)
            .trimmingCharacters(in: .whitespacesAndNewlines)
        let sanitized = raw
            .replacingOccurrences(of: #"[^\w\-. ]+"#, with: "-", options: .regularExpression)
            .trimmingCharacters(in: .whitespacesAndNewlines)
        return sanitized.isEmpty ? "stored-sample" : sanitized
    }

    private static func uniqueTargetURL(baseName: String, extension ext: String) -> URL {
        var candidate = samplesDirectoryURL.appendingPathComponent("\(baseName).\(ext)")
        var counter = 1
        while FileManager.default.fileExists(atPath: candidate.path) {
            candidate = samplesDirectoryURL.appendingPathComponent("\(baseName)-\(counter).\(ext)")
            counter += 1
        }
        return candidate
    }

    private static func copySourceFile(_ sourcePath: String, to targetURL: URL) throws {
        let sourceURL = URL(fileURLWithPath: sourcePath)
        if FileManager.default.fileExists(atPath: targetURL.path) {
            try FileManager.default.removeItem(at: targetURL)
        }
        try FileManager.default.copyItem(at: sourceURL, to: targetURL)
    }

    private static func renderTrimmedWaveFile(_ capture: SamplerCapture, targetURL: URL) throws {
        guard let start = capture.playbackStartSeconds, let end = capture.playbackEndSeconds, end > start else {
            throw NSError(domain: "SimplestSampler", code: 2, userInfo: nil)
        }

        let duration = end - start
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/afconvert")
        process.arguments = [
            "-f", "WAVE",
            "-d", "LEI16",
            "-ss", String(start),
            "-t", String(duration),
            capture.filePath,
            targetURL.path
        ]

        let pipe = Pipe()
        process.standardError = pipe
        try process.run()
        process.waitUntilExit()

        guard process.terminationStatus == 0, FileManager.default.fileExists(atPath: targetURL.path) else {
            try? FileManager.default.removeItem(at: targetURL)
            throw NSError(domain: "SimplestSampler", code: 3, userInfo: [NSLocalizedDescriptionKey: "afconvert failed"])
        }
    }
}
