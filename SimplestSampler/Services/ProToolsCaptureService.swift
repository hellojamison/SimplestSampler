import Foundation

actor ProToolsCaptureService {
    static let shared = ProToolsCaptureService()

    private var captureInFlight = false
    private let helper = PTSLHelperClient.shared

    func captureAndPlace(
        targetSlotIndex: Int?,
        recentCaptures: inout [SamplerCapture?],
        nextSequence: inout Int,
        storedCaptures: [SamplerCapture],
        onStatus: (@Sendable (String) -> Void)? = nil
    ) async throws -> PlacedSamplerCapture {
        guard !captureInFlight else {
            throw NSError(domain: "SimplestSampler", code: 1, userInfo: [NSLocalizedDescriptionKey: "A Pro Tools capture is already in progress."])
        }

        captureInFlight = true
        defer { captureInFlight = false }

        onStatus?("Connecting to Pro Tools...")
        try await helper.ping()

        let activeProtocol = (try? await helper.getActiveProtocol()) ?? ""
        CaptureDebugLogger.log("capture start", context: [
            "activeProtocol": activeProtocol,
            "targetSlotIndex": targetSlotIndex.map(String.init) ?? "auto"
        ])

        let source: SamplerCaptureSource
        if PTTimecodeMath.isLegacyPre25Protocol(activeProtocol) {
            onStatus?("Consolidating clip in Pro Tools...")
            source = try await captureLegacyPre25Consolidate(activeProtocol: activeProtocol, onStatus: onStatus)
        } else {
            onStatus?("Reading Pro Tools selection...")
            if let directSource = try await resolveSourceFromSelection() {
                source = directSource
            } else {
                throw directResolveFailedError(activeProtocol: activeProtocol)
            }
        }

        return try ActiveSlotService.placeCapture(
            source: source,
            recentCaptures: &recentCaptures,
            targetSlotIndex: targetSlotIndex,
            nextSequence: &nextSequence,
            storedCaptures: storedCaptures
        )
    }

    func resolveSourceFromSelection() async throws -> SamplerCaptureSource? {
        let segmentsPayload = try await helper.getSelectedClipSegmentsForSamplerOrNull()
        CaptureDebugLogger.log("resolve source segments", context: [
            "segmentCount": segmentsPayload?.segments.count ?? 0
        ])

        let segments = segmentsPayload?.segments ?? []
        let sessionFps = segmentsPayload?.sessionFps ?? segments.first?.sessionFps

        if let multichannelPlan = MultichannelWaveRenderer.resolveMultichannelPlan(segments, sessionFps: sessionFps) {
            do {
                let rendered = try MultichannelWaveRenderer.renderInterleavedWAV(plan: multichannelPlan)
                CaptureDebugLogger.log("resolve source multichannel", context: ["filePath": rendered.filePath])
                return rendered
            } catch {
                CaptureDebugLogger.log("resolve source multichannel failed", context: ["message": error.localizedDescription])
            }
        }

        let selectedSegmentCandidate = bestSegmentCandidate(from: segments, sessionFps: sessionFps)

        let selectedClip = try await helper.getSelectedClipFileOrNull()
        CaptureDebugLogger.log("resolve source selected clip", context: ["filePath": selectedClip?.filePath ?? ""])

        guard let selectedClip else {
            return selectedSegmentCandidate
        }

        let timelineSelection = try? await helper.getTimelineSelection()
        let clipSessionFps = resolvedSessionFps(
            clipFps: selectedClip.sessionFps,
            timelineFps: timelineSelection?.sessionFps,
            fallbackFps: sessionFps
        )
        let anchorTc = PTTimecodeMath.normalizeHelperTimecode(
            (timelineSelection?.inTime.isEmpty == false ? timelineSelection?.inTime : timelineSelection?.playStartMarkerTime) ?? ""
        )
        let outTc = PTTimecodeMath.normalizeHelperTimecode(timelineSelection?.outTime ?? "")
        let anchorFrames = !anchorTc.isEmpty && clipSessionFps != nil
            ? PTTimecodeMath.timecodeStringToFrameCount(anchorTc, fps: clipSessionFps!)
            : nil
        let outFrames = !outTc.isEmpty && clipSessionFps != nil
            ? PTTimecodeMath.timecodeStringToFrameCount(outTc, fps: clipSessionFps!)
            : nil
        let hasEditRange = timelineSelection?.hasSelection == true
            && anchorFrames != nil
            && outFrames != nil
            && outFrames! > anchorFrames!

        let sourceWindow = ProToolsSourceWindowMath.resolveClipSourceWindow(
            clip: selectedClip,
            anchorFrames: anchorFrames,
            outFrames: outFrames,
            fps: clipSessionFps,
            hasEditRange: hasEditRange
        )

        let selectedClipCandidate: SamplerCaptureSource? = {
            guard !selectedClip.filePath.isEmpty, let sourceWindow else { return nil }
            return captureSource(
                filePath: selectedClip.filePath,
                fileName: selectedClip.fileName,
                clipName: selectedClip.clipName,
                playbackStartSeconds: sourceWindow.startSec,
                playbackEndSeconds: sourceWindow.endSec,
                source: "pt-selected-clip"
            )
        }()

        let fullClipCandidate: SamplerCaptureSource? = {
            guard !selectedClip.filePath.isEmpty else { return nil }
            return captureSource(
                filePath: selectedClip.filePath,
                fileName: selectedClip.fileName,
                clipName: selectedClip.clipName,
                playbackStartSeconds: nil,
                playbackEndSeconds: nil,
                source: "pt-full-clip-fallback"
            )
        }()

        if selectedSegmentCandidate != nil {
            CaptureDebugLogger.log("resolve source prefer segment candidate")
            return selectedSegmentCandidate
        }

        if selectedClipCandidate != nil {
            CaptureDebugLogger.log("resolve source use clip candidate")
            return selectedClipCandidate
        }

        if fullClipCandidate != nil {
            CaptureDebugLogger.log("resolve source use full clip fallback")
            return fullClipCandidate
        }

        return selectedSegmentCandidate
    }

    private func bestSegmentCandidate(from segments: [PTClipSegment], sessionFps: Double?) -> SamplerCaptureSource? {
        for segment in segments {
            let fps = sessionFps ?? segment.sessionFps
            guard let sourceWindow = ProToolsSourceWindowMath.resolveSegmentSourceWindow(segment, fps: fps),
                  !segment.filePath.isEmpty else {
                continue
            }
            return captureSource(
                filePath: segment.filePath,
                fileName: segment.fileName,
                clipName: segment.clipName,
                playbackStartSeconds: sourceWindow.startSec,
                playbackEndSeconds: sourceWindow.endSec,
                source: "pt-selected-segment"
            )
        }
        return nil
    }

    private func resolvedSessionFps(clipFps: Double?, timelineFps: Double?, fallbackFps: Double?) -> Double? {
        for candidate in [clipFps, timelineFps, fallbackFps] {
            if let candidate, candidate.isFinite, candidate > 0 {
                return candidate
            }
        }
        return nil
    }

    private func directResolveFailedError(activeProtocol: String) -> NSError {
        if PTTimecodeMath.shouldAvoidConsolidateFallback(activeProtocol) {
            return NSError(
                domain: "SimplestSampler",
                code: 3,
                userInfo: [NSLocalizedDescriptionKey: "Sampler capture needs a directly resolvable Pro Tools clip selection on this Pro Tools version. Select a single clip or timeline range that maps to an existing source audio file and try again."]
            )
        }

        return NSError(
            domain: "SimplestSampler",
            code: 4,
            userInfo: [NSLocalizedDescriptionKey: "Could not resolve the selected Pro Tools clip to a source audio file. Select a clip or edit range on one track with Link Track and Edit Selection enabled, then try again."]
        )
    }

    private func captureLegacyPre25Consolidate(
        activeProtocol: String,
        onStatus: (@Sendable (String) -> Void)?
    ) async throws -> SamplerCaptureSource {
        let sessionPath = try await helper.getSessionPath()
        CaptureDebugLogger.log("legacy pre-25 consolidate", context: ["sessionPath": sessionPath, "activeProtocol": activeProtocol])

        let audioFileSnapshot = try? ConsolidateFallbackService.readSessionAudioFileSnapshot(sessionPath: sessionPath)
        try await ConsolidateFallbackService.triggerConsolidateSelection(helper: helper)
        try await Task.sleep(nanoseconds: UInt64(PTSLCaptureConstants.consolidateTriggerSettleSeconds * 1_000_000_000))

        onStatus?("Waiting for consolidated audio file...")
        return try await ConsolidateFallbackService.waitForNewConsolidatedAudioFileInSessionDirectory(
            sessionPath: sessionPath,
            audioFileSnapshot: audioFileSnapshot,
            previousSelectedFile: nil,
            timeoutSeconds: PTSLCaptureConstants.consolidateTimeoutOldPTSeconds
        )
    }

    private func captureSource(
        filePath: String,
        fileName: String,
        clipName: String,
        playbackStartSeconds: Double?,
        playbackEndSeconds: Double?,
        source: String
    ) -> SamplerCaptureSource {
        let defaultName = clipName.isEmpty ? ((fileName as NSString).deletingPathExtension) : clipName
        var identity = filePath
        if let start = playbackStartSeconds, let end = playbackEndSeconds {
            identity = "\(filePath)::\(String(format: "%.6f", start))::\(String(format: "%.6f", end))"
        }
        return SamplerCaptureSource(
            filePath: filePath,
            fileName: fileName,
            clipName: clipName,
            defaultDisplayName: defaultName,
            playbackStartSeconds: playbackStartSeconds,
            playbackEndSeconds: playbackEndSeconds,
            sourceIdentity: identity,
            managedByApp: source.hasPrefix("pt-multichannel"),
            source: source
        )
    }
}
