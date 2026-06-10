import AVFoundation
import CoreAudio
import Foundation

struct AudioOutputDevice: Identifiable, Equatable, Hashable {
    var id: String
    var uid: String
    var name: String
}

enum AudioOutputDeviceService {
    static func enumerateOutputDevices() -> [AudioOutputDevice] {
        var devices: [AudioOutputDevice] = []
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDevices,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )

        var dataSize: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &dataSize) == noErr else {
            return devices
        }

        let deviceCount = Int(dataSize) / MemoryLayout<AudioDeviceID>.size
        var deviceIDs = [AudioDeviceID](repeating: 0, count: deviceCount)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &dataSize, &deviceIDs) == noErr else {
            return devices
        }

        for deviceID in deviceIDs {
            guard deviceHasOutputChannels(deviceID), let name = deviceName(deviceID), let uid = deviceUID(deviceID) else {
                continue
            }
            devices.append(AudioOutputDevice(id: uid, uid: uid, name: name))
        }

        return devices.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }

    static func deviceID(forUID uid: String) -> AudioDeviceID? {
        guard !uid.isEmpty else { return nil }
        return enumerateOutputDevices().first(where: { $0.uid == uid }).flatMap { _ in
            lookupDeviceID(uid: uid)
        }
    }

    @discardableResult
    static func setOutputDevice(uid: String, on audioUnit: AudioUnit) -> Bool {
        guard !uid.isEmpty, var deviceID = lookupDeviceID(uid: uid) else { return false }
        let status = AudioUnitSetProperty(
            audioUnit,
            kAudioOutputUnitProperty_CurrentDevice,
            kAudioUnitScope_Global,
            0,
            &deviceID,
            UInt32(MemoryLayout<AudioDeviceID>.size)
        )
        return status == noErr
    }

    private static func lookupDeviceID(uid: String) -> AudioDeviceID? {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDevices,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )

        var dataSize: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &dataSize) == noErr else {
            return nil
        }

        let deviceCount = Int(dataSize) / MemoryLayout<AudioDeviceID>.size
        var deviceIDs = [AudioDeviceID](repeating: 0, count: deviceCount)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &dataSize, &deviceIDs) == noErr else {
            return nil
        }

        for deviceID in deviceIDs {
            if deviceUID(deviceID) == uid {
                return deviceID
            }
        }
        return nil
    }

    private static func deviceHasOutputChannels(_ deviceID: AudioDeviceID) -> Bool {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyStreamConfiguration,
            mScope: kAudioDevicePropertyScopeOutput,
            mElement: kAudioObjectPropertyElementMain
        )

        var dataSize: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(deviceID, &address, 0, nil, &dataSize) == noErr else {
            return false
        }

        let bufferList = UnsafeMutablePointer<AudioBufferList>.allocate(capacity: Int(dataSize))
        defer { bufferList.deallocate() }

        guard AudioObjectGetPropertyData(deviceID, &address, 0, nil, &dataSize, bufferList) == noErr else {
            return false
        }

        let buffers = UnsafeMutableAudioBufferListPointer(bufferList)
        return buffers.contains { $0.mNumberChannels > 0 }
    }

    private static func cfStringProperty(_ deviceID: AudioDeviceID, selector: AudioObjectPropertySelector) -> String? {
        var address = AudioObjectPropertyAddress(
            mSelector: selector,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )

        var unmanagedValue: Unmanaged<CFString>?
        var dataSize = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        let status = withUnsafeMutablePointer(to: &unmanagedValue) { pointer in
            AudioObjectGetPropertyData(deviceID, &address, 0, nil, &dataSize, pointer)
        }
        guard status == noErr, let unmanagedValue else { return nil }
        return unmanagedValue.takeRetainedValue() as String
    }

    private static func deviceName(_ deviceID: AudioDeviceID) -> String? {
        cfStringProperty(deviceID, selector: kAudioDevicePropertyDeviceNameCFString)
    }

    private static func deviceUID(_ deviceID: AudioDeviceID) -> String? {
        cfStringProperty(deviceID, selector: kAudioDevicePropertyDeviceUID)
    }
}
