import AVFoundation
import Foundation

@MainActor
final class AudioPlaybackService: ObservableObject {
    @Published private(set) var isPlaying = false
    @Published private(set) var playingCaptureId = ""

    private var audioEngine = AVAudioEngine()
    private var playerNode = AVAudioPlayerNode()
    private var audioFile: AVAudioFile?
    private var currentCapture: SamplerCapture?
    private var playbackStart: Double = 0
    private var playbackEnd: Double?
    private var positionTimer: Timer?
    private var volume: Int = SamplerConstants.defaultVolume
    private var outputDeviceUID: String = ""

    var onPlaybackFinished: (() -> Void)?

    init() {
        audioEngine.attach(playerNode)
        audioEngine.connect(playerNode, to: audioEngine.mainMixerNode, format: nil)
    }

    func setVolume(_ value: Int) {
        volume = max(0, min(SamplerConstants.maxVolume, value))
        let gain = min(1.0, Double(volume) / 80.0)
        audioEngine.mainMixerNode.outputVolume = Float(gain)
    }

    func setOutputDeviceUID(_ uid: String) {
        outputDeviceUID = uid
        applyOutputDevice()
    }

    private func applyOutputDevice() {
        guard let audioUnit = audioEngine.outputNode.audioUnit else { return }
        AudioOutputDeviceService.setOutputDevice(uid: outputDeviceUID, on: audioUnit)
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

        try prepareEngine()
        setVolume(volume)
        applyOutputDevice()

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
        startPositionMonitor()
    }

    func stop(updateStatus: Bool = true) {
        positionTimer?.invalidate()
        positionTimer = nil
        playerNode.stop()
        if audioEngine.isRunning {
            audioEngine.pause()
        }
        isPlaying = false
        playingCaptureId = ""
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
        onPlaybackFinished?()
    }

    private func startPositionMonitor() {
        positionTimer?.invalidate()
        guard let end = playbackEnd else { return }

        positionTimer = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard let self, self.isPlaying, let nodeTime = self.playerNode.lastRenderTime,
                      let playerTime = self.playerNode.playerTime(forNodeTime: nodeTime),
                      let file = self.audioFile else { return }

                let currentSeconds = Double(playerTime.sampleTime) / file.fileFormat.sampleRate + self.playbackStart
                if currentSeconds + 0.02 >= end {
                    self.finishPlayback()
                }
            }
        }
    }

    private func prepareEngine() throws {
        if !audioEngine.isRunning {
            try audioEngine.start()
        }
    }
}
