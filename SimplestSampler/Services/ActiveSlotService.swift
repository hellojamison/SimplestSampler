import Foundation

struct PlacedSamplerCapture {
    var capture: SamplerCapture
    var slotIndex: Int
}

enum ActiveSlotService {
    static func placeCapture(
        source: SamplerCaptureSource,
        recentCaptures: inout [SamplerCapture?],
        targetSlotIndex: Int?,
        nextSequence: inout Int,
        storedCaptures: [SamplerCapture]
    ) throws -> PlacedSamplerCapture {
        let normalizedFilePath = source.filePath.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !normalizedFilePath.isEmpty else {
            throw NSError(domain: "SimplestSampler", code: 1, userInfo: [NSLocalizedDescriptionKey: "The sampler capture did not return a valid file path."])
        }

        var slots = recentCaptures
        let slotCount = slots.count

        let normalizedTarget = isValidSlotIndex(targetSlotIndex, slotCount: slotCount) ? targetSlotIndex : nil
        let nextEntryIdentity = resolveSourceIdentity(source)
        let existingEntryIndex = slots.firstIndex {
            guard let entry = $0 else { return false }
            return resolveCaptureIdentity(entry) == nextEntryIdentity
        }
        let existingEntry = existingEntryIndex.map { slots[$0] } ?? nil
        let playbackRange = normalizePlaybackRange(source)

        let nextEntry = SamplerCapture(
            id: existingEntry?.id ?? "sampler-capture-\(nextSequence)",
            filePath: normalizedFilePath,
            fileName: source.fileName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
                ? URL(fileURLWithPath: normalizedFilePath).lastPathComponent
                : source.fileName,
            clipName: source.clipName.trimmingCharacters(in: .whitespacesAndNewlines),
            displayName: existingEntry?.displayName ?? deriveDefaultDisplayName(source),
            defaultDisplayName: source.defaultDisplayName.isEmpty ? deriveDefaultDisplayName(source) : source.defaultDisplayName,
            hasCustomDisplayName: existingEntry?.hasCustomDisplayName ?? false,
            playbackStartSeconds: playbackRange.start,
            playbackEndSeconds: playbackRange.end,
            capturedAt: Date().timeIntervalSince1970 * 1000,
            saved: storedCaptures.contains { resolveCaptureIdentity($0) == nextEntryIdentity },
            sourceIdentity: source.sourceIdentity.isEmpty ? nextEntryIdentity : source.sourceIdentity,
            managedByApp: source.managedByApp
        )

        if existingEntry == nil {
            nextSequence += 1
        }

        CaptureDebugLogger.log("place capture", context: [
            "targetSlotIndex": normalizedTarget.map(String.init) ?? "auto",
            "filePath": normalizedFilePath
        ])

        if let normalizedTarget {
            if let existingEntryIndex, existingEntryIndex != normalizedTarget {
                slots[existingEntryIndex] = nil
            }
            slots[normalizedTarget] = nextEntry
            recentCaptures = slots
            return PlacedSamplerCapture(capture: nextEntry, slotIndex: normalizedTarget)
        }

        if let existingEntryIndex {
            slots[existingEntryIndex] = nextEntry
            recentCaptures = slots
            return PlacedSamplerCapture(capture: nextEntry, slotIndex: existingEntryIndex)
        }

        if let emptySlotIndex = slots.firstIndex(where: { $0 == nil }) {
            slots[emptySlotIndex] = nextEntry
            recentCaptures = slots
            return PlacedSamplerCapture(capture: nextEntry, slotIndex: emptySlotIndex)
        }

        let replaceableSlotIndex = slots.enumerated().reduce(-1) { bestIndex, pair in
            let (index, entry) = pair
            guard let entry else { return bestIndex }
            if bestIndex < 0 { return index }
            let bestEntry = slots[bestIndex]
            let bestCapturedAt = bestEntry?.capturedAt ?? 0
            return entry.capturedAt < bestCapturedAt ? index : bestIndex
        }

        guard replaceableSlotIndex >= 0 else {
            throw slotUnavailableError(targetSlotIndex: nil)
        }

        slots[replaceableSlotIndex] = nextEntry
        recentCaptures = slots
        return PlacedSamplerCapture(capture: nextEntry, slotIndex: replaceableSlotIndex)
    }

    static func slotUnavailableError(targetSlotIndex: Int?) -> NSError {
        if let targetSlotIndex, targetSlotIndex >= 0 {
            return NSError(
                domain: "SimplestSampler",
                code: 2,
                userInfo: [NSLocalizedDescriptionKey: "Sampler slot \(targetSlotIndex + 1) is unavailable. Delete that capture or choose another slot before capturing into it."]
            )
        }
        return NSError(
            domain: "SimplestSampler",
            code: 3,
            userInfo: [NSLocalizedDescriptionKey: "No sampler slot is available. Delete a recent capture or choose a target slot before capturing again."]
        )
    }

    private static func isValidSlotIndex(_ index: Int?, slotCount: Int) -> Bool {
        guard let index else { return false }
        return index >= 0 && index < slotCount
    }

    private static func deriveDefaultDisplayName(_ source: SamplerCaptureSource) -> String {
        let clipName = source.clipName.trimmingCharacters(in: .whitespacesAndNewlines)
        if !clipName.isEmpty { return clipName }

        let fileName = source.fileName.trimmingCharacters(in: .whitespacesAndNewlines)
        if !fileName.isEmpty {
            let base = (fileName as NSString).deletingPathExtension
            return base.isEmpty ? fileName : base
        }

        let filePath = source.filePath.trimmingCharacters(in: .whitespacesAndNewlines)
        if !filePath.isEmpty {
            let base = ((filePath as NSString).lastPathComponent as NSString).deletingPathExtension
            return base.isEmpty ? filePath : base
        }

        return "Capture"
    }

    private static func normalizePlaybackRange(_ source: SamplerCaptureSource) -> (start: Double?, end: Double?) {
        guard let start = source.playbackStartSeconds,
              let end = source.playbackEndSeconds,
              start.isFinite, end.isFinite, end > start, start >= 0 else {
            return (nil, nil)
        }
        return (start, end)
    }

    private static func resolveSourceIdentity(_ source: SamplerCaptureSource) -> String {
        let explicit = source.sourceIdentity.trimmingCharacters(in: .whitespacesAndNewlines)
        if !explicit.isEmpty { return explicit }

        let filePath = source.filePath.trimmingCharacters(in: .whitespacesAndNewlines)
        let range = normalizePlaybackRange(source)
        if let start = range.start, let end = range.end {
            return "\(filePath)::\(String(format: "%.6f", start))::\(String(format: "%.6f", end))"
        }
        return filePath
    }

    private static func resolveCaptureIdentity(_ capture: SamplerCapture) -> String {
        let explicit = capture.sourceIdentity.trimmingCharacters(in: .whitespacesAndNewlines)
        if !explicit.isEmpty { return explicit }
        return SamplerCapture.buildSourceIdentity(filePath: capture.filePath)
    }
}
