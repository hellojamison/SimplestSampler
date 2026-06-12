#include "PluginLibraryBridge.h"
#include "SimplestSampler_AS_Defs.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

std::string HomeDirectory() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) : std::string();
}

std::string Trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string UnescapeJsonString(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            const char next = value[++i];
            switch (next) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default: out.push_back(next); break;
            }
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

std::string ExtractJsonString(const std::string& object, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const size_t keyPos = object.find(needle);
    if (keyPos == std::string::npos) {
        return {};
    }
    const size_t colon = object.find(':', keyPos + needle.size());
    if (colon == std::string::npos) {
        return {};
    }
    size_t pos = colon + 1;
    while (pos < object.size() && std::isspace(static_cast<unsigned char>(object[pos]))) {
        ++pos;
    }
    if (pos >= object.size() || object[pos] != '"') {
        return {};
    }
    ++pos;
    std::string raw;
    while (pos < object.size()) {
        if (object[pos] == '\\' && pos + 1 < object.size()) {
            raw.push_back(object[pos]);
            raw.push_back(object[pos + 1]);
            pos += 2;
            continue;
        }
        if (object[pos] == '"') {
            break;
        }
        raw.push_back(object[pos]);
        ++pos;
    }
    return UnescapeJsonString(raw);
}

int64_t ExtractJsonInt(const std::string& object, const std::string& key, int64_t fallback = 0) {
    const std::string needle = "\"" + key + "\"";
    const size_t keyPos = object.find(needle);
    if (keyPos == std::string::npos) {
        return fallback;
    }
    const size_t colon = object.find(':', keyPos + needle.size());
    if (colon == std::string::npos) {
        return fallback;
    }
    size_t pos = colon + 1;
    while (pos < object.size() && std::isspace(static_cast<unsigned char>(object[pos]))) {
        ++pos;
    }
    const size_t end = object.find_first_of(",}\n", pos);
    const std::string token = Trim(object.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
    if (token == "null" || token.empty()) {
        return fallback;
    }
    return std::strtoll(token.c_str(), nullptr, 10);
}

std::vector<std::string> ExtractJsonObjects(const std::string& arrayBody) {
    std::vector<std::string> objects;
    int depth = 0;
    size_t start = std::string::npos;
    for (size_t i = 0; i < arrayBody.size(); ++i) {
        const char ch = arrayBody[i];
        if (ch == '{') {
            if (depth == 0) {
                start = i;
            }
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && start != std::string::npos) {
                objects.push_back(arrayBody.substr(start, i - start + 1));
                start = std::string::npos;
            }
        }
    }
    return objects;
}

std::string ReadFileToString(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool WriteStringToFile(const std::string& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return static_cast<bool>(output);
}

} // namespace

std::string PluginLibraryBridge::ApplicationSupportDirectory() {
    return HomeDirectory() + "/Library/Application Support/SimplestSampler";
}

std::string PluginLibraryBridge::GeneratedCapturesDirectory() {
    return ApplicationSupportDirectory() + "/Generated Sampler Captures";
}

std::string PluginLibraryBridge::BridgeFilePath() {
    return ApplicationSupportDirectory() + "/plugin-active-slots.json";
}

bool PluginLibraryBridge::EnsureDirectories() {
    std::error_code ec;
    fs::create_directories(GeneratedCapturesDirectory(), ec);
    if (ec) {
        return false;
    }
    fs::create_directories(ApplicationSupportDirectory(), ec);
    return !ec;
}

std::string PluginLibraryBridge::EscapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string PluginLibraryBridge::SerializeDocument(const PluginBridgeDocument& document) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"activeSlotCount\": " << document.activeSlotCount << ",\n";
    json << "  \"slots\": [\n";
    for (size_t i = 0; i < document.slots.size(); ++i) {
        const PluginBridgeSlot& slot = document.slots[i];
        json << "    {\n";
        json << "      \"index\": " << slot.index << ",\n";
        json << "      \"id\": \"" << EscapeJson(slot.id) << "\",\n";
        json << "      \"filePath\": \"" << EscapeJson(slot.filePath) << "\",\n";
        json << "      \"displayName\": \"" << EscapeJson(slot.displayName) << "\",\n";
        json << "      \"playbackStartSeconds\": null,\n";
        json << "      \"playbackEndSeconds\": null,\n";
        json << "      \"capturedAt\": " << slot.capturedAtMs << ",\n";
        json << "      \"source\": \"" << EscapeJson(slot.source) << "\"\n";
        json << "    }";
        if (i + 1 < document.slots.size()) {
            json << ",";
        }
        json << "\n";
    }
    json << "  ],\n";
    json << "  \"shortcutBindings\": {\n";
    size_t bindingIndex = 0;
    for (const auto& entry : document.shortcutBindings) {
        json << "    \"" << EscapeJson(entry.first) << "\": \"" << EscapeJson(entry.second) << "\"";
        if (++bindingIndex < document.shortcutBindings.size()) {
            json << ",";
        }
        json << "\n";
    }
    json << "  }\n";
    json << "}\n";
    return json.str();
}

bool PluginLibraryBridge::ReadDocument(PluginBridgeDocument& outDocument) {
    const std::string contents = ReadFileToString(BridgeFilePath());
    if (contents.empty()) {
        outDocument.activeSlotCount = kSimplestSamplerDefaultActiveSlots;
        outDocument.slots.clear();
        outDocument.shortcutBindings.clear();
        return false;
    }

    outDocument.activeSlotCount = static_cast<int>(ExtractJsonInt(contents, "activeSlotCount", kSimplestSamplerDefaultActiveSlots));
    outDocument.slots.clear();

    const size_t slotsPos = contents.find("\"slots\"");
    if (slotsPos != std::string::npos) {
        const size_t arrayStart = contents.find('[', slotsPos);
        const size_t arrayEnd = contents.find(']', arrayStart == std::string::npos ? slotsPos : arrayStart);
        if (arrayStart != std::string::npos && arrayEnd != std::string::npos) {
            const std::string arrayBody = contents.substr(arrayStart, arrayEnd - arrayStart + 1);
            for (const std::string& object : ExtractJsonObjects(arrayBody)) {
                PluginBridgeSlot slot;
                slot.index = static_cast<int>(ExtractJsonInt(object, "index", 0));
                slot.id = ExtractJsonString(object, "id");
                slot.filePath = ExtractJsonString(object, "filePath");
                slot.displayName = ExtractJsonString(object, "displayName");
                slot.source = ExtractJsonString(object, "source");
                if (slot.source.empty()) {
                    slot.source = "aax-audiosuite-capture";
                }
                slot.capturedAtMs = ExtractJsonInt(object, "capturedAt", 0);
                outDocument.slots.push_back(slot);
            }
        }
    }

    outDocument.shortcutBindings.clear();
    const size_t bindingsPos = contents.find("\"shortcutBindings\"");
    if (bindingsPos != std::string::npos) {
        const size_t objectStart = contents.find('{', bindingsPos);
        const size_t objectEnd = contents.find('}', objectStart == std::string::npos ? bindingsPos : objectStart);
        if (objectStart != std::string::npos && objectEnd != std::string::npos) {
            const std::string objectBody = contents.substr(objectStart + 1, objectEnd - objectStart - 1);
            size_t pos = 0;
            while (pos < objectBody.size()) {
                const size_t keyStart = objectBody.find('"', pos);
                if (keyStart == std::string::npos) {
                    break;
                }
                const size_t keyEnd = objectBody.find('"', keyStart + 1);
                if (keyEnd == std::string::npos) {
                    break;
                }
                const std::string key = UnescapeJsonString(objectBody.substr(keyStart + 1, keyEnd - keyStart - 1));
                const size_t valueStart = objectBody.find('"', keyEnd + 1);
                if (valueStart == std::string::npos) {
                    break;
                }
                const size_t valueEnd = objectBody.find('"', valueStart + 1);
                if (valueEnd == std::string::npos) {
                    break;
                }
                const std::string value = UnescapeJsonString(objectBody.substr(valueStart + 1, valueEnd - valueStart - 1));
                if (!key.empty()) {
                    outDocument.shortcutBindings[key] = value;
                }
                pos = valueEnd + 1;
            }
        }
    }

    return true;
}

bool PluginLibraryBridge::WriteDocument(const PluginBridgeDocument& document) {
    if (!EnsureDirectories()) {
        return false;
    }
    return WriteStringToFile(BridgeFilePath(), SerializeDocument(document));
}

bool PluginLibraryBridge::AtomicWriteDocument(const PluginBridgeDocument& document) {
    if (!EnsureDirectories()) {
        return false;
    }
    const std::string tempPath = BridgeFilePath() + ".tmp";
    const std::string payload = SerializeDocument(document);
    if (!WriteStringToFile(tempPath, payload)) {
        return false;
    }
    if (std::rename(tempPath.c_str(), BridgeFilePath().c_str()) != 0) {
        std::remove(tempPath.c_str());
        return false;
    }
    return true;
}
