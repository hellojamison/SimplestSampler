#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct PluginBridgeSlot {
    int index = 0;
    std::string id;
    std::string filePath;
    std::string displayName;
    std::string source = "aax-audiosuite-capture";
    int64_t capturedAtMs = 0;
};

struct PluginBridgeDocument {
    int activeSlotCount = 4;
    std::vector<PluginBridgeSlot> slots;
    std::map<std::string, std::string> shortcutBindings;
};

class PluginLibraryBridge {
public:
    static std::string ApplicationSupportDirectory();
    static std::string GeneratedCapturesDirectory();
    static std::string BridgeFilePath();

    static bool EnsureDirectories();
    static bool ReadDocument(PluginBridgeDocument& outDocument);
    static bool WriteDocument(const PluginBridgeDocument& document);
    static bool AtomicWriteDocument(const PluginBridgeDocument& document);

    static std::string EscapeJson(const std::string& value);
    static std::string SerializeDocument(const PluginBridgeDocument& document);
};
