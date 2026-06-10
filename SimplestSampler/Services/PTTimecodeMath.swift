import Foundation

enum PTTimecodeMath {
    static func normalizeHelperTimecode(_ value: String) -> String {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return "" }

        let pattern = #"^(\d{1,2}):(\d{1,2}):(\d{1,2})[:;](\d{1,2})(?:[.;]\d+)?$"#
        guard let regex = try? NSRegularExpression(pattern: pattern),
              let match = regex.firstMatch(in: trimmed, range: NSRange(trimmed.startIndex..., in: trimmed)),
              match.numberOfRanges == 5 else {
            return trimmed
        }

        return (1..<5).compactMap { index -> String? in
            guard let range = Range(match.range(at: index), in: trimmed) else { return nil }
            return String(trimmed[range]).padding(toLength: 2, withPad: "0", startingAt: 0)
        }.joined(separator: ":")
    }

    static func timecodeStringToFrameCount(_ timecode: String, fps: Double) -> Int? {
        let parts = normalizeHelperTimecode(timecode)
            .split(separator: ":")
            .compactMap { Int($0) }
        guard parts.count == 4 else { return nil }

        let hours = parts[0]
        let minutes = parts[1]
        let seconds = parts[2]
        let frames = parts[3]
        let totalSeconds = seconds + (minutes + hours * 60) * 60
        let timecodeBaseFps = resolveTimecodeBaseFps(fps)
        return frames + totalSeconds * timecodeBaseFps
    }

    static func resolveTimecodeBaseFps(_ fps: Double) -> Int {
        guard fps.isFinite, fps > 0 else { return 24 }
        if abs(fps - 23.976023976) < 0.02 || abs(fps - 24) < 0.02 { return 24 }
        if abs(fps - 29.97002997) < 0.02 || abs(fps - 30) < 0.02 { return 30 }
        if abs(fps - 47.952047952) < 0.05 || abs(fps - 48) < 0.05 { return 48 }
        if abs(fps - 59.94005994) < 0.05 || abs(fps - 60) < 0.05 { return 60 }
        if abs(fps - 119.88011988) < 0.1 || abs(fps - 120) < 0.1 { return 120 }
        return max(1, Int(fps.rounded()))
    }

    static func parsePtslProtocolVersion(_ protocolString: String) -> ParsedPtslProtocol? {
        let trimmed = protocolString.trimmingCharacters(in: .whitespacesAndNewlines)
        let pattern = #"^\s*(\d+)\.(\d+)(?:\.(\d+))?"#
        guard let regex = try? NSRegularExpression(pattern: pattern),
              let match = regex.firstMatch(in: trimmed, range: NSRange(trimmed.startIndex..., in: trimmed)),
              match.numberOfRanges >= 3 else {
            return nil
        }

        func intValue(at index: Int, default defaultValue: Int = 0) -> Int? {
            guard let range = Range(match.range(at: index), in: trimmed) else { return defaultValue }
            return Int(trimmed[range])
        }

        guard let major = intValue(at: 1), let minor = intValue(at: 2) else { return nil }
        let revision = intValue(at: 3) ?? 0
        return ParsedPtslProtocol(major: major, minor: minor, revision: revision)
    }

    static func isLegacyPre25Protocol(_ protocolString: String) -> Bool {
        guard let parsed = parsePtslProtocolVersion(protocolString) else { return false }
        return parsed.major < 25
    }

    static func shouldAvoidConsolidateFallback(_ protocolString: String) -> Bool {
        guard let parsed = parsePtslProtocolVersion(protocolString) else { return false }
        return parsed.major < 25
    }
}
