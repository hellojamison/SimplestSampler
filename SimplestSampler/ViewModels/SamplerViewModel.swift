import AppKit
import Combine
import Foundation
import SwiftUI
import UniformTypeIdentifiers

@MainActor
final class SamplerViewModel: ObservableObject {
    @Published var recentCaptures: [SamplerCapture?]
    @Published var soundboardPads: [SamplerCapture?]
    @Published var storedCaptures: [SamplerCapture]
    @Published var storedCategories: [StoredCategory] = []
    @Published var storedCategoryFilter = StoredCategoryFilter.all
    @Published var selectedCaptureId = ""
    @Published var selectedCaptureSource = "recent"
    @Published var loadedCaptureId = ""
    @Published var activeTab = "recent"
    @Published var statusMessage = "Ready."
    @Published var statusIsError = false
    @Published var outputDevices: [AudioOutputDevice] = []
    @Published var capturingShortcutId = ""
    @Published var dropTargetIndex = -1
    @Published var selectedSoundboardPadIndex = -1
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

    var filteredStoredCaptures: [SamplerCapture] {
        storedCaptures.filter { StoredCategoryFilter.matches(capture: $0, filter: storedCategoryFilter) }
    }

    let preferencesStore: PreferencesStore
    let audioPlayback: AudioPlaybackService
    private let shortcutManager = GlobalShortcutManager()

    private var nextRecentSequence = 1
    private var nextSoundboardSequence = 1
    private var loadedCapture: SamplerCapture?
    private var captureTask: Task<Void, Never>?
    private var pluginBridgeWatcher: PluginBridgeWatcher?
    private var cancellables = Set<AnyCancellable>()

    var outputDeviceUID: String {
        preferencesStore.preferences.samplerAudioOutputDeviceUID
    }

    var appTheme: SamplerAppTheme {
        preferencesStore.preferences.appTheme
    }

    var themeMode: SamplerThemeMode {
        preferencesStore.preferences.themeMode
    }

    init(preferencesStore: PreferencesStore, audioPlayback: AudioPlaybackService) {
        self.preferencesStore = preferencesStore
        self.audioPlayback = audioPlayback
        recentCaptures = preferencesStore.sessionState.recentCaptures
        soundboardPads = preferencesStore.sessionState.soundboardPads
        activeSlotCount = preferencesStore.sessionState.activeSlotCount ?? SamplerConstants.defaultActiveSlots
        volume = preferencesStore.preferences.samplerVolume
        storedCaptures = []
        loadedCaptureId = preferencesStore.sessionState.loadedCaptureId
        activeTab = preferencesStore.sessionState.activeTab
        storedCategoryFilter = preferencesStore.sessionState.storedCategoryFilter ?? StoredCategoryFilter.all
        syncRecentCapturesToActiveSlotCount()
        if deduplicateSoundboardPads(preferredIndex: nil) {
            persistSoundboardPads()
        }

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
        syncPluginBridgeFromApp()
        startPluginBridgeWatcher()
        applyThemeAppearance()
    }

    func setThemeMode(_ mode: SamplerThemeMode) {
        preferencesStore.setThemeMode(mode)
        applyThemeAppearance()
    }

    func setAppTheme(_ appTheme: SamplerAppTheme) {
        preferencesStore.setAppTheme(appTheme)
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
        switch tab {
        case "stored":
            activeTab = "stored"
        case "simpleish":
            activeTab = "stored"
        case "soundboard":
            activeTab = "soundboard"
        default:
            activeTab = "recent"
        }
        preferencesStore.sessionState.activeTab = activeTab
        preferencesStore.saveSessionState()
        reconcileSelection()
        resizeWindowForCurrentTab()
        refreshStatusIfIdle()
    }

    func setStoredCategoryFilter(_ filter: String) {
        storedCategoryFilter = filter
        preferencesStore.sessionState.storedCategoryFilter = filter
        preferencesStore.saveSessionState()
        reconcileSelection()
        refreshStatusIfIdle()
    }

    func addCategory(name: String) {
        do {
            let category = try StoredLibraryService.addCategory(name: name)
            storedCategories.append(category)
            setStatus("Added category \(category.name).")
        } catch {
            setStatus(error.localizedDescription, isError: true)
        }
    }

    func renameCategory(id: String, name: String) {
        do {
            guard let updated = try StoredLibraryService.renameCategory(id: id, name: name) else { return }
            if let index = storedCategories.firstIndex(where: { $0.id == id }) {
                storedCategories[index] = updated
            }
            setStatus("Renamed category to \(updated.name).")
        } catch {
            setStatus(error.localizedDescription, isError: true)
        }
    }

    func deleteCategory(id: String) {
        do {
            guard try StoredLibraryService.deleteCategory(id: id) else { return }
            storedCategories.removeAll { $0.id == id }
            for index in storedCaptures.indices where storedCaptures[index].categoryId == id {
                storedCaptures[index].categoryId = nil
            }
            if storedCategoryFilter == id {
                setStoredCategoryFilter(StoredCategoryFilter.all)
            }
            reconcileSelection()
            setStatus("Deleted category.")
        } catch {
            setStatus(error.localizedDescription, isError: true)
        }
    }

    func setCaptureCategory(captureId: String, categoryId: String?) {
        do {
            guard let updated = try StoredLibraryService.setCaptureCategory(captureId: captureId, categoryId: categoryId) else {
                return
            }
            if let index = storedCaptures.firstIndex(where: { $0.id == captureId }) {
                storedCaptures[index] = updated
            }
            if loadedCaptureId == captureId {
                loadedCapture = updated
            }
            reconcileSelection()
            let label = categoryLabel(for: updated.categoryId)
            setStatus("Moved \(updated.slotLabel) to \(label).")
        } catch {
            setStatus(error.localizedDescription, isError: true)
        }
    }

    func categoryLabel(for categoryId: String?) -> String {
        guard let categoryId, !categoryId.isEmpty else { return "Uncategorized" }
        return storedCategories.first(where: { $0.id == categoryId })?.name ?? "Uncategorized"
    }

    func categoryLabel(for capture: SamplerCapture) -> String {
        categoryLabel(for: capture.categoryId)
    }

    func selectCapture(id: String, source: String) {
        selectedCaptureId = id
        selectedCaptureSource = source
        if source == "stored" {
            ensureStoredCaptureLoaded(id: id)
        }
    }

    func captureButtonTapped(slot: Int? = nil) {
        if activeTab == "soundboard" {
            let padIndex = slot.map { $0 - 1 } ?? soundboardTargetPadIndex()
            guard let padIndex else {
                setStatus("All sound board pads are full.", isError: true)
                return
            }
            performCapture(targetSlotIndex: nil, soundboardPadIndex: padIndex)
            return
        }

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
            let message: String
            switch activeTab {
            case "stored":
                message = "No stored sample is selected yet."
            case "soundboard":
                message = "No sound board pad is selected yet."
            default:
                message = "No sampler slot is selected yet."
            }
            setStatus(message, isError: true)
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

    func moveActiveSlot(from sourceIndex: Int, to destinationIndex: Int) {
        guard sourceIndex != destinationIndex,
              sourceIndex >= 0, sourceIndex < activeSlotCount,
              destinationIndex >= 0, destinationIndex < activeSlotCount else { return }

        let moved = recentCaptures.remove(at: sourceIndex)
        recentCaptures.insert(moved, at: destinationIndex)
        persistRecentCaptures()
        reconcileSelection()
        refreshStatusIfIdle()
    }

    func activeSlotDragPayload(for index: Int) -> ActiveSlotDragPayload? {
        guard index >= 0, index < activeSlotCount else { return nil }
        return ActiveSlotDragPayload(
            slotIndex: index,
            captureId: recentCaptures[index]?.id
        )
    }

    func soundboardPadDragPayload(for index: Int) -> SoundboardPadDragPayload? {
        guard index >= 0, index < soundboardPads.count, let capture = soundboardPads[index] else { return nil }
        return SoundboardPadDragPayload(
            padIndex: index,
            captureId: capture.id
        )
    }

    func assignActiveCaptureToSoundboard(from sourceIndex: Int) {
        guard let padIndex = soundboardTargetPadIndex() else {
            setStatus("All sound board pads are full.", isError: true)
            return
        }
        assignActiveCaptureToSoundboard(from: sourceIndex, padIndex: padIndex)
    }

    func assignActiveCaptureToSoundboard(from sourceIndex: Int, padIndex: Int) {
        guard let capture = activeCapture(at: sourceIndex) else {
            setStatus("Drag a filled Active slot onto a sound board pad.", isError: true)
            return
        }
        assignCaptureToSoundboard(capture, padIndex: padIndex)
    }

    func storeActiveCapture(from sourceIndex: Int) {
        storeActiveCapture(from: sourceIndex, categoryDrop: .preserveExisting)
    }

    func storeActiveCapture(from sourceIndex: Int, categoryId: String?) {
        storeActiveCapture(from: sourceIndex, categoryDrop: .assign(categoryId))
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

        for index in soundboardPads.indices {
            if soundboardPads[index]?.id == id {
                soundboardPads[index]?.displayName = displayName
                soundboardPads[index]?.hasCustomDisplayName = hasCustomDisplayName
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
        persistSoundboardPads()
        refreshStatusIfIdle()
    }

    func playSoundboardPad(at index: Int) {
        guard index >= 0, index < soundboardPads.count, let capture = soundboardPads[index] else {
            setStatus("No clip in sound board pad \(index + 1).", isError: true)
            return
        }
        selectedSoundboardPadIndex = index
        selectCapture(id: capture.id, source: "soundboard")
        playCapture(capture, source: "soundboard")
    }

    func clearSoundboardPad(at index: Int) {
        guard index >= 0, index < soundboardPads.count else { return }
        let removed = soundboardPads[index]
        soundboardPads[index] = nil
        persistSoundboardPads()

        if let removed {
            if removed.id == loadedCaptureId {
                audioPlayback.stop(updateStatus: false)
                loadedCaptureId = ""
                loadedCapture = nil
                preferencesStore.sessionState.loadedCaptureId = ""
                preferencesStore.saveSessionState()
            }
            if removed.id == selectedCaptureId, selectedCaptureSource == "soundboard" {
                selectedCaptureId = ""
            }
        }

        reconcileSelection()
        refreshStatusIfIdle()
    }

    func moveSoundboardPad(from sourceIndex: Int, to destinationIndex: Int) {
        guard sourceIndex != destinationIndex,
              sourceIndex >= 0, sourceIndex < soundboardPads.count,
              destinationIndex >= 0, destinationIndex < soundboardPads.count,
              let sourceCapture = soundboardPads[sourceIndex] else {
            return
        }

        let destinationCapture = soundboardPads[destinationIndex]
        soundboardPads[sourceIndex] = destinationCapture
        soundboardPads[destinationIndex] = sourceCapture
        _ = deduplicateSoundboardPads(preferredIndex: destinationIndex)
        persistSoundboardPads()

        syncSelectedSoundboardPadIndexAfterMove(from: sourceIndex, to: destinationIndex)
        reconcileSelection()
        refreshStatusIfIdle()

        if destinationCapture == nil {
            setStatus("Moved pad \(sourceIndex + 1) to pad \(destinationIndex + 1).")
        } else {
            setStatus("Swapped pad \(sourceIndex + 1) with pad \(destinationIndex + 1).")
        }
    }

    func loadDroppedFileOnSoundboard(path: String, padIndex: Int) {
        guard SamplerConstants.isSupportedAudioFile(path) else {
            setStatus("Drop a supported audio file such as WAV, AIFF, CAF, MP3, M4A, AAC, OGG, or FLAC.", isError: true)
            return
        }
        guard FileManager.default.fileExists(atPath: path) else {
            setStatus("The dropped sampler file was not found.", isError: true)
            return
        }
        guard padIndex >= 0, padIndex < soundboardPads.count else { return }

        let fileName = URL(fileURLWithPath: path).lastPathComponent
        let baseName = (fileName as NSString).deletingPathExtension
        let existing = soundboardPads[padIndex]
        let capture = SamplerCapture(
            id: existing?.id ?? "sampler-soundboard-\(nextSoundboardSequence)",
            filePath: path,
            fileName: fileName,
            displayName: baseName,
            defaultDisplayName: baseName,
            hasCustomDisplayName: false,
            capturedAt: Date().timeIntervalSince1970 * 1000,
            saved: isStoredIdentity(path: path)
        )
        if existing == nil {
            nextSoundboardSequence += 1
        }

        soundboardPads[padIndex] = capture
        _ = deduplicateSoundboardPads(preferredIndex: padIndex)
        persistSoundboardPads()
        selectCapture(id: capture.id, source: "soundboard")

        do {
            try audioPlayback.load(capture: capture)
            loadedCaptureId = capture.id
            loadedCapture = capture
            preferencesStore.sessionState.loadedCaptureId = loadedCaptureId
            preferencesStore.saveSessionState()
            setStatus("Loaded \(capture.slotLabel) on pad \(padIndex + 1).")
        } catch {
            setStatus(error.localizedDescription, isError: true)
        }
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
        syncPluginBridgeFromApp()
    }

    private func startPluginBridgeWatcher() {
        pluginBridgeWatcher = PluginBridgeWatcher { [weak self] in
            self?.reloadFromPluginBridge()
        }
    }

    private func syncPluginBridgeFromApp() {
        PluginBridgeService.syncFromApp(
            recentCaptures: recentCaptures,
            activeSlotCount: activeSlotCount,
            preferences: preferencesStore.preferences
        )
    }

    private func reloadFromPluginBridge() {
        let document = PluginBridgeService.loadDocument()
        guard PluginBridgeService.mergePluginCaptures(
            into: &recentCaptures,
            activeSlotCount: activeSlotCount,
            storedCaptures: storedCaptures,
            document: document
        ) else {
            return
        }

        persistRecentCaptures()
        reconcileSelection()
        refreshStatusIfIdle()
        setStatus("Updated Active slots from AudioSuite capture.")
    }

    private func performCapture(targetSlotIndex: Int?, soundboardPadIndex: Int? = nil) {
        guard !isCaptureInProgress else { return }

        captureTask?.cancel()
        isCaptureInProgress = true
        setStatus("Capturing from Pro Tools...", isError: false)

        let startingRecentCaptures = recentCaptures
        let startingStoredCaptures = storedCaptures
        let startingSequence = nextRecentSequence
        let startingSoundboardPads = soundboardPads
        let startingSoundboardSequence = nextSoundboardSequence

        captureTask = Task {
            defer {
                Task { @MainActor in
                    isCaptureInProgress = false
                    captureTask = nil
                }
            }

            do {
                let usesSoundboard = soundboardPadIndex != nil
                var slots: [SamplerCapture?] = usesSoundboard ? [nil] : startingRecentCaptures
                var sequence = startingSequence
                let captureTargetSlot = usesSoundboard ? 0 : targetSlotIndex
                let placed = try await CaptureTimeout.run(
                    seconds: PTSLCaptureConstants.captureMaxDurationSeconds,
                    message: "Capture timed out after \(Int(PTSLCaptureConstants.captureMaxDurationSeconds)) seconds. Make sure Pro Tools is running with a clip or edit range selected."
                ) {
                    try await ProToolsCaptureService.shared.captureAndPlace(
                        targetSlotIndex: captureTargetSlot,
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
                    if let soundboardPadIndex {
                        var capture = placed.capture
                        let existing = startingSoundboardPads[soundboardPadIndex]
                        capture.id = existing?.id ?? "sampler-soundboard-\(startingSoundboardSequence)"
                        if existing == nil {
                            nextSoundboardSequence = startingSoundboardSequence + 1
                        }
                        soundboardPads = startingSoundboardPads
                        soundboardPads[soundboardPadIndex] = capture
                        _ = deduplicateSoundboardPads(preferredIndex: soundboardPadIndex)
                        persistSoundboardPads()
                        selectCapture(id: capture.id, source: "soundboard")

                        do {
                            try audioPlayback.load(capture: capture)
                            loadedCaptureId = capture.id
                            loadedCapture = capture
                            preferencesStore.sessionState.loadedCaptureId = loadedCaptureId
                            preferencesStore.saveSessionState()
                            setStatus("Captured \(capture.slotLabel) on pad \(soundboardPadIndex + 1).")
                        } catch {
                            setStatus(error.localizedDescription, isError: true)
                        }
                        return
                    }

                    recentCaptures = slots
                    nextRecentSequence = sequence
                    persistRecentCaptures()
                    selectCapture(id: placed.capture.id, source: "recent")
                }

                guard soundboardPadIndex == nil else { return }

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

        if let capture = soundboardPads.compactMap({ $0 }).first(where: { $0.id == loadedCaptureId }) {
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
        storedCategories = metadata.categories
        if storedCategoryFilter != StoredCategoryFilter.all,
           storedCategoryFilter != StoredCategoryFilter.uncategorized,
           !storedCategories.contains(where: { $0.id == storedCategoryFilter }) {
            storedCategoryFilter = StoredCategoryFilter.all
            preferencesStore.sessionState.storedCategoryFilter = storedCategoryFilter
            preferencesStore.saveSessionState()
        }
        nextRecentSequence = max(nextRecentSequence, recentCaptures.compactMap { $0?.id }.compactMap { parseSequence($0, prefix: "sampler-capture-") }.max() ?? 0) + 1
        nextSoundboardSequence = max(
            nextSoundboardSequence,
            soundboardPads.compactMap { $0?.id }.compactMap { parseSequence($0, prefix: "sampler-soundboard-") }.max() ?? 0
        ) + 1

        for index in recentCaptures.indices {
            if let capture = recentCaptures[index] {
                recentCaptures[index]?.saved = storedCaptures.contains { $0.sourceIdentity == capture.sourceIdentity }
            }
        }
    }

    private func parseSequence(_ id: String, prefix: String) -> Int? {
        guard id.hasPrefix(prefix) else { return nil }
        let suffix = id.dropFirst(prefix.count)
        return Int(suffix)
    }

    private func activeCapture(at index: Int) -> SamplerCapture? {
        guard index >= 0, index < activeSlotCount else { return nil }
        return recentCaptures[index]
    }

    private func assignCaptureToSoundboard(_ sourceCapture: SamplerCapture, padIndex: Int) {
        guard padIndex >= 0, padIndex < soundboardPads.count else { return }

        let existing = soundboardPads[padIndex]
        var assigned = sourceCapture
        assigned.id = existing?.id ?? "sampler-soundboard-\(nextSoundboardSequence)"
        if existing == nil {
            nextSoundboardSequence += 1
        }

        soundboardPads[padIndex] = assigned
        _ = deduplicateSoundboardPads(preferredIndex: padIndex)
        persistSoundboardPads()
        selectedSoundboardPadIndex = padIndex
        selectCapture(id: assigned.id, source: "soundboard")

        do {
            try audioPlayback.load(capture: assigned)
            loadedCaptureId = assigned.id
            loadedCapture = assigned
            preferencesStore.sessionState.loadedCaptureId = loadedCaptureId
            preferencesStore.saveSessionState()
            setStatus("Loaded \(assigned.slotLabel) on pad \(padIndex + 1).")
        } catch {
            setStatus(error.localizedDescription, isError: true)
        }
    }

    private func storeActiveCapture(from sourceIndex: Int, categoryDrop: StoredCategoryDrop) {
        guard let capture = activeCapture(at: sourceIndex) else {
            setStatus("Drag a filled Active slot into Stored to keep it.", isError: true)
            return
        }

        do {
            let stored: SamplerCapture
            if recentCaptures[sourceIndex]?.saved != true {
                recentCaptures[sourceIndex]?.saved = true
                persistRecentCaptures()
            }
            if let existing = storedCaptures.first(where: { $0.sourceIdentity == capture.sourceIdentity }) {
                stored = existing
            } else {
                let created = try StoredLibraryService.storeCapture(from: capture)
                storedCaptures.append(created)
                stored = created
            }

            selectCapture(id: stored.id, source: "stored")

            switch categoryDrop {
            case .preserveExisting:
                setStatus("Stored \(stored.slotLabel).")
            case .assign(let categoryId):
                setCaptureCategory(captureId: stored.id, categoryId: categoryId)
            }
        } catch {
            setStatus(error.localizedDescription, isError: true)
        }
    }

    private func persistSoundboardPads() {
        preferencesStore.sessionState.soundboardPads = soundboardPads
        preferencesStore.saveSessionState()
    }

    private func deduplicateSoundboardPads(preferredIndex: Int?) -> Bool {
        var keptIndicesByID: [String: Int] = [:]
        var duplicateIndices = Set<Int>()

        if let preferredIndex,
           preferredIndex >= 0,
           preferredIndex < soundboardPads.count,
           let preferredID = soundboardPads[preferredIndex]?.id {
            keptIndicesByID[preferredID] = preferredIndex
        }

        for index in soundboardPads.indices {
            guard let captureID = soundboardPads[index]?.id else { continue }

            if let keptIndex = keptIndicesByID[captureID] {
                if keptIndex == index {
                    continue
                }

                if preferredIndex == index {
                    duplicateIndices.insert(keptIndex)
                    keptIndicesByID[captureID] = index
                } else {
                    duplicateIndices.insert(index)
                }
                continue
            }

            keptIndicesByID[captureID] = index
        }

        guard !duplicateIndices.isEmpty else { return false }

        for index in duplicateIndices {
            soundboardPads[index] = nil
        }

        return true
    }

    func selectSoundboardPad(at index: Int) {
        guard index >= 0, index < soundboardPads.count else { return }
        selectedSoundboardPadIndex = index
        if let capture = soundboardPads[index] {
            selectCapture(id: capture.id, source: "soundboard")
        } else {
            selectedCaptureId = ""
            selectedCaptureSource = "soundboard"
        }
    }

    private func soundboardTargetPadIndex() -> Int? {
        if selectedSoundboardPadIndex >= 0, selectedSoundboardPadIndex < soundboardPads.count {
            return selectedSoundboardPadIndex
        }
        if selectedCaptureSource == "soundboard",
           let selectedIndex = soundboardPads.firstIndex(where: { $0?.id == selectedCaptureId }) {
            return selectedIndex
        }
        return soundboardPads.firstIndex(where: { $0 == nil })
    }

    private func persistRecentCaptures() {
        preferencesStore.sessionState.recentCaptures = recentCaptures
        preferencesStore.sessionState.activeSlotCount = activeSlotCount
        preferencesStore.saveSessionState()
        syncPluginBridgeFromApp()
    }

    private func persistSessionState() {
        preferencesStore.sessionState.recentCaptures = recentCaptures
        preferencesStore.sessionState.activeSlotCount = activeSlotCount
        preferencesStore.saveSessionState()
        syncPluginBridgeFromApp()
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
        resizeWindowForCurrentTab()
    }

    func resizeWindowForCurrentTab() {
        guard let window = NSApp.keyWindow ?? NSApp.windows.first(where: { $0.isVisible && $0.canBecomeKey }) else {
            return
        }

        let targetHeight = preferredContentHeight()
        let currentContentHeight = window.contentRect(forFrameRect: window.frame).height
        guard currentContentHeight < targetHeight else { return }
        var frame = window.frame
        let heightDelta = targetHeight - currentContentHeight
        frame.origin.y -= heightDelta
        frame.size.height += heightDelta
        window.setFrame(frame, display: true, animate: true)
        preferencesStore.updateWindowFrame(window.frame)
    }

    func preferredContentHeight() -> CGFloat {
        SamplerConstants.contentHeight(
            forActiveTab: activeTab,
            activeSlotCount: activeSlotCount,
            showsSlotControls: showsSlotCountControls
        )
    }

    private func selectedCapture() -> SamplerCapture? {
        if selectedCaptureSource == "stored" {
            return storedCaptures.first { $0.id == selectedCaptureId }
        }
        if selectedCaptureSource == "soundboard" {
            return soundboardPads.compactMap { $0 }.first { $0.id == selectedCaptureId }
        }
        if let index = recentCaptures.firstIndex(where: { $0?.id == selectedCaptureId }) {
            return recentCaptures[index]
        }
        return nil
    }

    private func reconcileSelection() {
        if activeTab == "stored" {
            let visibleCaptures = filteredStoredCaptures
            if !visibleCaptures.contains(where: { $0.id == selectedCaptureId }) {
                selectedCaptureId = visibleCaptures.first?.id ?? ""
            }
            selectedCaptureSource = "stored"
            return
        }

        if activeTab == "soundboard" {
            if !soundboardPads.contains(where: { $0?.id == selectedCaptureId }) {
                selectedCaptureId = soundboardPads.compactMap { $0 }.first?.id ?? ""
            }
            selectedCaptureSource = "soundboard"
            return
        }

        if !recentCaptures.contains(where: { $0?.id == selectedCaptureId }) {
            selectedCaptureId = recentCaptures.compactMap { $0 }.first?.id ?? ""
            selectedCaptureSource = "recent"
        }
    }

    private func syncSelectedSoundboardPadIndexAfterMove(from sourceIndex: Int, to destinationIndex: Int) {
        if selectedCaptureSource == "soundboard", !selectedCaptureId.isEmpty {
            selectedSoundboardPadIndex = soundboardPads.firstIndex(where: { $0?.id == selectedCaptureId }) ?? -1
            return
        }

        if selectedSoundboardPadIndex == sourceIndex {
            selectedSoundboardPadIndex = destinationIndex
            return
        }

        if selectedSoundboardPadIndex == destinationIndex, soundboardPads[sourceIndex] != nil {
            selectedSoundboardPadIndex = sourceIndex
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

    private enum StoredCategoryDrop {
        case preserveExisting
        case assign(String?)
    }
}
