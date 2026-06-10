import AVFoundation
import Foundation

@MainActor
final class AudioPlaybackService: ObservableObject {
    @Published private(set) var isPlaying = false
    @Published private(set) var playingCaptureId = ""
    /// Progress through the loaded trim window, 0…1.
    @Published private(set) var playbackProgress: Double = 0
    /// Smoothed output level for the volume meter, 0…1.
    @Published private(set) var outputLevel: Double = 0

    private var audioEngine = AVAudioEngine()
    private var playerNode = AVAudioPlayerNode()
    private var audioFile: AVAudioFile?
    private var currentCapture: SamplerCapture?
    private var playbackStart: Double = 0
    private var playbackEnd: Double?
    private var positionTimer: Timer?
    private var volume: Int = SamplerConstants.defaultVolume
    private var outputDeviceUID: String = ""
    private var outputTapInstalled = false
    private var meterLevel: Float = 0
    private var pendingMeterPeak: Float = 0
    private var lastMeterPublishTime: CFAbsoluteTime = 0

    var onPlaybackFinished: (() -> Void)?

    init() {
        audioEngine.attach(playerNode)
        audioEngine.connect(playerNode, to: audioEngine.mainMixerNode, format: nil)
    }

    func setVolume(_ value: Int) {
        volume = SamplerVolumeMath.normalizedVolume(value)
        audioEngine.mainMixerNode.outputVolume = SamplerVolumeMath.gainMultiplier(forVolume: volume)
    }

    func setOutputDeviceUID(_ uid: String) {
        outputDeviceUID = uid
        applyOutputDevice(restartEngine: audioEngine.isRunning)
    }

    private func applyOutputDevice(restartEngine: Bool = false) {
        guard !outputDeviceUID.isEmpty,
              let audioUnit = audioEngine.outputNode.audioUnit else {
            return
        }

        let wasRunning = audioEngine.isRunning
        if wasRunning {
            audioEngine.pause()
        }

        _ = AudioOutputDeviceService.setOutputDevice(uid: outputDeviceUID, on: audioUnit)

        if restartEngine || wasRunning {
            try? audioEngine.start()
        }
    }

    func load(capture: SamplerCapture) throws {
        stop(updateStatus: false)

        let url = URL(fileURLWithPath: capture.filePath)
        guard FileManager.default.fileExists(atPath: url.path) else {
            throw NSError(domain: "SimplestSampler", code: 10, userInfo: [NSLocalizedDescriptionKey: "Could not load the sampler file."])
        }

        audioFile = try AVAudioFile(forReading: url)
        currentCapture = capture

        if let start = capture.playbackStartSeconds {
            playbackStart = max(0, start)
        } else {
            playbackStart = 0
        }

        if let end = capture.playbackEndSeconds, end > playbackStart {
            playbackEnd = end
        } else if let file = audioFile {
            playbackEnd = Double(file.length) / file.fileFormat.sampleRate
        } else {
            playbackEnd = nil
        }

        playbackProgress = 0
        try prepareEngine()
    }

    func play(captureId: String) throws {
        guard let capture = currentCapture, let file = audioFile else {
            throw NSError(domain: "SimplestSampler", code: 11, userInfo: [NSLocalizedDescriptionKey: "No sampler file is loaded yet."])
        }

        if isPlaying, playingCaptureId == captureId {
            stop()
            return
        }

        stop(updateStatus: false)
        currentCapture = capture
        playingCaptureId = captureId

        let sampleRate = file.fileFormat.sampleRate
        let startFrame = AVAudioFramePosition(playbackStart * sampleRate)
        let endFrame = AVAudioFramePosition((playbackEnd ?? Double(file.length) / sampleRate) * sampleRate)
        let frameCount = AVAudioFrameCount(max(0, endFrame - startFrame))

        guard frameCount > 0 else {
            throw NSError(domain: "SimplestSampler", code: 12, userInfo: [NSLocalizedDescriptionKey: "Could not play the loaded sampler file."])
        }

        setVolume(volume)
        applyOutputDevice(restartEngine: true)
        try prepareEngine()

        playerNode.scheduleSegment(
            file,
            startingFrame: startFrame,
            frameCount: frameCount,
            at: nil
        ) { [weak self] in
            Task { @MainActor in
                self?.finishPlayback()
            }
        }

        playerNode.play()
        isPlaying = true
        playbackProgress = 0
        startPositionMonitor()
    }

    func stop(updateStatus: Bool = true) {
        positionTimer?.invalidate()
        positionTimer = nil
        playerNode.stop()
        removeOutputLevelTapIfNeeded()
        if audioEngine.isRunning {
            audioEngine.pause()
        }
        isPlaying = false
        playingCaptureId = ""
        playbackProgress = 0
        resetOutputLevel()
        if updateStatus {
            onPlaybackFinished?()
        }
    }

    private func finishPlayback() {
        positionTimer?.invalidate()
        positionTimer = nil
        playerNode.stop()
        isPlaying = false
        playingCaptureId = ""
        playbackProgress = 0
        resetOutputLevel()
        onPlaybackFinished?()
    }

    private func startPositionMonitor() {
        positionTimer?.invalidate()
        guard let end = playbackEnd, end > playbackStart else { return }

        positionTimer = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { [weak self] _ in
            Task { @MainActor in
                self?.updatePlaybackProgress(endTime: end)
            }
        }
    }

    private func updatePlaybackProgress(endTime: Double) {
        guard isPlaying,
              let nodeTime = playerNode.lastRenderTime,
              let playerTime = playerNode.playerTime(forNodeTime: nodeTime),
              let file = audioFile else {
            return
        }

        let duration = endTime - playbackStart
        guard duration > 0 else {
            playbackProgress = 0
            return
        }

        let currentSeconds = Double(playerTime.sampleTime) / file.fileFormat.sampleRate + playbackStart
        playbackProgress = min(1, max(0, (currentSeconds - playbackStart) / duration))

        if currentSeconds + 0.02 >= endTime {
            finishPlayback()
        }
    }

    private func prepareEngine() throws {
        if !audioEngine.isRunning {
            try audioEngine.start()
        }
        installOutputLevelTapIfNeeded()
    }

    private func installOutputLevelTapIfNeeded() {
        guard !outputTapInstalled else { return }

        let mixer = audioEngine.mainMixerNode
        let format = mixer.outputFormat(forBus: 0)
        mixer.installTap(onBus: 0, bufferSize: 1024, format: format) { [weak self] buffer, _ in
            guard let peak = Self.peakLevel(in: buffer) else { return }
            Task { @MainActor [weak self] in
                self?.updateOutputLevel(peak: peak)
            }
        }
        outputTapInstalled = true
    }

    private func removeOutputLevelTapIfNeeded() {
        guard outputTapInstalled else { return }
        audioEngine.mainMixerNode.removeTap(onBus: 0)
        outputTapInstalled = false
    }

    private static func peakLevel(in buffer: AVAudioPCMBuffer) -> Float? {
        guard let channelData = buffer.floatChannelData else { return nil }

        let frameLength = Int(buffer.frameLength)
        let channelCount = Int(buffer.format.channelCount)
        guard frameLength > 0, channelCount > 0 else { return nil }

        var peak: Float = 0
        for channel in 0..<channelCount {
            let samples = channelData[channel]
            for frame in 0..<frameLength {
                peak = max(peak, abs(samples[frame]))
            }
        }
        return peak
    }

    private func updateOutputLevel(peak: Float) {
        pendingMeterPeak = max(pendingMeterPeak, peak)

        let now = CFAbsoluteTimeGetCurrent()
        guard now - lastMeterPublishTime >= (1.0 / 30.0) else { return }
        lastMeterPublishTime = now

        let samplePeak = pendingMeterPeak
        pendingMeterPeak = 0

        if samplePeak > meterLevel {
            meterLevel = (0.35 * meterLevel) + (0.65 * samplePeak)
        } else {
            meterLevel = (0.82 * meterLevel) + (0.18 * samplePeak)
        }
        outputLevel = displayLevel(from: meterLevel)
    }

    private func displayLevel(from peak: Float) -> Double {
        let floor: Float = 0.000_25
        let clamped = min(1, max(floor, peak))
        let normalized = (20 * log10(clamped) + 60) / 60
        return Double(min(1, max(0, normalized)))
    }

    private func resetOutputLevel() {
        meterLevel = 0
        pendingMeterPeak = 0
        lastMeterPublishTime = 0
        outputLevel = 0
    }
}
