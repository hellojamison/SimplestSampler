import AppKit
import Combine
import Foundation
import SwiftUI
import UniformTypeIdentifiers

@MainActor
final class SamplerViewModel: ObservableObject {
    @Published var recentCaptures: [SamplerCapture?]
    @Published var storedCaptures: [SamplerCapture]
    @Published var selectedCaptureId = ""
    @Published var selectedCaptureSource = "recent"
    @Published var loadedCaptureId = ""
    @Published var activeTab = "recent"
    @Published var statusMessage = "Ready."
    @Published var statusIsError = false
    @Published var outputDevices: [AudioOutputDevice] = []
    @Published var capturingShortcutId = ""
    @Published var dropTargetIndex = -1
    @Published var isCaptureInProgress = false
    @Published private(set) var activeSlotCount: Int
    @Published private(set) var volume: Int
    /// 0-based slot index targeted by the last capture shortcut; reserved for Phase 2 PT capture.
    @Published private(set) var targetSlotIndex: Int?

    var canAddActiveSlot: Bool {
        activeSlotCount < SamplerConstants.maxActiveSlots
    }

    var canRemoveActiveSlot: Bool {
        activeSlotCount > SamplerConstants.defaultActiveSlots
    }

    var showsSlotCountControls: Bool {
        activeTab == "recent" && (canAddActiveSlot || canRemoveActiveSlot)
    }

    let preferencesStore: PreferencesStore
    let audioPlayback: AudioPlaybackService
    private let shortcutManager = GlobalShortcutManager()

    private var nextRecentSequence = 1
    private var loadedCapture: SamplerCapture?
    private var captureTask: Task<Void, Never>?
    private var cancellables = Set<AnyCancellable>()

    var outputDeviceUID: String {
        preferencesStore.preferences.samplerAudioOutputDeviceUID
    }

    var themeMode: SamplerThemeMode {
        preferencesStore.preferences.themeMode
    }

    init(preferencesStore: PreferencesStore, audioPlayback: AudioPlaybackService) {
        self.preferencesStore = preferencesStore
        self.audioPlayback = audioPlayback
        recentCaptures = preferencesStore.sessionState.recentCaptures
        activeSlotCount = preferencesStore.sessionState.activeSlotCount ?? SamplerConstants.defaultActiveSlots
        volume = preferencesStore.preferences.samplerVolume
        storedCaptures = []
        loadedCaptureId = preferencesStore.sessionState.loadedCaptureId
        activeTab = preferencesStore.sessionState.activeTab
        syncRecentCapturesToActiveSlotCount()

        audioPlayback.onPlaybackFinished = { [weak self] in
            Task { @MainActor in
                self?.refreshPlaybackStatus()
            }
        }

        audioPlayback.objectWillChange
            .sink { [weak self] _ in
                self?.objectWillChange.send()
            }
            .store(in: &cancellables)

        reloadStoredLibrary()
        reconcileSelection()
        rehydrateLoadedCaptureOnLaunch()
        refreshOutputDevices()
        applyAudioSettings()
        shortcutManager.start { [weak self] shortcutID in
            Task { @MainActor in
                self?.handleGlobalShortcut(shortcutID)
            }
        }
        registerShortcuts()
        applyThemeAppearance()
    }

    func setThemeMode(_ mode: SamplerThemeMode) {
        preferencesStore.setThemeMode(mode)
        applyThemeAppearance()
    }

    func applyThemeAppearance() {
        themeMode.applyAppAppearance()
    }

    func refreshOutputDevices() {
        outputDevices = AudioOutputDeviceService.enumerateOutputDevices()
    }

    func applyAudioSettings() {
        audioPlayback.setVolume(volume)
        audioPlayback.setOutputDeviceUID(outputDeviceUID)
    }

    func setOutputDeviceUID(_ uid: String) {
        preferencesStore.setOutputDeviceUID(uid)
        audioPlayback.setOutputDeviceUID(uid)
        refreshStatusIfIdle()
    }

    func setVolume(_ value: Int) {
        let clamped = max(0, min(SamplerConstants.maxVolume, value))
        volume = clamped
        preferencesStore.setVolume(clamped)
        audioPlayback.setVolume(clamped)
    }

    func resetVolumeToDefault() {
        setVolume(SamplerConstants.defaultVolume)
    }

    func setActiveTab(_ tab: String) {
        activeTab = tab == "stored" ? "stored" : "recent"
        preferencesStore.sessionState.activeTab = activeTab
        preferencesStore.saveSessionState()
        reconcileSelection()
        refreshStatusIfIdle()
    }

    func selectCapture(id: String, source: String) {
        selectedCaptureId = id
        selectedCaptureSource = source
        if source == "stored" {
            ensureStoredCaptureLoaded(id: id)
        }
    }

    func captureButtonTapped(slot: Int? = nil) {
        if let slot {
            targetSlotIndex = slot - 1
        } else {
            targetSlotIndex = nil
        }
        performCapture(targetSlotIndex: targetSlotIndex)
    }

    func playToggleTapped() {
        if audioPlayback.isPlaying {
            audioPlayback.stop()
            refreshStatusIfIdle()
            return
        }
        playSelectedCapture()
    }

    func playSelectedCapture() {
        guard let capture = selectedCapture() else {
            setStatus(activeTab == "stored" ? "No stored sample is selected yet." : "No sampler slot is selected yet.", isError: true)
            return
        }
        playCapture(capture, source: selectedCaptureSource)
    }

    func playCapture(_ capture: SamplerCapture, source: String, toggleIfLoaded: Bool = true) {
        if toggleIfLoaded, audioPlayback.isPlaying, audioPlayback.playingCaptureId == capture.id {
            audioPlayback.stop()
            refreshStatusIfIdle()
            return
        }

        selectCapture(id: capture.id, source: source)

        do {
            if loadedCaptureId != capture.id || loadedCapture?.filePath != capture.filePath {
                try audioPlayback.load(capture: capture)
                loadedCaptureId = capture.id
                loadedCapture = capture
                preferencesStore.sessionState.loadedCaptureId = loadedCaptureId
                preferencesStore.saveSessionState()
            }
            try audioPlayback.play(captureId: capture.id)
            let outputLabel = outputLabel(for: outputDeviceUID)
            setStatus("Playing \(capture.slotLabel) on \(outputLabel).")
        } catch {
            setStatus(error.localizedDescription, isError: true)
        }
    }

    func addActiveSlot() {
        guard canAddActiveSlot else { return }
        activeSlotCount += 1
        recentCaptures.append(nil)
        persistSessionState()
        resizeWindowForActiveSlots()
        setStatus("Added slot \(activeSlotCount).")
    }

    func removeActiveSlot() {
        guard canRemoveActiveSlot else { return }

        let lastIndex = activeSlotCount - 1
        if let removed = recentCaptures[lastIndex] {
            if removed.id == loadedCaptureId {
                audioPlayback.stop(updateStatus: false)
                loadedCaptureId = ""
                loadedCapture = nil
                preferencesStore.sessionState.loadedCaptureId = ""
            }
            if removed.id == selectedCaptureId, selectedCaptureSource == "recent" {
                selectedCaptureId = ""
            }
        }

        recentCaptures.removeLast()
        activeSlotCount -= 1
        persistSessionState()
        resizeWindowForActiveSlots()
        reconcileSelection()
        refreshStatusIfIdle()
        setStatus("Removed slot. \(activeSlotCount) active slots.")
    }

    func playSlot(at index: Int) {
        guard index >= 0, index < activeSlotCount, let capture = recentCaptures[index] else {
            setStatus("No sampler file is stored in slot \(index + 1).", isError: true)
            return
        }
        selectCapture(id: capture.id, source: "recent")
        playCapture(capture, source: "recent")
    }

    func deleteActiveSlot(at index: Int) {
        guard index >= 0, index < recentCaptures.count else { return }
        let removed = recentCaptures[index]
        recentCaptures[index] = nil
        persistRecentCaptures()

        if let removed {
            if removed.id == loadedCaptureId {
                audioPlayback.stop(updateStatus: false)
                loadedCaptureId = ""
                loadedCapture = nil
                preferencesStore.sessionState.loadedCaptureId = ""
                preferencesStore.saveSessionState()
            }
            if removed.id == selectedCaptureId, selectedCaptureSource == "recent" {
                selectedCaptureId = ""
            }
        }
        reconcileSelection()
        refreshStatusIfIdle()
    }

    func toggleStore(at index: Int) {
        guard index >= 0, index < recentCaptures.count, let capture = recentCaptures[index] else { return }

        if capture.saved {
            if let stored = storedCaptures.first(where: { $0.sourceIdentity == capture.sourceIdentity }) {
                deleteStoredCapture(id: stored.id)
            }
            recentCaptures[index]?.saved = false
            persistRecentCaptures()
            refreshStatusIfIdle()
            return
        }

        do {
            let stored = try StoredLibraryService.storeCapture(from: capture)
            storedCaptures.append(stored)
            recentCaptures[index]?.saved = true
            persistRecentCaptures()
            setStatus("Stored \(stored.slotLabel).")
        } catch {
            setStatus(error.localizedDescription, isError: true)
        }
    }

    func revealStoredCaptureInFinder(id: String) {
        guard let capture = storedCaptures.first(where: { $0.id == id }) else {
            setStatus("Stored sample was not found.", isError: true)
            return
        }

        let fileURL = URL(fileURLWithPath: capture.filePath)
        guard FileManager.default.fileExists(atPath: fileURL.path) else {
            setStatus("The stored audio file is missing on disk.", isError: true)
            return
        }

        NSWorkspace.shared.activateFileViewerSelecting([fileURL])
        setStatus("Revealed \(capture.slotLabel) in Finder.")
    }

    func deleteStoredCapture(id: String) {
        do {
            if let removed = try StoredLibraryService.deleteStoredCapture(id: id) {
                if removed.id == loadedCaptureId {
                    audioPlayback.stop(updateStatus: false)
                    loadedCaptureId = ""
                    loadedCapture = nil
                }
                storedCaptures.removeAll { $0.id == id }
                for index in recentCaptures.indices {
                    if recentCaptures[index]?.id == removed.id || recentCaptures[index]?.sourceIdentity == removed.sourceIdentity {
                        recentCaptures[index]?.saved = false
                    }
                }
                persistRecentCaptures()
                reconcileSelection()
                refreshStatusIfIdle()
            }
        } catch {
            setStatus(error.localizedDescription, isError: true)
        }
    }

    func renameCapture(id: String, displayName: String, hasCustomDisplayName: Bool) {
        for index in recentCaptures.indices {
            if recentCaptures[index]?.id == id {
                recentCaptures[index]?.displayName = displayName
                recentCaptures[index]?.hasCustomDisplayName = hasCustomDisplayName
            }
        }

        if let storedIndex = storedCaptures.firstIndex(where: { $0.id == id }) {
            do {
                guard let updated = try StoredLibraryService.renameStoredCapture(
                    id: id,
                    displayName: displayName,
                    hasCustomDisplayName: hasCustomDisplayName
                ) else {
                    return
                }
                storedCaptures[storedIndex] = updated
                if loadedCaptureId == id {
                    let wasPlaying = audioPlayback.isPlaying && audioPlayback.playingCaptureId == id
                    loadedCapture = updated
                    try? audioPlayback.load(capture: updated)
                    if wasPlaying {
                        try? audioPlayback.play(captureId: id)
                    }
                }
            } catch {
                setStatus(error.localizedDescription, isError: true)
                return
            }
        }

        persistRecentCaptures()
        refreshStatusIfIdle()
    }

    func loadDroppedFile(path: String, slotIndex: Int) {
        guard SamplerConstants.isSupportedAudioFile(path) else {
            setStatus("Drop a supported audio file such as WAV, AIFF, CAF, MP3, M4A, AAC, OGG, or FLAC.", isError: true)
            return
        }
        guard FileManager.default.fileExists(atPath: path) else {
            setStatus("The dropped sampler file was not found.", isError: true)
            return
        }

        guard slotIndex >= 0, slotIndex < activeSlotCount else { return }

        let fileName = URL(fileURLWithPath: path).lastPathComponent
        let baseName = (fileName as NSString).deletingPathExtension
        let capture = SamplerCapture(
            id: "sampler-capture-\(nextRecentSequence)",
            filePath: path,
            fileName: fileName,
            displayName: baseName,
            defaultDisplayName: baseName,
            hasCustomDisplayName: false,
            capturedAt: Date().timeIntervalSince1970 * 1000,
            saved: isStoredIdentity(path: path)
        )
        nextRecentSequence += 1

        recentCaptures[slotIndex] = capture
        persistRecentCaptures()
        selectCapture(id: capture.id, source: "recent")

        do {
            try audioPlayback.load(capture: capture)
            loadedCaptureId = capture.id
            loadedCapture = capture
            preferencesStore.sessionState.loadedCaptureId = loadedCaptureId
            preferencesStore.saveSessionState()
            setStatus("Ready.")
        } catch {
            setStatus(error.localizedDescription, isError: true)
        }
    }

    func beginShortcutCapture(shortcutID: String) {
        capturingShortcutId = shortcutID
        setStatus("Listening for a shortcut. Press Escape to cancel.")
    }

    func cancelShortcutCapture() {
        capturingShortcutId = ""
        refreshStatusIfIdle()
    }

    func handleShortcutKeyEvent(_ event: NSEvent) -> Bool {
        guard !capturingShortcutId.isEmpty else { return false }

        if event.keyCode == 53 {
            cancelShortcutCapture()
            return true
        }

        let accelerator = acceleratorFromEvent(event)
        guard !accelerator.isEmpty else { return true }

        let shortcutID = capturingShortcutId
        let current = preferencesStore.shortcutAccelerator(for: shortcutID)
        if current == accelerator {
            capturingShortcutId = ""
            if let slot = ShortcutDefinitions.slotNumber(for: shortcutID) {
                let label = ShortcutDefinitions.actionLabel(for: shortcutID)
                setStatus("\(label) shortcut for slot \(slot) is unchanged.")
            } else {
                setStatus("Shortcut unchanged.")
            }
            return true
        }

        preferencesStore.setShortcutBinding(id: shortcutID, accelerator: accelerator)
        registerShortcuts()
        let action = ShortcutDefinitions.actionLabel(for: shortcutID)
        let slot = ShortcutDefinitions.slotNumber(for: shortcutID) ?? 0
        capturingShortcutId = ""
        setStatus("Saved \(action.lowercased()) shortcut for slot \(slot).")
        return true
    }

    func handleGlobalShortcut(_ shortcutID: String) {
        if shortcutID.hasPrefix("samplerCaptureSlot"), let slot = ShortcutDefinitions.slotNumber(for: shortcutID) {
            guard FrontmostAppService.shouldAllowGlobalCaptureShortcut() else { return }
            captureButtonTapped(slot: slot)
            return
        }
        if shortcutID.hasPrefix("samplerSlot"), let slot = ShortcutDefinitions.slotNumber(for: shortcutID) {
            playSlot(at: slot - 1)
        }
    }

    func handleSpaceKey() -> Bool {
        playSelectedCapture()
        return true
    }

    func slotState(for capture: SamplerCapture?) -> String {
        guard let capture else { return "Empty" }
        if audioPlayback.isPlaying, audioPlayback.playingCaptureId == capture.id { return "Playing" }
        if loadedCaptureId == capture.id { return "Loaded" }
        return "Ready"
    }

    func shortcutLabel(for shortcutID: String) -> String {
        let accelerator = preferencesStore.shortcutAccelerator(for: shortcutID)
        if accelerator.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            return "None"
        }
        return ParsedShortcut.parse(accelerator)?.formattedLabel() ?? accelerator
    }

    func defaultShortcutLabel(for shortcutID: String) -> String {
        ShortcutDefinitions.formattedDefaultLabel(for: shortcutID)
    }

    func isShortcutAtDefault(for shortcutID: String) -> Bool {
        preferencesStore.shortcutAccelerator(for: shortcutID)
            == (ShortcutDefinitions.defaultBindings[shortcutID] ?? "")
    }

    func hasCustomShortcutBindings() -> Bool {
        ShortcutDefinitions.allShortcutIDs.contains { !isShortcutAtDefault(for: $0) }
    }

    func clearShortcut(shortcutID: String) {
        if capturingShortcutId == shortcutID {
            cancelShortcutCapture()
        }
        preferencesStore.setShortcutBinding(id: shortcutID, accelerator: "")
        registerShortcuts()
        let action = ShortcutDefinitions.actionLabel(for: shortcutID)
        let slot = ShortcutDefinitions.slotNumber(for: shortcutID) ?? 0
        setStatus("Cleared \(action.lowercased()) shortcut for slot \(slot).")
    }

    func resetShortcutToDefault(shortcutID: String) {
        if capturingShortcutId == shortcutID {
            cancelShortcutCapture()
        }
        let defaultAccelerator = ShortcutDefinitions.defaultBindings[shortcutID] ?? ""
        preferencesStore.setShortcutBinding(id: shortcutID, accelerator: defaultAccelerator)
        registerShortcuts()
        let action = ShortcutDefinitions.actionLabel(for: shortcutID)
        let slot = ShortcutDefinitions.slotNumber(for: shortcutID) ?? 0
        setStatus("Reset \(action.lowercased()) shortcut for slot \(slot) to default.")
    }

    func resetAllShortcutsToDefaults() {
        cancelShortcutCapture()
        preferencesStore.resetShortcutBindingsToDefaults()
        registerShortcuts()
        setStatus("Reset all sampler shortcuts to defaults.")
    }

    func persistSession() {
        preferencesStore.sessionState.recentCaptures = recentCaptures
        preferencesStore.sessionState.activeSlotCount = activeSlotCount
        preferencesStore.sessionState.loadedCaptureId = loadedCaptureId
        preferencesStore.sessionState.activeTab = activeTab
        preferencesStore.saveSessionState()
    }

    private func registerShortcuts() {
        shortcutManager.updateBindings(preferencesStore.preferences.shortcutBindings)
    }

    private func performCapture(targetSlotIndex: Int?) {
        guard !isCaptureInProgress else { return }

        captureTask?.cancel()
        isCaptureInProgress = true
        setStatus("Capturing from Pro Tools...", isError: false)

        let startingRecentCaptures = recentCaptures
        let startingStoredCaptures = storedCaptures
        let startingSequence = nextRecentSequence

        captureTask = Task {
            defer {
                Task { @MainActor in
                    isCaptureInProgress = false
                    captureTask = nil
                }
            }

            do {
                var slots = startingRecentCaptures
                var sequence = startingSequence
                let placed = try await CaptureTimeout.run(
                    seconds: PTSLCaptureConstants.captureMaxDurationSeconds,
                    message: "Capture timed out after \(Int(PTSLCaptureConstants.captureMaxDurationSeconds)) seconds. Make sure Pro Tools is running with a clip or edit range selected."
                ) {
                    try await ProToolsCaptureService.shared.captureAndPlace(
                        targetSlotIndex: targetSlotIndex,
                        recentCaptures: &slots,
                        nextSequence: &sequence,
                        storedCaptures: startingStoredCaptures,
                        onStatus: { message in
                            Task { @MainActor in
                                self.setStatus(message, isError: false)
                            }
                        }
                    )
                }

                guard !Task.isCancelled else { return }

                await MainActor.run {
                    recentCaptures = slots
                    nextRecentSequence = sequence
                    persistRecentCaptures()
                    selectCapture(id: placed.capture.id, source: "recent")
                }

                try await MainActor.run {
                    try audioPlayback.load(capture: placed.capture)
                    loadedCaptureId = placed.capture.id
                    loadedCapture = placed.capture
                    preferencesStore.sessionState.loadedCaptureId = loadedCaptureId
                    preferencesStore.saveSessionState()
                    setStatus("Ready.")
                }
            } catch {
                guard !Task.isCancelled else { return }
                await MainActor.run {
                    setStatus(error.localizedDescription, isError: true)
                }
            }
        }
    }

    private func rehydrateLoadedCaptureOnLaunch() {
        guard !loadedCaptureId.isEmpty else { return }

        if let capture = recentCaptures.compactMap({ $0 }).first(where: { $0.id == loadedCaptureId }) {
            rehydrateCaptureIfFileExists(capture)
            return
        }

        if let capture = storedCaptures.first(where: { $0.id == loadedCaptureId }) {
            rehydrateCaptureIfFileExists(capture)
            return
        }

        clearStaleLoadedCapture()
    }

    private func rehydrateCaptureIfFileExists(_ capture: SamplerCapture) {
        guard FileManager.default.fileExists(atPath: capture.filePath) else {
            clearStaleLoadedCapture()
            return
        }

        do {
            try audioPlayback.load(capture: capture)
            loadedCapture = capture
        } catch {
            clearStaleLoadedCapture()
        }
    }

    private func clearStaleLoadedCapture() {
        loadedCaptureId = ""
        loadedCapture = nil
        preferencesStore.sessionState.loadedCaptureId = ""
        preferencesStore.saveSessionState()
    }

    private func reloadStoredLibrary() {
        let metadata = StoredLibraryService.loadLibrary()
        storedCaptures = metadata.captures
        nextRecentSequence = max(nextRecentSequence, recentCaptures.compactMap { $0?.id }.compactMap { parseSequence($0) }.max() ?? 0) + 1

        for index in recentCaptures.indices {
            if let capture = recentCaptures[index] {
                recentCaptures[index]?.saved = storedCaptures.contains { $0.sourceIdentity == capture.sourceIdentity }
            }
        }
    }

    private func parseSequence(_ id: String) -> Int? {
        let pattern = #"sampler-capture-(\d+)"#
        guard let match = id.range(of: pattern, options: .regularExpression) else { return nil }
        let digits = id[match].filter(\.isNumber)
        return Int(digits)
    }

    private func persistRecentCaptures() {
        preferencesStore.sessionState.recentCaptures = recentCaptures
        preferencesStore.sessionState.activeSlotCount = activeSlotCount
        preferencesStore.saveSessionState()
    }

    private func persistSessionState() {
        preferencesStore.sessionState.recentCaptures = recentCaptures
        preferencesStore.sessionState.activeSlotCount = activeSlotCount
        preferencesStore.saveSessionState()
    }

    private func syncRecentCapturesToActiveSlotCount() {
        while recentCaptures.count < activeSlotCount {
            recentCaptures.append(nil)
        }
        if recentCaptures.count > activeSlotCount {
            recentCaptures = Array(recentCaptures.prefix(activeSlotCount))
        }
    }

    func resizeWindowForActiveSlots() {
        guard let window = NSApp.keyWindow ?? NSApp.windows.first(where: { $0.isVisible && $0.canBecomeKey }) else {
            return
        }

        let targetHeight = SamplerConstants.contentHeight(
            forActiveSlotCount: activeSlotCount,
            showsSlotControls: showsSlotCountControls
        )
        var frame = window.frame
        let heightDelta = targetHeight - frame.size.height
        frame.origin.y -= heightDelta
        frame.size.height = targetHeight
        window.setFrame(frame, display: true, animate: true)
        preferencesStore.updateWindowFrame(window.frame)
    }

    func preferredContentHeight() -> CGFloat {
        SamplerConstants.contentHeight(
            forActiveSlotCount: activeSlotCount,
            showsSlotControls: showsSlotCountControls
        )
    }

    private func selectedCapture() -> SamplerCapture? {
        if selectedCaptureSource == "stored" {
            return storedCaptures.first { $0.id == selectedCaptureId }
        }
        if let index = recentCaptures.firstIndex(where: { $0?.id == selectedCaptureId }) {
            return recentCaptures[index]
        }
        return nil
    }

    private func reconcileSelection() {
        if activeTab == "stored" {
            if !storedCaptures.contains(where: { $0.id == selectedCaptureId }) {
                selectedCaptureId = storedCaptures.first?.id ?? ""
                selectedCaptureSource = "stored"
            }
            return
        }

        if !recentCaptures.contains(where: { $0?.id == selectedCaptureId }) {
            selectedCaptureId = recentCaptures.compactMap { $0 }.first?.id ?? ""
            selectedCaptureSource = "recent"
        }
    }

    private func ensureStoredCaptureLoaded(id: String) {
        guard let capture = storedCaptures.first(where: { $0.id == id }) else { return }
        guard loadedCaptureId != capture.id else { return }
        do {
            try audioPlayback.load(capture: capture)
            loadedCaptureId = capture.id
            loadedCapture = capture
            preferencesStore.sessionState.loadedCaptureId = loadedCaptureId
            preferencesStore.saveSessionState()
        } catch {
            setStatus(error.localizedDescription, isError: true)
        }
    }

    private func isStoredIdentity(path: String) -> Bool {
        storedCaptures.contains { $0.sourceIdentity == path }
    }

    private func outputLabel(for uid: String) -> String {
        if uid.isEmpty { return "System Default" }
        return outputDevices.first(where: { $0.uid == uid })?.name ?? "Selected Output"
    }

    private func setStatus(_ message: String, isError: Bool = false) {
        statusMessage = message
        statusIsError = isError
    }

    private func refreshStatusIfIdle() {
        if audioPlayback.isPlaying, let playing = loadedCapture {
            let outputLabel = outputLabel(for: outputDeviceUID)
            setStatus("Playing \(playing.slotLabel) on \(outputLabel).")
            return
        }
        if !isCaptureInProgress {
            setStatus("Ready.")
        }
    }

    private func refreshPlaybackStatus() {
        refreshStatusIfIdle()
    }

    private func acceleratorFromEvent(_ event: NSEvent) -> String {
        var parts: [String] = []
        if event.modifierFlags.contains(.command) { parts.append("Command") }
        if event.modifierFlags.contains(.control) { parts.append("Control") }
        if event.modifierFlags.contains(.option) { parts.append("Alt") }
        if event.modifierFlags.contains(.shift) { parts.append("Shift") }

        let keyToken = keyTokenFromEvent(event)
        guard !keyToken.isEmpty else { return "" }
        parts.append(keyToken)
        return parts.joined(separator: "+")
    }

    private func keyTokenFromEvent(_ event: NSEvent) -> String {
        let map: [UInt16: String] = [
            0: "A", 11: "B", 8: "C", 2: "D", 14: "E", 3: "F", 5: "G", 4: "H", 34: "I",
            38: "J", 40: "K", 37: "L", 46: "M", 45: "N", 31: "O", 35: "P", 12: "Q", 15: "R",
            1: "S", 17: "T", 32: "U", 9: "V", 13: "W", 7: "X", 16: "Y", 6: "Z",
            29: "0", 18: "1", 19: "2", 20: "3", 21: "4", 23: "5", 22: "6", 26: "7", 28: "8", 25: "9",
            122: "F1", 120: "F2", 99: "F3", 118: "F4", 96: "F5", 97: "F6", 98: "F7", 100: "F8",
            101: "F9", 109: "F10", 103: "F11", 111: "F12", 105: "F13", 107: "F14", 113: "F15", 106: "F16",
            49: "Space", 36: "Enter", 51: "Delete", 53: "Escape", 48: "Tab"
        ]
        return map[event.keyCode] ?? ""
    }
}
