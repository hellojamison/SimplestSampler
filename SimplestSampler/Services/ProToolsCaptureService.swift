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
            "releaseMajor": PTTimecodeMath.ptslReleaseMajor(activeProtocol).map(String.init) ?? "unknown",
            "legacyPre25": String(PTTimecodeMath.isLegacyPre25Protocol(activeProtocol)),
            "avoidConsolidateFallback": String(PTTimecodeMath.shouldAvoidConsolidateFallback(activeProtocol)),
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
            } else if PTTimecodeMath.shouldAvoidConsolidateFallback(activeProtocol) {
                throw await directResolveFailedError(activeProtocol: activeProtocol)
            } else {
                onStatus?("Consolidating selection in Pro Tools...")
                source = try await captureConsolidateFallback(onStatus: onStatus)
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
        let segments = segmentsPayload?.segments ?? []
        let segmentsSessionFps = resolveSessionFps(
            clip: nil,
            segmentsPayload: segmentsPayload,
            timeline: nil,
            segments: segments
        )

        CaptureDebugLogger.log("resolve source segments", context: [
            "segmentCount": segments.count,
            "sessionFps": segmentsSessionFps.map { String($0) } ?? "nil"
        ])

        if segments.count > 1 {
            CaptureDebugLogger.log("resolve source multi-segment selection", context: [
                "segmentCount": segments.count,
                "message": "continuing selected-clip-file fallback before render fallback"
            ])
        }

        if let multichannelPlan = MultichannelWaveRenderer.resolveMultichannelPlan(segments, sessionFps: segmentsSessionFps) {
            do {
                let rendered = try MultichannelWaveRenderer.renderInterleavedWAV(plan: multichannelPlan)
                CaptureDebugLogger.log("resolve source multichannel", context: ["filePath": rendered.filePath])
                return rendered
            } catch {
                CaptureDebugLogger.log("resolve source multichannel failed", context: ["message": error.localizedDescription])
            }
        }

        if let segmentCandidate = bestSegmentCandidate(from: segments, sessionFps: segmentsSessionFps) {
            CaptureDebugLogger.log("resolve source use segment candidate before clip math")
            return segmentCandidate
        }

        let selectedSegment = segments.count == 1 ? segments[0] : nil
        let selectedSegmentSessionFps = resolveSessionFps(
            clip: nil,
            segmentsPayload: segmentsPayload,
            timeline: nil,
            segments: selectedSegment.map { [$0] } ?? []
        ) ?? segmentsSessionFps
        let selectedSegmentCandidate = selectedSegment.flatMap {
            segmentCandidate(from: $0, sessionFps: selectedSegmentSessionFps)
        }

        let selectedClip = try await helper.getSelectedClipFileOrNull()
        CaptureDebugLogger.log("resolve source selected clip", context: [
            "filePath": selectedClip?.filePath ?? "",
            "sourceStartSeconds": selectedClip?.sourceStartSeconds.map { String($0) } ?? "nil",
            "sourceEndSeconds": selectedClip?.sourceEndSeconds.map { String($0) } ?? "nil"
        ])

        if let selectedClip,
           let directStart = selectedClip.sourceStartSeconds,
           let directEnd = selectedClip.sourceEndSeconds,
           directStart.isFinite, directEnd.isFinite, directEnd > directStart,
           !selectedClip.filePath.isEmpty {
            CaptureDebugLogger.log("resolve source use session-export clip file window")
            return captureSource(
                filePath: selectedClip.filePath,
                fileName: selectedClip.fileName,
                clipName: selectedClip.clipName,
                playbackStartSeconds: directStart,
                playbackEndSeconds: directEnd,
                source: "pt-session-export-clip-file"
            )
        }

        guard let selectedClip else {
            CaptureDebugLogger.log("resolve source segment fallback without selected clip")
            return selectedSegmentCandidate ?? bestSegmentCandidate(from: segments, sessionFps: segmentsSessionFps)
        }

        let timelineSelection = try? await helper.getTimelineSelection()
        let sessionFps = resolveSessionFps(
            clip: selectedClip,
            segmentsPayload: segmentsPayload,
            timeline: timelineSelection,
            segments: segments
        )
        let anchorTc = timelineAnchorTimecode(from: timelineSelection)
        let outTc = timelineSelection?.outTime.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        let anchorFrames = !anchorTc.isEmpty && sessionFps != nil
            ? PTTimecodeMath.timecodeStringToFrameCount(anchorTc, fps: sessionFps!)
            : nil
        let outFrames = !outTc.isEmpty && sessionFps != nil
            ? PTTimecodeMath.timecodeStringToFrameCount(outTc, fps: sessionFps!)
            : nil
        let hasEditRange = timelineSelection?.hasSelection == true
            && anchorFrames != nil
            && outFrames != nil
            && outFrames! > anchorFrames!

        let enrichedClip = await enrichClipForEditRange(
            selectedClip,
            anchorTc: anchorTc,
            sessionFps: sessionFps
        )
        let resolvedSessionFps = resolveSessionFps(
            clip: enrichedClip,
            segmentsPayload: segmentsPayload,
            timeline: timelineSelection,
            segments: segments
        )
        let resolvedAnchorFrames = !anchorTc.isEmpty && resolvedSessionFps != nil
            ? PTTimecodeMath.timecodeStringToFrameCount(anchorTc, fps: resolvedSessionFps!)
            : anchorFrames
        let resolvedOutFrames = !outTc.isEmpty && resolvedSessionFps != nil
            ? PTTimecodeMath.timecodeStringToFrameCount(outTc, fps: resolvedSessionFps!)
            : outFrames
        let resolvedHasEditRange = timelineSelection?.hasSelection == true
            && resolvedAnchorFrames != nil
            && resolvedOutFrames != nil
            && resolvedOutFrames! > resolvedAnchorFrames!

        let sourceWindow = ProToolsSourceWindowMath.resolveClipSourceWindow(
            clip: enrichedClip,
            anchorFrames: resolvedAnchorFrames,
            outFrames: resolvedOutFrames,
            fps: resolvedSessionFps,
            hasEditRange: resolvedHasEditRange
        )

        let selectedClipCandidate: SamplerCaptureSource? = {
            guard !enrichedClip.filePath.isEmpty, let sourceWindow else { return nil }
            return captureSource(
                filePath: enrichedClip.filePath,
                fileName: enrichedClip.fileName,
                clipName: enrichedClip.clipName,
                playbackStartSeconds: sourceWindow.startSec,
                playbackEndSeconds: sourceWindow.endSec,
                source: "pt-selected-clip"
            )
        }()

        let fullClipCandidate: SamplerCaptureSource? = {
            guard !resolvedHasEditRange, !enrichedClip.filePath.isEmpty else { return nil }
            return captureSource(
                filePath: enrichedClip.filePath,
                fileName: enrichedClip.fileName,
                clipName: enrichedClip.clipName,
                playbackStartSeconds: nil,
                playbackEndSeconds: nil,
                source: "pt-full-clip-fallback"
            )
        }()

        let rejectReasons = buildSourceWindowRejectReasons(
            selectedClip: enrichedClip,
            timelineSelection: timelineSelection,
            sessionFps: resolvedSessionFps,
            anchorTc: anchorTc,
            outTc: outTc,
            anchorFrames: resolvedAnchorFrames,
            outFrames: resolvedOutFrames,
            hasEditRange: resolvedHasEditRange,
            sourceWindow: sourceWindow
        )
        CaptureDebugLogger.log("resolve source selected clip decision", context: [
            "directCandidateAvailable": String(selectedClipCandidate != nil),
            "rejectReasons": rejectReasons,
            "hasEditRange": String(resolvedHasEditRange),
            "sessionFps": resolvedSessionFps.map { String($0) } ?? "nil",
            "anchorTc": anchorTc,
            "outTc": outTc
        ])

        if let selectedSegmentCandidate {
            CaptureDebugLogger.log("resolve source prefer sole segment candidate")
            return selectedSegmentCandidate
        }

        if let selectedClipCandidate {
            CaptureDebugLogger.log("resolve source use clip candidate")
            return selectedClipCandidate
        }

        if let fullClipCandidate {
            CaptureDebugLogger.log("resolve source use full clip fallback")
            return fullClipCandidate
        }

        if let anySegmentCandidate = bestSegmentCandidate(
            from: segments,
            sessionFps: resolvedSessionFps ?? segmentsSessionFps
        ) {
            CaptureDebugLogger.log("resolve source use any-segment fallback")
            return anySegmentCandidate
        }

        if resolvedHasEditRange {
            CaptureDebugLogger.log("resolve source edit range unresolved", context: [
                "clipStartTime": enrichedClip.clipStartTime,
                "hasSrcStart": String(enrichedClip.srcStartSeconds != nil),
                "rejectReasons": rejectReasons
            ])
        }

        return nil
    }

    private func enrichClipForEditRange(
        _ clip: PTSelectedClipFile,
        anchorTc: String,
        sessionFps: Double?
    ) async -> PTSelectedClipFile {
        var enriched = clip
        let needsClipStart = enriched.clipStartTime.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
        guard needsClipStart, !anchorTc.isEmpty else { return enriched }
        guard !enriched.clipId.isEmpty || !enriched.clipName.isEmpty else { return enriched }

        do {
            let resolved = try await helper.resolveClipStartTime(
                clipId: enriched.clipId,
                clipName: enriched.clipName,
                referenceTimecode: anchorTc
            )
            if !resolved.resolvedClipStartTime.isEmpty {
                enriched.clipStartTime = resolved.resolvedClipStartTime
            }
            if enriched.sessionFps == nil,
               let resolvedFps = resolved.sessionFps,
               resolvedFps.isFinite,
               resolvedFps > 0 {
                enriched.sessionFps = resolvedFps
            } else if enriched.sessionFps == nil, let sessionFps, sessionFps.isFinite, sessionFps > 0 {
                enriched.sessionFps = sessionFps
            }
            CaptureDebugLogger.log("resolve source enriched clip start time", context: [
                "clipStartTime": enriched.clipStartTime,
                "sessionFps": enriched.sessionFps.map { String($0) } ?? "nil"
            ])
        } catch {
            CaptureDebugLogger.log("resolve source clip start enrichment failed", context: [
                "message": error.localizedDescription
            ])
        }

        return enriched
    }

    private func resolveSessionFps(
        clip: PTSelectedClipFile?,
        segmentsPayload: PTClipSegmentsPayload?,
        timeline: PTTimelineSelection?,
        segments: [PTClipSegment]
    ) -> Double? {
        for candidate in [clip?.sessionFps, segmentsPayload?.sessionFps, timeline?.sessionFps, segments.first?.sessionFps] {
            guard let fps = candidate, fps.isFinite, fps > 0 else { continue }
            return fps
        }
        return nil
    }

    private func timelineAnchorTimecode(from timeline: PTTimelineSelection?) -> String {
        let inTime = timeline?.inTime.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        if !inTime.isEmpty {
            return inTime
        }
        return timeline?.playStartMarkerTime.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
    }

    private func segmentCandidate(from segment: PTClipSegment, sessionFps: Double?) -> SamplerCaptureSource? {
        let fps = sessionFps ?? segment.sessionFps
        guard let sourceWindow = ProToolsSourceWindowMath.resolveSegmentSourceWindow(segment, fps: fps),
              !segment.filePath.isEmpty else {
            return nil
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

    private func bestSegmentCandidate(from segments: [PTClipSegment], sessionFps: Double?) -> SamplerCaptureSource? {
        let ordered = segments.sorted { left, right in
            let leftIsExport = left.resolutionSource == "session_export"
            let rightIsExport = right.resolutionSource == "session_export"
            if leftIsExport != rightIsExport {
                return leftIsExport
            }
            return false
        }
        for segment in ordered {
            if let candidate = segmentCandidate(from: segment, sessionFps: sessionFps) {
                return candidate
            }
        }
        return nil
    }

    private func buildSourceWindowRejectReasons(
        selectedClip: PTSelectedClipFile,
        timelineSelection: PTTimelineSelection?,
        sessionFps: Double?,
        anchorTc: String,
        outTc: String,
        anchorFrames: Int?,
        outFrames: Int?,
        hasEditRange: Bool,
        sourceWindow: PTSourceWindow?
    ) -> [String] {
        var reasons: [String] = []
        if selectedClip.filePath.isEmpty { reasons.append("selected clip file path missing") }
        if timelineSelection == nil { reasons.append("timeline selection unavailable") }
        if timelineSelection?.hasSelection != true { reasons.append("timeline selection has no range") }
        if sessionFps == nil { reasons.append("session fps missing") }
        if anchorTc.isEmpty { reasons.append("selection in time missing") }
        if outTc.isEmpty { reasons.append("selection out time missing") }
        if anchorFrames == nil { reasons.append("selection in frame conversion failed") }
        if outFrames == nil { reasons.append("selection out frame conversion failed") }
        if !hasEditRange { reasons.append("selection range collapsed or invalid") }
        if selectedClip.srcStartSeconds == nil { reasons.append("selected clip source start missing") }
        if selectedClip.clipStartTime.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            reasons.append("selected clip timeline start missing")
        }
        if sourceWindow == nil { reasons.append("source playback window could not be computed") }
        return reasons
    }

    private func captureConsolidateFallback(
        onStatus: (@Sendable (String) -> Void)?
    ) async throws -> SamplerCaptureSource {
        let sessionPath = try await helper.getSessionPath()
        let previousSelectedFile = try await helper.getSelectedClipFileOrNull()
        let audioFileSnapshot = try? ConsolidateFallbackService.readSessionAudioFileSnapshot(sessionPath: sessionPath)

        CaptureDebugLogger.log("consolidate fallback start", context: [
            "sessionPath": sessionPath,
            "previousFilePath": previousSelectedFile?.filePath ?? ""
        ])

        try await ConsolidateFallbackService.triggerConsolidateSelection(helper: helper)
        try await Task.sleep(nanoseconds: UInt64(PTSLCaptureConstants.consolidateTriggerSettleSeconds * 1_000_000_000))

        onStatus?("Waiting for consolidated clip...")
        return try await ConsolidateFallbackService.waitForNewConsolidatedPtClipFile(
            previousSelectedFile: previousSelectedFile,
            sessionPath: sessionPath,
            audioFileSnapshot: audioFileSnapshot,
            allowAudioDirectoryFallback: true
        )
    }

    private func directResolveFailedError(activeProtocol: String) async -> NSError {
        let timelineSelection = try? await helper.getTimelineSelection()
        let selectedClip = try? await helper.getSelectedClipFileOrNull()

        if selectedClip?.filePath.isEmpty == false {
            return NSError(
                domain: "SimplestSampler",
                code: 3,
                userInfo: [NSLocalizedDescriptionKey: "Could not map the selected Pro Tools range to a playback window in the source audio file. Select a single linked clip or a shorter range and try again."]
            )
        }

        if timelineSelection?.hasSelection == true {
            return NSError(
                domain: "SimplestSampler",
                code: 4,
                userInfo: [NSLocalizedDescriptionKey: "No clip is selected for the current Pro Tools range. Make sure Link Track and Edit Selection is on in Pro Tools, then select the clip or range again."]
            )
        }

        if PTTimecodeMath.shouldAvoidConsolidateFallback(activeProtocol) {
            return NSError(
                domain: "SimplestSampler",
                code: 5,
                userInfo: [NSLocalizedDescriptionKey: "Sampler capture needs a directly resolvable Pro Tools clip selection on this Pro Tools version. Select a single clip or timeline range that maps to an existing source audio file and try again."]
            )
        }

        return NSError(
            domain: "SimplestSampler",
            code: 6,
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
