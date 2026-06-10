import Darwin
import Foundation

enum PTSLHelperError: LocalizedError {
    case helperMissing
    case helperLaunchFailed(String)
    case helperCommandFailed(String)
    case helperTimedOut(String)

    var errorDescription: String? {
        switch self {
        case .helperMissing:
            return "The Pro Tools connection helper was not found. Build the native helper and try again."
        case .helperLaunchFailed(let message):
            return message
        case .helperCommandFailed(let message):
            return message
        case .helperTimedOut(let message):
            return message
        }
    }
}

final class PTSLHelperClient: @unchecked Sendable {
    static let shared = PTSLHelperClient()

    private init() {}

    func ping() async throws {
        _ = try await run(args: ["--ping"], timeoutSeconds: 8)
    }

    func getActiveProtocol() async throws -> String {
        let result = try await run(args: ["--get-active-protocol"])
        return try parseProtocolOutput(result.stdout)
    }

    func getSessionPath() async throws -> String {
        let result = try await run(args: ["--get-session-path"])
        return try parseSessionPathOutput(result.stdout)
    }

    func getTimelineSelection() async throws -> PTTimelineSelection {
        let result = try await run(args: ["--get-timeline-selection"])
        return try parseTimelineSelectionOutput(result.stdout)
    }

    func getSelectedClipFile(helperErrorTitle: String = "Sampler capture error") async throws -> PTSelectedClipFile {
        let result = try await run(args: ["--get-selected-clip-file"], helperErrorTitle: helperErrorTitle)
        return try parseSelectedClipFileOutput(result.stdout)
    }

    func getSelectedClipFileOrNull(
        helperErrorTitle: String = "Sampler capture error",
        timeoutSeconds: TimeInterval = PTSLCaptureConstants.defaultTimeoutSeconds
    ) async throws -> PTSelectedClipFile? {
        do {
            return try await getSelectedClipFile(helperErrorTitle: helperErrorTitle, timeoutSeconds: timeoutSeconds)
        } catch {
            let message = error.localizedDescription.lowercased()
            if message.contains("could not resolve a file for the selected pro tools clip")
                || message.contains("returned no selected clip file path") {
                return nil
            }
            throw error
        }
    }

    func getSelectedClipSegments(helperErrorTitle: String = "Sampler capture error") async throws -> PTClipSegmentsPayload {
        let result = try await run(args: ["--get-selected-clip-segments"], helperErrorTitle: helperErrorTitle)
        return try parseSelectedClipSegmentsOutput(result.stdout)
    }

    func getSelectedClipSegmentsForSamplerOrNull(helperErrorTitle: String = "Sampler capture error") async throws -> PTClipSegmentsPayload? {
        do {
            let payload = try await getSelectedClipSegments(helperErrorTitle: helperErrorTitle)
            return payload.segments.isEmpty ? nil : payload
        } catch {
            let message = error.localizedDescription.lowercased()
            if message.contains("select one pro tools track")
                || message.contains("select exactly one pro tools track")
                || message.contains("set or select a clip range")
                || message.contains("no selected clips were found")
                || message.contains("select the clips on one track first")
                || message.contains("link track and edit selection") {
                return nil
            }
            throw error
        }
    }

    func consolidateClip(helperErrorTitle: String = "Pro Tools consolidate error") async throws {
        _ = try await run(args: ["--consolidate-clip"], helperErrorTitle: helperErrorTitle)
    }

    func resolveHelperPath() throws -> String {
        if let bundled = bundledHelperPath(), FileManager.default.fileExists(atPath: bundled) {
            return bundled
        }

        let devPath = developmentHelperPath()
        if FileManager.default.fileExists(atPath: devPath) {
            return devPath
        }

        throw PTSLHelperError.helperMissing
    }

    private struct HelperResult {
        var stdout: String
        var stderr: String
    }

    private func bundledHelperPath() -> String? {
        let resourceURL = Bundle.main.resourceURL?.appendingPathComponent("bin/ptsl_markers_helper")
        return resourceURL?.path
    }

    private func developmentHelperPath() -> String {
        let arch = ProcessInfo.processInfo.machineHardwareName
        let repoRoot = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
        return repoRoot.appendingPathComponent("bin/mac-\(arch)/ptsl_markers_helper").path
    }

    private func getSelectedClipFile(
        helperErrorTitle: String,
        timeoutSeconds: TimeInterval
    ) async throws -> PTSelectedClipFile {
        let result = try await run(
            args: ["--get-selected-clip-file"],
            helperErrorTitle: helperErrorTitle,
            timeoutSeconds: timeoutSeconds
        )
        return try parseSelectedClipFileOutput(result.stdout)
    }

    private func run(
        args: [String],
        helperErrorTitle: String = "Native helper error",
        timeoutSeconds: TimeInterval = PTSLCaptureConstants.defaultTimeoutSeconds
    ) async throws -> HelperResult {
        let helperPath = try resolveHelperPath()
        CaptureDebugLogger.log("helper command start", context: ["args": args.joined(separator: " ")])

        return try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async {
                do {
                    let result = try self.runProcess(
                        executablePath: helperPath,
                        args: args,
                        timeoutSeconds: timeoutSeconds,
                        helperErrorTitle: helperErrorTitle
                    )
                    CaptureDebugLogger.log("helper command success", context: ["args": args.first ?? ""])
                    continuation.resume(returning: result)
                } catch {
                    CaptureDebugLogger.log("helper command failed", context: ["message": error.localizedDescription])
                    continuation.resume(throwing: error)
                }
            }
        }
    }

    private func runProcess(
        executablePath: String,
        args: [String],
        timeoutSeconds: TimeInterval,
        helperErrorTitle: String
    ) throws -> HelperResult {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: executablePath)
        process.arguments = args

        let stdoutPipe = Pipe()
        let stderrPipe = Pipe()
        process.standardOutput = stdoutPipe
        process.standardError = stderrPipe

        do {
            try process.run()
        } catch {
            throw PTSLHelperError.helperLaunchFailed(error.localizedDescription)
        }

        let pid = process.processIdentifier
        let exitSemaphore = DispatchSemaphore(value: 0)
        var terminationStatus: Int32 = -1
        var timedOut = false

        process.terminationHandler = { proc in
            terminationStatus = proc.terminationStatus
            exitSemaphore.signal()
        }

        var stdoutData = Data()
        var stderrData = Data()

        stdoutPipe.fileHandleForReading.readabilityHandler = { handle in
            let chunk = handle.availableData
            if !chunk.isEmpty {
                stdoutData.append(chunk)
            }
        }
        stderrPipe.fileHandleForReading.readabilityHandler = { handle in
            let chunk = handle.availableData
            if !chunk.isEmpty {
                stderrData.append(chunk)
            }
        }

        let killTimer = DispatchSource.makeTimerSource(queue: DispatchQueue.global(qos: .userInitiated))
        killTimer.schedule(deadline: .now() + max(0.25, timeoutSeconds))
        killTimer.setEventHandler {
            guard process.isRunning else { return }
            timedOut = true
            kill(pid, SIGTERM)
            DispatchQueue.global().asyncAfter(deadline: .now() + 1.5) {
                if process.isRunning {
                    kill(pid, SIGKILL)
                }
            }
        }
        killTimer.resume()

        let maxWaitSeconds = max(0.25, timeoutSeconds) + 3.0
        let waitResult = exitSemaphore.wait(timeout: .now() + maxWaitSeconds)
        killTimer.cancel()

        stdoutPipe.fileHandleForReading.readabilityHandler = nil
        stderrPipe.fileHandleForReading.readabilityHandler = nil

        if waitResult == .timedOut, process.isRunning {
            timedOut = true
            kill(pid, SIGKILL)
            _ = exitSemaphore.wait(timeout: .now() + 2)
        }

        stdoutData.append(stdoutPipe.fileHandleForReading.readDataToEndOfFile())
        stderrData.append(stderrPipe.fileHandleForReading.readDataToEndOfFile())
        let stdout = String(data: stdoutData, encoding: .utf8) ?? ""
        let stderr = String(data: stderrData, encoding: .utf8) ?? ""

        guard terminationStatus == 0 else {
            let commandName = args.first?.replacingOccurrences(of: "--", with: "") ?? "helper command"
            let failureMessage: String
            if timedOut {
                failureMessage = "Timed out waiting for helper response to \(commandName)."
            } else if !stderr.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                failureMessage = String(stderr.suffix(800))
            } else {
                failureMessage = "Helper exited with code \(terminationStatus)."
            }
            throw PTSLHelperError.helperCommandFailed("\(helperErrorTitle): \(failureMessage)")
        }

        return HelperResult(stdout: stdout, stderr: stderr)
    }

    private func parseProtocolOutput(_ stdout: String) throws -> String {
        guard let data = stdout.data(using: .utf8),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw PTSLHelperError.helperCommandFailed("Could not confirm the Pro Tools automation connection.")
        }
        let protocolString = (json["protocol"] as? String)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        guard !protocolString.isEmpty else {
            throw PTSLHelperError.helperCommandFailed("Could not confirm the Pro Tools automation connection.")
        }
        return protocolString
    }

    private func parseSessionPathOutput(_ stdout: String) throws -> String {
        guard let data = stdout.data(using: .utf8),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw PTSLHelperError.helperCommandFailed("Could not read the current Pro Tools session path.")
        }
        let sessionPath = ((json["session_path"] as? String) ?? (json["sessionPath"] as? String))?
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        guard !sessionPath.isEmpty else {
            throw PTSLHelperError.helperCommandFailed("Could not read the current Pro Tools session path.")
        }
        return sessionPath
    }

    private func parseTimelineSelectionOutput(_ stdout: String) throws -> PTTimelineSelection {
        guard let data = stdout.data(using: .utf8),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw PTSLHelperError.helperCommandFailed("Could not read the Pro Tools timeline selection.")
        }

        let playStartMarkerTime = PTTimecodeMath.normalizeHelperTimecode(
            (json["play_start_marker_time"] as? String) ?? (json["playStartMarkerTime"] as? String) ?? ""
        )
        let inTime = PTTimecodeMath.normalizeHelperTimecode(
            (json["in_time"] as? String) ?? (json["inTime"] as? String) ?? ""
        )
        let outTime = PTTimecodeMath.normalizeHelperTimecode(
            (json["out_time"] as? String) ?? (json["outTime"] as? String) ?? ""
        )
        let preRollStartTime = PTTimecodeMath.normalizeHelperTimecode(
            (json["pre_roll_start_time"] as? String) ?? (json["preRollStartTime"] as? String) ?? ""
        )
        let postRollStopTime = PTTimecodeMath.normalizeHelperTimecode(
            (json["post_roll_stop_time"] as? String) ?? (json["postRollStopTime"] as? String) ?? ""
        )

        let preRollEnabled = (json["pre_roll_enabled"] as? Bool) ?? (json["preRollEnabled"] as? Bool) ?? false
        let postRollEnabled = (json["post_roll_enabled"] as? Bool) ?? (json["postRollEnabled"] as? Bool) ?? false
        let hasSelection = (json["has_selection"] as? Bool) ?? (json["hasSelection"] as? Bool)
            ?? (!inTime.isEmpty && !outTime.isEmpty && inTime != outTime)
        let sessionFps = doubleValue(json["session_fps"] ?? json["sessionFps"])
        let sessionFeetFramesFps = doubleValue(json["session_feet_frames_fps"] ?? json["sessionFeetFramesFps"])

        guard !playStartMarkerTime.isEmpty || !inTime.isEmpty else {
            throw PTSLHelperError.helperCommandFailed("Could not read the Pro Tools timeline selection.")
        }

        return PTTimelineSelection(
            playStartMarkerTime: playStartMarkerTime.isEmpty ? inTime : playStartMarkerTime,
            inTime: inTime.isEmpty ? playStartMarkerTime : inTime,
            outTime: outTime.isEmpty ? inTime : outTime,
            preRollStartTime: preRollStartTime,
            postRollStopTime: postRollStopTime,
            preRollEnabled: preRollEnabled,
            postRollEnabled: postRollEnabled,
            hasSelection: hasSelection,
            sessionFps: sessionFps,
            sessionFeetFramesFps: sessionFeetFramesFps
        )
    }

    private func parseSelectedClipFileOutput(_ stdout: String) throws -> PTSelectedClipFile {
        guard let data = stdout.data(using: .utf8),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw PTSLHelperError.helperCommandFailed("Could not read the selected Pro Tools clip file.")
        }

        let filePath = ((json["file_path"] as? String) ?? (json["filePath"] as? String))?
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        guard !filePath.isEmpty else {
            throw PTSLHelperError.helperCommandFailed("Could not find the audio file for the selected Pro Tools clip.")
        }

        let fileName = ((json["file_name"] as? String) ?? (json["fileName"] as? String))?
            .trimmingCharacters(in: .whitespacesAndNewlines)
        let resolvedFileName = fileName?.isEmpty == false ? fileName! : URL(fileURLWithPath: filePath).lastPathComponent

        return PTSelectedClipFile(
            filePath: filePath,
            fileName: resolvedFileName,
            fileId: stringValue(json["file_id"] ?? json["fileId"]),
            clipName: stringValue(json["clip_name"] ?? json["clipName"]),
            isOnline: (json["is_online"] as? Bool) ?? (json["isOnline"] as? Bool) ?? false,
            clipStartTime: PTTimecodeMath.normalizeHelperTimecode(
                (json["clip_start_time"] as? String) ?? (json["clipStartTime"] as? String) ?? ""
            ),
            srcStartSeconds: doubleValue(json["src_start_seconds"] ?? json["srcStartSeconds"]),
            clipId: stringValue(json["clip_id"] ?? json["clipId"]),
            sampleRateHz: doubleValue(json["sample_rate_hz"] ?? json["sampleRateHz"]),
            sessionFps: doubleValue(json["session_fps"] ?? json["sessionFps"])
        )
    }

    private func parseSelectedClipSegmentsOutput(_ stdout: String) throws -> PTClipSegmentsPayload {
        guard let data = stdout.data(using: .utf8),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw PTSLHelperError.helperCommandFailed("Could not read the selected Pro Tools clip range.")
        }

        let sessionFps = doubleValue(json["session_fps"] ?? json["sessionFps"])
        let sampleRateHz = doubleValue(json["sample_rate_hz"] ?? json["sampleRateHz"])
        let rawSegments = json["segments"] as? [[String: Any]] ?? []
        let segments: [PTClipSegment] = rawSegments.compactMap { segment in
            let filePath = stringValue(segment["file_path"] ?? segment["filePath"])
            let segmentStartTime = PTTimecodeMath.normalizeHelperTimecode(
                (segment["segment_start_time"] as? String) ?? (segment["segmentStartTime"] as? String) ?? ""
            )
            let segmentEndTime = PTTimecodeMath.normalizeHelperTimecode(
                (segment["segment_end_time"] as? String) ?? (segment["segmentEndTime"] as? String) ?? ""
            )
            guard !filePath.isEmpty, !segmentStartTime.isEmpty, !segmentEndTime.isEmpty else {
                return nil
            }

            let fileName = stringValue(segment["file_name"] ?? segment["fileName"])
            return PTClipSegment(
                filePath: filePath,
                fileName: fileName.isEmpty ? URL(fileURLWithPath: filePath).lastPathComponent : fileName,
                fileId: stringValue(segment["file_id"] ?? segment["fileId"]),
                clipName: stringValue(segment["clip_name"] ?? segment["clipName"]),
                clipId: stringValue(segment["clip_id"] ?? segment["clipId"]),
                resolutionSource: stringValue(segment["resolution_source"] ?? segment["resolutionSource"]),
                clipStartTime: PTTimecodeMath.normalizeHelperTimecode(
                    (segment["clip_start_time"] as? String) ?? (segment["clipStartTime"] as? String) ?? ""
                ),
                segmentStartTime: segmentStartTime,
                segmentEndTime: segmentEndTime,
                srcStartSeconds: doubleValue(segment["src_start_seconds"] ?? segment["srcStartSeconds"]),
                sourceStartSeconds: doubleValue(segment["source_start_seconds"] ?? segment["sourceStartSeconds"]),
                sourceEndSeconds: doubleValue(segment["source_end_seconds"] ?? segment["sourceEndSeconds"]),
                isOnline: (segment["is_online"] as? Bool) ?? (segment["isOnline"] as? Bool) ?? false,
                sampleRateHz: sampleRateHz,
                sessionFps: sessionFps
            )
        }

        return PTClipSegmentsPayload(
            trackName: stringValue(json["track_name"] ?? json["trackName"]),
            sessionFps: sessionFps,
            sampleRateHz: sampleRateHz,
            segments: segments
        )
    }

    private func stringValue(_ value: Any?) -> String {
        String(describing: value ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private func doubleValue(_ value: Any?) -> Double? {
        if let number = value as? Double, number.isFinite { return number }
        if let number = value as? Int { return Double(number) }
        if let number = value as? NSNumber { return number.doubleValue }
        return nil
    }
}

private extension ProcessInfo {
    var machineHardwareName: String {
        var sysinfo = utsname()
        uname(&sysinfo)
        return withUnsafePointer(to: &sysinfo.machine) {
            $0.withMemoryRebound(to: CChar.self, capacity: 1) {
                String(validatingUTF8: $0) ?? "arm64"
            }
        }
    }
}
