import Foundation

enum MultichannelWaveRenderer {
    private static let trimFormat = "WAVE"
    private static let trimDataFormat = "LEI16"

    static var generatedCapturesDirectoryURL: URL {
        StoredLibraryService.applicationSupportURL
            .appendingPathComponent(PTSLCaptureConstants.generatedCapturesDirectoryName, isDirectory: true)
    }

    static func ensureGeneratedCapturesDirectory() throws {
        try FileManager.default.createDirectory(at: generatedCapturesDirectoryURL, withIntermediateDirectories: true)
    }

    static func resolveMultichannelPlan(_ segments: [PTClipSegment], sessionFps: Double?) -> PTMultichannelPlan? {
        let usableSegments: [PTMultichannelSegmentEntry] = segments.enumerated().compactMap { index, segment in
            let fps = sessionFps ?? segment.sessionFps
            guard let sourceWindow = ProToolsSourceWindowMath.resolveSegmentSourceWindow(segment, fps: fps) else {
                return nil
            }
            let filePath = segment.filePath.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !filePath.isEmpty else { return nil }
            return PTMultichannelSegmentEntry(
                segment: segment,
                index: index,
                sourceWindow: sourceWindow,
                filePath: filePath,
                baseName: normalizeMultichannelBaseName(segment)
            )
        }

        guard usableSegments.count >= 2, usableSegments.count <= 8 else { return nil }

        let firstSegment = usableSegments[0].segment
        let firstBaseName = usableSegments[0].baseName.trimmingCharacters(in: .whitespacesAndNewlines)
        let firstSegmentStartTime = firstSegment.segmentStartTime.trimmingCharacters(in: .whitespacesAndNewlines)
        let firstSegmentEndTime = firstSegment.segmentEndTime.trimmingCharacters(in: .whitespacesAndNewlines)
        let firstDuration = usableSegments[0].sourceWindow.endSec - usableSegments[0].sourceWindow.startSec
        var uniqueFilePaths = Set<String>()

        for entry in usableSegments {
            let segmentStartTime = entry.segment.segmentStartTime.trimmingCharacters(in: .whitespacesAndNewlines)
            let segmentEndTime = entry.segment.segmentEndTime.trimmingCharacters(in: .whitespacesAndNewlines)
            let duration = entry.sourceWindow.endSec - entry.sourceWindow.startSec
            let hasMatchingRange = segmentStartTime == firstSegmentStartTime && segmentEndTime == firstSegmentEndTime
            let hasMatchingDuration = abs(duration - firstDuration) <= 0.05
            let hasMatchingBaseName = firstBaseName.isEmpty || entry.baseName.isEmpty || entry.baseName == firstBaseName
            if !hasMatchingRange || !hasMatchingDuration || !hasMatchingBaseName {
                return nil
            }
            uniqueFilePaths.insert(entry.filePath)
        }

        guard uniqueFilePaths.count == usableSegments.count else { return nil }

        let sortedChannels = usableSegments.sorted { left, right in
            let leftOrder = channelSortOrder(for: left.segment, fallbackIndex: left.index)
            let rightOrder = channelSortOrder(for: right.segment, fallbackIndex: right.index)
            if leftOrder != rightOrder { return leftOrder < rightOrder }
            return left.index < right.index
        }

        let baseName = firstBaseName.isEmpty
            ? (normalizeMultichannelBaseName(sortedChannels[0].segment).isEmpty ? "Sampler Capture" : normalizeMultichannelBaseName(sortedChannels[0].segment))
            : firstBaseName

        return PTMultichannelPlan(baseName: baseName, channels: sortedChannels)
    }

    static func renderInterleavedWAV(plan: PTMultichannelPlan) throws -> SamplerCaptureSource {
        let channelBuffers = try plan.channels.map { entry in
            try convertSegmentToTrimmedPCM(segment: entry.segment, sourceWindow: entry.sourceWindow)
        }
        let interleaved = try interleaveMonoPCMBuffers(channelBuffers)
        try ensureGeneratedCapturesDirectory()

        let safeBaseName = sanitizeBaseName(plan.baseName)
        let fileName = "\(safeBaseName)-\(Int(Date().timeIntervalSince1970 * 1000))-\(randomHex(count: 4)).wav"
        let targetURL = generatedCapturesDirectoryURL.appendingPathComponent(fileName)
        try interleaved.write(to: targetURL, options: .atomic)

        return SamplerCaptureSource(
            filePath: targetURL.path,
            fileName: fileName,
            clipName: plan.baseName,
            defaultDisplayName: plan.baseName,
            sourceIdentity: buildMultichannelSourceIdentity(plan: plan),
            managedByApp: true,
            source: "pt-multichannel-segments"
        )
    }

    static func convertSegmentToTrimmedPCM(segment: PTClipSegment, sourceWindow: PTSourceWindow) throws -> Data {
        let sourcePath = segment.filePath.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !sourcePath.isEmpty else {
            throw NSError(domain: "SimplestSampler", code: 1, userInfo: [NSLocalizedDescriptionKey: "A sampler channel segment did not include a valid source file."])
        }

        let tempDirectory = FileManager.default.temporaryDirectory
            .appendingPathComponent("simplestsampler-channel-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: tempDirectory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: tempDirectory) }

        let tempWaveURL = tempDirectory.appendingPathComponent("source.wav")
        try runAfconvert(sourcePath: sourcePath, targetURL: tempWaveURL)
        let sourceWave = try Data(contentsOf: tempWaveURL)
        return try trimPCMWaveBuffer(sourceWave, startSeconds: sourceWindow.startSec, endSeconds: sourceWindow.endSec)
    }

    private static func runAfconvert(sourcePath: String, targetURL: URL) throws {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/afconvert")
        process.arguments = ["-f", trimFormat, "-d", trimDataFormat, sourcePath, targetURL.path]
        let stderrPipe = Pipe()
        process.standardError = stderrPipe
        try process.run()
        process.waitUntilExit()
        guard process.terminationStatus == 0, FileManager.default.fileExists(atPath: targetURL.path) else {
            throw NSError(domain: "SimplestSampler", code: 2, userInfo: [NSLocalizedDescriptionKey: "afconvert failed"])
        }
    }

    private static func trimPCMWaveBuffer(_ waveBuffer: Data, startSeconds: Double, endSeconds: Double) throws -> Data {
        let parsed = try parsePCMWaveBuffer(waveBuffer)
        let startFrame = max(0, min(parsed.frameCount, Int(floor(startSeconds * Double(parsed.sampleRate)))))
        let endFrame = max(0, min(parsed.frameCount, Int(ceil(endSeconds * Double(parsed.sampleRate)))))
        guard endFrame > startFrame else {
            throw NSError(domain: "SimplestSampler", code: 3, userInfo: [NSLocalizedDescriptionKey: "The selected sampler range did not produce any audio."])
        }

        let byteStart = parsed.dataOffset + startFrame * parsed.blockAlign
        let byteEnd = parsed.dataOffset + endFrame * parsed.blockAlign
        let sampleData = waveBuffer.subdata(in: byteStart..<byteEnd)
        return buildPCMWaveBuffer(
            channelCount: parsed.channelCount,
            sampleRate: parsed.sampleRate,
            bitsPerSample: parsed.bitsPerSample,
            sampleData: sampleData
        )
    }

    private struct ParsedPCMWave {
        var audioFormat: UInt16
        var channelCount: Int
        var sampleRate: Int
        var blockAlign: Int
        var bitsPerSample: Int
        var dataOffset: Int
        var frameCount: Int
    }

    private static func parsePCMWaveBuffer(_ waveBuffer: Data) throws -> ParsedPCMWave {
        guard waveBuffer.count >= 44 else {
            throw NSError(domain: "SimplestSampler", code: 4, userInfo: [NSLocalizedDescriptionKey: "The converted WAV data is invalid."])
        }

        let riff = String(data: waveBuffer.subdata(in: 0..<4), encoding: .ascii)
        let wave = String(data: waveBuffer.subdata(in: 8..<12), encoding: .ascii)
        guard riff == "RIFF", wave == "WAVE" else {
            throw NSError(domain: "SimplestSampler", code: 5, userInfo: [NSLocalizedDescriptionKey: "The converted sampler audio is not a WAV file."])
        }

        var offset = 12
        var fmtChunk: ParsedPCMWave?
        var dataOffset = 0
        var dataSize = 0

        while offset + 8 <= waveBuffer.count {
            let chunkId = String(data: waveBuffer.subdata(in: offset..<(offset + 4)), encoding: .ascii) ?? ""
            let chunkSize = Int(waveBuffer.withUnsafeBytes { ptr in
                ptr.loadUnaligned(fromByteOffset: offset + 4, as: UInt32.self)
            }.littleEndian)
            let chunkDataOffset = offset + 8
            let chunkEndOffset = chunkDataOffset + chunkSize
            guard chunkEndOffset <= waveBuffer.count else {
                throw NSError(domain: "SimplestSampler", code: 6, userInfo: [NSLocalizedDescriptionKey: "The converted sampler WAV file is truncated."])
            }

            if chunkId == "fmt ", chunkSize >= 16 {
                let audioFormat = waveBuffer.withUnsafeBytes { ptr in
                    ptr.loadUnaligned(fromByteOffset: chunkDataOffset, as: UInt16.self)
                }.littleEndian
                let channelCount = Int(waveBuffer.withUnsafeBytes { ptr in
                    ptr.loadUnaligned(fromByteOffset: chunkDataOffset + 2, as: UInt16.self)
                }.littleEndian)
                let sampleRate = Int(waveBuffer.withUnsafeBytes { ptr in
                    ptr.loadUnaligned(fromByteOffset: chunkDataOffset + 4, as: UInt32.self)
                }.littleEndian)
                let blockAlign = Int(waveBuffer.withUnsafeBytes { ptr in
                    ptr.loadUnaligned(fromByteOffset: chunkDataOffset + 12, as: UInt16.self)
                }.littleEndian)
                let bitsPerSample = Int(waveBuffer.withUnsafeBytes { ptr in
                    ptr.loadUnaligned(fromByteOffset: chunkDataOffset + 14, as: UInt16.self)
                }.littleEndian)
                fmtChunk = ParsedPCMWave(
                    audioFormat: audioFormat,
                    channelCount: channelCount,
                    sampleRate: sampleRate,
                    blockAlign: blockAlign,
                    bitsPerSample: bitsPerSample,
                    dataOffset: 0,
                    frameCount: 0
                )
            } else if chunkId == "data" {
                dataOffset = chunkDataOffset
                dataSize = chunkSize
            }

            offset = chunkEndOffset + (chunkSize % 2)
        }

        guard var fmt = fmtChunk, dataSize > 0 else {
            throw NSError(domain: "SimplestSampler", code: 7, userInfo: [NSLocalizedDescriptionKey: "The converted sampler WAV file is missing audio data."])
        }
        guard fmt.audioFormat == 1, fmt.bitsPerSample == 16, fmt.sampleRate > 0, fmt.blockAlign > 0 else {
            throw NSError(domain: "SimplestSampler", code: 8, userInfo: [NSLocalizedDescriptionKey: "The converted sampler WAV format is unsupported."])
        }

        fmt.dataOffset = dataOffset
        fmt.frameCount = dataSize / fmt.blockAlign
        return fmt
    }

    private static func buildPCMWaveBuffer(channelCount: Int, sampleRate: Int, bitsPerSample: Int, sampleData: Data) -> Data {
        let blockAlign = channelCount * (bitsPerSample / 8)
        let byteRate = sampleRate * blockAlign
        var header = Data(count: 44)
        header.replaceSubrange(0..<4, with: Data("RIFF".utf8))
        writeUInt32LE(to: &header, offset: 4, value: UInt32(36 + sampleData.count))
        header.replaceSubrange(8..<12, with: Data("WAVE".utf8))
        header.replaceSubrange(12..<16, with: Data("fmt ".utf8))
        writeUInt32LE(to: &header, offset: 16, value: 16)
        writeUInt16LE(to: &header, offset: 20, value: 1)
        writeUInt16LE(to: &header, offset: 22, value: UInt16(channelCount))
        writeUInt32LE(to: &header, offset: 24, value: UInt32(sampleRate))
        writeUInt32LE(to: &header, offset: 28, value: UInt32(byteRate))
        writeUInt16LE(to: &header, offset: 32, value: UInt16(blockAlign))
        writeUInt16LE(to: &header, offset: 34, value: UInt16(bitsPerSample))
        header.replaceSubrange(36..<40, with: Data("data".utf8))
        writeUInt32LE(to: &header, offset: 40, value: UInt32(sampleData.count))
        var buffer = Data()
        buffer.append(header)
        buffer.append(sampleData)
        return buffer
    }

    private static func interleaveMonoPCMBuffers(_ channelWaveBuffers: [Data]) throws -> Data {
        guard channelWaveBuffers.count >= 2 else {
            throw NSError(domain: "SimplestSampler", code: 9, userInfo: [NSLocalizedDescriptionKey: "Sampler stereo capture needs at least two source channels."])
        }

        let parsedChannels = try channelWaveBuffers.map { buffer -> (Data, ParsedPCMWave) in
            (buffer, try parsePCMWaveBuffer(buffer))
        }
        let first = parsedChannels[0].1
        for (_, parsed) in parsedChannels {
            guard parsed.channelCount == 1 else {
                throw NSError(domain: "SimplestSampler", code: 10, userInfo: [NSLocalizedDescriptionKey: "Sampler stereo capture expected mono source channels."])
            }
            guard parsed.sampleRate == first.sampleRate, parsed.bitsPerSample == first.bitsPerSample else {
                throw NSError(domain: "SimplestSampler", code: 11, userInfo: [NSLocalizedDescriptionKey: "Sampler stereo capture source channels have mismatched format."])
            }
        }

        let frameCount = parsedChannels.map(\.1.frameCount).min() ?? 0
        guard frameCount > 0 else {
            throw NSError(domain: "SimplestSampler", code: 12, userInfo: [NSLocalizedDescriptionKey: "Sampler stereo capture did not produce any audio frames."])
        }

        let bytesPerSample = first.bitsPerSample / 8
        let channelCount = parsedChannels.count
        var sampleData = Data(count: frameCount * channelCount * bytesPerSample)

        sampleData.withUnsafeMutableBytes { destPtr in
            guard let destBase = destPtr.baseAddress?.assumingMemoryBound(to: UInt8.self) else { return }
            for frameIndex in 0..<frameCount {
                for channelIndex in 0..<channelCount {
                    let (waveBuffer, parsed) = parsedChannels[channelIndex]
                    let sourceOffset = parsed.dataOffset + frameIndex * parsed.blockAlign
                    let targetOffset = (frameIndex * channelCount + channelIndex) * bytesPerSample
                    waveBuffer.copyBytes(to: destBase.advanced(by: targetOffset), from: sourceOffset..<(sourceOffset + bytesPerSample))
                }
            }
        }

        return buildPCMWaveBuffer(
            channelCount: channelCount,
            sampleRate: first.sampleRate,
            bitsPerSample: first.bitsPerSample,
            sampleData: sampleData
        )
    }

    private static func buildMultichannelSourceIdentity(plan: PTMultichannelPlan) -> String {
        plan.channels.map { entry in
            let start = String(format: "%.6f", entry.sourceWindow.startSec)
            let end = String(format: "%.6f", entry.sourceWindow.endSec)
            return "\(entry.filePath)::\(start)::\(end)"
        }.joined(separator: "||")
    }

    private static func extractChannelSuffix(_ value: String) -> String {
        let name = (value as NSString).deletingPathExtension
        guard let regex = try? NSRegularExpression(pattern: #"(?:^|[._\-\s])(lfe|lf|ls|rs|lc|rc|c|l|r)$"#, options: .caseInsensitive),
              let match = regex.firstMatch(in: name, range: NSRange(name.startIndex..., in: name)),
              match.numberOfRanges > 1,
              let range = Range(match.range(at: 1), in: name) else {
            return ""
        }
        return String(name[range]).lowercased()
    }

    private static func channelSortOrder(for segment: PTClipSegment, fallbackIndex: Int) -> Int {
        let suffix = extractChannelSuffix(segment.fileName)
            .nilIfEmpty
            ?? extractChannelSuffix(segment.filePath)
            .nilIfEmpty
            ?? extractChannelSuffix(segment.clipName)
        let order: [String: Int] = ["l": 0, "r": 1, "c": 2, "lfe": 3, "lf": 3, "ls": 4, "rs": 5, "lc": 6, "rc": 7]
        return order[suffix ?? ""] ?? (100 + fallbackIndex)
    }

    private static func normalizeMultichannelBaseName(_ segment: PTClipSegment) -> String {
        let preferred = [segment.clipName, segment.fileName, segment.filePath]
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .first { !$0.isEmpty } ?? ""
        let parsedName = (preferred as NSString).deletingPathExtension
        return parsedName
            .replacingOccurrences(of: #"(?:^|[._\-\s])(lfe|lf|ls|rs|lc|rc|c|l|r)$"#, with: "", options: [.regularExpression, .caseInsensitive])
            .replacingOccurrences(of: #"\s+"#, with: " ", options: .regularExpression)
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private static func sanitizeBaseName(_ value: String) -> String {
        let sanitized = value
            .replacingOccurrences(of: #"[^\w\-. ]+"#, with: "-", options: .regularExpression)
            .trimmingCharacters(in: .whitespacesAndNewlines)
        return sanitized.isEmpty ? "sampler-capture" : sanitized
    }

    private static func randomHex(count: Int) -> String {
        (0..<count).map { _ in String(format: "%02x", Int.random(in: 0...255)) }.joined()
    }

    private static func writeUInt16LE(to data: inout Data, offset: Int, value: UInt16) {
        var little = value.littleEndian
        withUnsafeBytes(of: &little) { data.replaceSubrange(offset..<(offset + 2), with: $0) }
    }

    private static func writeUInt32LE(to data: inout Data, offset: Int, value: UInt32) {
        var little = value.littleEndian
        withUnsafeBytes(of: &little) { data.replaceSubrange(offset..<(offset + 4), with: $0) }
    }
}

private extension String {
    var nilIfEmpty: String? {
        isEmpty ? nil : self
    }
}

enum ProToolsSourceWindowMath {
    static func resolveClipSourceWindow(
        clip: PTSelectedClipFile,
        anchorFrames: Int?,
        outFrames: Int?,
        fps: Double?,
        hasEditRange: Bool
    ) -> PTSourceWindow? {
        guard hasEditRange,
              let anchorFrames,
              let outFrames,
              let fps,
              fps > 0,
              outFrames > anchorFrames else {
            return nil
        }

        guard let srcStart = clip.srcStartSeconds, srcStart.isFinite else { return nil }
        let clipStartTc = clip.clipStartTime.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !clipStartTc.isEmpty else { return nil }
        guard let clipStartFrames = PTTimecodeMath.timecodeStringToFrameCount(clipStartTc, fps: fps) else { return nil }

        let selectionStartSec = srcStart + Double(anchorFrames - clipStartFrames) / fps
        let selectionEndSec = selectionStartSec + Double(outFrames - anchorFrames) / fps
        guard selectionStartSec.isFinite, selectionEndSec.isFinite, selectionEndSec > selectionStartSec else { return nil }

        let windowStartSec = max(0, selectionStartSec)
        let windowEndSec = max(windowStartSec, selectionEndSec)
        guard (windowEndSec - windowStartSec) >= (1 / fps) else { return nil }

        return PTSourceWindow(startSec: windowStartSec, endSec: windowEndSec, anchorFileSec: selectionStartSec)
    }

    static func resolveSegmentSourceWindow(_ segment: PTClipSegment, fps: Double?) -> PTSourceWindow? {
        if let directStart = segment.sourceStartSeconds,
           let directEnd = segment.sourceEndSeconds,
           directStart.isFinite, directEnd.isFinite, directEnd > directStart {
            return PTSourceWindow(startSec: directStart, endSec: directEnd, anchorFileSec: directStart)
        }

        guard let fps, fps > 0 else { return nil }
        guard let srcStart = segment.srcStartSeconds, srcStart.isFinite else { return nil }

        let clipStartTc = segment.clipStartTime.trimmingCharacters(in: .whitespacesAndNewlines)
        let segmentStartTc = segment.segmentStartTime.trimmingCharacters(in: .whitespacesAndNewlines)
        let segmentEndTc = segment.segmentEndTime.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !clipStartTc.isEmpty, !segmentStartTc.isEmpty, !segmentEndTc.isEmpty else { return nil }

        guard let clipStartFrames = PTTimecodeMath.timecodeStringToFrameCount(clipStartTc, fps: fps),
              let segmentStartFrames = PTTimecodeMath.timecodeStringToFrameCount(segmentStartTc, fps: fps),
              let segmentEndFrames = PTTimecodeMath.timecodeStringToFrameCount(segmentEndTc, fps: fps),
              segmentEndFrames > segmentStartFrames else {
            return nil
        }

        let startSec = srcStart + Double(segmentStartFrames - clipStartFrames) / fps
        let endSec = startSec + Double(segmentEndFrames - segmentStartFrames) / fps
        guard startSec.isFinite, endSec.isFinite, endSec > startSec else { return nil }

        return PTSourceWindow(
            startSec: startSec < 0 ? 0 : startSec,
            endSec: endSec,
            anchorFileSec: startSec
        )
    }
}
