import AppKit
import ApplicationServices
import Foundation

enum ConsolidateFallbackService {
    static func triggerConsolidateSelection(helper: PTSLHelperClient = .shared) async throws {
        do {
            try await helper.consolidateClip()
        } catch {
            if shouldFallbackToAccessibility(error) {
                try await triggerConsolidateViaAccessibility()
                return
            }
            throw error
        }
    }

    static func readSessionAudioFileSnapshot(sessionPath: String) throws -> SessionAudioFileSnapshot {
        let audioFilesDirectory = try resolveAudioFilesDirectory(from: sessionPath)
        let entries = try FileManager.default.contentsOfDirectory(atPath: audioFilesDirectory)
        var files: [SessionAudioFileEntry] = []

        for entry in entries {
            let filePath = (audioFilesDirectory as NSString).appendingPathComponent(entry)
            var isDirectory: ObjCBool = false
            guard FileManager.default.fileExists(atPath: filePath, isDirectory: &isDirectory), !isDirectory.boolValue else {
                continue
            }
            guard isSamplerAudioFileExtension(filePath) else { continue }

            let attributes = try FileManager.default.attributesOfItem(atPath: filePath)
            let size = (attributes[.size] as? NSNumber)?.int64Value ?? 0
            let mtime = (attributes[.modificationDate] as? Date)?.timeIntervalSince1970 ?? 0
            files.append(SessionAudioFileEntry(filePath: filePath, fileName: entry, size: size, mtimeMs: mtime * 1000))
        }

        return SessionAudioFileSnapshot(audioFilesDirectory: audioFilesDirectory, files: files)
    }

    static func waitForNewConsolidatedAudioFileInSessionDirectory(
        sessionPath: String,
        audioFileSnapshot: SessionAudioFileSnapshot?,
        previousSelectedFile: PTSelectedClipFile?,
        timeoutSeconds: TimeInterval = PTSLCaptureConstants.consolidateTimeoutOldPTSeconds
    ) async throws -> SamplerCaptureSource {
        guard !sessionPath.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            throw NSError(domain: "SimplestSampler", code: 1, userInfo: [NSLocalizedDescriptionKey: "The current Pro Tools session did not return a valid path."])
        }

        let deadline = Date().addingTimeInterval(max(1, timeoutSeconds))
        while Date() < deadline {
            try await Task.sleep(nanoseconds: UInt64(PTSLCaptureConstants.consolidatePollIntervalSeconds * 1_000_000_000))
            let currentSnapshot = try readSessionAudioFileSnapshot(sessionPath: sessionPath)
            if let candidate = pickConsolidatedAudioFile(from: currentSnapshot, previousSnapshot: audioFileSnapshot, previousSelectedFile: previousSelectedFile),
               FileManager.default.fileExists(atPath: candidate.filePath) {
                return candidate
            }
        }

        throw NSError(domain: "SimplestSampler", code: 2, userInfo: [NSLocalizedDescriptionKey: "Timed out waiting for Pro Tools to write the consolidated audio file."])
    }

    static func waitForNewConsolidatedPtClipFile(
        helper: PTSLHelperClient = .shared,
        previousSelectedFile: PTSelectedClipFile?,
        sessionPath: String,
        audioFileSnapshot: SessionAudioFileSnapshot?,
        allowAudioDirectoryFallback: Bool,
        timeoutSeconds: TimeInterval = PTSLCaptureConstants.consolidateTimeoutSeconds
    ) async throws -> SamplerCaptureSource {
        let deadline = Date().addingTimeInterval(max(1, timeoutSeconds))
        var lastSelectionError: Error?

        while Date() < deadline {
            try await Task.sleep(nanoseconds: UInt64(PTSLCaptureConstants.consolidatePollIntervalSeconds * 1_000_000_000))
            do {
                if let nextSelected = try await helper.getSelectedClipFileOrNull(
                    timeoutSeconds: PTSLCaptureConstants.helperPollTimeoutSeconds
                ) {
                    guard FileManager.default.fileExists(atPath: nextSelected.filePath) else { continue }
                    if let previousSelectedFile, areSelectedClipFilesEquivalent(previousSelectedFile, nextSelected) {
                        continue
                    }
                    return captureSource(from: nextSelected, source: "pt-consolidate-selected-clip")
                }
            } catch {
                lastSelectionError = error
            }

            if allowAudioDirectoryFallback, !sessionPath.isEmpty {
                if let currentSnapshot = try? readSessionAudioFileSnapshot(sessionPath: sessionPath),
                   let candidate = pickConsolidatedAudioFile(from: currentSnapshot, previousSnapshot: audioFileSnapshot, previousSelectedFile: previousSelectedFile),
                   FileManager.default.fileExists(atPath: candidate.filePath) {
                    return candidate
                }
            }
        }

        if let lastSelectionError {
            throw lastSelectionError
        }
        throw NSError(domain: "SimplestSampler", code: 3, userInfo: [NSLocalizedDescriptionKey: "Timed out waiting for Pro Tools to finish consolidating a new clip file."])
    }

    static func areSelectedClipFilesEquivalent(_ left: PTSelectedClipFile, _ right: PTSelectedClipFile) -> Bool {
        let leftPath = left.filePath.trimmingCharacters(in: .whitespacesAndNewlines)
        let rightPath = right.filePath.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !leftPath.isEmpty, leftPath == rightPath else { return false }

        let leftId = left.fileId.trimmingCharacters(in: .whitespacesAndNewlines)
        let rightId = right.fileId.trimmingCharacters(in: .whitespacesAndNewlines)
        if !leftId.isEmpty, !rightId.isEmpty, leftId != rightId { return false }

        let leftName = left.clipName.trimmingCharacters(in: .whitespacesAndNewlines)
        let rightName = right.clipName.trimmingCharacters(in: .whitespacesAndNewlines)
        if !leftName.isEmpty, !rightName.isEmpty, leftName != rightName { return false }

        return true
    }

    private static func shouldFallbackToAccessibility(_ error: Error) -> Bool {
        let message = error.localizedDescription.lowercased()
        return message.contains("unknown")
            || message.contains("unsupported")
            || message.contains("not implemented")
            || message.contains("invalid command")
            || message.contains("consolidateclip")
    }

    private static func triggerConsolidateViaAccessibility() async throws {
        guard AXIsProcessTrusted() else {
            throw NSError(
                domain: "SimplestSampler",
                code: 4,
                userInfo: [NSLocalizedDescriptionKey: "SimplestSampler needs Accessibility permission to trigger Pro Tools Consolidate Clip."]
            )
        }

        let repeatCount = max(
            1,
            Int(ceil(PTSLCaptureConstants.consolidateFrontmostTimeoutSeconds / PTSLCaptureConstants.consolidateFrontmostPollSeconds))
        )
        let delaySeconds = PTSLCaptureConstants.consolidateFrontmostPollSeconds

        let script = """
        tell application "Pro Tools" to activate
        tell application "System Events"
            if not (exists process "Pro Tools") then error "Pro Tools is not running."
            tell process "Pro Tools"
                set frontmost to true
                set didBecomeFrontmost to false
                repeat \(repeatCount) times
                    if frontmost then
                        set didBecomeFrontmost to true
                        exit repeat
                    end if
                    delay \(delaySeconds)
                end repeat
                if not didBecomeFrontmost then error "Pro Tools did not become frontmost in time for consolidate."
                key code 20 using {shift down, option down}
            end tell
        end tell
        """

        try await runAppleScript(script)
    }

    private static func runAppleScript(_ source: String) async throws {
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
            DispatchQueue.global(qos: .userInitiated).async {
                var error: NSDictionary?
                let script = NSAppleScript(source: source)
                script?.executeAndReturnError(&error)
                if let error {
                    let message = (error[NSAppleScript.errorMessage] as? String) ?? "AppleScript failed."
                    continuation.resume(throwing: NSError(domain: "SimplestSampler", code: 5, userInfo: [NSLocalizedDescriptionKey: message]))
                } else {
                    continuation.resume()
                }
            }
        }
    }

    private static func resolveAudioFilesDirectory(from sessionPath: String) throws -> String {
        let normalizedPath = sessionPath.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !normalizedPath.isEmpty else {
            throw NSError(domain: "SimplestSampler", code: 6, userInfo: [NSLocalizedDescriptionKey: "The current Pro Tools session did not return a valid path."])
        }

        var candidates: [String] = []
        let ext = (normalizedPath as NSString).pathExtension
        if !ext.isEmpty {
            candidates.append((normalizedPath as NSString).deletingLastPathComponent.appending("/Audio Files"))
        }
        candidates.append((normalizedPath as NSString).appendingPathComponent("Audio Files"))

        let uniqueCandidates = Array(Set(candidates.map { ($0 as NSString).standardizingPath }))
        for candidate in uniqueCandidates {
            var isDirectory: ObjCBool = false
            if FileManager.default.fileExists(atPath: candidate, isDirectory: &isDirectory), isDirectory.boolValue {
                return candidate
            }
        }

        throw NSError(domain: "SimplestSampler", code: 7, userInfo: [NSLocalizedDescriptionKey: "Could not find the session Audio Files folder."])
    }

    private static func isSamplerAudioFileExtension(_ filePath: String) -> Bool {
        let ext = (filePath as NSString).pathExtension.lowercased()
        return ext == "wav" || ext == "aif" || ext == "aiff" || ext == "caf"
    }

    private static func pickConsolidatedAudioFile(
        from currentSnapshot: SessionAudioFileSnapshot,
        previousSnapshot: SessionAudioFileSnapshot?,
        previousSelectedFile: PTSelectedClipFile?
    ) -> SamplerCaptureSource? {
        let previousByPath = Dictionary(uniqueKeysWithValues: (previousSnapshot?.files ?? []).map { ($0.filePath, $0) })
        let previousSelectedPath = previousSelectedFile?.filePath.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""

        let candidates = currentSnapshot.files.filter { entry in
            let filePath = entry.filePath.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !filePath.isEmpty, filePath != previousSelectedPath, entry.size > 0 else { return false }

            let previousEntry = previousByPath[filePath]
            let isNewFile = previousEntry == nil
            let isChangedFile = previousEntry.map {
                $0.size != entry.size || abs($0.mtimeMs - entry.mtimeMs) > 1
            } ?? false
            return isNewFile || isChangedFile
        }.sorted { left, right in
            if left.mtimeMs != right.mtimeMs { return left.mtimeMs > right.mtimeMs }
            return left.fileName.localizedCompare(right.fileName) == .orderedDescending
        }

        guard let newest = candidates.first else { return nil }
        let clipName = (newest.fileName as NSString).deletingPathExtension
        return SamplerCaptureSource(
            filePath: newest.filePath,
            fileName: newest.fileName,
            clipName: clipName,
            defaultDisplayName: clipName,
            sourceIdentity: newest.filePath,
            managedByApp: false,
            source: "pt-consolidate-audio-files"
        )
    }

    private static func captureSource(from clip: PTSelectedClipFile, source: String) -> SamplerCaptureSource {
        SamplerCaptureSource(
            filePath: clip.filePath,
            fileName: clip.fileName,
            clipName: clip.clipName,
            defaultDisplayName: clip.clipName.isEmpty ? ((clip.fileName as NSString).deletingPathExtension) : clip.clipName,
            sourceIdentity: clip.filePath,
            managedByApp: false,
            source: source
        )
    }
}
