#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/grpcpp.h>

#include "ptsl_descriptor.hpp"

namespace {

constexpr char kGrpcAddress[] = "localhost:31416";
constexpr char kGrpcMethod[] = "/ptsl.PTSL/SendGrpcRequest";
constexpr char kGrpcStreamingMethod[] = "/ptsl.PTSL/SendGrpcStreamingRequest";
constexpr char kCompanyName[] = "SimplestSampler";
constexpr char kApplicationName[] = "SimplestSampler";
#ifndef PTSL_PROTOCOL_VERSION_MAJOR
#define PTSL_PROTOCOL_VERSION_MAJOR 2025
#endif

#ifndef PTSL_PROTOCOL_VERSION_MINOR
#define PTSL_PROTOCOL_VERSION_MINOR 10
#endif

#ifndef PTSL_PROTOCOL_VERSION_REVISION
#define PTSL_PROTOCOL_VERSION_REVISION 0
#endif

// Defaults match the embedded 2025.10 SDK descriptor, but build-time overrides let us
// target older Pro Tools/PTSL combinations such as PT 2022.12 on Monterey.
constexpr std::int32_t kPtslProtocolVersionMajor = PTSL_PROTOCOL_VERSION_MAJOR;
constexpr std::int32_t kPtslProtocolVersionMinor = PTSL_PROTOCOL_VERSION_MINOR;
constexpr std::int32_t kPtslProtocolVersionRevision = PTSL_PROTOCOL_VERSION_REVISION;
constexpr int kCommandIdAuthorizeConnection = 60;
constexpr int kCommandIdGetMemoryLocations = 61;
constexpr int kCommandIdRegisterConnection = 70;
constexpr int kCommandIdSetTimelineSelection = 81;
constexpr int kCommandIdGetTimelineSelection = 82;
constexpr char kDefaultProToolsInfoPlistPath[] = "/Applications/Pro Tools.app/Contents/Info.plist";
void* const kGrpcFinishTag = reinterpret_cast<void*>(1);
void* const kGrpcStartTag = reinterpret_cast<void*>(2);
void* const kGrpcReadTag = reinterpret_cast<void*>(3);

struct PtslProtocolVersion {
  int major = static_cast<int>(kPtslProtocolVersionMajor);
  int minor = static_cast<int>(kPtslProtocolVersionMinor);
  int revision = static_cast<int>(kPtslProtocolVersionRevision);
};

struct EventSubscription {
  std::string event_id;
  std::string event_data_json;
};

struct Marker {
  std::string name;
  std::string start_time;
  std::string end_time;
  std::string comments;
  std::string location;
  std::string track_name;
};

struct TrackInfo {
  std::string id;
  std::string name;
  std::string type;
  std::string format;
  std::optional<bool> is_record_enabled;
  std::optional<bool> is_record_safe_enabled;
  std::optional<bool> is_muted;
  std::optional<std::string> is_selected_state;
};

struct FileLocationEntry {
  std::string path;
  std::string file_id;
  bool is_online = false;
};

struct ClipInfo {
  std::string file_id;
  std::string clip_id;
  std::string clip_full_name;
  std::string clip_root_name;
  std::string file_path;
  std::optional<long long> src_start_position;
  std::optional<std::string> src_start_time_type;
};

struct PlaylistInfo {
  std::string playlist_id;
  std::string playlist_name;
  bool is_target = false;
  bool is_solo_comp_lane_on = false;
};

struct PlaylistElementClipInfo {
  bool is_null = false;
  std::string clip_id;
};

struct PlaylistElementInfo {
  std::string start_time;
  std::string play_time;
  std::string stop_time;
  std::string end_time;
  std::vector<PlaylistElementClipInfo> channel_clips;
};

struct SelectedClipSegmentInfo {
  FileLocationEntry file_location;
  std::string clip_id;
  std::string clip_name;
  std::string resolution_source;
  std::string clip_start_time;
  std::string segment_start_time;
  std::string segment_end_time;
  std::optional<double> src_start_seconds;
  std::optional<double> source_start_seconds;
  std::optional<double> source_end_seconds;
  long long segment_start_subframes = 0;
};

struct SessionExportClipPlacement {
  std::string clip_name;
  std::string start_time;
  std::string end_time;
  long long start_subframes = 0;
  long long end_subframes = 0;
};

struct BarsBeatsPosition {
  long long bar = 0;
  long long beat = 0;
  long long tick = 0;
};

struct MemoryLocationInfo {
  int number = 0;
  std::string name;
  std::string start_time;
  std::string end_time;
  std::string comments;
  std::string time_properties;
  std::string location;
  std::string track_name;
};

struct TrackRename {
  std::string track_id;
  std::string current_name;
  std::string new_name;
};

struct TrackMuteStateUpdate {
  std::vector<std::string> track_names;
  bool enabled = false;
};

struct ClipGroupFailure {
  std::string name;
  std::string start_time;
  std::string end_time;
  std::string error;
};

struct ParsedStoredMarkerComments {
  std::string character_name;
  std::string comment_text;
};

struct PreparedClipGroupCue {
  std::string name;
  std::string group_name;
  std::string character_name;
  std::string track_pool_key;
  std::string track_pool_label;
  std::string start_time;
  std::string end_time;
  long long start_subframes = 0;
  long long end_subframes = 0;
  std::size_t slot_index = 0;
};

enum class MakePtClipTracksMode {
  kPerCharacter,
  kGenericNoOverlaps,
  kSingleGeneric,
};

struct RenamePlanTrack {
  std::string track_id;
  std::string saved_name;
};

struct RenamePlan {
  std::string marker_name;
  std::string primary_track_id;
  std::string primary_track_name;
  std::vector<RenamePlanTrack> tracks;
};

struct DropToTakePlan {
  std::string primary_track_id;
  std::string primary_track_name;
  std::string placement_mode;
  std::string take_track_keyword;
  std::vector<RenamePlanTrack> tracks;
};

struct RenameTrackResult {
  std::string track_id;
  std::string live_track_id;
  std::string saved_name;
  std::string current_name;
  std::string new_name;
  std::string status;
};

struct MarkerEditPlan {
  Marker previous_marker;
  Marker next_marker;
};

struct TimelineSelection {
  std::string play_start_marker_time;
  std::string in_time;
  std::string out_time;
  std::string pre_roll_start_time;
  std::string post_roll_stop_time;
  bool pre_roll_enabled = false;
  bool post_roll_enabled = false;
};

struct EditSelectionBounds {
  std::string in_time;
  std::string out_time;
};

struct TimeCodeRateInfo {
  int nominal_fps = 30;
  bool drop_frame = false;
  int actual_fps_numerator = 30;
  int actual_fps_denominator = 1;
};

class PtslClient;
void EnsureClientConnected(PtslClient& client);
std::string ExecuteGetSelectedClipFile(PtslClient& client);
std::string ExecuteGetSelectedClipSegments(PtslClient& client);
std::string ExecuteWriteSelectedTranscriptionToJsonFile(PtslClient& client);
std::string ExecuteCreatePtClipGroupsFromFile(PtslClient& client,
                                              const std::string& input_path,
                                              bool avoid_overlaps);
std::string ExecuteDeletePtCueClipsFromFile(PtslClient& client,
                                            const std::string& input_path);
std::string ExecuteCreateClipGroupFromSelection(PtslClient& client,
                                                std::string_view requested_group_name);
std::string ExecuteMakePtClipTracksFromFile(PtslClient& client,
                                            const std::string& input_path,
                                            MakePtClipTracksMode mode);
std::string ExecuteCreateCharacterTrack(PtslClient& client, const std::string& track_name);
std::string ExecuteSelectTrackByName(PtslClient& client, const std::string& track_name);
bool ShouldUseLegacyDropToTakeSessionExportResolution(const PtslClient& client);
void RecoverPtslSessionAfterError(PtslClient& client,
                                  std::string_view context_label,
                                  std::string_view message);
bool ShouldPreferEditSelectionForDropToTake(const PtslClient& client);
std::vector<std::string> ResolveLegacySelectedClipNamesForDropToTake(PtslClient& client);
std::vector<std::string> ResolveSelectedClipNamesForDropToTake(
    PtslClient& client,
    bool use_legacy_drop_to_take_resolution);
bool ShouldPrimeDropToTakeSourceSelection(
    PtslClient& client,
    const TimeCodeRateInfo& rate_info,
    const std::vector<std::string>& selected_clip_names);
bool ShouldAvoidTimelineSelectionReadsForDropToTake(const PtslClient& client);
bool IsPtsl23_12(const PtslClient& client);
void AppendUniqueClipName(std::vector<std::string>& names, std::string candidate);
std::string ExecuteSetTimelineRolls(PtslClient& client,
                                    const std::optional<long long>& pre_roll_frames,
                                    const std::optional<long long>& post_roll_frames,
                                    const std::optional<long long>& pre_roll_milliseconds,
                                    const std::optional<long long>& post_roll_milliseconds,
                                    const std::optional<bool>& pre_roll_enabled,
                                    const std::optional<bool>& post_roll_enabled);
std::optional<bool> ExtractPayloadOptionalBoolValue(const std::string& payload_json,
                                                    const char* snake_key,
                                                    const char* camel_key);
std::optional<long long> ExtractPayloadOptionalLongLongValue(const std::string& payload_json,
                                                             const char* snake_key,
                                                             const char* camel_key,
                                                             const char* label);

struct ResponseEnvelope {
  int task_status = -1;
  std::string response_body_json;
  std::string response_error_json;
};

struct TransportStatusInfo {
  std::string current_setting;
  bool is_transport_armed = false;
};

constexpr int kCreateMarkerSettleTimeoutMs = 1200;
constexpr int kCreateMarkerSettlePollIntervalMs = 75;

bool PtslProtocolVersionEquals(const PtslProtocolVersion& left, const PtslProtocolVersion& right) {
  return left.major == right.major
      && left.minor == right.minor
      && left.revision == right.revision;
}

std::string FormatPtslProtocolVersion(const PtslProtocolVersion& version) {
  std::ostringstream output;
  output << version.major << '.' << version.minor << '.' << version.revision;
  return output.str();
}

int NormalizedPtslReleaseMajor(const PtslProtocolVersion& version) {
  if (version.major >= 2000 && version.major < 2100) {
    return version.major - 2000;
  }
  return version.major;
}

std::optional<int> NormalizedPtslReleaseMajor(const std::optional<PtslProtocolVersion>& version) {
  if (!version.has_value()) {
    return std::nullopt;
  }
  return NormalizedPtslReleaseMajor(*version);
}

bool IsPtslHandshakeDebugEnabled() {
  const char* raw = std::getenv("PTSL_HELPER_DEBUG");
  if (!raw) {
    return false;
  }
  std::string normalized(raw);
  normalized.erase(
      normalized.begin(),
      std::find_if(
          normalized.begin(),
          normalized.end(),
          [](unsigned char ch) { return std::isspace(ch) == 0; }));
  normalized.erase(
      std::find_if(
          normalized.rbegin(),
          normalized.rend(),
          [](unsigned char ch) { return std::isspace(ch) == 0; }).base(),
      normalized.end());
  std::transform(
      normalized.begin(),
      normalized.end(),
      normalized.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
}

void LogPtslHandshakeDebug(const std::string& message) {
  if (!IsPtslHandshakeDebugEnabled()) {
    return;
  }
  std::cerr << "[ptsl-helper-debug] " << message << '\n';
}

std::optional<PtslProtocolVersion> ParsePtslProtocolVersion(std::string_view raw_input) {
  static const std::regex pattern(R"(^\s*(\d+)\.(\d+)(?:\.(\d+))?(?:\.\d+)?\s*$)");
  const std::string input(raw_input);
  std::smatch match;
  if (!std::regex_match(input, match, pattern)) {
    return std::nullopt;
  }

  PtslProtocolVersion version;
  try {
    version.major = std::stoi(match[1].str());
    version.minor = std::stoi(match[2].str());
    version.revision = match[3].matched ? std::stoi(match[3].str()) : 0;
  } catch (...) {
    return std::nullopt;
  }
  return version;
}

bool SupportsClearAllMemoryLocations(const std::optional<PtslProtocolVersion>& version) {
  if (!version.has_value()) {
    return false;
  }
  const auto release_major = NormalizedPtslReleaseMajor(*version);
  if (release_major > 24) {
    return true;
  }
  return release_major == 24 && version->minor >= 10;
}

bool IsPtsl24_10Protocol(const std::optional<PtslProtocolVersion>& version) {
  if (!version.has_value()) {
    return false;
  }
  const auto release_major = NormalizedPtslReleaseMajor(*version);
  return release_major == 24 && version->minor == 10;
}

std::optional<std::string> RunCommandAndCaptureFirstLine(const std::string& command) {
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) {
    return std::nullopt;
  }

  std::array<char, 256> buffer{};
  std::string output;
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
    output.append(buffer.data());
  }
  const int exit_code = pclose(pipe);
  if (exit_code != 0) {
    return std::nullopt;
  }

  const auto first_newline = output.find('\n');
  if (first_newline != std::string::npos) {
    output.erase(first_newline);
  }
  if (output.empty()) {
    return std::nullopt;
  }
  return output;
}

std::string ShellSingleQuote(std::string_view value) {
  std::string quoted = "'";
  for (const char ch : value) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push_back(ch);
    }
  }
  quoted += "'";
  return quoted;
}

std::string TrimCommandOutput(std::string value) {
  value.erase(
      value.begin(),
      std::find_if(
          value.begin(),
          value.end(),
          [](unsigned char ch) { return std::isspace(ch) == 0; }));
  value.erase(
      std::find_if(
          value.rbegin(),
          value.rend(),
          [](unsigned char ch) { return std::isspace(ch) == 0; }).base(),
      value.end());
  return value;
}

int RunCommandAndCaptureOutput(const std::string& command, std::string* output) {
  if (output) {
    output->clear();
  }
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) {
    return -1;
  }

  std::array<char, 512> buffer{};
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
    if (output) {
      output->append(buffer.data());
    }
  }
  return pclose(pipe);
}

std::string BuildOsaScriptCommand(const std::vector<std::string>& script_lines) {
  std::string command = "/usr/bin/osascript";
  for (const auto& line : script_lines) {
    command += " -e ";
    command += ShellSingleQuote(line);
  }
  command += " 2>&1";
  return command;
}

void InvokeProToolsClipGroupMenuItem() {
  const std::vector<std::string> script_lines = {
      "tell application \"Pro Tools\" to activate",
      "tell application \"System Events\"",
      "if not (exists process \"Pro Tools\") then error \"Pro Tools is not running.\"",
      "tell process \"Pro Tools\"",
      "set frontmost to true",
      "set didBecomeFrontmost to false",
      "repeat 30 times",
      "if frontmost then",
      "set didBecomeFrontmost to true",
      "exit repeat",
      "end if",
      "delay 0.05",
      "end repeat",
      "if not didBecomeFrontmost then error \"Pro Tools did not become frontmost in time.\"",
      "tell menu bar 1",
      "if not (exists menu bar item \"Clip\") then error \"Could not find the Pro Tools Clip menu.\"",
      "tell menu bar item \"Clip\"",
      "tell menu \"Clip\"",
      "set targetItem to missing value",
      "if exists menu item \"Group\" then",
      "set targetItem to menu item \"Group\"",
      "else if exists menu item \"Group Clips\" then",
      "set targetItem to menu item \"Group Clips\"",
      "else",
      "error \"Could not find Clip > Group in Pro Tools.\"",
      "end if",
      "set groupItemReady to false",
      "repeat 20 times",
      "if enabled of targetItem then",
      "set groupItemReady to true",
      "exit repeat",
      "end if",
      "delay 0.05",
      "end repeat",
      "if not groupItemReady then error \"Clip > Group menu item was not enabled in time.\"",
      "click targetItem",
      "delay 0.15",
      "end tell",
      "end tell",
      "end tell",
      "end tell",
      "end tell"
  };

  std::string output;
  const int exit_code = RunCommandAndCaptureOutput(BuildOsaScriptCommand(script_lines), &output);
  if (exit_code == 0) {
    return;
  }

  std::string detail = TrimCommandOutput(output);
  if (detail.empty()) {
    detail = "osascript exited with status " + std::to_string(exit_code);
  }
  throw std::runtime_error(
      "Clip > Group menu fallback failed. Grant OverCue Accessibility access, keep Pro Tools visible, "
      "and confirm the Clip > Group menu item is enabled. Detail: "
      + detail);
}

std::optional<PtslProtocolVersion> DetectInstalledProToolsProtocolVersion() {
  std::vector<std::string> candidate_info_plists;
  if (const char* app_path = std::getenv("PRO_TOOLS_APP_PATH")) {
    std::string normalized_path(app_path);
    if (!normalized_path.empty()) {
      if (normalized_path.size() >= 4
          && normalized_path.compare(normalized_path.size() - 4, 4, ".app") == 0) {
        normalized_path += "/Contents/Info.plist";
      }
      candidate_info_plists.push_back(std::move(normalized_path));
    }
  }
  candidate_info_plists.emplace_back(kDefaultProToolsInfoPlistPath);

  for (const auto& plist_path : candidate_info_plists) {
    std::ifstream info_plist(plist_path);
    if (!info_plist.good()) {
      continue;
    }

    const auto version_string = RunCommandAndCaptureFirstLine(
        std::string("/usr/bin/plutil -extract CFBundleShortVersionString raw -o - '")
        + plist_path
        + "' 2>/dev/null");
    if (!version_string) {
      continue;
    }

    const auto version = ParsePtslProtocolVersion(*version_string);
    if (version) {
      return version;
    }
  }

  return std::nullopt;
}

void AppendPtslProtocolVersionCandidate(std::vector<PtslProtocolVersion>& candidates,
                                        const std::optional<PtslProtocolVersion>& candidate) {
  if (!candidate.has_value()) {
    return;
  }
  for (const auto& existing : candidates) {
    if (PtslProtocolVersionEquals(existing, *candidate)) {
      return;
    }
  }
  candidates.push_back(*candidate);
}

std::optional<PtslProtocolVersion> BuildPtslProtocolVersionAlias(const PtslProtocolVersion& version) {
  if (version.major >= 2000 && version.major < 2100) {
    return PtslProtocolVersion{version.major - 2000, version.minor, version.revision};
  }
  if (version.major >= 20 && version.major < 100) {
    return PtslProtocolVersion{2000 + version.major, version.minor, version.revision};
  }
  return std::nullopt;
}

std::optional<PtslProtocolVersion> BuildPtslLegacyProtocolVersion(const PtslProtocolVersion& version) {
  const bool is_2022_series =
      ((version.major == 22 || version.major == 2022) && version.minor >= 12);
  if (is_2022_series) {
    return PtslProtocolVersion{1, 0, 0};
  }
  return std::nullopt;
}

bool IsLegacyAuthorizeConnectionProtocol(const PtslProtocolVersion& version) {
  return version.major == 1;
}

bool HasPtslAuthString() {
  const char* auth_string_raw = std::getenv("PTSL_AUTH_STRING");
  if (!auth_string_raw) {
    return false;
  }
  const std::string auth_string(auth_string_raw);
  return std::any_of(
      auth_string.begin(),
      auth_string.end(),
      [](unsigned char ch) { return std::isspace(ch) == 0; });
}

void AppendPtslProtocolVersionCandidateWithAlias(
    std::vector<PtslProtocolVersion>& candidates,
    const std::optional<PtslProtocolVersion>& candidate,
    bool include_legacy_authorize_candidate = false) {
  AppendPtslProtocolVersionCandidate(candidates, candidate);
  if (candidate.has_value()) {
    AppendPtslProtocolVersionCandidate(candidates, BuildPtslProtocolVersionAlias(*candidate));
    if (include_legacy_authorize_candidate) {
      AppendPtslProtocolVersionCandidate(candidates, BuildPtslLegacyProtocolVersion(*candidate));
    }
  }
}

std::vector<PtslProtocolVersion> BuildPtslProtocolVersionCandidates() {
  std::vector<PtslProtocolVersion> candidates;
  const auto explicit_protocol = ParsePtslProtocolVersion(std::getenv("PTSL_PROTOCOL_VERSION")
      ? std::string_view(std::getenv("PTSL_PROTOCOL_VERSION"))
      : std::string_view());
  const bool explicit_legacy_authorize_protocol = explicit_protocol.has_value()
      && IsLegacyAuthorizeConnectionProtocol(*explicit_protocol);
  const bool allow_legacy_authorize_candidate =
      explicit_legacy_authorize_protocol || HasPtslAuthString();
  AppendPtslProtocolVersionCandidateWithAlias(
      candidates,
      explicit_protocol,
      allow_legacy_authorize_candidate);
  AppendPtslProtocolVersionCandidateWithAlias(
      candidates,
      DetectInstalledProToolsProtocolVersion(),
      allow_legacy_authorize_candidate);
  AppendPtslProtocolVersionCandidateWithAlias(
      candidates,
      PtslProtocolVersion{
          static_cast<int>(kPtslProtocolVersionMajor),
          static_cast<int>(kPtslProtocolVersionMinor),
          static_cast<int>(kPtslProtocolVersionRevision),
      },
      allow_legacy_authorize_candidate);
  AppendPtslProtocolVersionCandidateWithAlias(
      candidates,
      PtslProtocolVersion{22, 12, 0},
      allow_legacy_authorize_candidate);

  if (candidates.empty()) {
    candidates.push_back(PtslProtocolVersion{});
  }
  return candidates;
}

std::vector<std::string> SplitTabs(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (true) {
    const auto end = line.find('\t', start);
    if (end == std::string::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, end - start));
    start = end + 1;
  }
  return fields;
}

std::string DecodeBase64(std::string_view input) {
  static constexpr std::uint8_t kInvalid = 0xff;
  static const std::uint8_t kDecodeTable[256] = {
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, 62,       kInvalid, kInvalid, kInvalid, 63,
      52,       53,       54,       55,       56,       57,       58,       59,
      60,       61,       kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, 0,        1,        2,        3,        4,        5,        6,
      7,        8,        9,        10,       11,       12,       13,       14,
      15,       16,       17,       18,       19,       20,       21,       22,
      23,       24,       25,       kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, 26,       27,       28,       29,       30,       31,       32,
      33,       34,       35,       36,       37,       38,       39,       40,
      41,       42,       43,       44,       45,       46,       47,       48,
      49,       50,       51,       kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
      kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid,
  };

  std::string output;
  output.reserve((input.size() * 3) / 4);

  int val = 0;
  int valb = -8;
  for (unsigned char ch : input) {
    if (std::isspace(ch)) {
      continue;
    }
    if (ch == '=') {
      break;
    }
    const auto decoded = kDecodeTable[ch];
    if (decoded == kInvalid) {
      throw std::runtime_error("Invalid base64 marker payload");
    }
    val = (val << 6) + decoded;
    valb += 6;
    if (valb >= 0) {
      output.push_back(static_cast<char>((val >> valb) & 0xff));
      valb -= 8;
    }
  }

  return output;
}

std::string EncodeBase64(std::string_view input) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz"
      "0123456789+/";

  std::string output;
  output.reserve(((input.size() + 2) / 3) * 4);

  std::size_t index = 0;
  while (index + 3 <= input.size()) {
    const auto chunk = (static_cast<std::uint32_t>(static_cast<unsigned char>(input[index])) << 16)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(input[index + 1])) << 8)
        | static_cast<std::uint32_t>(static_cast<unsigned char>(input[index + 2]));
    output.push_back(kAlphabet[(chunk >> 18) & 0x3f]);
    output.push_back(kAlphabet[(chunk >> 12) & 0x3f]);
    output.push_back(kAlphabet[(chunk >> 6) & 0x3f]);
    output.push_back(kAlphabet[chunk & 0x3f]);
    index += 3;
  }

  if (index < input.size()) {
    std::uint32_t chunk = static_cast<std::uint32_t>(static_cast<unsigned char>(input[index])) << 16;
    if (index + 1 < input.size()) {
      chunk |= static_cast<std::uint32_t>(static_cast<unsigned char>(input[index + 1])) << 8;
    }
    output.push_back(kAlphabet[(chunk >> 18) & 0x3f]);
    output.push_back(kAlphabet[(chunk >> 12) & 0x3f]);
    output.push_back(index + 1 < input.size() ? kAlphabet[(chunk >> 6) & 0x3f] : '=');
    output.push_back('=');
  }

  return output;
}

std::string JsonEscape(std::string_view input) {
  std::ostringstream escaped;
  escaped.fill('0');
  for (unsigned char ch : input) {
    switch (ch) {
      case '\\':
        escaped << "\\\\";
        break;
      case '"':
        escaped << "\\\"";
        break;
      case '\b':
        escaped << "\\b";
        break;
      case '\f':
        escaped << "\\f";
        break;
      case '\n':
        escaped << "\\n";
        break;
      case '\r':
        escaped << "\\r";
        break;
      case '\t':
        escaped << "\\t";
        break;
      default:
        if (ch < 0x20) {
          escaped << "\\u"
                  << std::hex << std::uppercase
                  << std::setw(4) << static_cast<int>(ch)
                  << std::dec << std::nouppercase;
        } else {
          escaped << static_cast<char>(ch);
        }
        break;
    }
  }
  return escaped.str();
}

std::string UnescapeJsonString(std::string_view input) {
  std::string output;
  output.reserve(input.size());

  for (std::size_t i = 0; i < input.size(); i += 1) {
    const char ch = input[i];
    if (ch != '\\') {
      output.push_back(ch);
      continue;
    }

    if (i + 1 >= input.size()) {
      break;
    }

    const char esc = input[++i];
    switch (esc) {
      case '\\':
        output.push_back('\\');
        break;
      case '"':
        output.push_back('"');
        break;
      case '/':
        output.push_back('/');
        break;
      case 'b':
        output.push_back('\b');
        break;
      case 'f':
        output.push_back('\f');
        break;
      case 'n':
        output.push_back('\n');
        break;
      case 'r':
        output.push_back('\r');
        break;
      case 't':
        output.push_back('\t');
        break;
      case 'u':
        if (i + 4 < input.size()) {
          const auto hex = std::string(input.substr(i + 1, 4));
          const auto code = static_cast<unsigned int>(std::stoul(hex, nullptr, 16));
          if (code <= 0x7f) {
            output.push_back(static_cast<char>(code));
          }
          i += 4;
        }
        break;
      default:
        output.push_back(esc);
        break;
    }
  }

  return output;
}

std::optional<std::string> ExtractJsonStringField(const std::string& json, const std::string& key) {
  const auto keyToken = "\"" + key + "\"";
  auto pos = json.find(keyToken);
  if (pos == std::string::npos) {
    return std::nullopt;
  }

  pos = json.find(':', pos + keyToken.size());
  if (pos == std::string::npos) {
    return std::nullopt;
  }

  pos += 1;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
    pos += 1;
  }
  if (pos >= json.size() || json[pos] != '"') {
    return std::nullopt;
  }

  pos += 1;
  std::string raw;
  bool escaped = false;
  while (pos < json.size()) {
    const char ch = json[pos++];
    if (!escaped && ch == '"') {
      return UnescapeJsonString(raw);
    }
    if (!escaped && ch == '\\') {
      escaped = true;
      raw.push_back(ch);
      continue;
    }
    escaped = false;
    raw.push_back(ch);
  }

  return std::nullopt;
}

std::optional<std::string> ExtractJsonScalarField(const std::string& json, const std::string& key) {
  const auto keyToken = "\"" + key + "\"";
  auto pos = json.find(keyToken);
  if (pos == std::string::npos) {
    return std::nullopt;
  }

  pos = json.find(':', pos + keyToken.size());
  if (pos == std::string::npos) {
    return std::nullopt;
  }

  pos += 1;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
    pos += 1;
  }
  if (pos >= json.size()) {
    return std::nullopt;
  }

  if (json[pos] == '"') {
    return ExtractJsonStringField(json, key);
  }

  const auto value_start = pos;
  while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']') {
    pos += 1;
  }

  auto value = json.substr(value_start, pos - value_start);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

std::optional<bool> ExtractJsonBoolField(const std::string& json, const std::string& key) {
  const auto value = ExtractJsonScalarField(json, key);
  if (!value) {
    return std::nullopt;
  }
  if (*value == "true") {
    return true;
  }
  if (*value == "false") {
    return false;
  }
  return std::nullopt;
}

int DropFramesPerMinute(const TimeCodeRateInfo& rate_info) {
  if (!rate_info.drop_frame) {
    return 0;
  }

  switch (rate_info.nominal_fps) {
    case 30:
      return 2;
    case 60:
      return 4;
    case 120:
      return 8;
    default:
      throw std::runtime_error("Unsupported drop-frame rate");
  }
}

TimeCodeRateInfo ParseTimeCodeRateInfo(const std::string& value) {
  if (value == "STCR_Fps23976") {
    return {24, false, 24000, 1001};
  }
  if (value == "STCR_Fps24") {
    return {24, false, 24, 1};
  }
  if (value == "STCR_Fps25") {
    return {25, false, 25, 1};
  }
  if (value == "STCR_Fps2997") {
    return {30, false, 30000, 1001};
  }
  if (value == "STCR_Fps2997Drop") {
    return {30, true, 30000, 1001};
  }
  if (value == "STCR_Fps30") {
    return {30, false, 30, 1};
  }
  if (value == "STCR_Fps30Drop") {
    return {30, true, 30, 1};
  }
  if (value == "STCR_Fps47952") {
    return {48, false, 48000, 1001};
  }
  if (value == "STCR_Fps48") {
    return {48, false, 48, 1};
  }
  if (value == "STCR_Fps50") {
    return {50, false, 50, 1};
  }
  if (value == "STCR_Fps5994") {
    return {60, false, 60000, 1001};
  }
  if (value == "STCR_Fps5994Drop") {
    return {60, true, 60000, 1001};
  }
  if (value == "STCR_Fps60") {
    return {60, false, 60, 1};
  }
  if (value == "STCR_Fps60Drop") {
    return {60, true, 60, 1};
  }
  if (value == "STCR_Fps100") {
    return {100, false, 100, 1};
  }
  if (value == "STCR_Fps11988") {
    return {120, false, 120000, 1001};
  }
  if (value == "STCR_Fps11988Drop") {
    return {120, true, 120000, 1001};
  }
  if (value == "STCR_Fps120") {
    return {120, false, 120, 1};
  }
  if (value == "STCR_Fps120Drop") {
    return {120, true, 120, 1};
  }

  throw std::runtime_error(std::string("Unsupported session timecode rate: ") + value);
}

TimeCodeRateInfo ParseFeetFramesRateInfo(const std::string& value) {
  if (value == "SFFR_Fps23976" || value == "SFFRate_Fps23976") {
    return {24, false, 24000, 1001};
  }
  if (value == "SFFR_Fps24" || value == "SFFRate_Fps24") {
    return {24, false, 24, 1};
  }
  if (value == "SFFR_Fps25" || value == "SFFRate_Fps25") {
    return {25, false, 25, 1};
  }

  throw std::runtime_error(std::string("Unsupported session feet+frames rate: ") + value);
}

std::optional<double> ResolveSessionFps(const TimeCodeRateInfo& rate_info) {
  if (rate_info.actual_fps_denominator <= 0) {
    return std::nullopt;
  }

  const double fps = static_cast<double>(rate_info.actual_fps_numerator)
      / static_cast<double>(rate_info.actual_fps_denominator);
  if (!std::isfinite(fps) || fps <= 0) {
    return std::nullopt;
  }

  return fps;
}

long long TimecodeStringToSubframes(const std::string& value, const TimeCodeRateInfo& rate_info) {
  static const std::regex pattern(R"(^\s*(\d{2}):(\d{2}):(\d{2})[:;](\d{2})(?:\.(\d{1,3}))?\s*$)");
  std::smatch match;
  if (!std::regex_match(value, match, pattern)) {
    throw std::runtime_error(std::string("Invalid timecode value: ") + value);
  }

  const int hours = std::stoi(match[1].str());
  const int minutes = std::stoi(match[2].str());
  const int seconds = std::stoi(match[3].str());
  const int frames = std::stoi(match[4].str());
  int subframes = 0;
  if (match[5].matched) {
    auto subframe_text = match[5].str();
    if (subframe_text.size() == 1) {
      subframe_text.push_back('0');
    } else if (subframe_text.size() == 3) {
      const int thousandths = std::stoi(subframe_text);
      subframes = std::clamp((thousandths + 5) / 10, 0, 99);
      subframe_text.clear();
    }
    if (!subframe_text.empty()) {
      subframes = std::stoi(subframe_text);
    }
  }

  long long total_frames =
      ((static_cast<long long>(hours) * 3600) +
       (static_cast<long long>(minutes) * 60) +
       seconds) * rate_info.nominal_fps +
      frames;

  if (rate_info.drop_frame) {
    const int drop_frames = DropFramesPerMinute(rate_info);
    const int total_minutes = (hours * 60) + minutes;
    total_frames -= static_cast<long long>(drop_frames) *
                    (total_minutes - (total_minutes / 10));
  }

  return (total_frames * 100) + subframes;
}

std::optional<long long> TryTimecodeStringToSubframes(const std::string& value,
                                                      const TimeCodeRateInfo& rate_info) {
  try {
    return TimecodeStringToSubframes(value, rate_info);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::string SubframesToTimecodeString(long long total_subframes, const TimeCodeRateInfo& rate_info) {
  if (total_subframes < 0) {
    total_subframes = 0;
  }

  long long total_frames = total_subframes / 100;
  const int subframes = static_cast<int>(total_subframes % 100);
  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  int frames = 0;

  if (rate_info.drop_frame) {
    const int drop_frames = DropFramesPerMinute(rate_info);
    const long long frames_per_minute = (rate_info.nominal_fps * 60) - drop_frames;
    const long long frames_per_10_minutes = (rate_info.nominal_fps * 60 * 10) - (drop_frames * 9);
    const long long frames_per_24_hours = frames_per_10_minutes * 6 * 24;

    long long frame_number = total_frames % frames_per_24_hours;
    const long long ten_minute_blocks = frame_number / frames_per_10_minutes;
    const long long frames_into_block = frame_number % frames_per_10_minutes;

    frame_number += static_cast<long long>(drop_frames) * 9 * ten_minute_blocks;
    if (frames_into_block >= drop_frames) {
      frame_number += static_cast<long long>(drop_frames) *
                      ((frames_into_block - drop_frames) / frames_per_minute);
    }

    hours = static_cast<int>(frame_number / (rate_info.nominal_fps * 3600));
    minutes = static_cast<int>((frame_number / (rate_info.nominal_fps * 60)) % 60);
    seconds = static_cast<int>((frame_number / rate_info.nominal_fps) % 60);
    frames = static_cast<int>(frame_number % rate_info.nominal_fps);
  } else {
    hours = static_cast<int>(total_frames / (rate_info.nominal_fps * 3600));
    total_frames %= static_cast<long long>(rate_info.nominal_fps) * 3600;
    minutes = static_cast<int>(total_frames / (rate_info.nominal_fps * 60));
    total_frames %= static_cast<long long>(rate_info.nominal_fps) * 60;
    seconds = static_cast<int>(total_frames / rate_info.nominal_fps);
    frames = static_cast<int>(total_frames % rate_info.nominal_fps);
  }

  std::ostringstream timecode;
  timecode << std::setfill('0')
           << std::setw(2) << hours << ':'
           << std::setw(2) << minutes << ':'
           << std::setw(2) << seconds << ':'
           << std::setw(2) << frames << '.'
           << std::setw(2) << subframes;
  return timecode.str();
}

std::optional<std::string> ExtractJsonArrayField(const std::string& json, const std::string& key) {
  const auto keyToken = "\"" + key + "\"";
  auto pos = json.find(keyToken);
  if (pos == std::string::npos) {
    return std::nullopt;
  }

  pos = json.find(':', pos + keyToken.size());
  if (pos == std::string::npos) {
    return std::nullopt;
  }

  pos += 1;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
    pos += 1;
  }
  if (pos >= json.size() || json[pos] != '[') {
    return std::nullopt;
  }

  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  const auto array_start = pos + 1;
  for (; pos < json.size(); pos += 1) {
    const char ch = json[pos];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        in_string = false;
      }
      continue;
    }

    if (ch == '"') {
      in_string = true;
      continue;
    }
    if (ch == '[') {
      depth += 1;
      continue;
    }
    if (ch == ']') {
      depth -= 1;
      if (depth == 0) {
        return json.substr(array_start, pos - array_start);
      }
    }
  }

  return std::nullopt;
}

std::optional<std::string> ExtractJsonObjectField(const std::string& json, const std::string& key) {
  const auto keyToken = "\"" + key + "\"";
  auto pos = json.find(keyToken);
  if (pos == std::string::npos) {
    return std::nullopt;
  }

  pos = json.find(':', pos + keyToken.size());
  if (pos == std::string::npos) {
    return std::nullopt;
  }

  pos += 1;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
    pos += 1;
  }
  if (pos >= json.size() || json[pos] != '{') {
    return std::nullopt;
  }

  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  const auto object_start = pos;
  for (; pos < json.size(); pos += 1) {
    const char ch = json[pos];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        in_string = false;
      }
      continue;
    }

    if (ch == '"') {
      in_string = true;
      continue;
    }
    if (ch == '{') {
      depth += 1;
      continue;
    }
    if (ch == '}') {
      depth -= 1;
      if (depth == 0) {
        return json.substr(object_start, pos - object_start + 1);
      }
    }
  }

  return std::nullopt;
}

std::vector<std::string> ExtractTopLevelJsonObjects(std::string_view json_array_contents) {
  std::vector<std::string> objects;
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  std::size_t object_start = std::string_view::npos;

  for (std::size_t i = 0; i < json_array_contents.size(); i += 1) {
    const char ch = json_array_contents[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        in_string = false;
      }
      continue;
    }

    if (ch == '"') {
      in_string = true;
      continue;
    }
    if (ch == '{') {
      if (depth == 0) {
        object_start = i;
      }
      depth += 1;
      continue;
    }
    if (ch == '}') {
      depth -= 1;
      if (depth == 0 && object_start != std::string_view::npos) {
        objects.emplace_back(json_array_contents.substr(object_start, i - object_start + 1));
        object_start = std::string_view::npos;
      }
    }
  }

  return objects;
}

std::vector<std::string> ExtractTopLevelJsonStrings(std::string_view json_array_contents) {
  std::vector<std::string> values;
  bool in_string = false;
  bool escaped = false;
  std::size_t string_start = std::string_view::npos;

  for (std::size_t index = 0; index < json_array_contents.size(); index += 1) {
    const char ch = json_array_contents[index];
    if (!in_string) {
      if (ch == '"') {
        in_string = true;
        string_start = index + 1;
      }
      continue;
    }

    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      if (string_start != std::string_view::npos) {
        values.push_back(UnescapeJsonString(json_array_contents.substr(string_start, index - string_start)));
      }
      in_string = false;
      string_start = std::string_view::npos;
    }
  }

  return values;
}

std::vector<int> ExtractJsonIntFields(const std::string& json, const std::string& key) {
  std::vector<int> values;
  const std::regex pattern("\"" + key + R"("\s*:\s*([0-9]+))");

  for (std::sregex_iterator it(json.begin(), json.end(), pattern), end; it != end; ++it) {
    const auto number = std::stoll((*it)[1].str());
    if (number > std::numeric_limits<int>::max()) {
      throw std::runtime_error("Memory location number exceeded int range");
    }
    values.push_back(static_cast<int>(number));
  }

  return values;
}

std::vector<Marker> LoadMarkers(const std::string& input_path) {
  std::ifstream input(input_path);
  if (!input) {
    throw std::runtime_error("Could not open marker payload file: " + input_path);
  }

  std::vector<Marker> markers;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }

    const auto fields = SplitTabs(line);
    if (fields.size() != 4 && fields.size() != 6) {
      throw std::runtime_error("Invalid marker payload line: expected 4 or 6 tab-separated fields");
    }

    markers.push_back({
        DecodeBase64(fields[0]),
        DecodeBase64(fields[1]),
        DecodeBase64(fields[2]),
        DecodeBase64(fields[3]),
        fields.size() >= 6 ? DecodeBase64(fields[4]) : std::string(),
        fields.size() >= 6 ? DecodeBase64(fields[5]) : std::string(),
    });
  }

  return markers;
}

std::vector<TrackRename> LoadTrackRenames(const std::string& input_path) {
  std::ifstream input(input_path);
  if (!input) {
    throw std::runtime_error("Could not open rename payload file: " + input_path);
  }

  std::vector<TrackRename> renames;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }

    const auto fields = SplitTabs(line);
    if (fields.size() != 3) {
      throw std::runtime_error("Invalid rename payload line: expected 3 tab-separated fields");
    }

    renames.push_back({
        DecodeBase64(fields[0]),
        DecodeBase64(fields[1]),
        DecodeBase64(fields[2]),
    });
  }

  return renames;
}

TrackMuteStateUpdate LoadTrackMuteStateUpdate(const std::string& input_path) {
  std::ifstream input(input_path);
  if (!input) {
    throw std::runtime_error("Could not open track mute payload file: " + input_path);
  }

  TrackMuteStateUpdate update;
  std::string line;
  bool saw_enabled = false;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }

    const auto fields = SplitTabs(line);
    if (fields.empty()) {
      continue;
    }

    if (fields[0] == "ENABLED") {
      if (fields.size() != 2) {
        throw std::runtime_error("Invalid track mute enabled line");
      }

      const auto value = fields[1];
      if (value == "1" || value == "true" || value == "TRUE") {
        update.enabled = true;
      } else if (value == "0" || value == "false" || value == "FALSE") {
        update.enabled = false;
      } else {
        throw std::runtime_error("Invalid track mute enabled value");
      }
      saw_enabled = true;
      continue;
    }

    if (fields[0] == "TRACK") {
      if (fields.size() != 2) {
        throw std::runtime_error("Invalid track mute track line");
      }

      const auto track_name = DecodeBase64(fields[1]);
      if (!track_name.empty()) {
        update.track_names.push_back(track_name);
      }
      continue;
    }

    throw std::runtime_error("Invalid track mute payload line");
  }

  if (!saw_enabled) {
    throw std::runtime_error("Track mute payload did not include enabled state");
  }
  if (update.track_names.empty()) {
    throw std::runtime_error("Track mute payload did not include any tracks");
  }

  return update;
}

RenamePlan LoadRenamePlan(const std::string& input_path) {
  std::ifstream input(input_path);
  if (!input) {
    throw std::runtime_error("Could not open rename plan file: " + input_path);
  }

  RenamePlan plan;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }

    const auto fields = SplitTabs(line);
    if (fields.empty()) {
      continue;
    }

    if (fields[0] == "MARKER") {
      if (fields.size() != 2) {
        throw std::runtime_error("Invalid rename plan marker line");
      }
      plan.marker_name = DecodeBase64(fields[1]);
      continue;
    }

    if (fields[0] == "PRIMARY") {
      if (fields.size() != 3) {
        throw std::runtime_error("Invalid rename plan primary track line");
      }
      plan.primary_track_id = DecodeBase64(fields[1]);
      plan.primary_track_name = DecodeBase64(fields[2]);
      continue;
    }

    if (fields[0] == "TRACK") {
      if (fields.size() != 3) {
        throw std::runtime_error("Invalid rename plan track line");
      }
      plan.tracks.push_back({
          DecodeBase64(fields[1]),
          DecodeBase64(fields[2]),
      });
      continue;
    }

    throw std::runtime_error("Unknown rename plan line type");
  }

  if (plan.marker_name.empty()) {
    throw std::runtime_error("Rename plan did not include a marker name");
  }
  if (plan.primary_track_id.empty()) {
    throw std::runtime_error("Rename plan did not include a primary track");
  }
  if (plan.tracks.empty()) {
    throw std::runtime_error("Rename plan did not include any tracks");
  }

  return plan;
}

DropToTakePlan LoadDropToTakePlan(const std::string& input_path) {
  std::ifstream input(input_path);
  if (!input) {
    throw std::runtime_error("Could not open drop-to-take plan file: " + input_path);
  }

  DropToTakePlan plan;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }

    const auto fields = SplitTabs(line);
    if (fields.empty()) {
      continue;
    }

    if (fields[0] == "PRIMARY") {
      if (fields.size() != 3) {
        throw std::runtime_error("Invalid drop-to-take plan primary track line");
      }
      plan.primary_track_id = DecodeBase64(fields[1]);
      plan.primary_track_name = DecodeBase64(fields[2]);
      continue;
    }

    if (fields[0] == "PLACEMENT_MODE") {
      if (fields.size() != 2) {
        throw std::runtime_error("Invalid drop-to-take plan placement mode line");
      }
      plan.placement_mode = fields[1];
      continue;
    }

    if (fields[0] == "TAKE_TRACK_KEYWORD") {
      if (fields.size() != 2) {
        throw std::runtime_error("Invalid drop-to-take plan take track keyword line");
      }
      plan.take_track_keyword = DecodeBase64(fields[1]);
      continue;
    }

    if (fields[0] == "TRACK") {
      if (fields.size() != 3) {
        throw std::runtime_error("Invalid drop-to-take plan track line");
      }
      plan.tracks.push_back({
          DecodeBase64(fields[1]),
          DecodeBase64(fields[2]),
      });
      continue;
    }

    throw std::runtime_error("Unknown drop-to-take plan line type");
  }

  if (plan.primary_track_id.empty()) {
    throw std::runtime_error("Drop-to-take plan did not include a primary track");
  }
  if (plan.placement_mode.empty()) {
    plan.placement_mode = "below-last-take";
  }
  if (plan.take_track_keyword.empty()) {
    plan.take_track_keyword = "take";
  }
  if (plan.tracks.empty()) {
    throw std::runtime_error("Drop-to-take plan did not include any tracks");
  }

  return plan;
}

bool StringContainsIgnoreCase(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) {
    return true;
  }
  if (haystack.size() < needle.size()) {
    return false;
  }
  for (std::size_t i = 0; i + needle.size() <= haystack.size(); i += 1) {
    bool match = true;
    for (std::size_t j = 0; j < needle.size(); j += 1) {
      if (std::tolower(static_cast<unsigned char>(haystack[i + j])) !=
          std::tolower(static_cast<unsigned char>(needle[j]))) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  return false;
}

MarkerEditPlan LoadMarkerEditPlan(const std::string& input_path, bool require_next_marker = true) {
  std::ifstream input(input_path);
  if (!input) {
    throw std::runtime_error("Could not open marker edit plan file: " + input_path);
  }

  MarkerEditPlan plan;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }

    const auto fields = SplitTabs(line);
    if (fields.size() != 5 && fields.size() != 7) {
      throw std::runtime_error("Invalid marker edit plan line");
    }

    Marker* target_marker = nullptr;
    if (fields[0] == "PREVIOUS") {
      target_marker = &plan.previous_marker;
    } else if (fields[0] == "NEXT") {
      target_marker = &plan.next_marker;
    } else {
      throw std::runtime_error("Unknown marker edit plan line type");
    }

    *target_marker = {
        DecodeBase64(fields[1]),
        DecodeBase64(fields[2]),
        DecodeBase64(fields[3]),
        DecodeBase64(fields[4]),
        fields.size() >= 7 ? DecodeBase64(fields[5]) : std::string(),
        fields.size() >= 7 ? DecodeBase64(fields[6]) : std::string(),
    };
  }

  if (plan.previous_marker.name.empty() || plan.previous_marker.start_time.empty()) {
    throw std::runtime_error("Marker edit plan did not include the previous marker");
  }
  if (require_next_marker && (plan.next_marker.name.empty() || plan.next_marker.start_time.empty())) {
    throw std::runtime_error("Marker edit plan did not include the next marker");
  }

  return plan;
}

std::vector<TrackInfo> ExtractTracksFromResponse(const std::string& json) {
  std::vector<TrackInfo> tracks;
  auto track_list = ExtractJsonArrayField(json, "track_list");
  if (!track_list) {
    track_list = ExtractJsonArrayField(json, "trackList");
  }
  if (!track_list) {
    return tracks;
  }

  for (const auto& object_json : ExtractTopLevelJsonObjects(*track_list)) {
    auto id = ExtractJsonScalarField(object_json, "id");
    if (!id) {
      id = ExtractJsonScalarField(object_json, "track_id");
    }

    auto name = ExtractJsonStringField(object_json, "name");
    if (!name) {
      name = ExtractJsonStringField(object_json, "track_name");
    }
    auto type = ExtractJsonStringField(object_json, "type");
    if (!type) {
      type = ExtractJsonStringField(object_json, "track_type");
    }
    if (!type) {
      type = ExtractJsonStringField(object_json, "trackType");
    }
    if (!type) {
      type = ExtractJsonStringField(object_json, "media_type");
    }
    if (!type) {
      type = ExtractJsonStringField(object_json, "mediaType");
    }
    auto format = ExtractJsonStringField(object_json, "format");
    if (!format) {
      format = ExtractJsonStringField(object_json, "track_format");
    }
    if (!format) {
      format = ExtractJsonStringField(object_json, "trackFormat");
    }
    auto is_record_enabled = ExtractJsonBoolField(object_json, "is_record_enabled");
    if (!is_record_enabled) {
      is_record_enabled = ExtractJsonBoolField(object_json, "isRecordEnabled");
    }
    auto is_record_safe_enabled = ExtractJsonBoolField(object_json, "is_record_enabled_safe");
    if (!is_record_safe_enabled) {
      is_record_safe_enabled = ExtractJsonBoolField(object_json, "isRecordEnabledSafe");
    }
    if (!is_record_safe_enabled) {
      is_record_safe_enabled = ExtractJsonBoolField(object_json, "is_record_safe_enabled");
    }
    if (!is_record_safe_enabled) {
      is_record_safe_enabled = ExtractJsonBoolField(object_json, "isRecordSafeEnabled");
    }

    auto is_muted = ExtractJsonBoolField(object_json, "is_muted");
    if (!is_muted) {
      is_muted = ExtractJsonBoolField(object_json, "isMuted");
    }
    if (!is_muted) {
      is_muted = ExtractJsonBoolField(object_json, "muted");
    }

    std::optional<std::string> is_selected_state;
    if (const auto track_attributes = ExtractJsonObjectField(object_json, "track_attributes")) {
      if (!is_muted) {
        is_muted = ExtractJsonBoolField(*track_attributes, "is_muted");
      }
      if (!is_muted) {
        is_muted = ExtractJsonBoolField(*track_attributes, "isMuted");
      }
      if (!is_muted) {
        is_muted = ExtractJsonBoolField(*track_attributes, "muted");
      }
      is_selected_state = ExtractJsonScalarField(*track_attributes, "is_selected");
      if (!is_selected_state) {
        is_selected_state = ExtractJsonScalarField(*track_attributes, "isSelected");
      }
    }
    if (!is_selected_state) {
      is_selected_state = ExtractJsonScalarField(object_json, "is_selected");
    }
    if (!is_selected_state) {
      is_selected_state = ExtractJsonScalarField(object_json, "isSelected");
    }
    if (is_selected_state) {
      const auto normalized = *is_selected_state;
      if (normalized == "0") {
        is_selected_state = std::string("None");
      } else if (normalized == "1") {
        is_selected_state = std::string("SetExplicitly");
      } else if (normalized == "2") {
        is_selected_state = std::string("SetImplicitly");
      } else if (normalized == "3") {
        is_selected_state = std::string("SetExplicitlyAndImplicitly");
      } else if (normalized.empty()) {
        is_selected_state = std::nullopt;
      } else {
        is_selected_state = normalized;
      }
    }

    if (!id || !name) {
      continue;
    }

    tracks.push_back({
        *id,
        *name,
        type.value_or(""),
        format.value_or(""),
        is_record_enabled,
        is_record_safe_enabled,
        is_muted,
        is_selected_state});
  }

  return tracks;
}

std::vector<std::string> ExtractCreatedTrackNamesFromResponse(const std::string& json) {
  auto created_track_names = ExtractJsonArrayField(json, "created_track_names");
  if (!created_track_names) {
    created_track_names = ExtractJsonArrayField(json, "createdTrackNames");
  }
  if (!created_track_names) {
    return {};
  }
  return ExtractTopLevelJsonStrings(*created_track_names);
}

std::vector<MemoryLocationInfo> ExtractMemoryLocationsFromResponse(const std::string& json) {
  std::vector<MemoryLocationInfo> memory_locations;
  auto memory_location_list = ExtractJsonArrayField(json, "memory_locations");
  if (!memory_location_list) {
    memory_location_list = ExtractJsonArrayField(json, "memoryLocations");
  }
  if (!memory_location_list) {
    return memory_locations;
  }

  for (const auto& object_json : ExtractTopLevelJsonObjects(*memory_location_list)) {
    auto number = ExtractJsonScalarField(object_json, "number");
    auto name = ExtractJsonStringField(object_json, "name");
    auto start_time = ExtractJsonStringField(object_json, "start_time");
    if (!start_time) {
      start_time = ExtractJsonStringField(object_json, "startTime");
    }
    auto end_time = ExtractJsonStringField(object_json, "end_time");
    if (!end_time) {
      end_time = ExtractJsonStringField(object_json, "endTime");
    }
    auto comments = ExtractJsonStringField(object_json, "comments");
    auto time_properties = ExtractJsonStringField(object_json, "time_properties");
    if (!time_properties) {
      time_properties = ExtractJsonStringField(object_json, "timeProperties");
    }
    auto location = ExtractJsonStringField(object_json, "location");
    auto track_name = ExtractJsonStringField(object_json, "track_name");
    if (!track_name) {
      track_name = ExtractJsonStringField(object_json, "trackName");
    }

    if (!number || !time_properties) {
      continue;
    }

    bool is_number = !number->empty();
    std::size_t index = 0;
    if (is_number && (*number)[0] == '-') {
      index = 1;
    }
    if (index >= number->size()) {
      is_number = false;
    }
    for (; is_number && index < number->size(); index += 1) {
      if (!std::isdigit(static_cast<unsigned char>((*number)[index]))) {
        is_number = false;
      }
    }

    if (!is_number) {
      continue;
    }

    const auto parsed_number = std::stoll(*number);
    if (parsed_number > std::numeric_limits<int>::max()) {
      throw std::runtime_error("Memory location number exceeded int range");
    }

    memory_locations.push_back({
        static_cast<int>(parsed_number),
        name ? *name : std::string(),
        start_time ? *start_time : std::string(),
        end_time ? *end_time : std::string(),
        comments ? *comments : std::string(),
        *time_properties,
        location ? *location : std::string(),
        track_name ? *track_name : std::string(),
    });
  }

  return memory_locations;
}

std::vector<FileLocationEntry> ExtractFileLocationsFromResponse(const std::string& json) {
  std::vector<FileLocationEntry> file_locations;
  auto file_location_list = ExtractJsonArrayField(json, "file_locations");
  if (!file_location_list) {
    file_location_list = ExtractJsonArrayField(json, "fileLocations");
  }
  if (!file_location_list) {
    return file_locations;
  }

  for (const auto& object_json : ExtractTopLevelJsonObjects(*file_location_list)) {
    auto path = ExtractJsonStringField(object_json, "path");
    if (!path) {
      continue;
    }

    FileLocationEntry entry;
    entry.path = *path;
    entry.file_id = ExtractJsonStringField(object_json, "file_id").value_or("");
    if (entry.file_id.empty()) {
      entry.file_id = ExtractJsonStringField(object_json, "fileId").value_or("");
    }

    if (const auto info_json = ExtractJsonObjectField(object_json, "info")) {
      entry.is_online = ExtractJsonBoolField(*info_json, "is_online").value_or(false);
    }

    file_locations.push_back(std::move(entry));
  }

  return file_locations;
}

std::vector<ClipInfo> ExtractClipsFromResponse(const std::string& json) {
  std::vector<ClipInfo> clips;
  auto clip_list = ExtractJsonArrayField(json, "clips");
  if (!clip_list) {
    clip_list = ExtractJsonArrayField(json, "clip_list");
  }
  if (!clip_list) {
    return clips;
  }

  for (const auto& object_json : ExtractTopLevelJsonObjects(*clip_list)) {
    auto file_id = ExtractJsonStringField(object_json, "file_id");
    if (!file_id) {
      file_id = ExtractJsonStringField(object_json, "fileId");
    }
    auto clip_id = ExtractJsonStringField(object_json, "clip_id");
    if (!clip_id) {
      clip_id = ExtractJsonStringField(object_json, "clipId");
    }
    auto clip_full_name = ExtractJsonStringField(object_json, "clip_full_name");
    if (!clip_full_name) {
      clip_full_name = ExtractJsonStringField(object_json, "clipFullName");
    }
    auto clip_root_name = ExtractJsonStringField(object_json, "clip_root_name");
    if (!clip_root_name) {
      clip_root_name = ExtractJsonStringField(object_json, "clipRootName");
    }

    if ((!clip_full_name || clip_full_name->empty()) && (!clip_root_name || clip_root_name->empty())) {
      continue;
    }

    ClipInfo clip;
    clip.file_id = file_id.value_or("");
    clip.clip_id = clip_id.value_or("");
    clip.clip_full_name = clip_full_name.value_or("");
    clip.clip_root_name = clip_root_name.value_or("");
    clip.file_path = ExtractJsonStringField(object_json, "file_path").value_or(
        ExtractJsonStringField(object_json, "filePath").value_or(
            ExtractJsonStringField(object_json, "path").value_or("")));
    if (clip.file_path.empty()) {
      if (const auto file_location_json = ExtractJsonObjectField(object_json, "file_location")) {
        clip.file_path = ExtractJsonStringField(*file_location_json, "path").value_or("");
      } else if (const auto file_location_json = ExtractJsonObjectField(object_json, "fileLocation")) {
        clip.file_path = ExtractJsonStringField(*file_location_json, "path").value_or("");
      }
    }

    if (const auto src_start_json = ExtractJsonObjectField(object_json, "src_start_point")) {
      if (const auto pos_str = ExtractJsonScalarField(*src_start_json, "position")) {
        try {
          clip.src_start_position = std::stoll(*pos_str);
          clip.src_start_time_type = ExtractJsonStringField(*src_start_json, "time_type").value_or(
              ExtractJsonStringField(*src_start_json, "timeType").value_or(""));
        } catch (const std::exception&) {
          clip.src_start_position = std::nullopt;
          clip.src_start_time_type = std::nullopt;
        }
      }
    } else if (const auto src_start_json = ExtractJsonObjectField(object_json, "srcStartPoint")) {
      if (const auto pos_str = ExtractJsonScalarField(*src_start_json, "position")) {
        try {
          clip.src_start_position = std::stoll(*pos_str);
          clip.src_start_time_type = ExtractJsonStringField(*src_start_json, "time_type").value_or(
              ExtractJsonStringField(*src_start_json, "timeType").value_or(""));
        } catch (const std::exception&) {
          clip.src_start_position = std::nullopt;
          clip.src_start_time_type = std::nullopt;
        }
      }
    }

    clips.push_back(std::move(clip));
  }

  return clips;
}

std::optional<std::string> ExtractTimelineLocationValue(const std::string& object_json, const std::string& key) {
  auto location_json = ExtractJsonObjectField(object_json, key);
  if (!location_json) {
    const auto camel_key = [&]() {
      std::string camel;
      bool upper_next = false;
      for (const char ch : key) {
        if (ch == '_') {
          upper_next = true;
          continue;
        }
        camel.push_back(upper_next ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : ch);
        upper_next = false;
      }
      return camel;
    }();
    location_json = ExtractJsonObjectField(object_json, camel_key);
  }

  if (location_json) {
    auto location = ExtractJsonStringField(*location_json, "location");
    if (location && !location->empty()) {
      return *location;
    }
  }

  auto direct_value = ExtractJsonStringField(object_json, key);
  if (!direct_value) {
    const auto camel_key = [&]() {
      std::string camel;
      bool upper_next = false;
      for (const char ch : key) {
        if (ch == '_') {
          upper_next = true;
          continue;
        }
        camel.push_back(upper_next ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : ch);
        upper_next = false;
      }
      return camel;
    }();
    direct_value = ExtractJsonStringField(object_json, camel_key);
  }

  if (direct_value && !direct_value->empty()) {
    return *direct_value;
  }

  return std::nullopt;
}

std::vector<PlaylistInfo> ExtractPlaylistsFromResponse(const std::string& json) {
  std::vector<PlaylistInfo> playlists;
  auto playlist_list = ExtractJsonArrayField(json, "playlists");
  if (!playlist_list) {
    playlist_list = ExtractJsonArrayField(json, "playlist_list");
  }
  if (!playlist_list) {
    return playlists;
  }

  for (const auto& object_json : ExtractTopLevelJsonObjects(*playlist_list)) {
    auto playlist_id = ExtractJsonStringField(object_json, "playlist_id");
    if (!playlist_id) {
      playlist_id = ExtractJsonStringField(object_json, "playlistId");
    }
    auto playlist_name = ExtractJsonStringField(object_json, "playlist_name");
    if (!playlist_name) {
      playlist_name = ExtractJsonStringField(object_json, "playlistName");
    }
    if (!playlist_id || playlist_id->empty()) {
      continue;
    }

    PlaylistInfo info;
    info.playlist_id = *playlist_id;
    info.playlist_name = playlist_name.value_or("");
    info.is_target = ExtractJsonBoolField(object_json, "is_target").value_or(
        ExtractJsonBoolField(object_json, "isTarget").value_or(false));
    info.is_solo_comp_lane_on = ExtractJsonBoolField(object_json, "is_solo_comp_lane_on").value_or(
        ExtractJsonBoolField(object_json, "isSoloCompLaneOn").value_or(false));
    playlists.push_back(std::move(info));
  }

  return playlists;
}

std::vector<PlaylistElementInfo> ExtractPlaylistElementsFromResponse(const std::string& json) {
  std::vector<PlaylistElementInfo> elements;
  auto element_list = ExtractJsonArrayField(json, "elements_list");
  if (!element_list) {
    element_list = ExtractJsonArrayField(json, "elementsList");
  }
  if (!element_list) {
    return elements;
  }

  for (const auto& object_json : ExtractTopLevelJsonObjects(*element_list)) {
    PlaylistElementInfo element;
    element.start_time = ExtractTimelineLocationValue(object_json, "start_time").value_or("");
    element.play_time = ExtractTimelineLocationValue(object_json, "play_time").value_or("");
    element.stop_time = ExtractTimelineLocationValue(object_json, "stop_time").value_or("");
    element.end_time = ExtractTimelineLocationValue(object_json, "end_time").value_or("");

    auto channel_clips_json = ExtractJsonArrayField(object_json, "channel_clips");
    if (!channel_clips_json) {
      channel_clips_json = ExtractJsonArrayField(object_json, "channelClips");
    }
    if (!channel_clips_json) {
      continue;
    }

    for (const auto& clip_json : ExtractTopLevelJsonObjects(*channel_clips_json)) {
      PlaylistElementClipInfo channel_clip;
      channel_clip.is_null = ExtractJsonBoolField(clip_json, "is_null").value_or(
          ExtractJsonBoolField(clip_json, "isNull").value_or(false));
      channel_clip.clip_id = ExtractJsonStringField(clip_json, "clip_id").value_or(
          ExtractJsonStringField(clip_json, "clipId").value_or(""));
      element.channel_clips.push_back(std::move(channel_clip));
    }

    elements.push_back(std::move(element));
  }

  return elements;
}

bool LooksLikeJsonNumber(std::string_view value) {
  if (value.empty()) {
    return false;
  }

  std::size_t index = 0;
  if (value[0] == '-') {
    index = 1;
  }
  if (index >= value.size()) {
    return false;
  }

  for (; index < value.size(); index += 1) {
    if (!std::isdigit(static_cast<unsigned char>(value[index]))) {
      return false;
    }
  }
  return true;
}

std::string Trimmed(std::string_view value) {
  std::size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
    start += 1;
  }

  std::size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    end -= 1;
  }

  return std::string(value.substr(start, end - start));
}

std::string NormalizeDropToTakeTrackKeyword(std::string_view keyword) {
  const auto trimmed_keyword = Trimmed(keyword);
  return trimmed_keyword.empty() ? "take" : trimmed_keyword;
}

bool TrackMatchesDropToTakeKeyword(
    const TrackInfo& track,
    std::string_view normalized_take_track_keyword) {
  return StringContainsIgnoreCase(track.name, normalized_take_track_keyword);
}

std::string BuildDropToTakeNoTargetTrackMessage(
    std::string_view normalized_take_track_keyword,
    std::string_view primary_track_type,
    int required_channel_count) {
  const auto keyword = NormalizeDropToTakeTrackKeyword(normalized_take_track_keyword);
  const auto track_kind = StringContainsIgnoreCase(primary_track_type, "audio")
      ? std::string("compatible audio tracks")
      : std::string("compatible tracks");
  const int safe_required_channel_count = std::max(1, required_channel_count);
  return std::string("No open Take tracks found. Looking below the captured record tracks for at least ")
      + std::to_string(safe_required_channel_count)
      + (safe_required_channel_count == 1 ? " channel of " : " channels of ")
      + track_kind
      + " with \""
      + keyword
      + "\" in the name and no clips in this time range.";
}

std::string LowercaseAscii(std::string_view value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (unsigned char ch : value) {
    lowered.push_back(static_cast<char>(std::tolower(ch)));
  }
  return lowered;
}

std::string CompactTrackType(std::string_view value) {
  const auto lowered = LowercaseAscii(Trimmed(value));
  std::string compacted;
  compacted.reserve(lowered.size());
  for (unsigned char ch : lowered) {
    if (std::isalnum(ch)) {
      compacted.push_back(static_cast<char>(ch));
    }
  }
  return compacted;
}

std::string ExtractTrackLayoutToken(std::string_view track_type) {
  const auto lowered = LowercaseAscii(Trimmed(track_type));
  if (lowered.empty()) {
    return "";
  }

  static const std::regex numeric_layout_pattern(R"((\d+(?:\.\d+)+))");
  std::smatch numeric_layout_match;
  if (std::regex_search(lowered, numeric_layout_match, numeric_layout_pattern) && numeric_layout_match.size() > 1) {
    return numeric_layout_match[1].str();
  }

  static const std::regex named_layout_pattern(
      R"(\b(mono|stereo|lcrs|lcr|quad|hex|octo|binaural)\b)");
  std::smatch named_layout_match;
  if (std::regex_search(lowered, named_layout_match, named_layout_pattern) && named_layout_match.size() > 1) {
    return named_layout_match[1].str();
  }

  return "";
}

bool AreTrackTypesCompatible(std::string_view source_type, std::string_view candidate_type) {
  const auto source_compact = CompactTrackType(source_type);
  const auto candidate_compact = CompactTrackType(candidate_type);
  if (source_compact.empty() || candidate_compact.empty()) {
    return true;
  }
  if (source_compact == candidate_compact) {
    return true;
  }

  const auto source_layout = ExtractTrackLayoutToken(source_type);
  const auto candidate_layout = ExtractTrackLayoutToken(candidate_type);
  return !source_layout.empty() && source_layout == candidate_layout;
}

std::string TrackFormatForChannelCount(const TrackInfo& track) {
  const auto format = Trimmed(track.format);
  if (!format.empty()) {
    return format;
  }
  return Trimmed(track.type);
}

bool TrackHasExplicitChannelFormat(const TrackInfo& track) {
  const auto compacted_format = CompactTrackType(track.format);
  return !compacted_format.empty()
      && compacted_format.find("unknown") == std::string::npos
      && compacted_format.find("none") == std::string::npos;
}

int AmbisonicOrderChannelCount(std::string_view compacted_format) {
  if (compacted_format.find("1storderambisonics") != std::string::npos
      || compacted_format.find("firstorderambisonics") != std::string::npos) {
    return 4;
  }
  if (compacted_format.find("2ndorderambisonics") != std::string::npos
      || compacted_format.find("secondorderambisonics") != std::string::npos) {
    return 9;
  }
  if (compacted_format.find("3rdorderambisonics") != std::string::npos
      || compacted_format.find("thirdorderambisonics") != std::string::npos) {
    return 16;
  }
  return 0;
}

int NumericLayoutChannelCount(std::string_view format) {
  const auto lowered = LowercaseAscii(format);
  if (lowered.find("tf_") == std::string::npos
      && lowered.find("tformat_") == std::string::npos
      && lowered.find('.') == std::string::npos) {
    return 0;
  }

  int total = 0;
  bool saw_number = false;
  for (std::size_t index = 0; index < lowered.size();) {
    if (!std::isdigit(static_cast<unsigned char>(lowered[index]))) {
      index += 1;
      continue;
    }
    int value = 0;
    while (index < lowered.size() && std::isdigit(static_cast<unsigned char>(lowered[index]))) {
      value = (value * 10) + (lowered[index] - '0');
      index += 1;
    }
    total += value;
    saw_number = true;
  }

  return saw_number ? total : 0;
}

int TrackChannelCount(const TrackInfo& track) {
  const auto format = TrackFormatForChannelCount(track);
  const auto compacted_format = CompactTrackType(format);
  if (compacted_format.empty()) {
    return StringContainsIgnoreCase(track.type, "audio") ? 1 : 0;
  }
  if (compacted_format.find("mono") != std::string::npos) {
    return 1;
  }
  if (compacted_format.find("stereo") != std::string::npos
      || compacted_format.find("binaural") != std::string::npos) {
    return 2;
  }
  if (compacted_format.find("lcrs") != std::string::npos
      || compacted_format.find("quad") != std::string::npos) {
    return 4;
  }
  if (compacted_format.find("lcr") != std::string::npos) {
    return 3;
  }
  if (compacted_format.find("hex") != std::string::npos) {
    return 6;
  }
  if (compacted_format.find("octo") != std::string::npos) {
    return 8;
  }
  const int ambisonic_count = AmbisonicOrderChannelCount(compacted_format);
  if (ambisonic_count > 0) {
    return ambisonic_count;
  }
  const int numeric_count = NumericLayoutChannelCount(format);
  if (numeric_count > 0) {
    return numeric_count;
  }
  return StringContainsIgnoreCase(track.type, "audio") ? 1 : 0;
}

void ReplaceAll(std::string& value, std::string_view needle, std::string_view replacement) {
  if (needle.empty()) {
    return;
  }

  std::size_t position = 0;
  while ((position = value.find(needle.data(), position, needle.size())) != std::string::npos) {
    value.replace(position, needle.size(), replacement.data(), replacement.size());
    position += replacement.size();
  }
}

std::string NormalizeComparableTimecode(std::string_view value) {
  const auto trimmed = Trimmed(value);
  static const std::regex pattern(R"(^(\d{1,2}):(\d{1,2}):(\d{1,2})[:;](\d{1,2})(?:[.;]\d+)?$)");
  std::smatch match;
  if (!std::regex_match(trimmed, match, pattern)) {
    return trimmed;
  }

  std::ostringstream normalized;
  normalized << std::setfill('0')
             << std::setw(2) << std::stoi(match[1].str()) << ':'
             << std::setw(2) << std::stoi(match[2].str()) << ':'
             << std::setw(2) << std::stoi(match[3].str()) << ':'
             << std::setw(2) << std::stoi(match[4].str());
  return normalized.str();
}

std::string NormalizeSessionStartTimecode(std::string_view value) {
  const auto normalized = NormalizeComparableTimecode(value);
  if (!normalized.empty() && normalized != Trimmed(value)) {
    return normalized;
  }

  const auto trimmed = Trimmed(value);
  static const std::regex seconds_only_pattern(R"(^(\d{1,2}):(\d{1,2}):(\d{1,2})(?:[.;]\d+)?$)");
  std::smatch match;
  if (!std::regex_match(trimmed, match, seconds_only_pattern)) {
    return normalized;
  }

  std::ostringstream normalized_seconds;
  normalized_seconds << std::setfill('0')
                     << std::setw(2) << std::stoi(match[1].str()) << ':'
                     << std::setw(2) << std::stoi(match[2].str()) << ':'
                     << std::setw(2) << std::stoi(match[3].str()) << ":00";
  return normalized_seconds.str();
}

std::string NormalizeSelectedClipName(std::string current_name);
bool DoNormalizedClipNamesMatch(std::string_view left_name, std::string_view right_name);
std::string ExtractSessionExportTrackBody(std::string_view session_export_text, std::string_view track_name);

bool SessionExportTrackHeaderMatches(std::string_view header_name, std::string_view wanted_track_name) {
  const std::string h = LowercaseAscii(Trimmed(std::string(header_name)));
  const std::string w = LowercaseAscii(Trimmed(std::string(wanted_track_name)));
  if (h.empty() || w.empty()) {
    return false;
  }
  if (h == w) {
    return true;
  }
  if (h.rfind(w, 0) == 0 && h.size() > w.size()) {
    const unsigned char boundary = static_cast<unsigned char>(h[w.size()]);
    return std::isspace(boundary) != 0 || boundary == '(' || boundary == '[' || boundary == '-';
  }
  return false;
}

bool SubframeRangesOverlap(long long a0, long long a1, long long b0, long long b1) {
  if (a0 > a1) {
    std::swap(a0, a1);
  }
  if (b0 > b1) {
    std::swap(b0, b1);
  }
  return a0 <= b1 && b0 <= a1;
}

std::optional<BarsBeatsPosition> ParseBarsBeatsPosition(std::string_view value) {
  static const std::regex pattern(R"(^\s*(-?\d+)\|\s*(-?\d+)(?:\|\s*(-?\d+))?\s*$)");
  std::match_results<std::string_view::const_iterator> match;
  if (!std::regex_match(value.begin(), value.end(), match, pattern)) {
    return std::nullopt;
  }

  BarsBeatsPosition position;
  position.bar = std::stoll(std::string(match[1].first, match[1].second));
  position.beat = std::stoll(std::string(match[2].first, match[2].second));
  position.tick = match[3].matched
      ? std::stoll(std::string(match[3].first, match[3].second))
      : 0;
  return position;
}

bool BarsBeatsPositionLess(const BarsBeatsPosition& left, const BarsBeatsPosition& right) {
  if (left.bar != right.bar) {
    return left.bar < right.bar;
  }
  if (left.beat != right.beat) {
    return left.beat < right.beat;
  }
  return left.tick < right.tick;
}

bool BarsBeatsRangesOverlap(BarsBeatsPosition a0,
                            BarsBeatsPosition a1,
                            BarsBeatsPosition b0,
                            BarsBeatsPosition b1) {
  if (BarsBeatsPositionLess(a1, a0)) {
    std::swap(a0, a1);
  }
  if (BarsBeatsPositionLess(b1, b0)) {
    std::swap(b0, b1);
  }
  return !BarsBeatsPositionLess(a1, b0) && !BarsBeatsPositionLess(b1, a0);
}

std::vector<std::string> ExtractSessionExportFields(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (start <= line.size()) {
    const auto tab = line.find('\t', start);
    const auto raw_field = tab == std::string::npos
        ? line.substr(start)
        : line.substr(start, tab - start);
    const auto trimmed_field = Trimmed(raw_field);
    if (!trimmed_field.empty()) {
      fields.push_back(trimmed_field);
    }
    if (tab == std::string::npos) {
      break;
    }
    start = tab + 1;
  }
  return fields;
}

bool SessionExportFieldsLookLikeClipRow(const std::vector<std::string>& fields) {
  if (fields.size() < 5) {
    return false;
  }
  if (!std::all_of(fields[0].begin(), fields[0].end(), [](unsigned char ch) { return std::isdigit(ch); })) {
    return false;
  }
  if (!std::all_of(fields[1].begin(), fields[1].end(), [](unsigned char ch) { return std::isdigit(ch); })) {
    return false;
  }
  return true;
}

std::optional<std::pair<long long, long long>> ParseSessionExportSubframeRangeFromFields(
    const std::vector<std::string>& fields,
    const TimeCodeRateInfo& rate_info) {
  if (!SessionExportFieldsLookLikeClipRow(fields)) {
    return std::nullopt;
  }

  try {
    const std::string start_tc = NormalizeComparableTimecode(fields[3]);
    const std::string end_tc = NormalizeComparableTimecode(fields[4]);
    if (start_tc.empty() || end_tc.empty()) {
      return std::nullopt;
    }
    const long long start_sf = TimecodeStringToSubframes(start_tc, rate_info);
    const long long end_sf = TimecodeStringToSubframes(end_tc, rate_info);
    return std::make_pair(std::min(start_sf, end_sf), std::max(start_sf, end_sf));
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool SessionExportTrackBodyHasClipRows(std::string_view session_export_text, std::string_view track_name) {
  const std::string body_for_track = ExtractSessionExportTrackBody(session_export_text, track_name);
  if (body_for_track.empty()) {
    return false;
  }

  std::istringstream lines{body_for_track};
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (SessionExportFieldsLookLikeClipRow(ExtractSessionExportFields(line))) {
      return true;
    }
  }
  return false;
}

/**
 * Parses ExportSessionInfoAsText track EDL sections and returns true if any clip on
 * `track_name` overlaps [range_start_sf, range_end_sf] in session timecode.
 */
bool SessionExportTrackOverlapsSubframeRange(std::string_view session_export_text,
                                            std::string_view track_name,
                                            long long range_start_sf,
                                            long long range_end_sf,
                                            const TimeCodeRateInfo& rate_info) {
  const std::string body_for_track = ExtractSessionExportTrackBody(session_export_text, track_name);
  if (body_for_track.empty()) {
    return false;
  }

  bool saw_clip_row = false;
  bool saw_parseable_range = false;
  std::istringstream lines{body_for_track};
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    const auto fields = ExtractSessionExportFields(line);
    if (!SessionExportFieldsLookLikeClipRow(fields)) {
      continue;
    }
    saw_clip_row = true;

    const auto parsed_range = ParseSessionExportSubframeRangeFromFields(fields, rate_info);
    if (!parsed_range.has_value()) {
      continue;
    }

    saw_parseable_range = true;
    if (SubframeRangesOverlap(parsed_range->first, parsed_range->second, range_start_sf, range_end_sf)) {
      return true;
    }
  }

  return saw_clip_row && !saw_parseable_range;
}

std::optional<std::pair<BarsBeatsPosition, BarsBeatsPosition>> FindClipBarsBeatsRangeFromSessionExport(
    std::string_view session_export_text,
    std::string_view clip_name) {
  if (Trimmed(session_export_text).empty() || Trimmed(clip_name).empty()) {
    return std::nullopt;
  }

  std::istringstream lines{std::string(session_export_text)};
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    const auto fields = ExtractSessionExportFields(line);
    if (fields.size() < 5) {
      continue;
    }
    if (!std::all_of(fields[0].begin(), fields[0].end(), [](unsigned char ch) { return std::isdigit(ch); })) {
      continue;
    }
    if (!std::all_of(fields[1].begin(), fields[1].end(), [](unsigned char ch) { return std::isdigit(ch); })) {
      continue;
    }
    if (!DoNormalizedClipNamesMatch(fields[2], clip_name)) {
      continue;
    }

    const auto start = ParseBarsBeatsPosition(fields[3]);
    const auto end = ParseBarsBeatsPosition(fields[4]);
    if (start && end) {
      return std::make_pair(*start, *end);
    }
  }

  return std::nullopt;
}

std::optional<std::pair<BarsBeatsPosition, BarsBeatsPosition>> FindClipBarsBeatsRangeOnTrackFromSessionExport(
    std::string_view session_export_text,
    std::string_view track_name,
    std::string_view clip_name) {
  const std::string body_for_track = ExtractSessionExportTrackBody(session_export_text, track_name);
  if (body_for_track.empty() || Trimmed(clip_name).empty()) {
    return std::nullopt;
  }

  std::istringstream lines{body_for_track};
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    const auto fields = ExtractSessionExportFields(line);
    if (fields.size() < 5) {
      continue;
    }
    if (!std::all_of(fields[0].begin(), fields[0].end(), [](unsigned char ch) { return std::isdigit(ch); })) {
      continue;
    }
    if (!std::all_of(fields[1].begin(), fields[1].end(), [](unsigned char ch) { return std::isdigit(ch); })) {
      continue;
    }
    if (!DoNormalizedClipNamesMatch(fields[2], clip_name)) {
      continue;
    }

    const auto start = ParseBarsBeatsPosition(fields[3]);
    const auto end = ParseBarsBeatsPosition(fields[4]);
    if (start && end) {
      return std::make_pair(*start, *end);
    }
  }

  return std::nullopt;
}

bool SessionExportTrackOverlapsBarsBeatsRange(std::string_view session_export_text,
                                              std::string_view track_name,
                                              const BarsBeatsPosition& range_start,
                                              const BarsBeatsPosition& range_end) {
  const std::string body_for_track = ExtractSessionExportTrackBody(session_export_text, track_name);
  if (body_for_track.empty()) {
    return false;
  }

  std::istringstream lines{body_for_track};
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    const auto fields = ExtractSessionExportFields(line);
    if (fields.size() < 5) {
      continue;
    }
    if (!std::all_of(fields[0].begin(), fields[0].end(), [](unsigned char ch) { return std::isdigit(ch); })) {
      continue;
    }
    if (!std::all_of(fields[1].begin(), fields[1].end(), [](unsigned char ch) { return std::isdigit(ch); })) {
      continue;
    }

    const auto start = ParseBarsBeatsPosition(fields[3]);
    const auto end = ParseBarsBeatsPosition(fields[4]);
    if (!start || !end) {
      continue;
    }
    if (BarsBeatsRangesOverlap(*start, *end, range_start, range_end)) {
      return true;
    }
  }

  return false;
}

std::string ExtractSessionExportTrackBody(std::string_view session_export_text, std::string_view track_name) {
  if (Trimmed(session_export_text).empty()) {
    return "";
  }

  const std::string export_copy(session_export_text);

  static const std::regex header_re(
      R"(^[^\S\r\n]*[Tt][Rr][Aa][Cc][Kk]\s+[Nn][Aa][Mm][Ee]\s*:\s*([^\r\n]+))",
      std::regex_constants::multiline);

  std::vector<std::tuple<std::size_t, std::size_t, std::string>> headers;
  for (std::sregex_iterator it(export_copy.begin(), export_copy.end(), header_re), end; it != end; ++it) {
    const std::smatch& m = *it;
    const std::size_t begin_pos = static_cast<std::size_t>(m.position());
    const std::size_t end_pos = begin_pos + static_cast<std::size_t>(m.length());
    headers.emplace_back(begin_pos, end_pos, m[1].str());
  }

  for (std::size_t i = 0; i < headers.size(); i += 1) {
    if (!SessionExportTrackHeaderMatches(std::get<2>(headers[i]), track_name)) {
      continue;
    }
    const std::size_t body_start = std::get<1>(headers[i]);
    const std::size_t body_end =
        (i + 1 < headers.size()) ? std::get<0>(headers[i + 1]) : export_copy.size();
    return export_copy.substr(body_start, body_end - body_start);
  }

  return "";
}

bool SessionExportTrackBodyContainsTimecode(std::string_view session_export_text, std::string_view track_name) {
  const auto body_for_track = ExtractSessionExportTrackBody(session_export_text, track_name);
  if (body_for_track.empty()) {
    return false;
  }

  static const std::regex timecode_re(R"(\d{2}:\d{2}:\d{2}[:;]\d{2}(?:\.\d{1,3})?)");
  return std::regex_search(body_for_track, timecode_re);
}

bool SessionExportTrackBodyContainsSampleLocations(std::string_view session_export_text, std::string_view track_name) {
  const auto body_for_track = ExtractSessionExportTrackBody(session_export_text, track_name);
  if (body_for_track.empty()) {
    return false;
  }

  static const std::regex sample_row_re(R"(^\s*\d+\s+\d+\s+.+?\s+[-]?\d+\s+[-]?\d+\s+[-]?\d+)",
                                        std::regex_constants::multiline);
  return std::regex_search(body_for_track, sample_row_re);
}

bool SessionExportContainsTimecode(std::string_view session_export_text) {
  static const std::regex timecode_re(R"(\d{2}:\d{2}:\d{2}[:;]\d{2}(?:\.\d{1,3})?)");
  return std::regex_search(std::string(session_export_text), timecode_re);
}

bool SessionExportContainsSampleLocations(std::string_view session_export_text) {
  static const std::regex sample_row_re(R"(^\s*\d+\s+\d+\s+.+?\s+[-]?\d+\s+[-]?\d+\s+[-]?\d+)",
                                        std::regex_constants::multiline);
  return std::regex_search(std::string(session_export_text), sample_row_re);
}

std::vector<std::string> ExtractTrackClipNamesFromSessionExport(
    std::string_view session_export_text,
    std::string_view track_name) {
  std::vector<std::string> clip_names;
  const std::string body_for_track = ExtractSessionExportTrackBody(session_export_text, track_name);
  if (body_for_track.empty()) {
    return clip_names;
  }

  std::unordered_set<std::string> seen_names;
  std::istringstream lines{body_for_track};
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
      const auto tab = line.find('\t', start);
      const auto raw_field = tab == std::string::npos
          ? line.substr(start)
          : line.substr(start, tab - start);
      const auto trimmed_field = Trimmed(raw_field);
      if (!trimmed_field.empty()) {
        fields.push_back(trimmed_field);
      }
      if (tab == std::string::npos) {
        break;
      }
      start = tab + 1;
    }

    if (fields.size() < 5) {
      continue;
    }
    if (!std::all_of(fields[0].begin(), fields[0].end(), [](unsigned char ch) { return std::isdigit(ch); })) {
      continue;
    }
    if (!std::all_of(fields[1].begin(), fields[1].end(), [](unsigned char ch) { return std::isdigit(ch); })) {
      continue;
    }

    const auto normalized_name = NormalizeSelectedClipName(fields[2]);
    if (normalized_name.empty()) {
      continue;
    }
    const auto folded_name = LowercaseAscii(normalized_name);
    if (!seen_names.insert(folded_name).second) {
      continue;
    }
    clip_names.push_back(normalized_name);
  }

  return clip_names;
}

std::vector<SessionExportClipPlacement> FindTrackClipPlacementsFromSessionExport(
    std::string_view session_export_text,
    std::string_view track_name,
    long long range_start_sf,
    long long range_end_sf,
    const std::vector<std::string>& clip_names,
    const TimeCodeRateInfo& rate_info) {
  std::vector<SessionExportClipPlacement> placements;
  const std::string body_for_track = ExtractSessionExportTrackBody(session_export_text, track_name);
  if (body_for_track.empty()) {
    return placements;
  }

  std::unordered_set<std::string> wanted_names;
  for (const auto& clip_name : clip_names) {
    const auto normalized = LowercaseAscii(NormalizeSelectedClipName(clip_name));
    if (!normalized.empty()) {
      wanted_names.insert(normalized);
    }
  }

  std::istringstream lines{body_for_track};
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    const auto fields = ExtractSessionExportFields(line);
    if (!SessionExportFieldsLookLikeClipRow(fields)) {
      continue;
    }

    const std::string normalized_name = NormalizeSelectedClipName(fields[2]);
    if (normalized_name.empty()) {
      continue;
    }
    if (!wanted_names.empty() && wanted_names.find(LowercaseAscii(normalized_name)) == wanted_names.end()) {
      continue;
    }

    const auto parsed_range = ParseSessionExportSubframeRangeFromFields(fields, rate_info);
    if (!parsed_range.has_value()) {
      continue;
    }

    if (!SubframeRangesOverlap(parsed_range->first, parsed_range->second, range_start_sf, range_end_sf)) {
      continue;
    }

    placements.push_back({
        normalized_name,
        NormalizeComparableTimecode(fields[3]),
        NormalizeComparableTimecode(fields[4]),
        parsed_range->first,
        parsed_range->second,
    });
  }

  return placements;
}

std::vector<SessionExportClipPlacement> FindAnyClipPlacementsFromSessionExport(
    std::string_view session_export_text,
    long long range_start_sf,
    long long range_end_sf,
    const std::vector<std::string>& clip_names,
    const TimeCodeRateInfo& rate_info) {
  std::vector<SessionExportClipPlacement> placements;
  if (Trimmed(session_export_text).empty()) {
    return placements;
  }

  std::unordered_set<std::string> wanted_names;
  for (const auto& clip_name : clip_names) {
    const auto normalized = LowercaseAscii(NormalizeSelectedClipName(clip_name));
    if (!normalized.empty()) {
      wanted_names.insert(normalized);
    }
  }

  std::istringstream lines{std::string(session_export_text)};
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    const auto fields = ExtractSessionExportFields(line);
    if (!SessionExportFieldsLookLikeClipRow(fields)) {
      continue;
    }

    const std::string normalized_name = NormalizeSelectedClipName(fields[2]);
    if (normalized_name.empty()) {
      continue;
    }
    if (!wanted_names.empty() && wanted_names.find(LowercaseAscii(normalized_name)) == wanted_names.end()) {
      continue;
    }

    const auto parsed_range = ParseSessionExportSubframeRangeFromFields(fields, rate_info);
    if (!parsed_range.has_value()) {
      continue;
    }

    if (!SubframeRangesOverlap(parsed_range->first, parsed_range->second, range_start_sf, range_end_sf)) {
      continue;
    }

    placements.push_back({
        normalized_name,
        NormalizeComparableTimecode(fields[3]),
        NormalizeComparableTimecode(fields[4]),
        parsed_range->first,
        parsed_range->second,
    });
  }

  return placements;
}

std::string BuildClipPlacementSignature(std::string_view clip_name,
                                        long long start_subframes,
                                        long long end_subframes) {
  const auto normalized_name = LowercaseAscii(NormalizeSelectedClipName(std::string(clip_name)));
  std::ostringstream signature;
  signature << normalized_name
            << '\x1f'
            << std::min(start_subframes, end_subframes)
            << '\x1f'
            << std::max(start_subframes, end_subframes);
  return signature.str();
}

std::unordered_set<std::string> CollectTrackClipPlacementSignaturesFromSessionExport(
    std::string_view session_export_text,
    std::string_view track_name,
    const TimeCodeRateInfo& rate_info) {
  std::unordered_set<std::string> signatures;
  const auto placements = FindTrackClipPlacementsFromSessionExport(
      session_export_text,
      track_name,
      0LL,
      std::numeric_limits<long long>::max(),
      {},
      rate_info);
  for (const auto& placement : placements) {
    signatures.insert(BuildClipPlacementSignature(
        placement.clip_name,
        placement.start_subframes,
        placement.end_subframes));
  }
  return signatures;
}

void LogDropToTakePrimaryTrackEndTime(std::string_view track_name,
                                      std::string_view source_label,
                                      std::string_view end_timecode) {
  std::cerr << "[drop-to-take] primary-track-end-time"
            << " track=\"" << track_name << "\""
            << " source=\"" << source_label << "\""
            << " end=\"" << NormalizeComparableTimecode(std::string(end_timecode)) << "\"\n";
}

std::vector<std::string> CollectUniqueClipNamesFromPlacements(
    const std::vector<SessionExportClipPlacement>& placements) {
  std::vector<std::string> names;
  names.reserve(placements.size());
  for (const auto& placement : placements) {
    AppendUniqueClipName(names, placement.clip_name);
  }
  return names;
}

std::optional<std::string> FindClipTimelineStartFromSessionExportByName(
    std::string_view session_export_text,
    std::string_view clip_name,
    std::string_view reference_timecode,
    const TimeCodeRateInfo& rate_info) {
  const std::string normalized_target = NormalizeSelectedClipName(std::string(clip_name));
  if (normalized_target.empty() || Trimmed(session_export_text).empty()) {
    return std::nullopt;
  }

  long long reference_subframes = -1;
  const auto normalized_reference = NormalizeComparableTimecode(reference_timecode);
  if (!Trimmed(normalized_reference).empty()) {
    try {
      reference_subframes = TimecodeStringToSubframes(normalized_reference, rate_info);
    } catch (const std::exception&) {
      reference_subframes = -1;
    }
  }

  static const std::regex clip_row_re(
      R"(^\s*\d+\s+\d+\s+(.+?)\s+(\d{2}:\d{2}:\d{2}[:;]\d{2}(?:\.\d{1,3})?)\s+(\d{2}:\d{2}:\d{2}[:;]\d{2}(?:\.\d{1,3})?))");

  std::istringstream lines{std::string(session_export_text)};
  std::string line;
  std::optional<std::string> best_start_time;
  long long best_distance = std::numeric_limits<long long>::max();

  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    std::smatch match;
    if (!std::regex_search(line, match, clip_row_re) || match.size() < 4) {
      continue;
    }

    const std::string normalized_candidate = NormalizeSelectedClipName(match[1].str());
    if (normalized_candidate.empty()
        || LowercaseAscii(normalized_candidate) != LowercaseAscii(normalized_target)) {
      continue;
    }

    const std::string start_tc = NormalizeComparableTimecode(match[2].str());
    const std::string end_tc = NormalizeComparableTimecode(match[3].str());
    if (start_tc.empty()) {
      continue;
    }

    long long distance = 0;
    if (reference_subframes >= 0) {
      try {
        const auto start_sf = TimecodeStringToSubframes(start_tc, rate_info);
        const auto end_sf = TimecodeStringToSubframes(end_tc, rate_info);
        const auto range_start = std::min(start_sf, end_sf);
        const auto range_end = std::max(start_sf, end_sf);
        distance = (reference_subframes >= range_start && reference_subframes <= range_end)
            ? 0
            : std::llabs(start_sf - reference_subframes);
      } catch (const std::exception&) {
        continue;
      }
    }

    if (!best_start_time || distance < best_distance) {
      best_start_time = start_tc;
      best_distance = distance;
      if (best_distance == 0 && reference_subframes >= 0) {
        return best_start_time;
      }
    }
  }

  return best_start_time;
}

std::string SanitizeMarkerCommentForClipName(std::string comment) {
  comment = Trimmed(comment);
  if (comment.rfind("Selected.", 0) == 0) {
    comment.erase(0, std::string("Selected.").size());
    comment = Trimmed(comment);
  }
  if (comment.empty()) {
    comment = "(No Text)";
  }

  ReplaceAll(comment, "\r\n", " ");
  ReplaceAll(comment, "\n", " ");
  ReplaceAll(comment, "\r", " ");
  ReplaceAll(comment, "\\n", " ");

  std::string sanitized;
  sanitized.reserve(comment.size() * 2);
  for (unsigned char raw_ch : comment) {
    const char ch = static_cast<char>(raw_ch);
    switch (ch) {
      case ',':
      case '#':
      case '!':
      case '$':
      case '%':
      case '^':
      case '*':
      case ';':
      case ':':
      case '{':
      case '}':
      case '=':
      case '`':
      case '~':
      case '(':
      case ')':
      case '\'':
        break;
      case '.':
      case '"':
      case '?':
        sanitized.push_back('_');
        break;
      case '/':
        sanitized.push_back('-');
        break;
      case '&':
      case '+':
        sanitized += " and ";
        break;
      default:
        sanitized.push_back(ch);
        break;
    }
  }

  std::string collapsed;
  collapsed.reserve(sanitized.size());
  bool last_was_underscore = false;
  bool last_was_space = false;
  for (unsigned char raw_ch : sanitized) {
    const char ch = static_cast<char>(raw_ch);
    if (std::isspace(raw_ch) != 0) {
      if (!last_was_space) {
        collapsed.push_back(' ');
        last_was_space = true;
      }
      continue;
    }

    if (ch == '_') {
      if (!last_was_underscore) {
        collapsed.push_back(ch);
        last_was_underscore = true;
      }
      last_was_space = false;
      continue;
    }

    collapsed.push_back(ch);
    last_was_underscore = false;
    last_was_space = false;
  }

  collapsed = Trimmed(collapsed);
  collapsed = std::regex_replace(
      collapsed,
      std::regex(R"(^[_\-\s]*\d{1,3}[A-Za-z]?[_\-\s]+)"),
      "");
  collapsed = Trimmed(collapsed);
  return collapsed.empty() ? std::string("No Text") : collapsed;
}

std::string NormalizeCueClipVisibleTextForClipName(std::string text) {
  text = Trimmed(std::move(text));
  if (text.empty()) {
    return "No Text";
  }

  ReplaceAll(text, "\r\n", " ");
  ReplaceAll(text, "\n", " ");
  ReplaceAll(text, "\r", " ");
  ReplaceAll(text, "\\n", " ");

  std::string normalized;
  normalized.reserve(text.size());
  bool last_was_space = false;
  for (unsigned char raw_ch : text) {
    if (raw_ch < 0x20 || raw_ch == 0x7f || std::isspace(raw_ch) != 0) {
      if (!last_was_space) {
        normalized.push_back(' ');
        last_was_space = true;
      }
      continue;
    }

    normalized.push_back(static_cast<char>(raw_ch));
    last_was_space = false;
  }

  normalized = Trimmed(std::move(normalized));
  return normalized.empty() ? std::string("No Text") : normalized;
}

std::string TruncateUtf8ToByteLength(std::string value, std::size_t max_bytes) {
  if (value.size() <= max_bytes) {
    return value;
  }

  std::size_t index = 0;
  std::size_t last_valid_boundary = 0;
  while (index < value.size() && index < max_bytes) {
    const auto ch = static_cast<unsigned char>(value[index]);
    std::size_t sequence_length = 1;
    if ((ch & 0x80) == 0x00) {
      sequence_length = 1;
    } else if ((ch & 0xE0) == 0xC0) {
      sequence_length = 2;
    } else if ((ch & 0xF0) == 0xE0) {
      sequence_length = 3;
    } else if ((ch & 0xF8) == 0xF0) {
      sequence_length = 4;
    }

    if (index + sequence_length > max_bytes || index + sequence_length > value.size()) {
      break;
    }
    bool valid_continuation = true;
    for (std::size_t continuation_index = 1; continuation_index < sequence_length; continuation_index += 1) {
      const auto continuation = static_cast<unsigned char>(value[index + continuation_index]);
      if ((continuation & 0xC0) != 0x80) {
        valid_continuation = false;
        break;
      }
    }
    if (!valid_continuation) {
      sequence_length = 1;
    }

    index += sequence_length;
    last_valid_boundary = index;
  }
  return Trimmed(value.substr(0, last_valid_boundary));
}

constexpr std::size_t kProToolsRenameSelectedClipMaxNameBytes = 248;

std::string LimitRenameSelectedClipName(std::string name) {
  return TruncateUtf8ToByteLength(Trimmed(std::move(name)), kProToolsRenameSelectedClipMaxNameBytes);
}

struct ClipGroupCommentParts {
  std::string visible_comment;
  std::string metadata_tail;
};

std::string NormalizeReadableClipMetadataTail(std::string metadata_tail) {
  metadata_tail = Trimmed(std::move(metadata_tail));
  ReplaceAll(metadata_tail, "\r\n", " ");
  ReplaceAll(metadata_tail, "\n", " ");
  ReplaceAll(metadata_tail, "\r", " ");
  ReplaceAll(metadata_tail, "\\n", " ");
  metadata_tail = std::regex_replace(metadata_tail, std::regex(R"(\s+)"), " ");
  return Trimmed(std::move(metadata_tail));
}

ClipGroupCommentParts SplitClipGroupCommentMetadataTail(std::string comment) {
  comment = Trimmed(std::move(comment));
  if (comment.empty()) {
    return {"", ""};
  }

  const std::regex metadata_pattern(R"(\s*(\[OCM1-[0-9A-Fa-f]+\])\s*$)");
  std::smatch match;
  if (std::regex_search(comment, match, metadata_pattern)) {
    return {
        Trimmed(comment.substr(0, static_cast<std::size_t>(match.position(0)))),
        Trimmed(match[1].str())};
  }

  const std::regex readable_metadata_pattern(
      R"((^|\s+\|\s+|\s+-\s+)(R(?:eason)?\s*#?\s*\d{1,3}|Reason|Comment|Comments|Character|Char|Actor|ADR Code|Code):?\s*)",
      std::regex_constants::icase);
  if (std::regex_search(comment, match, readable_metadata_pattern)) {
    return {
        Trimmed(comment.substr(0, static_cast<std::size_t>(match.position(0)))),
        NormalizeReadableClipMetadataTail(comment.substr(static_cast<std::size_t>(match.position(0))))};
  }

  return {comment, ""};
}

std::string AppendClipMetadataTailPreservingLimit(std::string prefix, std::string metadata_tail) {
  prefix = Trimmed(std::move(prefix));
  metadata_tail = NormalizeReadableClipMetadataTail(std::move(metadata_tail));
  if (metadata_tail.empty()) {
    return LimitRenameSelectedClipName(std::move(prefix));
  }

  constexpr std::size_t separator_bytes = 1;
  if (prefix.empty()) {
    return LimitRenameSelectedClipName(std::move(metadata_tail));
  }

  if (prefix.size() + separator_bytes + metadata_tail.size() <= kProToolsRenameSelectedClipMaxNameBytes) {
    return prefix + " " + metadata_tail;
  }

  if (metadata_tail.size() + separator_bytes >= kProToolsRenameSelectedClipMaxNameBytes) {
    return LimitRenameSelectedClipName(std::move(metadata_tail));
  }

  const auto allowed_prefix_bytes = kProToolsRenameSelectedClipMaxNameBytes
      - metadata_tail.size()
      - separator_bytes;
  const auto limited_prefix = TruncateUtf8ToByteLength(std::move(prefix), allowed_prefix_bytes);
  if (limited_prefix.empty()) {
    return LimitRenameSelectedClipName(std::move(metadata_tail));
  }

  return Trimmed(limited_prefix + " " + metadata_tail);
}

ParsedStoredMarkerComments ParseStoredMarkerComments(std::string comments) {
  ReplaceAll(comments, "\r\n", "\n");
  const std::regex pattern(R"(^\s*\[([^\]\r\n]+)\](?:\s*\n\s*|\s+)?([\s\S]*)$)");
  std::smatch match;
  if (!std::regex_match(comments, match, pattern)) {
    return {"", comments};
  }

  const auto character_name = Trimmed(match[1].str());
  if (character_name.empty()) {
    return {"", comments};
  }
  if (character_name.rfind("OCM1-", 0) == 0) {
    return {"", comments};
  }

  auto comment_text = match[2].str();
  while (!comment_text.empty() && std::isspace(static_cast<unsigned char>(comment_text.front())) != 0) {
    comment_text.erase(comment_text.begin());
  }
  return {character_name, comment_text};
}

std::string BuildClipGroupName(std::string_view cue_name, std::string_view comment_text) {
  const auto trimmed_cue_name = Trimmed(std::string(cue_name));
  const auto comment_parts = SplitClipGroupCommentMetadataTail(std::string(comment_text));
  if (comment_parts.visible_comment.empty()) {
    if (!comment_parts.metadata_tail.empty()) {
      return AppendClipMetadataTailPreservingLimit("", comment_parts.metadata_tail);
    }
    return LimitRenameSelectedClipName(NormalizeCueClipVisibleTextForClipName(trimmed_cue_name));
  }

  return AppendClipMetadataTailPreservingLimit(
      NormalizeCueClipVisibleTextForClipName(comment_parts.visible_comment),
      comment_parts.metadata_tail);
}

std::string BuildMissingCueTrackInstructionMessage(std::string_view track_pool_label) {
  std::ostringstream message;
  message << "No matching " << track_pool_label
          << " found. Create one, show it in Edit, and try again.";
  return message.str();
}

std::string BuildClipGroupRenameSelectionFailureMessage() {
  return "Clip group was created but not left selected. Keep the target track visible and Edit focused, then try again.";
}

std::string BuildClipGroupNoEditSelectionFailureMessage() {
  return "Cue has no usable time range for clip grouping. Set an out point after the in point and try again.";
}

bool IsClipRenameSelectionFailure(std::string_view message) {
  const auto lower = LowercaseAscii(std::string(message));
  return lower.find("could not find a selected pro tools clip") != std::string::npos
      || lower.find("no clip is selected") != std::string::npos
      || lower.find("not left selected") != std::string::npos;
}

std::optional<MemoryLocationInfo> ResolveCurrentMarkerForTimelineSelection(
    const TimelineSelection& selection,
    const std::vector<MemoryLocationInfo>& memory_locations) {
  const auto target_start_time = [&]() -> std::string {
    const auto play_start = NormalizeComparableTimecode(selection.play_start_marker_time);
    if (!play_start.empty()) {
      return play_start;
    }

    const auto in_time = NormalizeComparableTimecode(selection.in_time);
    if (!in_time.empty()) {
      return in_time;
    }

    const auto out_time = NormalizeComparableTimecode(selection.out_time);
    if (!out_time.empty()) {
      return out_time;
    }

    return "";
  }();

  if (target_start_time.empty()) {
    return std::nullopt;
  }

  std::vector<MemoryLocationInfo> candidates;
  for (const auto& memory_location : memory_locations) {
    if (memory_location.time_properties != "TP_Marker") {
      continue;
    }
    if (NormalizeComparableTimecode(memory_location.start_time) != target_start_time) {
      continue;
    }
    candidates.push_back(memory_location);
  }

  if (candidates.empty()) {
    return std::nullopt;
  }

  std::stable_sort(candidates.begin(), candidates.end(), [](const MemoryLocationInfo& left, const MemoryLocationInfo& right) {
    const bool left_is_streamer = LowercaseAscii(Trimmed(left.name)) == "streamer";
    const bool right_is_streamer = LowercaseAscii(Trimmed(right.name)) == "streamer";
    if (left_is_streamer != right_is_streamer) {
      return !left_is_streamer;
    }

    const bool left_has_comments = !Trimmed(left.comments).empty();
    const bool right_has_comments = !Trimmed(right.comments).empty();
    if (left_has_comments != right_has_comments) {
      return left_has_comments;
    }

    return left.number > right.number;
  });

  return candidates.front();
}

std::string BuildClipRenameFromMarkerJson(const MemoryLocationInfo& marker, std::string_view new_name) {
  std::ostringstream json;
  json << '{'
       << "\"marker_number\":" << marker.number << ','
       << "\"marker_name\":\"" << JsonEscape(marker.name) << "\","
       << "\"marker_start_time\":\"" << JsonEscape(marker.start_time) << "\","
       << "\"new_name\":\"" << JsonEscape(new_name) << "\""
       << '}';
  return json.str();
}

std::string BuildClipRenameJson(std::string_view previous_name, std::string_view new_name) {
  std::ostringstream json;
  json << '{'
       << "\"previous_name\":\"" << JsonEscape(previous_name) << "\","
       << "\"clip_name\":\"" << JsonEscape(new_name) << "\""
       << '}';
  return json.str();
}

std::string BasenameFromPath(std::string_view full_path) {
  const auto separator_index = full_path.find_last_of("/:\\");
  if (separator_index == std::string_view::npos) {
    return std::string(full_path);
  }
  return std::string(full_path.substr(separator_index + 1));
}

std::string BuildSelectedClipFileJson(const FileLocationEntry& file_location,
                                      std::string_view clip_name,
                                      const std::optional<std::string>& clip_id,
                                      const std::optional<std::string>& clip_start_time,
                                      std::optional<double> src_start_seconds,
                                      int sample_rate_hz,
                                      std::optional<double> session_fps,
                                      std::optional<double> source_start_seconds = std::nullopt,
                                      std::optional<double> source_end_seconds = std::nullopt) {
  std::ostringstream json;
  json << '{'
       << "\"file_path\":\"" << JsonEscape(file_location.path) << "\","
       << "\"file_name\":\"" << JsonEscape(BasenameFromPath(file_location.path)) << "\","
       << "\"file_id\":\"" << JsonEscape(file_location.file_id) << "\","
       << "\"clip_name\":\"" << JsonEscape(clip_name) << "\","
       << "\"is_online\":" << (file_location.is_online ? "true" : "false");

  if (clip_id && !Trimmed(*clip_id).empty()) {
    json << ",\"clip_id\":\"" << JsonEscape(*clip_id) << "\"";
  }
  if (clip_start_time && !Trimmed(*clip_start_time).empty()) {
    json << ",\"clip_start_time\":\"" << JsonEscape(*clip_start_time) << "\"";
  }
  if (src_start_seconds.has_value()) {
    std::ostringstream value;
    value << std::fixed << std::setprecision(9) << *src_start_seconds;
    json << ",\"src_start_seconds\":" << value.str();
  }
  if (sample_rate_hz > 0) {
    json << ",\"sample_rate_hz\":" << sample_rate_hz;
  }
  if (session_fps.has_value() && std::isfinite(*session_fps) && *session_fps > 0) {
    std::ostringstream value;
    value << std::fixed << std::setprecision(6) << *session_fps;
    json << ",\"session_fps\":" << value.str();
  }
  if (source_start_seconds.has_value()) {
    std::ostringstream value;
    value << std::fixed << std::setprecision(9) << *source_start_seconds;
    json << ",\"source_start_seconds\":" << value.str();
  }
  if (source_end_seconds.has_value()) {
    std::ostringstream value;
    value << std::fixed << std::setprecision(9) << *source_end_seconds;
    json << ",\"source_end_seconds\":" << value.str();
  }
  json << '}';
  return json.str();
}

std::string BuildSelectedClipFileJson(const FileLocationEntry& file_location, std::string_view clip_name) {
  return BuildSelectedClipFileJson(
      file_location,
      clip_name,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      0,
      std::nullopt);
}

std::string BuildSelectedClipSegmentsJson(std::string_view track_name,
                                          const std::vector<SelectedClipSegmentInfo>& segments,
                                          int sample_rate_hz,
                                          std::optional<double> session_fps) {
  std::ostringstream json;
  json << '{'
       << "\"track_name\":\"" << JsonEscape(track_name) << "\"";
  if (sample_rate_hz > 0) {
    json << ",\"sample_rate_hz\":" << sample_rate_hz;
  }
  if (session_fps.has_value() && std::isfinite(*session_fps) && *session_fps > 0) {
    std::ostringstream value;
    value << std::fixed << std::setprecision(6) << *session_fps;
    json << ",\"session_fps\":" << value.str();
  }
  json << ",\"segments\":[";
  for (std::size_t index = 0; index < segments.size(); index += 1) {
    if (index > 0) {
      json << ',';
    }
    const auto& segment = segments[index];
    json << '{'
         << "\"file_path\":\"" << JsonEscape(segment.file_location.path) << "\","
         << "\"file_name\":\"" << JsonEscape(BasenameFromPath(segment.file_location.path)) << "\","
         << "\"file_id\":\"" << JsonEscape(segment.file_location.file_id) << "\","
         << "\"clip_name\":\"" << JsonEscape(segment.clip_name) << "\","
         << "\"is_online\":" << (segment.file_location.is_online ? "true" : "false");
    if (!Trimmed(segment.clip_id).empty()) {
      json << ",\"clip_id\":\"" << JsonEscape(segment.clip_id) << "\"";
    }
    if (!Trimmed(segment.resolution_source).empty()) {
      json << ",\"resolution_source\":\"" << JsonEscape(segment.resolution_source) << "\"";
    }
    if (!Trimmed(segment.clip_start_time).empty()) {
      json << ",\"clip_start_time\":\"" << JsonEscape(segment.clip_start_time) << "\"";
    }
    if (!Trimmed(segment.segment_start_time).empty()) {
      json << ",\"segment_start_time\":\"" << JsonEscape(segment.segment_start_time) << "\"";
    }
    if (!Trimmed(segment.segment_end_time).empty()) {
      json << ",\"segment_end_time\":\"" << JsonEscape(segment.segment_end_time) << "\"";
    }
    if (segment.src_start_seconds.has_value()) {
      std::ostringstream value;
      value << std::fixed << std::setprecision(9) << *segment.src_start_seconds;
      json << ",\"src_start_seconds\":" << value.str();
    }
    if (segment.source_start_seconds.has_value()) {
      std::ostringstream value;
      value << std::fixed << std::setprecision(9) << *segment.source_start_seconds;
      json << ",\"source_start_seconds\":" << value.str();
    }
    if (segment.source_end_seconds.has_value()) {
      std::ostringstream value;
      value << std::fixed << std::setprecision(9) << *segment.source_end_seconds;
      json << ",\"source_end_seconds\":" << value.str();
    }
    json << '}';
  }
  json << "]}";
  return json.str();
}

std::string StripFilenameExtension(std::string_view file_name) {
  const auto dot_index = file_name.find_last_of('.');
  if (dot_index == std::string_view::npos || dot_index == 0) {
    return std::string(file_name);
  }
  return std::string(file_name.substr(0, dot_index));
}

std::string NormalizeSelectedClipName(std::string current_name) {
  current_name = StripFilenameExtension(BasenameFromPath(current_name));
  current_name = std::regex_replace(
      current_name,
      std::regex(R"(\.(L|R|C|Ls|Rs)$)", std::regex::icase),
      "");
  return Trimmed(current_name);
}

std::string StripTrailingClipTakeSuffix(std::string normalized_name) {
  normalized_name = NormalizeSelectedClipName(std::move(normalized_name));
  normalized_name = std::regex_replace(normalized_name, std::regex(R"(-\d+$)"), "");
  return Trimmed(normalized_name);
}

enum class ClipRenameBehaviorMode {
  kReplaceSuffix,
  kAppend,
  kPrepend,
  kReplace
};

ClipRenameBehaviorMode ParseClipRenameBehaviorMode(std::string_view raw_mode) {
  const auto normalized_mode = LowercaseAscii(Trimmed(std::string(raw_mode)));
  if (normalized_mode == "append") {
    return ClipRenameBehaviorMode::kAppend;
  }
  if (normalized_mode == "prepend") {
    return ClipRenameBehaviorMode::kPrepend;
  }
  if (normalized_mode == "replace") {
    return ClipRenameBehaviorMode::kReplace;
  }
  return ClipRenameBehaviorMode::kReplaceSuffix;
}

bool DoNormalizedClipNamesMatch(std::string_view left_name, std::string_view right_name) {
  const auto left = LowercaseAscii(NormalizeSelectedClipName(std::string(left_name)));
  const auto right = LowercaseAscii(NormalizeSelectedClipName(std::string(right_name)));
  if (left.empty() || right.empty()) {
    return false;
  }
  if (left == right) {
    return true;
  }

  const auto left_without_take = LowercaseAscii(StripTrailingClipTakeSuffix(left));
  const auto right_without_take = LowercaseAscii(StripTrailingClipTakeSuffix(right));
  return !left_without_take.empty() && left_without_take == right_without_take;
}

std::string BuildClipRenameTargetName(std::string current_name,
                                      std::string replacement_suffix,
                                      ClipRenameBehaviorMode mode,
                                      std::string_view separator) {
  current_name = NormalizeSelectedClipName(std::move(current_name));
  replacement_suffix = SanitizeMarkerCommentForClipName(std::move(replacement_suffix));
  const std::string normalized_separator = Trimmed(std::string(separator));

  if (mode == ClipRenameBehaviorMode::kReplace) {
    return replacement_suffix;
  }

  if (mode == ClipRenameBehaviorMode::kAppend) {
    if (current_name.empty()) {
      return replacement_suffix;
    }
    return normalized_separator.empty()
        ? current_name + replacement_suffix
        : current_name + normalized_separator + replacement_suffix;
  }

  if (mode == ClipRenameBehaviorMode::kPrepend) {
    if (current_name.empty()) {
      return replacement_suffix;
    }
    return normalized_separator.empty()
        ? replacement_suffix + current_name
        : replacement_suffix + normalized_separator + current_name;
  }

  const auto hyphen_index = current_name.find('-');
  if (hyphen_index == std::string::npos) {
    if (current_name.empty()) {
      return replacement_suffix;
    }
    return current_name + "-" + replacement_suffix;
  }

  return current_name.substr(0, hyphen_index + 1) + replacement_suffix;
}

std::string ChooseMoreSpecificClipName(std::string left, std::string right) {
  left = NormalizeSelectedClipName(std::move(left));
  right = NormalizeSelectedClipName(std::move(right));

  if (left.empty()) {
    return right;
  }
  if (right.empty()) {
    return left;
  }

  const auto extends_with_delimiter = [](std::string_view candidate, std::string_view base) {
    return candidate.size() > base.size() &&
        candidate.compare(0, base.size(), base) == 0 &&
        (candidate[base.size()] == '-' || candidate[base.size()] == '_');
  };

  if (extends_with_delimiter(left, right)) {
    return left;
  }
  if (extends_with_delimiter(right, left)) {
    return right;
  }

  const bool left_has_hyphen = left.find('-') != std::string::npos;
  const bool right_has_hyphen = right.find('-') != std::string::npos;
  if (left_has_hyphen != right_has_hyphen) {
    return left_has_hyphen ? left : right;
  }

  return left.size() >= right.size() ? left : right;
}

void AppendUniqueClipName(std::vector<std::string>& names, std::string candidate) {
  candidate = NormalizeSelectedClipName(std::move(candidate));
  if (candidate.empty()) {
    return;
  }

  const auto folded_candidate = LowercaseAscii(candidate);
  for (const auto& existing : names) {
    if (LowercaseAscii(existing) == folded_candidate) {
      return;
    }
  }
  names.push_back(std::move(candidate));
}

std::optional<std::string> ResolveBestClipNameForFileId(const std::vector<ClipInfo>& clips, const std::string& file_id) {
  if (file_id.empty()) {
    return std::nullopt;
  }

  std::vector<const ClipInfo*> matching_clips;
  for (const auto& clip : clips) {
    if (clip.file_id == file_id) {
      matching_clips.push_back(&clip);
    }
  }
  if (matching_clips.empty()) {
    return std::nullopt;
  }

  std::stable_sort(matching_clips.begin(), matching_clips.end(), [](const ClipInfo* left, const ClipInfo* right) {
    const auto left_name = !Trimmed(left->clip_full_name).empty() ? left->clip_full_name : left->clip_root_name;
    const auto right_name = !Trimmed(right->clip_full_name).empty() ? right->clip_full_name : right->clip_root_name;
    const bool left_has_suffix = left_name.find('-') != std::string::npos;
    const bool right_has_suffix = right_name.find('-') != std::string::npos;
    if (left_has_suffix != right_has_suffix) {
      return left_has_suffix;
    }
    return left_name.size() > right_name.size();
  });

  const auto chosen_name = !Trimmed(matching_clips.front()->clip_full_name).empty()
      ? matching_clips.front()->clip_full_name
      : matching_clips.front()->clip_root_name;
  const auto normalized = NormalizeSelectedClipName(chosen_name);
  if (normalized.empty()) {
    return std::nullopt;
  }
  return normalized;
}

std::vector<std::string> ResolveSelectedClipCurrentNames(
    const std::vector<FileLocationEntry>& clips_list_locations,
    const std::vector<FileLocationEntry>& timeline_locations,
    const std::vector<ClipInfo>& clips) {
  std::vector<std::string> names;

  for (const auto& entry : clips_list_locations) {
    const auto matched_name = ResolveBestClipNameForFileId(clips, entry.file_id).value_or("");
    const auto chosen_name = ChooseMoreSpecificClipName(matched_name, entry.path);
    if (!chosen_name.empty()) {
      AppendUniqueClipName(names, chosen_name);
    }
  }

  if (!names.empty()) {
    return names;
  }

  for (const auto& entry : timeline_locations) {
    const auto matched_name = ResolveBestClipNameForFileId(clips, entry.file_id).value_or("");
    const auto chosen_name = ChooseMoreSpecificClipName(matched_name, entry.path);
    if (!chosen_name.empty()) {
      AppendUniqueClipName(names, chosen_name);
    }
  }

  return names;
}

std::optional<FileLocationEntry> ResolvePreferredSelectedClipLocation(
    const std::vector<FileLocationEntry>& clips_list_locations,
    const std::vector<FileLocationEntry>& timeline_locations) {
  const auto choose_best_location = [](const std::vector<FileLocationEntry>& locations) -> std::optional<FileLocationEntry> {
    const FileLocationEntry* best_location = nullptr;
    int best_score = std::numeric_limits<int>::min();

    for (const auto& entry : locations) {
      if (Trimmed(entry.path).empty()) {
        continue;
      }

      int score = 0;
      if (entry.is_online) {
        score += 100;
      }
      if (!Trimmed(entry.file_id).empty()) {
        score += 10;
      }
      if (!NormalizeSelectedClipName(entry.path).empty()) {
        score += 1;
      }

      if (!best_location || score > best_score) {
        best_location = &entry;
        best_score = score;
      }
    }

    if (!best_location) {
      return std::nullopt;
    }
    return *best_location;
  };

  if (const auto preferred_clip_list_location = choose_best_location(clips_list_locations)) {
    return preferred_clip_list_location;
  }

  return choose_best_location(timeline_locations);
}

std::string ResolveSelectedClipDisplayName(const FileLocationEntry& file_location,
                                           const std::vector<ClipInfo>& clips) {
  const auto matched_name = ResolveBestClipNameForFileId(clips, file_location.file_id).value_or("");
  return ChooseMoreSpecificClipName(matched_name, file_location.path);
}

std::string ResolveSelectedClipCurrentName(
    const std::vector<FileLocationEntry>& clips_list_locations,
    const std::vector<FileLocationEntry>& timeline_locations,
    const std::vector<ClipInfo>& clips) {
  std::vector<ClipInfo> matching_clips;
  const auto collect_matching_clips = [&](const std::vector<FileLocationEntry>& locations) {
    for (const auto& entry : locations) {
      if (entry.file_id.empty()) {
        continue;
      }
      for (const auto& clip : clips) {
        if (clip.file_id == entry.file_id) {
          matching_clips.push_back(clip);
        }
      }
    }
  };

  collect_matching_clips(clips_list_locations);
  collect_matching_clips(timeline_locations);

  if (!matching_clips.empty()) {
    std::stable_sort(matching_clips.begin(), matching_clips.end(), [](const ClipInfo& left, const ClipInfo& right) {
      const auto left_name = !Trimmed(left.clip_full_name).empty() ? left.clip_full_name : left.clip_root_name;
      const auto right_name = !Trimmed(right.clip_full_name).empty() ? right.clip_full_name : right.clip_root_name;
      const bool left_has_suffix = left_name.find('-') != std::string::npos;
      const bool right_has_suffix = right_name.find('-') != std::string::npos;
      if (left_has_suffix != right_has_suffix) {
        return left_has_suffix;
      }
      return left_name.size() > right_name.size();
    });

    const auto chosen_name = !Trimmed(matching_clips.front().clip_full_name).empty()
        ? matching_clips.front().clip_full_name
        : matching_clips.front().clip_root_name;
    const auto normalized = NormalizeSelectedClipName(chosen_name);
    if (!normalized.empty()) {
      return normalized;
    }
  }

  const auto choose_best_path = [](const std::vector<FileLocationEntry>& locations) -> std::string {
    std::string fallback;
    for (const auto& entry : locations) {
      const auto normalized = NormalizeSelectedClipName(entry.path);
      if (normalized.empty()) {
        continue;
      }
      if (normalized.find('-') != std::string::npos) {
        return normalized;
      }
      if (fallback.empty()) {
        fallback = normalized;
      }
    }
    return fallback;
  };

  const auto clips_list_name = choose_best_path(clips_list_locations);
  if (!clips_list_name.empty()) {
    return clips_list_name;
  }

  const auto timeline_name = choose_best_path(timeline_locations);
  if (!timeline_name.empty()) {
    return timeline_name;
  }

  return "";
}

std::optional<std::string> ResolveClipNameFromClipId(const std::vector<ClipInfo>& clips, const std::string& clip_id) {
  if (clip_id.empty()) {
    return std::nullopt;
  }

  for (const auto& clip : clips) {
    if (clip.clip_id != clip_id) {
      continue;
    }
    const auto chosen_name = !Trimmed(clip.clip_full_name).empty() ? clip.clip_full_name : clip.clip_root_name;
    const auto normalized = NormalizeSelectedClipName(chosen_name);
    if (!normalized.empty()) {
      return normalized;
    }
  }

  return std::nullopt;
}

const ClipInfo* ResolveClipInfoById(const std::vector<ClipInfo>& clips, std::string_view clip_id) {
  if (Trimmed(clip_id).empty()) {
    return nullptr;
  }

  for (const auto& clip : clips) {
    if (clip.clip_id == clip_id) {
      return &clip;
    }
  }

  return nullptr;
}

double MediaTimePositionToSeconds(long long position,
                                  const std::string& time_type,
                                  int sample_rate_hz,
                                  const TimeCodeRateInfo& rate_info);

const ClipInfo* ResolveClipInfoByNormalizedName(
    const std::vector<ClipInfo>& clips,
    std::string_view normalized_name,
    const std::unordered_map<std::string, FileLocationEntry>& file_locations_by_id,
    const std::unordered_set<std::string>& preferred_file_ids) {
  const std::string target = LowercaseAscii(Trimmed(std::string(normalized_name)));
  if (target.empty()) {
    return nullptr;
  }

  const ClipInfo* best = nullptr;
  int best_score = std::numeric_limits<int>::min();
  for (const auto& clip : clips) {
    const auto primary_name = !Trimmed(clip.clip_full_name).empty() ? clip.clip_full_name : clip.clip_root_name;
    const auto normalized_primary = LowercaseAscii(NormalizeSelectedClipName(primary_name));
    if (!DoNormalizedClipNamesMatch(normalized_primary, target)) {
      continue;
    }

    int score = 0;
    if (normalized_primary == target) {
      score += 200;
    } else {
      score += 100;
    }
    if (!Trimmed(clip.file_id).empty()) {
      score += 10;
    }
    if (!Trimmed(clip.clip_id).empty()) {
      score += 5;
    }
    if (clip.src_start_position && clip.src_start_time_type) {
      score += 5;
    }
    if (!Trimmed(clip.file_path).empty()) {
      score += 25;
    }
    if (file_locations_by_id.find(clip.file_id) != file_locations_by_id.end()) {
      score += 50;
    }
    if (preferred_file_ids.find(clip.file_id) != preferred_file_ids.end()) {
      score += 100;
    }

    if (!best || score > best_score) {
      best = &clip;
      best_score = score;
    }
  }

  return best;
}

std::optional<FileLocationEntry> ResolveFileLocationByNormalizedName(
    const std::vector<FileLocationEntry>& locations,
    std::string_view normalized_name) {
  const std::string target = LowercaseAscii(NormalizeSelectedClipName(std::string(normalized_name)));
  if (target.empty()) {
    return std::nullopt;
  }

  const FileLocationEntry* best = nullptr;
  int best_score = std::numeric_limits<int>::min();
  for (const auto& entry : locations) {
    const auto normalized_path = LowercaseAscii(NormalizeSelectedClipName(entry.path));
    if (!DoNormalizedClipNamesMatch(normalized_path, target)) {
      continue;
    }

    int score = 0;
    if (normalized_path == target) {
      score += 200;
    } else {
      score += 100;
    }
    if (entry.is_online) {
      score += 25;
    }
    if (!Trimmed(entry.file_id).empty()) {
      score += 10;
    }

    if (!best || score > best_score) {
      best = &entry;
      best_score = score;
    }
  }

  if (!best) {
    return std::nullopt;
  }
  return *best;
}

bool SelectedClipSegmentHasSourceWindow(const SelectedClipSegmentInfo& segment) {
  return segment.source_start_seconds.has_value()
      && segment.source_end_seconds.has_value()
      && std::isfinite(*segment.source_start_seconds)
      && std::isfinite(*segment.source_end_seconds)
      && *segment.source_end_seconds > *segment.source_start_seconds;
}

int CountSelectedClipSegmentsWithSourceWindows(const std::vector<SelectedClipSegmentInfo>& segments) {
  int count = 0;
  for (const auto& segment : segments) {
    if (SelectedClipSegmentHasSourceWindow(segment)) {
      count += 1;
    }
  }
  return count;
}

bool ShouldPreferSessionExportSegments(
    const std::vector<SelectedClipSegmentInfo>& current,
    const std::vector<SelectedClipSegmentInfo>& export_segments) {
  if (export_segments.empty()) {
    return false;
  }
  if (current.empty()) {
    return true;
  }

  const int current_source_count = CountSelectedClipSegmentsWithSourceWindows(current);
  const int export_source_count = CountSelectedClipSegmentsWithSourceWindows(export_segments);
  if (export_source_count > current_source_count) {
    return true;
  }
  if (export_source_count > 0 && current_source_count == 0) {
    return true;
  }
  if (export_segments.size() > current.size()) {
    return true;
  }
  if (current.size() == 1 && export_segments.size() == 1) {
    const bool current_is_export = Trimmed(current.front().resolution_source) == "session_export";
    const bool export_is_export = Trimmed(export_segments.front().resolution_source) == "session_export";
    if (!current_is_export && export_is_export) {
      return true;
    }
  }
  return false;
}

void MaybeAssignPreferredSessionExportSegments(
    std::vector<SelectedClipSegmentInfo>& segments,
    std::vector<SelectedClipSegmentInfo>&& export_segments) {
  if (ShouldPreferSessionExportSegments(segments, export_segments)) {
    segments = std::move(export_segments);
  }
}

std::vector<SelectedClipSegmentInfo> BuildSelectedClipSegmentsFromSessionExport(
    std::string_view session_export_text,
    std::string_view track_name,
    long long range_start_sf,
    long long range_end_sf,
    const std::vector<std::string>& selected_clip_names,
    const std::vector<FileLocationEntry>& preferred_locations,
    const std::vector<ClipInfo>& clips,
    const std::unordered_map<std::string, FileLocationEntry>& file_locations_by_id,
    int sample_rate_hz,
    std::optional<double> session_fps,
    const TimeCodeRateInfo& rate_info) {
  const auto placements = FindTrackClipPlacementsFromSessionExport(
      session_export_text,
      track_name,
      range_start_sf,
      range_end_sf,
      selected_clip_names,
      rate_info);
  if (placements.empty()) {
    return {};
  }

  std::unordered_set<std::string> preferred_file_ids;
  preferred_file_ids.reserve(preferred_locations.size());
  std::unordered_map<std::string, FileLocationEntry> preferred_locations_by_name;
  const auto remember_preferred_location = [&](const std::string& normalized_name, const FileLocationEntry& entry) {
    if (normalized_name.empty() || Trimmed(entry.path).empty()) {
      return;
    }
    const int score = (entry.is_online ? 100 : 0) + (Trimmed(entry.file_id).empty() ? 0 : 10);
    const auto existing = preferred_locations_by_name.find(normalized_name);
    if (existing == preferred_locations_by_name.end()) {
      preferred_locations_by_name.emplace(normalized_name, entry);
      return;
    }
    const auto& current = existing->second;
    const int current_score = (current.is_online ? 100 : 0) + (Trimmed(current.file_id).empty() ? 0 : 10);
    if (score > current_score) {
      existing->second = entry;
    }
  };

  for (const auto& entry : preferred_locations) {
    if (!Trimmed(entry.file_id).empty()) {
      preferred_file_ids.insert(entry.file_id);
    }
    const auto matched_name = ResolveBestClipNameForFileId(clips, entry.file_id).value_or("");
    const auto chosen_name = ChooseMoreSpecificClipName(matched_name, entry.path);
    const auto normalized_name = LowercaseAscii(NormalizeSelectedClipName(chosen_name.empty() ? entry.path : chosen_name));
    remember_preferred_location(normalized_name, entry);
  }

  const double session_seconds_per_subframe = session_fps.has_value() && *session_fps > 0
      ? 1.0 / (*session_fps * 100.0)
      : 0.0;

  std::vector<SelectedClipSegmentInfo> segments;
  segments.reserve(placements.size());
  for (const auto& placement : placements) {
    const auto placement_start_sf = std::min(placement.start_subframes, placement.end_subframes);
    const auto placement_end_sf = std::max(placement.start_subframes, placement.end_subframes);
    const auto overlap_start_sf = std::max(range_start_sf, placement_start_sf);
    const auto overlap_end_sf = std::min(range_end_sf, placement_end_sf);
    if (overlap_end_sf <= overlap_start_sf) {
      continue;
    }

    const auto normalized_name = LowercaseAscii(NormalizeSelectedClipName(placement.clip_name));
    const auto* clip_info = ResolveClipInfoByNormalizedName(clips, normalized_name, file_locations_by_id, preferred_file_ids);

    FileLocationEntry resolved_location;
    const auto preferred_it = preferred_locations_by_name.find(normalized_name);
    if (preferred_it != preferred_locations_by_name.end()) {
      resolved_location = preferred_it->second;
    } else if (clip_info) {
      const auto location_it = file_locations_by_id.find(clip_info->file_id);
      if (location_it != file_locations_by_id.end()) {
        resolved_location = location_it->second;
      } else if (!Trimmed(clip_info->file_path).empty()) {
        resolved_location.path = clip_info->file_path;
        resolved_location.file_id = clip_info->file_id;
        resolved_location.is_online = true;
      }
    }
    if (Trimmed(resolved_location.path).empty()) {
      continue;
    }

    std::optional<double> src_start_seconds;
    if (clip_info && clip_info->src_start_position && clip_info->src_start_time_type) {
      src_start_seconds = MediaTimePositionToSeconds(
          *clip_info->src_start_position,
          *clip_info->src_start_time_type,
          sample_rate_hz,
          rate_info);
    }

    std::optional<double> source_start_seconds;
    std::optional<double> source_end_seconds;
    if (src_start_seconds.has_value() && session_seconds_per_subframe > 0) {
      source_start_seconds =
          *src_start_seconds + static_cast<double>(overlap_start_sf - placement_start_sf) * session_seconds_per_subframe;
      source_end_seconds =
          *source_start_seconds + static_cast<double>(overlap_end_sf - overlap_start_sf) * session_seconds_per_subframe;
    }

    segments.push_back({
        resolved_location,
        clip_info ? clip_info->clip_id : std::string(),
        placement.clip_name,
        "session_export",
        placement.start_time,
        SubframesToTimecodeString(overlap_start_sf, rate_info),
        SubframesToTimecodeString(overlap_end_sf, rate_info),
        src_start_seconds,
        source_start_seconds,
        source_end_seconds,
        overlap_start_sf,
    });
  }

  return segments;
}

std::string SelectTimelineReferenceTime(const TimelineSelection& selection) {
  const auto in_time = NormalizeComparableTimecode(selection.in_time);
  if (!in_time.empty()) {
    return in_time;
  }

  const auto play_start = NormalizeComparableTimecode(selection.play_start_marker_time);
  if (!play_start.empty()) {
    return play_start;
  }

  return NormalizeComparableTimecode(selection.out_time);
}

std::optional<std::string> ResolveTimelineClipNameFromSelection(
    const TimelineSelection& selection,
    const std::vector<PlaylistElementInfo>& elements,
    const std::vector<ClipInfo>& clips,
    const TimeCodeRateInfo& rate_info) {
  const auto start_reference = SelectTimelineReferenceTime(selection);
  if (start_reference.empty()) {
    return std::nullopt;
  }

  const auto end_reference = [&]() {
    const auto normalized_out = NormalizeComparableTimecode(selection.out_time);
    if (!normalized_out.empty()) {
      return normalized_out;
    }
    return start_reference;
  }();

  const auto selection_start_subframes = TimecodeStringToSubframes(start_reference, rate_info);
  const auto selection_end_subframes = std::max(
      selection_start_subframes,
      TimecodeStringToSubframes(end_reference, rate_info));

  for (const auto& element : elements) {
    const auto play_time = !NormalizeComparableTimecode(element.play_time).empty()
        ? NormalizeComparableTimecode(element.play_time)
        : NormalizeComparableTimecode(element.start_time);
    const auto stop_time = !NormalizeComparableTimecode(element.stop_time).empty()
        ? NormalizeComparableTimecode(element.stop_time)
        : NormalizeComparableTimecode(element.end_time);
    if (play_time.empty() || stop_time.empty()) {
      continue;
    }

    const auto element_start_subframes = TimecodeStringToSubframes(play_time, rate_info);
    const auto element_stop_subframes = TimecodeStringToSubframes(stop_time, rate_info);
    if (selection_end_subframes < element_start_subframes || selection_start_subframes > element_stop_subframes) {
      continue;
    }

    for (const auto& channel_clip : element.channel_clips) {
      if (channel_clip.is_null || channel_clip.clip_id.empty()) {
        continue;
      }
      if (const auto name = ResolveClipNameFromClipId(clips, channel_clip.clip_id)) {
        return name;
      }
    }
  }

  return std::nullopt;
}

std::string NormalizeComparableMarkerEndTime(std::string_view start_time, std::string_view end_time) {
  const auto normalized_start = NormalizeComparableTimecode(start_time);
  const auto normalized_end = NormalizeComparableTimecode(end_time);
  if (normalized_end.empty() || normalized_end == normalized_start) {
    return std::string();
  }
  return normalized_end;
}

std::string NormalizeMarkerLocationValue(std::string_view location) {
  auto normalized = LowercaseAscii(Trimmed(std::string(location)));
  normalized.erase(
      std::remove_if(normalized.begin(), normalized.end(), [](unsigned char ch) {
        return ch == '_' || std::isspace(ch);
      }),
      normalized.end());
  if (normalized == "mlctrack" || normalized == "markerlocationtrack" || normalized == "track") {
    return "track";
  }
  if (
      normalized == "mlcmainruler"
      || normalized == "markerlocationmainruler"
      || normalized == "mainruler"
      || normalized == "global") {
    return "main-ruler";
  }
  if (
      normalized == "mlcnamedruler"
      || normalized == "markerlocationnamedruler"
      || normalized == "namedruler") {
    return "named-ruler";
  }
  return normalized;
}

bool MemoryLocationMatchesMarkerScope(const MemoryLocationInfo& memory_location, const Marker& marker) {
  const auto marker_location = NormalizeMarkerLocationValue(marker.location);
  if (!marker_location.empty()) {
    const auto memory_location_value = NormalizeMarkerLocationValue(memory_location.location);
    const bool blank_track_location_with_track_name =
        marker_location == "track"
        && memory_location_value.empty()
        && !Trimmed(memory_location.track_name).empty();
    if (memory_location_value != marker_location && !blank_track_location_with_track_name) {
      return false;
    }
  }

  const auto marker_track_name = Trimmed(marker.track_name);
  if (!marker_track_name.empty() && Trimmed(memory_location.track_name) != marker_track_name) {
    return false;
  }

  return true;
}

int ParseSessionSampleRateHz(std::string_view value) {
  auto normalized = Trimmed(value);
  if (normalized.empty()) {
    throw std::runtime_error("Empty session sample rate");
  }

  for (auto& ch : normalized) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }

  if (normalized.rfind("SR_", 0) == 0) {
    normalized.erase(0, 3);
  }

  std::string compact;
  compact.reserve(normalized.size());
  bool saw_decimal = false;
  for (std::size_t i = 0; i < normalized.size(); i += 1) {
    const char ch = normalized[i];
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      compact.push_back(ch);
      continue;
    }
    if ((ch == '.' || ch == '_') && !saw_decimal) {
      compact.push_back('.');
      saw_decimal = true;
      continue;
    }
    if (ch == 'K' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
      continue;
    }
  }

  if (compact.empty()) {
    throw std::runtime_error(std::string("Unsupported session sample rate: ") + std::string(value));
  }

  const long double parsed = std::stold(compact);
  if (parsed <= 0.0L) {
    throw std::runtime_error(std::string("Unsupported session sample rate: ") + std::string(value));
  }

  const long double sample_rate_hz = parsed < 1000.0L ? (parsed * 1000.0L) : parsed;
  return static_cast<int>(std::llround(sample_rate_hz));
}

long long SamplesToSubframes(long long sample_count,
                             int sample_rate_hz,
                             const TimeCodeRateInfo& rate_info) {
  if (sample_count <= 0 || sample_rate_hz <= 0) {
    return 0;
  }

  const long double numerator =
      static_cast<long double>(sample_count) *
      static_cast<long double>(rate_info.actual_fps_numerator) *
      100.0L;
  const long double denominator =
      static_cast<long double>(sample_rate_hz) *
      static_cast<long double>(rate_info.actual_fps_denominator);
  return static_cast<long long>(std::llround(numerator / denominator));
}

std::string BuildTracksJson(std::string_view key, const std::vector<TrackInfo>& tracks) {
  std::ostringstream json;
  json << '{' << '"' << key << "\":[";
  for (std::size_t i = 0; i < tracks.size(); i += 1) {
    if (i > 0) {
      json << ',';
    }
    json << '{'
         << "\"id\":";
    if (LooksLikeJsonNumber(tracks[i].id)) {
      json << tracks[i].id;
    } else {
      json << '"' << JsonEscape(tracks[i].id) << '"';
    }
    json << ','
         << "\"name\":\"" << JsonEscape(tracks[i].name) << "\","
         << "\"type\":\"" << JsonEscape(tracks[i].type) << "\"";
    if (!tracks[i].format.empty()) {
      json << ",\"format\":\"" << JsonEscape(tracks[i].format) << "\"";
    }
    if (tracks[i].is_record_enabled.has_value()) {
      json << ",\"is_record_enabled\":"
           << (tracks[i].is_record_enabled.value() ? "true" : "false");
    }
    if (tracks[i].is_record_safe_enabled.has_value()) {
      json << ",\"is_record_enabled_safe\":"
           << (tracks[i].is_record_safe_enabled.value() ? "true" : "false");
    }
    if (tracks[i].is_muted.has_value()) {
      json << ",\"is_muted\":"
           << (tracks[i].is_muted.value() ? "true" : "false");
    }
    if (tracks[i].is_selected_state.has_value() && !Trimmed(*tracks[i].is_selected_state).empty()) {
      json << ",\"is_selected_state\":\"" << JsonEscape(*tracks[i].is_selected_state) << "\"";
    }
    json << '}';
  }
  json << "]}";
  return json.str();
}

std::string BuildMarkersJson(std::string_view key, const std::vector<MemoryLocationInfo>& markers) {
  std::ostringstream json;
  json << '{' << '"' << key << "\":[";
  for (std::size_t i = 0; i < markers.size(); i += 1) {
    if (i > 0) {
      json << ',';
    }
    json << '{'
         << "\"number\":" << markers[i].number << ','
         << "\"name\":\"" << JsonEscape(markers[i].name) << "\","
         << "\"start_time\":\"" << JsonEscape(markers[i].start_time) << "\","
         << "\"end_time\":\"" << JsonEscape(markers[i].end_time) << "\","
         << "\"comments\":\"" << JsonEscape(markers[i].comments) << "\"";
    if (!Trimmed(markers[i].location).empty()) {
      json << ",\"location\":\"" << JsonEscape(markers[i].location) << "\"";
    }
    if (!Trimmed(markers[i].track_name).empty()) {
      json << ",\"track_name\":\"" << JsonEscape(markers[i].track_name) << "\"";
    }
    json << '}';
  }
  json << "]}";
  return json.str();
}

std::string BuildStringValueJson(std::string_view key, std::string_view value) {
  std::ostringstream json;
  json << '{'
       << '"' << key << "\":\"" << JsonEscape(value) << "\""
       << '}';
  return json.str();
}

std::string BuildTransportStatusJson(const TransportStatusInfo& status) {
  std::ostringstream json;
  json << '{'
       << "\"current_setting\":\"" << JsonEscape(status.current_setting) << "\","
       << "\"is_transport_armed\":" << (status.is_transport_armed ? "true" : "false")
       << '}';
  return json.str();
}

std::string BuildTransportArmedJson(bool is_transport_armed) {
  std::ostringstream json;
  json << '{'
       << "\"is_transport_armed\":" << (is_transport_armed ? "true" : "false")
       << '}';
  return json.str();
}

std::string BuildTimelineSelectionJson(const TimelineSelection& selection,
                                       std::optional<double> session_fps = std::nullopt,
                                       std::optional<double> session_feet_frames_fps = std::nullopt) {
  const bool has_selection =
      !selection.in_time.empty() &&
      !selection.out_time.empty() &&
      selection.in_time != selection.out_time;

  std::ostringstream json;
  json << '{'
       << "\"play_start_marker_time\":\"" << JsonEscape(selection.play_start_marker_time) << "\","
       << "\"in_time\":\"" << JsonEscape(selection.in_time) << "\","
       << "\"out_time\":\"" << JsonEscape(selection.out_time) << "\","
       << "\"pre_roll_start_time\":\"" << JsonEscape(selection.pre_roll_start_time) << "\","
       << "\"post_roll_stop_time\":\"" << JsonEscape(selection.post_roll_stop_time) << "\","
       << "\"pre_roll_enabled\":" << (selection.pre_roll_enabled ? "true" : "false") << ','
       << "\"post_roll_enabled\":" << (selection.post_roll_enabled ? "true" : "false") << ','
       << "\"has_selection\":" << (has_selection ? "true" : "false");
  if (session_fps.has_value() && std::isfinite(*session_fps) && *session_fps > 0) {
    std::ostringstream value;
    value << std::fixed << std::setprecision(6) << *session_fps;
    json << ",\"session_fps\":" << value.str();
  }
  if (session_feet_frames_fps.has_value()
      && std::isfinite(*session_feet_frames_fps)
      && *session_feet_frames_fps > 0) {
    std::ostringstream value;
    value << std::fixed << std::setprecision(6) << *session_feet_frames_fps;
    json << ",\"session_feet_frames_fps\":" << value.str();
  }
  json << '}';
  return json.str();
}

TimelineSelection ResolveEffectiveTimelineSelection(PtslClient& client);
std::optional<std::string> FindClipTimelinePlayTimeFromSelection(PtslClient& client,
                                                                 const std::string& clip_id,
                                                                 const TimelineSelection& selection,
                                                                 const TimeCodeRateInfo& rate_info);
std::optional<std::string> FindClipTimelinePlayTimeFromClipId(PtslClient& client,
                                                              std::string_view clip_id,
                                                              std::string_view reference_timecode,
                                                              const TimeCodeRateInfo& rate_info);

std::string DeriveRenamedTrackName(const std::string& current_track_name,
                                   const std::string& reference_track_name,
                                   const std::string& marker_name) {
  if (current_track_name.empty() || reference_track_name.empty() || marker_name.empty()) {
    return "";
  }
  if (current_track_name == reference_track_name) {
    return marker_name;
  }

  std::string renamed_track_name = current_track_name;
  std::size_t match_index = renamed_track_name.find(reference_track_name);
  if (match_index == std::string::npos) {
    return "";
  }

  std::size_t search_start = 0;
  while (match_index != std::string::npos) {
    renamed_track_name.replace(match_index, reference_track_name.size(), marker_name);
    search_start = match_index + marker_name.size();
    match_index = renamed_track_name.find(reference_track_name, search_start);
  }

  return renamed_track_name;
}

std::string BuildRenamePlanResultJson(const std::string& marker_name,
                                      const std::string& primary_track_id,
                                      const std::string& primary_track_name,
                                      const std::vector<RenameTrackResult>& results) {
  std::ostringstream json;
  json << '{'
       << "\"marker_name\":\"" << JsonEscape(marker_name) << "\","
       << "\"primary_track_id\":\"" << JsonEscape(primary_track_id) << "\","
       << "\"primary_track_name\":\"" << JsonEscape(primary_track_name) << "\","
       << "\"track_results\":[";

  for (std::size_t i = 0; i < results.size(); i += 1) {
    if (i > 0) {
      json << ',';
    }

    const auto& result = results[i];
    json << '{'
         << "\"track_id\":\"" << JsonEscape(result.track_id) << "\","
         << "\"live_track_id\":\"" << JsonEscape(result.live_track_id) << "\","
         << "\"saved_name\":\"" << JsonEscape(result.saved_name) << "\","
         << "\"current_name\":\"" << JsonEscape(result.current_name) << "\","
         << "\"new_name\":\"" << JsonEscape(result.new_name) << "\","
         << "\"status\":\"" << JsonEscape(result.status) << "\""
         << '}';
  }

  json << "]}";
  return json.str();
}

grpc::ByteBuffer StringToByteBuffer(const std::string& payload) {
  grpc::Slice slice(payload.data(), payload.size());
  return grpc::ByteBuffer(&slice, 1);
}

std::string ByteBufferToString(const grpc::ByteBuffer& buffer) {
  std::vector<grpc::Slice> slices;
  buffer.Dump(&slices);

  std::string output;
  std::size_t total_size = 0;
  for (const auto& slice : slices) {
    total_size += slice.size();
  }
  output.reserve(total_size);

  for (const auto& slice : slices) {
    output.append(reinterpret_cast<const char*>(slice.begin()), slice.size());
  }

  return output;
}

class PtslSchema {
 public:
  PtslSchema() {
    google::protobuf::FileDescriptorProto file_proto;
    if (!file_proto.ParseFromArray(ptsl_descriptor::kData, static_cast<int>(ptsl_descriptor::kSize))) {
      throw std::runtime_error("Failed to parse embedded PTSL descriptor");
    }

    const auto* file_descriptor = pool_.BuildFile(file_proto);
    if (!file_descriptor) {
      throw std::runtime_error("Failed to build PTSL descriptor pool");
    }

    request_desc_ = pool_.FindMessageTypeByName("ptsl.Request");
    response_desc_ = pool_.FindMessageTypeByName("ptsl.Response");
    request_header_desc_ = pool_.FindMessageTypeByName("ptsl.RequestHeader");
    response_header_desc_ = pool_.FindMessageTypeByName("ptsl.ResponseHeader");
    command_id_enum_ = pool_.FindEnumTypeByName("ptsl.CommandId");
    task_status_enum_ = pool_.FindEnumTypeByName("ptsl.TaskStatus");

    if (!request_desc_ || !response_desc_ || !request_header_desc_ || !response_header_desc_ ||
        !command_id_enum_ || !task_status_enum_) {
      throw std::runtime_error("Embedded PTSL descriptor did not contain the expected message types");
    }
  }

  std::unique_ptr<google::protobuf::Message> NewMessage(const google::protobuf::Descriptor* descriptor) const {
    const auto* prototype = factory_.GetPrototype(descriptor);
    if (!prototype) {
      throw std::runtime_error("No dynamic protobuf prototype for descriptor");
    }
    return std::unique_ptr<google::protobuf::Message>(prototype->New());
  }

  const google::protobuf::FieldDescriptor* Field(const google::protobuf::Descriptor* descriptor,
                                                 const char* name) const {
    const auto* field = descriptor->FindFieldByName(name);
    if (!field) {
      throw std::runtime_error(std::string("Missing field in descriptor: ") + name);
    }
    return field;
  }

  int EnumValue(const google::protobuf::EnumDescriptor* descriptor, const char* name) const {
    const auto* value = descriptor->FindValueByName(name);
    if (!value && descriptor == command_id_enum_ && std::string_view(name).rfind("CId_", 0) != 0) {
      value = descriptor->FindValueByName((std::string("CId_") + name).c_str());
    }
    if (!value) {
      throw std::runtime_error(std::string("Missing enum value: ") + name);
    }
    return value->number();
  }

  const google::protobuf::Descriptor* request_descriptor() const { return request_desc_; }
  const google::protobuf::Descriptor* response_descriptor() const { return response_desc_; }
  const google::protobuf::Descriptor* request_header_descriptor() const { return request_header_desc_; }
  const google::protobuf::Descriptor* response_header_descriptor() const { return response_header_desc_; }
  const google::protobuf::EnumDescriptor* command_id_enum() const { return command_id_enum_; }
  const google::protobuf::EnumDescriptor* task_status_enum() const { return task_status_enum_; }

 private:
  google::protobuf::DescriptorPool pool_;
  mutable google::protobuf::DynamicMessageFactory factory_;
  const google::protobuf::Descriptor* request_desc_ = nullptr;
  const google::protobuf::Descriptor* response_desc_ = nullptr;
  const google::protobuf::Descriptor* request_header_desc_ = nullptr;
  const google::protobuf::Descriptor* response_header_desc_ = nullptr;
  const google::protobuf::EnumDescriptor* command_id_enum_ = nullptr;
  const google::protobuf::EnumDescriptor* task_status_enum_ = nullptr;
};

class PtslClient {
 public:
  PtslClient()
      : schema_(),
        protocol_candidates_(BuildPtslProtocolVersionCandidates()),
        active_protocol_version_(protocol_candidates_.front()),
        channel_(grpc::CreateChannel(kGrpcAddress, grpc::InsecureChannelCredentials())),
        stub_(channel_) {}

  bool HostReadyCheck() {
    std::vector<std::string> failure_details;
    std::string last_error;
    for (const auto& candidate : protocol_candidates_) {
      active_protocol_version_ = candidate;
      LogPtslHandshakeDebug(
          std::string("HostReadyCheck trying protocol ")
          + FormatPtslProtocolVersion(candidate));
      try {
        const auto response = SendCommand("HostReadyCheck", "");
        if (response.task_status == CompletedStatus()) {
          LogPtslHandshakeDebug(
              std::string("HostReadyCheck succeeded with protocol ")
              + FormatPtslProtocolVersion(candidate));
          last_host_ready_error_.clear();
          RememberSuccessfulProtocolVersion(candidate);
          return true;
        }
        last_error = BuildCommandFailureDetail("HostReadyCheck", response, candidate);
      } catch (const std::exception& error) {
        last_error = std::string("HostReadyCheck failed for PTSL protocol ")
            + FormatPtslProtocolVersion(candidate)
            + ": "
            + error.what();
      }
      failure_details.push_back(last_error);
    }

    if (failure_details.empty()) {
      LogPtslHandshakeDebug("HostReadyCheck failed with no protocol responses.");
    } else {
      for (const auto& detail : failure_details) {
        LogPtslHandshakeDebug(detail);
      }
    }
    last_host_ready_error_ =
        "OverCue could not connect to Pro Tools automation. Make sure Pro Tools is open and ready, then try again.";
    return false;
  }

  std::string LastHostReadyError() const {
    return last_host_ready_error_;
  }

  std::string ActiveProtocolVersionString() const {
    return FormatPtslProtocolVersion(active_protocol_version_);
  }

  ResponseEnvelope ProbeCommandId(int command_id,
                                  const std::string& request_body_json,
                                  std::chrono::milliseconds timeout) {
    return SendCommandIdInternal(command_id, "ProbeCommand", request_body_json, timeout);
  }

  void RegisterConnection() {
    if (UsesLegacyAuthorizeConnection()) {
      LogPtslHandshakeDebug("Using legacy AuthorizeConnection flow");
      RegisterLegacyConnection();
      return;
    }

    LogPtslHandshakeDebug("Using RegisterConnection flow");
    const auto request_body =
        std::string("{\"company_name\":\"") + JsonEscape(kCompanyName) +
        "\",\"application_name\":\"" + JsonEscape(kApplicationName) + "\"}";

    const auto response = SendCommand("RegisterConnection", request_body);
    EnsureCompleted("RegisterConnection", response);

    auto session_id = ExtractJsonStringField(response.response_body_json, "session_id");
    if (!session_id) {
      session_id = ExtractJsonStringField(response.response_body_json, "sessionId");
    }
    if (!session_id || session_id->empty()) {
      throw std::runtime_error("RegisterConnection returned no session_id");
    }
    std::lock_guard<std::mutex> lock(session_id_mutex_);
    session_id_ = *session_id;
  }

  bool HasSession() const {
    std::lock_guard<std::mutex> lock(session_id_mutex_);
    return !session_id_.empty();
  }

  std::string GetSessionId() const {
    std::lock_guard<std::mutex> lock(session_id_mutex_);
    return session_id_;
  }

  void ClearSession() {
    std::lock_guard<std::mutex> lock(session_id_mutex_);
    session_id_.clear();
  }

  void SubscribeToEvents(const std::vector<EventSubscription>& events) {
    if (events.empty()) {
      return;
    }
    const auto response = SendCommand("SubscribeToEvents", BuildEventSubscriptionRequestJson(events));
    EnsureCompleted("SubscribeToEvents", response);
  }

  void UnsubscribeFromEvents(const std::vector<EventSubscription>& events) {
    if (events.empty()) {
      return;
    }
    const auto response = SendCommand("UnsubscribeFromEvents", BuildEventSubscriptionRequestJson(events));
    EnsureCompleted("UnsubscribeFromEvents", response);
  }

  void PollEvents(const std::function<void(const std::string&)>& on_event,
                  const std::function<void(grpc::ClientContext*)>& on_context_state = nullptr) {
    const auto command_id = schema_.EnumValue(schema_.command_id_enum(), "PollEvents");

    auto request_header = schema_.NewMessage(schema_.request_header_descriptor());
    const auto* request_header_descriptor = schema_.request_header_descriptor();
    const auto* request_header_reflection = request_header->GetReflection();
    request_header_reflection->SetString(
        request_header.get(),
        schema_.Field(request_header_descriptor, "task_id"),
        "");
    request_header_reflection->SetEnumValue(
        request_header.get(),
        schema_.Field(request_header_descriptor, "command"),
        command_id);
    ApplyProtocolVersionToRequestHeader(request_header.get());
    request_header_reflection->SetString(
        request_header.get(),
        schema_.Field(request_header_descriptor, "session_id"),
        GetSessionId());

    auto request = schema_.NewMessage(schema_.request_descriptor());
    const auto* request_reflection = request->GetReflection();
    request_reflection->MutableMessage(
        request.get(),
        schema_.Field(schema_.request_descriptor(), "header"))
        ->CopyFrom(*request_header);
    request_reflection->SetString(
        request.get(),
        schema_.Field(schema_.request_descriptor(), "request_body_json"),
        "{}");

    std::string request_bytes;
    if (!request->SerializeToString(&request_bytes)) {
      throw std::runtime_error("OverCue could not start listening to Pro Tools automation events.");
    }

    grpc::ClientContext context;
    grpc::CompletionQueue completion_queue;
    grpc::ByteBuffer response_buffer;
    grpc::Status status;
    auto request_buffer = StringToByteBuffer(request_bytes);

    auto* raw_reader = grpc::internal::ClientAsyncReaderFactory<grpc::ByteBuffer>::Create(
        channel_.get(),
        &completion_queue,
        grpc::internal::RpcMethod(
            kGrpcStreamingMethod,
            nullptr,
            grpc::internal::RpcMethod::SERVER_STREAMING),
        &context,
        request_buffer,
        true,
        kGrpcStartTag);
    std::unique_ptr<grpc::ClientAsyncReader<grpc::ByteBuffer>> reader(raw_reader);
    if (!reader) {
      throw std::runtime_error("OverCue could not start listening to Pro Tools automation events.");
    }

    if (on_context_state) {
      on_context_state(&context);
    }

    bool read_started = false;
    grpc::Status finish_status;
    try {
      void* got_tag = nullptr;
      bool ok = false;
      while (completion_queue.Next(&got_tag, &ok)) {
        if (got_tag == kGrpcStartTag) {
          if (!ok) {
            throw std::runtime_error("PollEvents start call failed");
          }
          response_buffer = grpc::ByteBuffer();
          reader->Read(&response_buffer, kGrpcReadTag);
          read_started = true;
          continue;
        }

        if (got_tag == kGrpcReadTag) {
          if (ok) {
            const auto response_bytes = ByteBufferToString(response_buffer);
            const auto envelope = ParseResponseEnvelope(response_bytes, "PollEvents");
            if (!Trimmed(envelope.response_body_json).empty() && on_event) {
              on_event(envelope.response_body_json);
            }
            response_buffer = grpc::ByteBuffer();
            reader->Read(&response_buffer, kGrpcReadTag);
            continue;
          }

          reader->Finish(&status, kGrpcFinishTag);
          continue;
        }

        if (got_tag == kGrpcFinishTag) {
          finish_status = status;
          break;
        }
      }
    } catch (...) {
      if (on_context_state) {
        on_context_state(nullptr);
      }
      completion_queue.Shutdown();
      throw;
    }

    if (on_context_state) {
      on_context_state(nullptr);
    }
    completion_queue.Shutdown();

    if (!read_started) {
      throw std::runtime_error("PollEvents did not start reading");
    }

    if (!finish_status.ok() && finish_status.error_code() != grpc::StatusCode::CANCELLED) {
      throw std::runtime_error(finish_status.error_message());
    }
  }

  int GetHighestMemoryLocationNumber() {
    const auto response = SendGetMemoryLocationsCommand();
    EnsureCompleted("GetMemoryLocations", response);

    const auto values = ExtractJsonIntFields(response.response_body_json, "number");
    if (values.empty()) {
      return 0;
    }
    return *std::max_element(values.begin(), values.end());
  }

  std::vector<int> GetMarkerMemoryLocationNumbers() {
    const auto response = SendGetMemoryLocationsCommand();
    EnsureCompleted("GetMemoryLocations", response);

    std::vector<int> marker_numbers;
    for (const auto& memory_location : ExtractMemoryLocationsFromResponse(response.response_body_json)) {
      if (memory_location.time_properties == "TP_Marker") {
        marker_numbers.push_back(memory_location.number);
      }
    }
    return marker_numbers;
  }

  std::vector<MemoryLocationInfo> GetMemoryLocations() {
    const auto response = SendGetMemoryLocationsCommand();
    EnsureCompleted("GetMemoryLocations", response);
    auto memory_locations = ExtractMemoryLocationsFromResponse(response.response_body_json);

    bool needs_timecode_normalization = false;
    for (const auto& memory_location : memory_locations) {
      if (LooksLikeJsonNumber(Trimmed(memory_location.start_time)) ||
          LooksLikeJsonNumber(Trimmed(memory_location.end_time))) {
        needs_timecode_normalization = true;
        break;
      }
    }

    if (!needs_timecode_normalization) {
      return memory_locations;
    }

    const auto rate_info = GetSessionTimeCodeRateInfo();
    const auto sample_rate_hz = GetSessionSampleRateHz();
    long long session_start_subframes = 0;
    try {
      session_start_subframes = TryTimecodeStringToSubframes(GetSessionStartTime(), rate_info).value_or(0LL);
    } catch (const std::exception& error) {
      std::cerr << "[ptsl-helper] warning: could not resolve session start time for numeric marker conversion: "
                << error.what() << "\n";
    }
    for (auto& memory_location : memory_locations) {
      const auto trimmed_start = Trimmed(memory_location.start_time);
      if (LooksLikeJsonNumber(trimmed_start)) {
        memory_location.start_time = SubframesToTimecodeString(
            session_start_subframes + SamplesToSubframes(std::stoll(trimmed_start), sample_rate_hz, rate_info),
            rate_info);
      }

      const auto trimmed_end = Trimmed(memory_location.end_time);
      if (LooksLikeJsonNumber(trimmed_end)) {
        memory_location.end_time = SubframesToTimecodeString(
            session_start_subframes + SamplesToSubframes(std::stoll(trimmed_end), sample_rate_hz, rate_info),
            rate_info);
      }
    }

    return memory_locations;
  }

  ResponseEnvelope SendGetMemoryLocationsCommand() {
    static const char* kRequestBodies[] = {
        "",
        "{}",
        "{\"pagination_request\":{\"limit\":1000,\"offset\":0}}",
    };

    ResponseEnvelope last_response;
    bool saw_response = false;
    for (const auto* request_body : kRequestBodies) {
      const auto response = SendCommand("GetMemoryLocations", request_body);
      if (response.task_status == CompletedStatus()) {
        return response;
      }
      last_response = response;
      saw_response = true;
    }

    const auto detail = ExtractResponseFailureDetail(last_response);
    if (detail.find("VENUE is not connected") != std::string::npos) {
      for (const auto* request_body : kRequestBodies) {
        const auto response = SendCommandId(
            kCommandIdGetMemoryLocations,
            "GetMemoryLocations",
            request_body);
        if (response.task_status == CompletedStatus()) {
          return response;
        }
        last_response = response;
        saw_response = true;
      }
    }

    if (IsPtsl24_10Protocol(active_protocol_version_)
        && IsGetMemoryLocationsEmptyOffsetFailure(last_response)) {
      for (int attempt = 0; attempt < 2; attempt += 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(125));
        for (const auto* request_body : kRequestBodies) {
          const auto response = SendCommand("GetMemoryLocations", request_body);
          if (response.task_status == CompletedStatus()) {
            return response;
          }
          last_response = response;
          saw_response = true;
        }
        if (!IsGetMemoryLocationsEmptyOffsetFailure(last_response)) {
          break;
        }
      }

      if (IsGetMemoryLocationsEmptyOffsetFailure(last_response)) {
        std::cerr << "[ptsl-helper] warning: treating 24.10 empty GetMemoryLocations page as an empty memory-location list\n";
        return BuildEmptyMemoryLocationsResponse();
      }
    }

    if (saw_response) {
      return last_response;
    }
    throw std::runtime_error("GetMemoryLocations returned no response");
  }

  bool DoesCreatedMarkerMatch(const MemoryLocationInfo& memory_location, int number, const Marker& marker) const {
    if (
        memory_location.time_properties != "TP_Marker"
        || memory_location.number != number
        || Trimmed(memory_location.name) != Trimmed(marker.name)
        || NormalizeComparableTimecode(memory_location.start_time) != NormalizeComparableTimecode(marker.start_time)
        || NormalizeComparableMarkerEndTime(memory_location.start_time, memory_location.end_time)
            != NormalizeComparableMarkerEndTime(marker.start_time, marker.end_time)
        || Trimmed(memory_location.comments) != Trimmed(marker.comments)) {
      return false;
    }

    if (!Trimmed(marker.location).empty()
        && NormalizeMarkerLocationValue(memory_location.location) != NormalizeMarkerLocationValue(marker.location)) {
      return false;
    }

    if (!Trimmed(marker.track_name).empty() && Trimmed(memory_location.track_name) != Trimmed(marker.track_name)) {
      return false;
    }

    return true;
  }

  bool WaitForCreatedMarker(int number, const Marker& marker, int timeout_ms = kCreateMarkerSettleTimeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
      for (const auto& memory_location : GetMemoryLocations()) {
        if (DoesCreatedMarkerMatch(memory_location, number, marker)) {
          return true;
        }
      }

      if (std::chrono::steady_clock::now() >= deadline) {
        return false;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(kCreateMarkerSettlePollIntervalMs));
    }
  }

  void CreateMarker(int number, const Marker& marker) {
    const auto create_request_body = BuildCreateMarkerRequestJson(number, marker);

    auto response = SendCommand("CreateMemoryLocation", create_request_body);
    if (response.task_status == CompletedStatus()) {
      return;
    }

    if (WaitForCreatedMarker(number, marker)) {
      return;
    }

    const auto edit_request_body = BuildEditMarkerRequestJson(number, marker);
    response = SendCommand("EditMemoryLocation", edit_request_body);
    EnsureCompleted("EditMemoryLocation", response);
  }

  void ClearMemoryLocations(const std::vector<int>& location_numbers) {
    if (location_numbers.empty()) {
      return;
    }

    const auto request_body = BuildClearMemoryLocationRequestJson(location_numbers);
    const auto response = SendCommand("ClearMemoryLocation", request_body);
    EnsureCompleted("ClearMemoryLocation", response);
  }

  void ClearAllMemoryLocations() {
    const auto response = SendCommand("ClearAllMemoryLocations", "");
    EnsureCompleted("ClearAllMemoryLocations", response);
  }

  void EditMarker(int number, const Marker& marker) {
    const auto request_body = BuildEditMarkerRequestJson(number, marker);
    const auto response = SendCommand("EditMemoryLocation", request_body);
    EnsureCompleted("EditMemoryLocation", response);
  }

  void JumpToTimecode(const std::string& timecode) {
    const auto current_selection = GetTimelineSelection();
    const auto rate_info = GetSessionTimeCodeRateInfo();
    ResponseEnvelope last_response;
    bool saw_response = false;
    for (const auto& request_body : BuildSetTimelineSelectionRequestJsonCandidates(timecode, current_selection, rate_info)) {
      const auto response = SendCommandId(kCommandIdSetTimelineSelection, "SetTimelineSelection", request_body);
      if (response.task_status == CompletedStatus()) {
        return;
      }

      LogPtslHandshakeDebug(
          std::string("SetTimelineSelection rejected request body ")
          + request_body
          + " with "
          + (response.response_error_json.empty() ? response.response_body_json : response.response_error_json));
      last_response = response;
      saw_response = true;
    }

    if (saw_response) {
      EnsureCompleted("SetTimelineSelection", last_response);
    }
  }

  void SetTimelineSelectionRange(const std::string& start_timecode,
                                 const std::string& end_timecode,
                                 bool avoid_timeline_selection_read = false) {
    const auto rate_info = GetCurrentSessionTimeCodeRateInfo();
    if (avoid_timeline_selection_read) {
      TimelineSelection current_selection;
      try {
        current_selection = ResolveEffectiveTimelineSelection(*this);
      } catch (const std::exception& error) {
        throw std::runtime_error(
            std::string("Could not read current Pro Tools pre/post-roll settings before setting drop-to-take selection: ")
            + error.what());
      }
      SetTimelineSelectionRange(start_timecode, end_timecode, current_selection, true);
      return;
    }

    const auto current_selection = ResolveEffectiveTimelineSelection(*this);
    SetTimelineSelectionRange(start_timecode, end_timecode, current_selection);
  }

  void SetTimelineSelectionRange(const std::string& start_timecode,
                                 const std::string& end_timecode,
                                 const TimelineSelection& current_selection,
                                 bool require_roll_restore = false) {
    const auto rate_info = GetCurrentSessionTimeCodeRateInfo();
    const auto request_bodies = BuildSetTimelineSelectionRangeRequestJsonCandidates(
        start_timecode,
        end_timecode,
        current_selection,
        rate_info,
        require_roll_restore);
    ResponseEnvelope last_response;
    bool saw_response = false;
    for (const auto& request_body : request_bodies) {
      LogPtslHandshakeDebug(
          std::string("SetTimelineSelection(range) trying request body ")
          + request_body);
      const auto response = SendCommandId(kCommandIdSetTimelineSelection, "SetTimelineSelection", request_body);
      if (response.task_status == CompletedStatus()) {
        return;
      }

      LogPtslHandshakeDebug(
          std::string("SetTimelineSelection(range) rejected request body ")
          + request_body
          + " with "
          + (response.response_error_json.empty() ? response.response_body_json : response.response_error_json));
      last_response = response;
      saw_response = true;
    }

    if (saw_response) {
      EnsureCompleted("SetTimelineSelection", last_response);
    }
  }

  void SetTimelineSelectionRangeMinimal(const std::string& start_timecode,
                                        const std::string& end_timecode) {
    const auto request_body = BuildSetTimelineSelectionRangeMinimalRequestJson(
        start_timecode,
        end_timecode);
    const auto response = SendCommandId(kCommandIdSetTimelineSelection, "SetTimelineSelection", request_body);
    EnsureCompleted("SetTimelineSelection", response);
  }

  void SetTimelineRolls(const TimelineSelection& current_selection,
                        const TimeCodeRateInfo& rate_info,
                        const std::optional<long long>& pre_roll_frames,
                        const std::optional<long long>& post_roll_frames,
                        const std::optional<long long>& pre_roll_milliseconds,
                        const std::optional<long long>& post_roll_milliseconds,
                        const std::optional<bool>& pre_roll_enabled,
                        const std::optional<bool>& post_roll_enabled) {
    ResponseEnvelope last_response;
    bool saw_response = false;
    for (const auto& request_body : BuildSetTimelineRollsRequestJsonCandidates(
             current_selection,
             rate_info,
             pre_roll_frames,
             post_roll_frames,
             pre_roll_milliseconds,
             post_roll_milliseconds,
             pre_roll_enabled,
             post_roll_enabled)) {
      const auto response = SendCommandId(kCommandIdSetTimelineSelection, "SetTimelineSelection", request_body);
      if (response.task_status == CompletedStatus()) {
        return;
      }

      LogPtslHandshakeDebug(
          std::string("SetTimelineSelection rejected request body ")
          + request_body
          + " with "
          + (response.response_error_json.empty() ? response.response_body_json : response.response_error_json));
      last_response = response;
      saw_response = true;
    }

    if (saw_response) {
      EnsureCompleted("SetTimelineSelection", last_response);
    }
  }

  void TogglePlayState() {
    const auto response = SendCommand("TogglePlayState", "");
    EnsureCompleted("TogglePlayState", response);
  }

  void ToggleRecordEnable() {
    const auto response = SendCommand("ToggleRecordEnable", "");
    EnsureCompleted("ToggleRecordEnable", response);
  }

  void ConsolidateClip() {
    const auto response = SendCommand("ConsolidateClip", "");
    EnsureCompleted("ConsolidateClip", response);
  }

  TransportStatusInfo GetTransportStatus() {
    const auto state_response = SendCommand("GetTransportState", "");
    EnsureCompleted("GetTransportState", state_response);

    std::string armed_response_json = "{}";
    try {
      const auto armed_response = SendCommand("GetTransportArmed", "");
      EnsureCompleted("GetTransportArmed", armed_response);
      armed_response_json = armed_response.response_body_json;
    } catch (...) {
      armed_response_json = "{}";
    }

    return ParseTransportStatus(state_response.response_body_json, armed_response_json);
  }

  bool GetTransportArmed() {
    const auto armed_response = SendCommand("GetTransportArmed", "");
    EnsureCompleted("GetTransportArmed", armed_response);

    auto is_transport_armed = ExtractJsonBoolField(armed_response.response_body_json, "is_transport_armed");
    if (!is_transport_armed) {
      is_transport_armed = ExtractJsonBoolField(armed_response.response_body_json, "transport_armed");
    }
    return is_transport_armed.value_or(false);
  }

  std::string GetSessionPath() {
    const auto response = SendCommand("GetSessionPath", "");
    EnsureCompleted("GetSessionPath", response);
    return ParseSessionPath(response.response_body_json);
  }

  std::vector<TrackInfo> GetSelectedTracks() {
    const auto active_protocol = ParsePtslProtocolVersion(ActiveProtocolVersionString());
    const auto release_major = NormalizedPtslReleaseMajor(active_protocol);
    if (release_major.has_value()
        && *release_major < 25) {
      std::vector<TrackInfo> all_tracks;
      try {
        all_tracks = GetTracks("TLFilter_All");
      } catch (const std::exception&) {
        all_tracks.clear();
      }

      std::vector<TrackInfo> selected_from_attributes;
      for (const auto& track : all_tracks) {
        const auto selected_state = LowercaseAscii(Trimmed(track.is_selected_state.value_or("")));
        if (selected_state == "setexplicitly"
            || selected_state == "setimplicitly"
            || selected_state == "setexplicitlyandimplicitly") {
          selected_from_attributes.push_back(track);
        }
      }
      if (!selected_from_attributes.empty()) {
        return selected_from_attributes;
      }

      std::vector<TrackInfo> best_legacy_subset;
      const auto consider_legacy_subset = [&](const char* filter_name) {
        try {
          const auto candidate_tracks = GetTracks(filter_name);
          if (candidate_tracks.empty()) {
            return;
          }
          if (!all_tracks.empty() && candidate_tracks.size() >= all_tracks.size()) {
            return;
          }
          if (best_legacy_subset.empty() || candidate_tracks.size() < best_legacy_subset.size()) {
            best_legacy_subset = candidate_tracks;
          }
        } catch (const std::exception&) {
        }
      };

      consider_legacy_subset("TLFilter_SelectedExplicitly");
      consider_legacy_subset("TLFilter_HasEditSelectionExplicitly");
      consider_legacy_subset("TLFilter_HasEditSelection");

      if (!best_legacy_subset.empty()) {
        return best_legacy_subset;
      }
    }
    return GetTracks("TLFilter_Selected");
  }

  std::vector<TrackInfo> GetAllTracks() {
    return GetTracks("TLFilter_All");
  }

  std::vector<FileLocationEntry> GetFileLocations(const std::vector<std::string>& filters) {
    const auto request_body = BuildGetFileLocationRequestJson(filters);
    const auto response = SendCommand("GetFileLocation", request_body);
    EnsureCompleted("GetFileLocation", response);
    return ExtractFileLocationsFromResponse(response.response_body_json);
  }

  std::vector<ClipInfo> GetClipList() {
    const auto request_body = BuildGetClipListRequestJson();
    const auto response = SendCommand("GetClipList", request_body);
    EnsureCompleted("GetClipList", response);
    return ExtractClipsFromResponse(response.response_body_json);
  }

  std::vector<PlaylistInfo> GetTrackPlaylists(const TrackInfo& track) {
    const auto request_body = BuildGetTrackPlaylistsRequestJson(track);
    const auto response = SendCommand("GetTrackPlaylists", request_body);
    EnsureCompleted("GetTrackPlaylists", response);
    return ExtractPlaylistsFromResponse(response.response_body_json);
  }

  std::vector<PlaylistElementInfo> GetPlaylistElements(std::string_view playlist_id,
                                                       std::string_view start_time,
                                                       std::string_view end_time) {
    const auto request_body = BuildGetPlaylistElementsRequestJson(playlist_id, start_time, end_time);
    const auto response = SendCommand("GetPlaylistElements", request_body);
    EnsureCompleted("GetPlaylistElements", response);
    return ExtractPlaylistElementsFromResponse(response.response_body_json);
  }

  TimelineSelection GetCurrentTimelineSelection() {
    return GetTimelineSelection();
  }

  TimeCodeRateInfo GetCurrentSessionTimeCodeRateInfo() {
    return GetSessionTimeCodeRateInfo();
  }

  void RenameTrack(const TrackRename& rename) {
    const auto request_body = BuildRenameTrackRequestJson(rename);
    const auto response = SendCommand("RenameTargetTrack", request_body);
    EnsureCompleted("RenameTargetTrack", response);
  }

  EditSelectionBounds GetEditSelectionBounds() {
    const auto command_id = schema_.EnumValue(schema_.command_id_enum(), "GetEditSelection");
    ResponseEnvelope response;
    try {
      response = SendCommandIdInternal(
          command_id,
          "GetEditSelection",
          "{\"location_type\":\"TLType_TimeCode\"}",
          std::chrono::seconds(5));
    } catch (const std::exception& error) {
      const std::string detail = error.what();
      if (detail.find("Deadline Exceeded") != std::string::npos
          || detail.find("timed out") != std::string::npos) {
        const auto timeline_selection = GetTimelineSelection();
        EditSelectionBounds fallback_bounds;
        fallback_bounds.in_time = timeline_selection.in_time;
        fallback_bounds.out_time = timeline_selection.out_time;
        return fallback_bounds;
      }
      throw;
    }
    if (response.task_status != CompletedStatus()) {
      std::string detail;
      if (!response.response_error_json.empty()) {
        if (auto message = ExtractJsonStringField(response.response_error_json, "command_error_message")) {
          detail = *message;
        } else {
          detail = response.response_error_json;
        }
      } else {
        detail = response.response_body_json;
      }

      if (detail.find("not compatible with this version of Pro Tools") != std::string::npos
          || detail.find("Deadline Exceeded") != std::string::npos
          || detail.find("timed out") != std::string::npos) {
        const auto timeline_selection = GetTimelineSelection();
        EditSelectionBounds fallback_bounds;
        fallback_bounds.in_time = timeline_selection.in_time;
        fallback_bounds.out_time = timeline_selection.out_time;
        return fallback_bounds;
      }
    }

    EnsureCompleted("GetEditSelection", response);
    EditSelectionBounds bounds;
    bounds.in_time = ExtractJsonStringField(response.response_body_json, "in_time").value_or("");
    if (bounds.in_time.empty()) {
      bounds.in_time = ExtractJsonStringField(response.response_body_json, "inTime").value_or("");
    }
    bounds.out_time = ExtractJsonStringField(response.response_body_json, "out_time").value_or("");
    if (bounds.out_time.empty()) {
      bounds.out_time = ExtractJsonStringField(response.response_body_json, "outTime").value_or("");
    }
    return bounds;
  }

  std::string ExportSessionInfoTextForTrackEdls() {
    const std::string tmp_path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp")
                                 + "/ptsl_session_edl.txt";

    struct OutputTypeAttempt {
      const char* value;
      bool is_int;
      bool reads_response_body;
    };
    static const OutputTypeAttempt kAttempts[] = {
      {"ESIOType_File", false, false},
      {"ESI_File", false, false},
      {"1", true, false},
      {"ESIOType_String", false, true},
      {"ESI_String", false, true},
      {"2", true, true},
    };
    struct TrackOffsetAttempt {
      const char* field_name;
      const char* value;
      enum class Mode {
        TimeCode,
        Samples,
        Fallback,
      } mode;
    };
    static const TrackOffsetAttempt kTrackOffsetAttempts[] = {
      {"track_offset_options", "TimeCode", TrackOffsetAttempt::Mode::TimeCode},
      {"track_offset_options", "Samples", TrackOffsetAttempt::Mode::Samples},
      {"track_offset_options", "BarsBeats", TrackOffsetAttempt::Mode::Fallback},
      {"location_type", "TLType_TimeCode", TrackOffsetAttempt::Mode::TimeCode},
      {"location_type", "TLType_Samples", TrackOffsetAttempt::Mode::Samples},
      {"location_type", "TimeCode", TrackOffsetAttempt::Mode::TimeCode},
      {"location_type", "Samples", TrackOffsetAttempt::Mode::Samples},
      {"location_type", "TLType_BarsBeats", TrackOffsetAttempt::Mode::Fallback},
      {"location_type", "BarsBeats", TrackOffsetAttempt::Mode::Fallback},
    };
    std::string last_error;
    std::string fallback_contents;
    for (const auto& offset_attempt : kTrackOffsetAttempts) {
      for (const auto& attempt : kAttempts) {
        std::ostringstream json;
        json << '{'
             << "\"include_file_list\":false,"
             << "\"include_clip_list\":false,"
             << "\"include_markers\":false,"
             << "\"include_plugin_list\":false,"
             << "\"include_track_edls\":true,"
             << "\"show_sub_frames\":false,"
             << "\"include_user_timestamps\":false,"
             << "\"track_list_type\":\"AllTracks\","
             << "\"fade_handling_type\":\"DontShowCrossfades\","
             << '"' << offset_attempt.field_name << "\":\"" << offset_attempt.value << "\","
             << "\"text_as_file_format\":\"UTF8\",";
        if (attempt.is_int) {
          json << "\"output_type\":" << attempt.value;
        } else {
          json << "\"output_type\":\"" << attempt.value << "\"";
        }
        if (!attempt.reads_response_body) {
          json << ",\"output_path\":\"" << JsonEscape(tmp_path) << "\"";
        }
        json << '}';
        const auto response = SendCommandIdInternal(
            schema_.EnumValue(schema_.command_id_enum(), "ExportSessionInfoAsText"),
            "ExportSessionInfoAsText",
            json.str(),
            std::chrono::seconds(8));
        if (response.task_status == CompletedStatus()) {
          std::string contents;
          if (attempt.reads_response_body) {
            contents = ExtractJsonStringField(response.response_body_json, "session_info").value_or("");
            if (contents.empty()) {
              contents = ExtractJsonStringField(response.response_body_json, "sessionInfo").value_or("");
            }
          } else {
            std::ifstream file(tmp_path);
            if (!file) {
              std::cerr << "[ptsl] ExportSessionInfoAsText wrote no file at " << tmp_path << "\n";
              throw std::runtime_error("Could not read the Pro Tools session tracks.");
            }
            contents.assign((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
            std::remove(tmp_path.c_str());
          }

          if (offset_attempt.mode == TrackOffsetAttempt::Mode::TimeCode) {
            if (SessionExportContainsTimecode(contents)) {
              return contents;
            }
          } else if (offset_attempt.mode == TrackOffsetAttempt::Mode::Samples) {
            if (SessionExportContainsSampleLocations(contents)) {
              return contents;
            }
          } else if (fallback_contents.empty()) {
            fallback_contents = contents;
          }
          if (fallback_contents.empty()) {
            fallback_contents = contents;
          }
          continue;
        }
        if (!response.response_error_json.empty()) {
          if (auto msg = ExtractJsonStringField(response.response_error_json, "command_error_message")) {
            last_error = *msg;
          }
        }
      }
    }
    if (!fallback_contents.empty()) {
      return fallback_contents;
    }
    std::cerr << "[ptsl] ExportSessionInfoAsText failed with all output_type attempts. Last error: "
              << last_error << "\n";
    throw std::runtime_error("Could not read the Pro Tools session tracks.");
  }

  void SetTrackMuteState(const std::vector<std::string>& track_names, bool enabled) {
    const auto request_body = BuildSetTrackMuteStateRequestJson(track_names, enabled);
    const auto response = SendCommand("SetTrackMuteState", request_body);
    EnsureCompleted("SetTrackMuteState", response);
  }

  std::vector<std::string> CreateNewTracks(std::string_view track_name,
                                           int number_of_tracks = 1,
                                           std::string_view insertion_point_position = "",
                                           std::string_view insertion_point_track_name = "") {
    const auto request_body = BuildCreateNewTracksRequestJson(
        track_name,
        number_of_tracks,
        insertion_point_position,
        insertion_point_track_name);
    const auto response = SendCommandIdInternal(
        schema_.EnumValue(schema_.command_id_enum(), "CId_CreateNewTracks"),
        "CreateNewTracks",
        request_body,
        std::chrono::seconds(5));
    EnsureCompleted("CreateNewTracks", response);
    return ExtractCreatedTrackNamesFromResponse(response.response_body_json);
  }

  void RenameSelectedClip(std::string_view new_name, bool rename_file = false) {
    const auto request_body = BuildRenameSelectedClipRequestJson(new_name, rename_file);
    const auto response = SendCommand("RenameSelectedClip", request_body);
    EnsureCompleted("RenameSelectedClip", response);
  }

  void RenameTargetClip(std::string_view clip_name, std::string_view new_name, bool rename_file = false) {
    const auto request_body = BuildRenameTargetClipRequestJson(clip_name, new_name, rename_file);
    const auto response = SendCommand("RenameTargetClip", request_body);
    EnsureCompleted("RenameTargetClip", response);
  }

  void GroupClips() {
    static const char* kRequestBodies[] = {
        "",
        "{}",
    };

    ResponseEnvelope last_response;
    bool saw_response = false;
    for (const auto* request_body : kRequestBodies) {
      const auto response = SendCommandIdInternal(
          schema_.EnumValue(schema_.command_id_enum(), "CId_GroupClips"),
          "GroupClips",
          request_body,
          std::chrono::seconds(5));
      if (response.task_status == CompletedStatus()) {
        return;
      }

      last_response = response;
      saw_response = true;
    }

    if (saw_response) {
      EnsureCompleted("GroupClips", last_response);
    }
    throw std::runtime_error("GroupClips returned no response");
  }

  std::string WriteSelectedTranscriptionToJSONFile() {
    const auto response = SendCommandIdInternal(
        schema_.EnumValue(schema_.command_id_enum(), "CId_WriteSelectedTranscriptionToJSONFile"),
        "WriteSelectedTranscriptionToJSONFile",
        "",
        std::chrono::seconds(8));
    EnsureCompleted("WriteSelectedTranscriptionToJSONFile", response);
    const auto path_to_json_file = ExtractJsonStringField(response.response_body_json, "path_to_json_file")
        .value_or(ExtractJsonStringField(response.response_body_json, "pathToJsonFile").value_or(""));
    if (Trimmed(path_to_json_file).empty()) {
      throw std::runtime_error("Pro Tools did not return a transcription JSON file path.");
    }
    return path_to_json_file;
  }

  void SelectAllClipsOnTrack(const std::string& track_name) {
    std::ostringstream json;
    json << "{\"track_name\":\"" << JsonEscape(track_name) << "\"}";
    const auto response = SendCommandIdInternal(
        schema_.EnumValue(schema_.command_id_enum(), "SelectAllClipsOnTrack"),
        "SelectAllClipsOnTrack",
        json.str(),
        std::chrono::seconds(5));
    EnsureCompleted("SelectAllClipsOnTrack", response);
  }

  void Cut() {
    const auto response = SendCommandIdInternal(
        schema_.EnumValue(schema_.command_id_enum(), "Cut"),
        "Cut",
        "",
        std::chrono::seconds(5));
    EnsureCompleted("Cut", response);
  }

  void Clear() {
    const auto response = SendCommandIdInternal(
        schema_.EnumValue(schema_.command_id_enum(), "Clear"),
        "Clear",
        "",
        std::chrono::seconds(5));
    EnsureCompleted("Clear", response);
  }

  void Paste() {
    const auto response = SendCommandIdInternal(
        schema_.EnumValue(schema_.command_id_enum(), "Paste"),
        "Paste",
        "",
        std::chrono::seconds(5));
    EnsureCompleted("Paste", response);
  }

  void SelectTracksByNameExclusive(const std::string& track_name) {
    SelectTracksByNameExclusive(std::vector<std::string>{track_name});
  }

  void SelectTracksByNameExclusive(const std::vector<std::string>& track_names) {
    static const char* kSelectionModeValues[] = {
        "\"SM_Replace\"",
        "0",
        "\"Replace\"",
        "1",
    };

    if (track_names.empty()) {
      throw std::runtime_error("SelectTracksByName requires at least one track name");
    }

    ResponseEnvelope last_response;
    bool saw_response = false;
    for (const auto* selection_mode : kSelectionModeValues) {
      std::ostringstream request_body;
      request_body << "{\"track_names\":[";
      for (std::size_t index = 0; index < track_names.size(); index += 1) {
        if (index > 0) {
          request_body << ',';
        }
        request_body << '"' << JsonEscape(track_names[index]) << '"';
      }
      request_body << "],\"selection_mode\":" << selection_mode
                   << ",\"pagination_request\":{\"limit\":1000,\"offset\":0}}";

      const auto response = SendCommandIdInternal(
          schema_.EnumValue(schema_.command_id_enum(), "SelectTracksByName"),
          "SelectTracksByName",
          request_body.str(),
          std::chrono::seconds(5));
      if (response.task_status == CompletedStatus()) {
        return;
      }

      last_response = response;
      saw_response = true;
    }

    if (saw_response) {
      EnsureCompleted("SelectTracksByName", last_response);
    }
    throw std::runtime_error("SelectTracksByName returned no response");
  }

  void SelectTracksByNameReplace(const std::string& track_name) {
    SelectTracksByNameExclusive(track_name);
  }

  void SelectTracksByNameReplace(const std::vector<std::string>& track_names) {
    SelectTracksByNameExclusive(track_names);
  }

  public:
  TimelineSelection GetTimelineSelection() {
    ResponseEnvelope last_response;
    bool saw_response = false;
    for (const auto& request_body : BuildGetTimelineSelectionRequestJsonCandidates()) {
      const auto response = SendCommandIdInternal(
          kCommandIdGetTimelineSelection,
          "GetTimelineSelection",
          request_body,
          std::chrono::seconds(5));
      if (response.task_status == CompletedStatus()) {
        return ParseTimelineSelection(response.response_body_json);
      }

      LogPtslHandshakeDebug(
          std::string("GetTimelineSelection rejected request body ")
          + request_body
          + " with "
          + (response.response_error_json.empty() ? response.response_body_json : response.response_error_json));
      last_response = response;
      saw_response = true;
    }

    if (saw_response) {
      EnsureCompleted("GetTimelineSelection", last_response);
    }
    throw std::runtime_error("Could not read the Pro Tools timeline selection.");
  }

  TimelineSelection GetTimelineSelectionBarsBeats() {
    static const char* kRequestBodies[] = {
        "{\"time_scale\":\"BarsBeats\"}",
        "{\"location_type\":\"TLType_BarsBeats\"}",
    };

    ResponseEnvelope last_response;
    bool saw_response = false;
    for (const auto* request_body : kRequestBodies) {
      const auto response = SendCommandIdInternal(
          kCommandIdGetTimelineSelection,
          "GetTimelineSelection",
          request_body,
          std::chrono::seconds(5));
      if (response.task_status == CompletedStatus()) {
        return ParseTimelineSelection(response.response_body_json);
      }

      LogPtslHandshakeDebug(
          std::string("GetTimelineSelection(BarsBeats) rejected request body ")
          + request_body
          + " with "
          + (response.response_error_json.empty() ? response.response_body_json : response.response_error_json));
      last_response = response;
      saw_response = true;
    }

    if (saw_response) {
      EnsureCompleted("GetTimelineSelection", last_response);
    }
    throw std::runtime_error("Could not read the Pro Tools timeline selection.");
  }

  TimeCodeRateInfo GetSessionTimeCodeRateInfo() {
    const auto response = SendCommandIdInternal(
        schema_.EnumValue(schema_.command_id_enum(), "GetSessionTimeCodeRate"),
        "GetSessionTimeCodeRate",
        "",
        std::chrono::seconds(5));
    EnsureCompleted("GetSessionTimeCodeRate", response);

    auto current_setting = ExtractJsonStringField(response.response_body_json, "current_setting");
    if (!current_setting) {
      current_setting = ExtractJsonStringField(response.response_body_json, "time_code_rate");
    }
    if (!current_setting) {
      throw std::runtime_error("Could not read the Pro Tools session timecode rate.");
    }

    return ParseTimeCodeRateInfo(*current_setting);
  }

  TimeCodeRateInfo GetSessionFeetFramesRateInfo() {
    const auto response = SendCommandIdInternal(
        schema_.EnumValue(schema_.command_id_enum(), "GetSessionFeetFramesRate"),
        "GetSessionFeetFramesRate",
        "",
        std::chrono::seconds(5));
    EnsureCompleted("GetSessionFeetFramesRate", response);

    auto current_setting = ExtractJsonStringField(response.response_body_json, "current_setting");
    if (!current_setting) {
      current_setting = ExtractJsonStringField(response.response_body_json, "feet_frames_rate");
    }
    if (!current_setting) {
      throw std::runtime_error("Could not read the Pro Tools session feet+frames rate.");
    }

    return ParseFeetFramesRateInfo(*current_setting);
  }

  int GetSessionSampleRateHz() {
    const auto response = SendCommand("GetSessionSampleRate", "");
    EnsureCompleted("GetSessionSampleRate", response);

    const char* keys[] = {
        "current_setting",
        "sample_rate",
        "sampleRate",
        "session_sample_rate",
        "sample_rate_hz",
        "sampleRateHz",
    };

    for (const auto* key : keys) {
      auto value = ExtractJsonStringField(response.response_body_json, key);
      if (!value) {
        value = ExtractJsonScalarField(response.response_body_json, key);
      }
      if (value && !Trimmed(*value).empty()) {
        return ParseSessionSampleRateHz(*value);
      }
    }

    throw std::runtime_error("GetSessionSampleRate returned no current_setting");
  }

  std::string GetSessionStartTime() {
    const auto response = SendCommand("GetSessionStartTime", "");
    EnsureCompleted("GetSessionStartTime", response);

    const char* keys[] = {
        "session_start_time",
        "sessionStartTime",
        "current_setting",
    };

    for (const auto* key : keys) {
      const auto value = ExtractJsonStringField(response.response_body_json, key);
      if (value && !Trimmed(*value).empty()) {
        return NormalizeSessionStartTimecode(*value);
      }
    }

    throw std::runtime_error("GetSessionStartTime returned no session_start_time");
  }

  ResponseEnvelope SendCommand(const char* command_name, const std::string& request_body_json) {
    const auto command_id = schema_.EnumValue(schema_.command_id_enum(), command_name);
    return SendCommandId(command_id, command_name, request_body_json);
  }

  ResponseEnvelope SendCommandId(int command_id, const char* command_name, const std::string& request_body_json) {
    return SendCommandIdInternal(command_id, command_name, request_body_json, std::nullopt);
  }

  ResponseEnvelope SendCommandIdInternal(int command_id,
                                         const char* command_name,
                                         const std::string& request_body_json,
                                         std::optional<std::chrono::milliseconds> timeout) {
    auto request_header = schema_.NewMessage(schema_.request_header_descriptor());
    const auto* request_header_descriptor = schema_.request_header_descriptor();
    const auto* request_header_reflection = request_header->GetReflection();
    request_header_reflection->SetString(
        request_header.get(),
        schema_.Field(request_header_descriptor, "task_id"),
        "");
    request_header_reflection->SetEnumValue(
        request_header.get(),
        schema_.Field(request_header_descriptor, "command"),
        command_id);
    ApplyProtocolVersionToRequestHeader(request_header.get());
    request_header_reflection->SetString(
        request_header.get(),
        schema_.Field(request_header_descriptor, "session_id"),
        GetSessionId());

    auto request = schema_.NewMessage(schema_.request_descriptor());
    const auto* request_reflection = request->GetReflection();
    request_reflection->MutableMessage(
        request.get(),
        schema_.Field(schema_.request_descriptor(), "header"))
        ->CopyFrom(*request_header);
    request_reflection->SetString(
        request.get(),
        schema_.Field(schema_.request_descriptor(), "request_body_json"),
        request_body_json);

    std::string request_bytes;
    if (!request->SerializeToString(&request_bytes)) {
      std::cerr << "[ptsl] Failed to serialize request for " << command_name << "\n";
      throw std::runtime_error(
          "OverCue could not send the Pro Tools automation request. Make sure Pro Tools is open and try again.");
    }

    grpc::ClientContext context;
    if (timeout.has_value()) {
      context.set_deadline(std::chrono::system_clock::now() + timeout.value());
    } else {
      const std::string_view command_name_view(command_name ? command_name : "");
      if (command_name_view == "HostReadyCheck"
          || command_name_view == "RegisterConnection"
          || command_name_view == "AuthorizeConnection") {
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
      }
    }
    grpc::CompletionQueue completion_queue;
    grpc::ByteBuffer response_buffer;
    grpc::Status status;
    auto request_buffer = StringToByteBuffer(request_bytes);

    auto reader = stub_.PrepareUnaryCall(&context, kGrpcMethod, request_buffer, &completion_queue);
    if (!reader) {
      std::cerr << "[ptsl] Failed to prepare gRPC call for " << command_name << "\n";
      throw std::runtime_error(
          "OverCue could not communicate with Pro Tools automation. Make sure Pro Tools is open and try again.");
    }

    reader->StartCall();
    reader->Finish(&response_buffer, &status, kGrpcFinishTag);

    void* got_tag = nullptr;
    bool ok = false;
    if (!completion_queue.Next(&got_tag, &ok) || got_tag != kGrpcFinishTag || !ok) {
      std::cerr << "[ptsl] gRPC completion failed for " << command_name << "\n";
      throw std::runtime_error(
          "OverCue could not communicate with Pro Tools automation. Make sure Pro Tools is open and try again.");
    }

    if (!status.ok()) {
      std::cerr << "[ptsl] " << command_name
                << " failed (gRPC status "
                << static_cast<int>(status.error_code())
                << ")";
      const auto error_text = Trimmed(status.error_message());
      if (!error_text.empty()) {
        std::cerr << ": " << error_text;
      }
      std::cerr << "\n";
      throw std::runtime_error(
          "OverCue could not communicate with Pro Tools automation. Make sure Pro Tools is open and not busy, then try again.");
    }

    const auto response_bytes = ByteBufferToString(response_buffer);
    return ParseResponseEnvelope(response_bytes, command_name);
  }

  ResponseEnvelope ParseResponseEnvelope(const std::string& response_bytes, const char* command_name) const {
    auto response = schema_.NewMessage(schema_.response_descriptor());
    if (!response->ParseFromString(response_bytes)) {
      std::cerr << "[ptsl] Failed to parse gRPC response for " << command_name << "\n";
      throw std::runtime_error("OverCue could not read the Pro Tools automation response.");
    }

    const auto* response_reflection = response->GetReflection();
    const auto& response_header = response_reflection->GetMessage(
        *response, schema_.Field(schema_.response_descriptor(), "header"));
    const auto* response_header_reflection = response_header.GetReflection();

    ResponseEnvelope envelope;
    envelope.task_status = response_header_reflection->GetEnumValue(
        response_header,
        schema_.Field(schema_.response_header_descriptor(), "status"));
    envelope.response_body_json = response_reflection->GetString(
        *response, schema_.Field(schema_.response_descriptor(), "response_body_json"));
    envelope.response_error_json = response_reflection->GetString(
        *response, schema_.Field(schema_.response_descriptor(), "response_error_json"));
    return envelope;
  }

  void EnsureCompleted(const char* command_name, const ResponseEnvelope& response) const {
    if (response.task_status == CompletedStatus()) {
      return;
    }

    throw std::runtime_error(BuildCommandFailureDetail(command_name, response, active_protocol_version_));
  }

 private:
  std::string ExtractResponseFailureDetail(const ResponseEnvelope& response) const {
    std::string detail;
    if (!response.response_error_json.empty()) {
      if (auto message = ExtractJsonStringField(response.response_error_json, "command_error_message")) {
        detail = *message;
      } else {
        detail = response.response_error_json;
      }
    } else if (!response.response_body_json.empty()) {
      detail = response.response_body_json;
    } else {
      detail = "unknown PTSL error";
    }
    return detail;
  }

  bool IsGetMemoryLocationsEmptyOffsetFailure(const ResponseEnvelope& response) const {
    const auto detail = ExtractResponseFailureDetail(response);
    return detail.find("offset is not less than items count") != std::string::npos;
  }

  ResponseEnvelope BuildEmptyMemoryLocationsResponse() const {
    ResponseEnvelope response;
    response.task_status = CompletedStatus();
    response.response_body_json =
        "{\"memory_locations\":[],\"pagination_response\":{\"total\":0,\"limit\":0,\"offset\":0}}";
    return response;
  }

  std::string BuildCommandFailureDetail(const char* command_name,
                                        const ResponseEnvelope& response,
                                        const PtslProtocolVersion& version) const {
    return std::string(command_name)
        + " failed for PTSL protocol "
        + FormatPtslProtocolVersion(version)
        + ": "
        + ExtractResponseFailureDetail(response);
  }

  void RememberSuccessfulProtocolVersion(const PtslProtocolVersion& version) {
    const auto found = std::find_if(
        protocol_candidates_.begin(),
        protocol_candidates_.end(),
        [&version](const PtslProtocolVersion& candidate) {
          return PtslProtocolVersionEquals(candidate, version);
        });
    if (found != protocol_candidates_.end()) {
      std::rotate(protocol_candidates_.begin(), found, found + 1);
      active_protocol_version_ = protocol_candidates_.front();
    }
  }

  bool UsesLegacyAuthorizeConnection() const {
    return active_protocol_version_.major == 1;
  }

  void RegisterLegacyConnection() {
    const char* auth_string_raw = std::getenv("PTSL_AUTH_STRING");
    const std::string auth_string = auth_string_raw ? std::string(auth_string_raw) : std::string();
    if (Trimmed(auth_string).empty()) {
      throw std::runtime_error(
          "PTSL protocol 1.0.0 requires AuthorizeConnection with an Avid certificate auth_string. "
          "Set PTSL_AUTH_STRING to your certificate JSON or certificate path.");
    }

    LogPtslHandshakeDebug("AuthorizeConnection sending auth_string payload");
    const auto response = SendCommandId(
        kCommandIdAuthorizeConnection,
        "AuthorizeConnection",
        std::string("{\"auth_string\":\"") + JsonEscape(auth_string) + "\"}");
    EnsureCompleted("AuthorizeConnection", response);
    LogPtslHandshakeDebug(
        std::string("AuthorizeConnection response body: ")
        + response.response_body_json);

    const auto authorized = ExtractJsonBoolField(response.response_body_json, "is_authorized");
    if (authorized.has_value() && !authorized.value()) {
      auto message = ExtractJsonStringField(response.response_body_json, "message");
      if (!message) {
        message = ExtractJsonScalarField(response.response_body_json, "message");
      }
      throw std::runtime_error(
          message && !Trimmed(*message).empty()
              ? *message
              : "AuthorizeConnection returned is_authorized=false");
    }

    auto session_id = ExtractJsonStringField(response.response_body_json, "session_id");
    if (!session_id) {
      session_id = ExtractJsonStringField(response.response_body_json, "sessionId");
    }
    if (!session_id || session_id->empty()) {
      throw std::runtime_error("AuthorizeConnection returned no session_id");
    }

    std::lock_guard<std::mutex> lock(session_id_mutex_);
    session_id_ = *session_id;
  }

  void ApplyProtocolVersionToRequestHeader(google::protobuf::Message* request_header) const {
    const auto* request_header_descriptor = schema_.request_header_descriptor();
    const auto* request_header_reflection = request_header->GetReflection();
    request_header_reflection->SetInt32(
        request_header,
        schema_.Field(request_header_descriptor, "version"),
        active_protocol_version_.major);
    if (const auto* version_minor_field = request_header_descriptor->FindFieldByName("version_minor")) {
      request_header_reflection->SetInt32(
          request_header,
          version_minor_field,
          active_protocol_version_.minor);
    }
    if (const auto* version_revision_field = request_header_descriptor->FindFieldByName("version_revision")) {
      request_header_reflection->SetInt32(
          request_header,
          version_revision_field,
          active_protocol_version_.revision);
    }
    if (const auto* versioned_header_field = request_header_descriptor->FindFieldByName("versioned_request_header_json")) {
      request_header_reflection->SetString(
          request_header,
          versioned_header_field,
          "");
    }
  }

  int CompletedStatus() const {
    return schema_.EnumValue(schema_.task_status_enum(), "Completed");
  }

  static std::string BuildEventSubscriptionRequestJson(const std::vector<EventSubscription>& events) {
    std::ostringstream json;
    json << "{\"events\":[";
    for (std::size_t index = 0; index < events.size(); index += 1) {
      if (index > 0) {
        json << ',';
      }
      json << '{'
           << "\"event_id\":\"" << JsonEscape(events[index].event_id) << "\","
           << "\"event_data_json\":\"" << JsonEscape(events[index].event_data_json) << "\""
           << '}';
    }
    json << "]}";
    return json.str();
  }

  static std::string BuildCreateMarkerRequestJson(int number, const Marker& marker) {
    const bool is_track_marker = NormalizeMarkerLocationValue(marker.location) == "track";
    if (is_track_marker && Trimmed(marker.track_name).empty()) {
      throw std::runtime_error("Track marker creation requires a track name.");
    }

    std::ostringstream json;
    json << '{'
         << "\"number\":" << number << ','
         << "\"name\":\"" << JsonEscape(marker.name) << "\","
         << "\"comments\":\"" << JsonEscape(marker.comments) << "\","
         << "\"time_properties\":\"TP_Marker\","
         << "\"reference\":\"" << (is_track_marker ? "MLR_FollowTrackTimebase" : "MLR_Absolute") << "\","
         << "\"start_time\":\"" << JsonEscape(marker.start_time) << "\",";

    if (!marker.end_time.empty()) {
      json << "\"end_time\":\"" << JsonEscape(marker.end_time) << "\",";
    }

    json << "\"general_properties\":{"
         << "\"zoom_settings\":false,"
         << "\"pre_post_roll_times\":false,"
         << "\"track_visibility\":false,"
         << "\"track_heights\":false,"
         << "\"group_enables\":false,"
         << "\"window_configuration\":false,"
         << "\"window_configuration_index\":0,"
         << "\"window_configuration_name\":\"\""
         << "}";

    if (is_track_marker) {
      json << ",\"location\":\"MLC_Track\","
           << "\"track_name\":\"" << JsonEscape(Trimmed(marker.track_name)) << "\"";
    }

    json << '}';

    return json.str();
  }

  static std::string BuildEditMarkerRequestJson(int number, const Marker& marker) {
    const bool is_track_marker = NormalizeMarkerLocationValue(marker.location) == "track";
    if (is_track_marker && Trimmed(marker.track_name).empty()) {
      throw std::runtime_error("Track marker editing requires a track name.");
    }

    std::ostringstream json;
    json << '{'
         << "\"number\":" << number << ','
         << "\"name\":\"" << JsonEscape(marker.name) << "\","
         << "\"comments\":\"" << JsonEscape(marker.comments) << "\","
         << "\"time_properties\":\"TP_Marker\","
         << "\"reference\":\"" << (is_track_marker ? "MLR_FollowTrackTimebase" : "MLR_Absolute") << "\","
         << "\"start_time\":\"" << JsonEscape(marker.start_time) << "\",";

    if (!marker.end_time.empty()) {
      json << "\"end_time\":\"" << JsonEscape(marker.end_time) << "\",";
    }

    json << "\"general_properties\":{"
         << "\"zoom_settings\":false,"
         << "\"pre_post_roll_times\":false,"
         << "\"track_visibility\":false,"
         << "\"track_heights\":false,"
         << "\"group_enables\":false,"
         << "\"window_configuration\":false,"
         << "\"window_configuration_index\":0,"
         << "\"window_configuration_name\":\"\""
         << "}";

    if (is_track_marker) {
      json << ",\"location\":\"MLC_Track\","
           << "\"track_name\":\"" << JsonEscape(Trimmed(marker.track_name)) << "\"";
    }

    json << '}';

    return json.str();
  }

  static std::string BuildClearMemoryLocationRequestJson(const std::vector<int>& location_numbers) {
    std::ostringstream json;
    json << '{'
         << "\"location_list\":[";
    for (std::size_t i = 0; i < location_numbers.size(); i += 1) {
      if (i > 0) {
        json << ',';
      }
      json << location_numbers[i];
    }
    json << "]}";
    return json.str();
  }

  static std::vector<std::string> BuildSetTimelineSelectionRequestJsonCandidates(
      const std::string& timecode,
      const TimelineSelection& current_selection,
      const TimeCodeRateInfo& rate_info) {
    const auto target_subframes = TimecodeStringToSubframes(timecode, rate_info);
    const auto target_timecode = SubframesToTimecodeString(target_subframes, rate_info);

    const auto current_in_subframes =
        TryTimecodeStringToSubframes(current_selection.in_time, rate_info).value_or(target_subframes);
    const auto current_out_subframes =
        TryTimecodeStringToSubframes(current_selection.out_time, rate_info).value_or(target_subframes);
    const auto current_pre_roll_subframes =
        TryTimecodeStringToSubframes(current_selection.pre_roll_start_time, rate_info).value_or(current_in_subframes);
    const auto current_post_roll_subframes =
        TryTimecodeStringToSubframes(current_selection.post_roll_stop_time, rate_info).value_or(current_out_subframes);

    const auto pre_roll_distance = std::max(0LL, current_in_subframes - current_pre_roll_subframes);
    const auto post_roll_distance = std::max(0LL, current_post_roll_subframes - current_out_subframes);
    const auto preserved_pre_roll_timecode = SubframesToTimecodeString(
        std::max(0LL, target_subframes - pre_roll_distance),
        rate_info);
    const auto preserved_post_roll_timecode = SubframesToTimecodeString(
        std::max(0LL, target_subframes + post_roll_distance),
        rate_info);
    const auto pre_roll_enabled = current_selection.pre_roll_enabled ? "TB_True" : "TB_False";
    const auto post_roll_enabled = current_selection.post_roll_enabled ? "TB_True" : "TB_False";

    const auto build_request = [&](bool include_roll_times,
                                   bool include_roll_flags,
                                   bool include_update_options) {
      std::ostringstream json;
      json << '{'
           << "\"play_start_marker_time\":\"" << JsonEscape(target_timecode) << "\","
           << "\"in_time\":\"" << JsonEscape(target_timecode) << "\","
           << "\"out_time\":\"" << JsonEscape(target_timecode) << "\"";
      if (include_roll_times) {
        json << ",\"pre_roll_start_time\":\"" << JsonEscape(preserved_pre_roll_timecode) << "\","
             << "\"post_roll_stop_time\":\"" << JsonEscape(preserved_post_roll_timecode) << "\"";
      }
      if (include_roll_flags) {
        json << ",\"pre_roll_enabled\":\"" << pre_roll_enabled << "\","
             << "\"post_roll_enabled\":\"" << post_roll_enabled << "\"";
      }
      if (include_update_options) {
        json << ",\"update_video_to\":\"TUV_None\","
             << "\"propagate_to_satellites\":\"TB_False\"";
      }
      json << '}';
      return json.str();
    };

    std::vector<std::string> requests;
    requests.push_back(build_request(true, true, true));
    requests.push_back(build_request(true, true, false));
    requests.push_back(build_request(true, false, false));
    requests.push_back(build_request(false, false, false));
    return requests;
  }

  static std::vector<std::string> BuildSetTimelineSelectionRangeRequestJsonCandidates(
      const std::string& start_timecode,
      const std::string& end_timecode,
      const TimelineSelection& current_selection,
      const TimeCodeRateInfo& rate_info,
      bool require_roll_restore = false) {
    const auto start_subframes = TimecodeStringToSubframes(start_timecode, rate_info);
    const auto end_subframes = TimecodeStringToSubframes(end_timecode, rate_info);
    const auto current_in_subframes =
        TryTimecodeStringToSubframes(current_selection.in_time, rate_info).value_or(start_subframes);
    const auto current_out_subframes =
        TryTimecodeStringToSubframes(current_selection.out_time, rate_info).value_or(end_subframes);
    const auto current_pre_roll_subframes =
        TryTimecodeStringToSubframes(current_selection.pre_roll_start_time, rate_info).value_or(current_in_subframes);
    const auto current_post_roll_subframes =
        TryTimecodeStringToSubframes(current_selection.post_roll_stop_time, rate_info).value_or(current_out_subframes);
    const auto pre_roll_distance = std::max(0LL, current_in_subframes - current_pre_roll_subframes);
    const auto post_roll_distance = std::max(0LL, current_post_roll_subframes - current_out_subframes);
    const auto preserved_pre_roll_timecode = SubframesToTimecodeString(
        std::max(0LL, start_subframes - pre_roll_distance),
        rate_info);
    const auto preserved_post_roll_timecode = SubframesToTimecodeString(
        std::max(0LL, end_subframes + post_roll_distance),
        rate_info);
    const auto pre_roll_enabled = current_selection.pre_roll_enabled ? "TB_True" : "TB_False";
    const auto post_roll_enabled = current_selection.post_roll_enabled ? "TB_True" : "TB_False";

    const auto build_request = [&](bool include_roll_times,
                                   bool include_roll_flags,
                                   bool include_update_options) {
      std::ostringstream json;
      json << '{'
           << "\"play_start_marker_time\":\"" << JsonEscape(start_timecode) << "\","
           << "\"in_time\":\"" << JsonEscape(start_timecode) << "\","
           << "\"out_time\":\"" << JsonEscape(end_timecode) << "\"";
      if (include_roll_times) {
        json << ",\"pre_roll_start_time\":\"" << JsonEscape(preserved_pre_roll_timecode) << "\","
             << "\"post_roll_stop_time\":\"" << JsonEscape(preserved_post_roll_timecode) << "\"";
      }
      if (include_roll_flags) {
        json << ",\"pre_roll_enabled\":\"" << pre_roll_enabled << "\","
             << "\"post_roll_enabled\":\"" << post_roll_enabled << "\"";
      }
      if (include_update_options) {
        json << ",\"update_video_to\":\"TUV_None\","
             << "\"propagate_to_satellites\":\"TB_False\"";
      }
      json << '}';
      return json.str();
    };

    std::vector<std::string> requests;
    requests.push_back(build_request(true, true, false));
    requests.push_back(build_request(true, true, true));
    if (!require_roll_restore) {
      requests.push_back(build_request(true, false, false));
      requests.push_back(build_request(false, false, false));
    }
    return requests;
  }

  static std::string BuildSetTimelineSelectionRangeMinimalRequestJson(
      const std::string& start_timecode,
      const std::string& end_timecode) {
    std::ostringstream json;
    json << '{'
         << "\"play_start_marker_time\":\"" << JsonEscape(start_timecode) << "\","
         << "\"in_time\":\"" << JsonEscape(start_timecode) << "\","
         << "\"out_time\":\"" << JsonEscape(end_timecode) << "\""
         << '}';
    return json.str();
  }

  static std::vector<std::string> BuildSetTimelineRollsRequestJsonCandidates(
      const TimelineSelection& current_selection,
      const TimeCodeRateInfo& rate_info,
      const std::optional<long long>& pre_roll_frames,
      const std::optional<long long>& post_roll_frames,
      const std::optional<long long>& pre_roll_milliseconds,
      const std::optional<long long>& post_roll_milliseconds,
      const std::optional<bool>& pre_roll_enabled,
      const std::optional<bool>& post_roll_enabled) {
    const auto current_play_start_subframes = TryTimecodeStringToSubframes(
        current_selection.play_start_marker_time,
        rate_info).value_or(
            TryTimecodeStringToSubframes(current_selection.in_time, rate_info).value_or(0LL));
    const auto current_in_subframes = TryTimecodeStringToSubframes(
        current_selection.in_time,
        rate_info).value_or(current_play_start_subframes);
    const auto current_out_subframes = TryTimecodeStringToSubframes(
        current_selection.out_time,
        rate_info).value_or(current_in_subframes);
    const auto current_pre_roll_subframes = TryTimecodeStringToSubframes(
        current_selection.pre_roll_start_time,
        rate_info).value_or(current_in_subframes);
    const auto current_post_roll_subframes = TryTimecodeStringToSubframes(
        current_selection.post_roll_stop_time,
        rate_info).value_or(current_out_subframes);

    const auto milliseconds_to_subframes = [&](long long milliseconds) {
      const long double clamped_milliseconds = static_cast<long double>(std::max(0LL, milliseconds));
      const long double subframes =
          (clamped_milliseconds / 1000.0L)
          * static_cast<long double>(std::max(1, rate_info.nominal_fps))
          * 100.0L;
      return std::max(0LL, static_cast<long long>(std::llround(subframes)));
    };

    const auto next_pre_roll_subframes = pre_roll_milliseconds.has_value()
        ? std::max(0LL, current_in_subframes - milliseconds_to_subframes(*pre_roll_milliseconds))
        : pre_roll_frames.has_value()
        ? std::max(0LL, current_in_subframes - (std::max(0LL, *pre_roll_frames) * 100LL))
        : current_pre_roll_subframes;
    const auto next_post_roll_subframes = post_roll_milliseconds.has_value()
        ? std::max(0LL, current_out_subframes + milliseconds_to_subframes(*post_roll_milliseconds))
        : post_roll_frames.has_value()
        ? std::max(0LL, current_out_subframes + (std::max(0LL, *post_roll_frames) * 100LL))
        : current_post_roll_subframes;
    const auto next_pre_roll_enabled = pre_roll_enabled.has_value()
        ? *pre_roll_enabled
        : pre_roll_milliseconds.has_value() || pre_roll_frames.has_value()
            ? true
            : current_selection.pre_roll_enabled;
    const auto next_post_roll_enabled = post_roll_enabled.has_value()
        ? *post_roll_enabled
        : post_roll_milliseconds.has_value() || post_roll_frames.has_value()
            ? true
            : current_selection.post_roll_enabled;
    const auto pre_roll_enabled_value = next_pre_roll_enabled ? "TB_True" : "TB_False";
    const auto post_roll_enabled_value = next_post_roll_enabled ? "TB_True" : "TB_False";

    const auto play_start_timecode = SubframesToTimecodeString(current_play_start_subframes, rate_info);
    const auto in_timecode = SubframesToTimecodeString(current_in_subframes, rate_info);
    const auto out_timecode = SubframesToTimecodeString(current_out_subframes, rate_info);
    const auto pre_roll_timecode = SubframesToTimecodeString(next_pre_roll_subframes, rate_info);
    const auto post_roll_timecode = SubframesToTimecodeString(next_post_roll_subframes, rate_info);

    const auto build_request = [&](bool include_update_options) {
      std::ostringstream json;
      json << '{'
           << "\"play_start_marker_time\":\"" << JsonEscape(play_start_timecode) << "\","
           << "\"in_time\":\"" << JsonEscape(in_timecode) << "\","
           << "\"out_time\":\"" << JsonEscape(out_timecode) << "\","
           << "\"pre_roll_start_time\":\"" << JsonEscape(pre_roll_timecode) << "\","
           << "\"post_roll_stop_time\":\"" << JsonEscape(post_roll_timecode) << "\","
           << "\"pre_roll_enabled\":\"" << pre_roll_enabled_value << "\","
           << "\"post_roll_enabled\":\"" << post_roll_enabled_value << "\"";
      if (include_update_options) {
        json << ",\"update_video_to\":\"TUV_None\","
             << "\"propagate_to_satellites\":\"TB_False\"";
      }
      json << '}';
      return json.str();
    };

    std::vector<std::string> requests;
    requests.push_back(build_request(true));
    requests.push_back(build_request(false));
    return requests;
  }

  static std::vector<std::string> BuildGetTimelineSelectionRequestJsonCandidates() {
    return {
        "{\"time_scale\":\"TimeCode\"}",
        "{\"location_type\":\"TLType_TimeCode\"}",
    };
  }

  static TimelineSelection ParseTimelineSelection(const std::string& json) {
    TimelineSelection selection;

    auto require_string = [&](const char* key) -> std::string {
      auto value = ExtractJsonStringField(json, key);
      if (!value) {
        std::cerr << "[ptsl] GetTimelineSelection returned no " << key << "\n";
        throw std::runtime_error("Could not read the Pro Tools timeline selection.");
      }
      return *value;
    };

    auto require_bool = [&](const char* key) -> bool {
      auto value = ExtractJsonBoolField(json, key);
      if (!value) {
        std::cerr << "[ptsl] GetTimelineSelection returned no " << key << "\n";
        throw std::runtime_error("Could not read the Pro Tools timeline selection.");
      }
      return *value;
    };

    selection.play_start_marker_time = require_string("play_start_marker_time");
    selection.in_time = require_string("in_time");
    selection.out_time = require_string("out_time");
    selection.pre_roll_start_time = require_string("pre_roll_start_time");
    selection.post_roll_stop_time = require_string("post_roll_stop_time");
    selection.pre_roll_enabled = require_bool("pre_roll_enabled");
    selection.post_roll_enabled = require_bool("post_roll_enabled");
    return selection;
  }

  static std::string ParseSessionPath(const std::string& json) {
    auto session_path_object = ExtractJsonObjectField(json, "session_path");
    if (!session_path_object) {
      session_path_object = ExtractJsonObjectField(json, "sessionPath");
    }

    if (session_path_object) {
      auto path_value = ExtractJsonStringField(*session_path_object, "path");
      if (path_value && !path_value->empty()) {
        return *path_value;
      }
    }

    auto direct_path = ExtractJsonStringField(json, "session_path");
    if (!direct_path) {
      direct_path = ExtractJsonStringField(json, "sessionPath");
    }
    if (direct_path && !direct_path->empty()) {
      return *direct_path;
    }

    std::cerr << "[ptsl] GetSessionPath returned no session_path\n";
    throw std::runtime_error("Could not read the current Pro Tools session path.");
  }

  static TransportStatusInfo ParseTransportStatus(const std::string& state_json, const std::string& armed_json) {
    auto current_setting = ExtractJsonStringField(state_json, "current_setting");
    if (!current_setting) {
      current_setting = ExtractJsonStringField(state_json, "transport_state");
    }
    if (!current_setting || current_setting->empty()) {
      std::cerr << "[ptsl] GetTransportState returned no current_setting\n";
      throw std::runtime_error("Could not read the Pro Tools transport state.");
    }

    auto is_transport_armed = ExtractJsonBoolField(armed_json, "is_transport_armed");
    if (!is_transport_armed) {
      is_transport_armed = ExtractJsonBoolField(armed_json, "transport_armed");
    }

    return {*current_setting, is_transport_armed.value_or(false)};
  }

  std::vector<TrackInfo> GetTracks(const char* filter_name) {
    ResponseEnvelope last_response;
    bool saw_response = false;
    for (const auto& request_body : BuildGetTrackListRequestJsonCandidates(filter_name)) {
      const auto response = SendCommandIdInternal(
          schema_.EnumValue(schema_.command_id_enum(), "GetTrackList"),
          "GetTrackList",
          request_body,
          std::chrono::seconds(5));
      if (response.task_status == CompletedStatus()) {
        return ExtractTracksFromResponse(response.response_body_json);
      }

      LogPtslHandshakeDebug(
          std::string("GetTrackList rejected request body ")
          + request_body
          + " with "
          + (response.response_error_json.empty() ? response.response_body_json : response.response_error_json));
      last_response = response;
      saw_response = true;
    }

    if (saw_response) {
      EnsureCompleted("GetTrackList", last_response);
    }
    throw std::runtime_error("GetTrackList returned no response");
  }

  static std::vector<std::string> BuildGetTrackListRequestJsonCandidates(const char* filter_name) {
    const std::string normalized_filter = filter_name ? std::string(filter_name) : std::string();
    const bool has_filter = !Trimmed(normalized_filter).empty();
    std::vector<std::string> requests;

    const auto build_request = [&](bool include_page_limit,
                                   bool include_filter,
                                   bool include_additive,
                                   bool include_pagination_request) {
      std::ostringstream json;
      json << '{';
      bool needs_comma = false;
      const auto append_field = [&](const std::string& field_json) {
        if (needs_comma) {
          json << ',';
        }
        json << field_json;
        needs_comma = true;
      };

      if (include_page_limit) {
        append_field("\"page_limit\":10000");
      }
      if (include_filter && has_filter) {
        append_field(
            std::string("\"track_filter_list\":[{\"filter\":\"")
            + JsonEscape(normalized_filter)
            + "\",\"is_inverted\":false}]");
      }
      if (include_additive && include_filter && has_filter) {
        append_field("\"is_filter_list_additive\":true");
      }
      if (include_pagination_request) {
        append_field("\"pagination_request\":{\"limit\":1000,\"offset\":0}");
      }
      json << '}';
      return json.str();
    };

    requests.push_back(build_request(true, true, true, false));
    requests.push_back(build_request(false, true, true, false));
    requests.push_back(build_request(true, false, false, false));
    requests.push_back(build_request(false, false, false, false));
    requests.push_back(build_request(true, true, true, true));
    return requests;
  }

  static std::string BuildGetFileLocationRequestJson(const std::vector<std::string>& filters) {
    std::ostringstream json;
    json << '{'
         << "\"page_limit\":1000";
    if (!filters.empty()) {
      json << ",\"file_filters\":[";
      for (std::size_t index = 0; index < filters.size(); index += 1) {
        if (index > 0) {
          json << ',';
        }
        json << '"' << JsonEscape(filters[index]) << '"';
      }
      json << ']';
    }
    json << ",\"pagination_request\":{\"limit\":1000,\"offset\":0}"
         << '}';
    return json.str();
  }

  static std::string BuildGetClipListRequestJson() {
    return "{\"pagination_request\":{\"limit\":1000,\"offset\":0}}";
  }

  static std::string BuildGetTrackPlaylistsRequestJson(const TrackInfo& track) {
    std::ostringstream json;
    json << '{';
    if (!Trimmed(track.id).empty()) {
      json << "\"track_id\":\"" << JsonEscape(track.id) << "\",";
    } else if (!Trimmed(track.name).empty()) {
      json << "\"track_name\":\"" << JsonEscape(track.name) << "\",";
    } else {
      throw std::runtime_error("Cannot get playlists without a selected track");
    }
    json << "\"pagination_request\":{\"limit\":200,\"offset\":0}"
         << '}';
    return json.str();
  }

  static std::string BuildGetPlaylistElementsRequestJson(std::string_view playlist_id,
                                                         std::string_view start_time,
                                                         std::string_view end_time) {
    if (Trimmed(std::string(playlist_id)).empty()) {
      throw std::runtime_error("Cannot get playlist elements without playlist_id");
    }

    const auto start = Trimmed(std::string(start_time));
    const auto end = Trimmed(std::string(end_time));

    std::ostringstream json;
    json << '{'
         << "\"playlist_id\":\"" << JsonEscape(playlist_id) << "\"";
    if (!start.empty()) {
      json << ",\"start_time\":{\"location\":\"" << JsonEscape(start) << "\",\"time_type\":\"TLType_TimeCode\"}";
    }
    if (!end.empty()) {
      json << ",\"end_time\":{\"location\":\"" << JsonEscape(end) << "\",\"time_type\":\"TLType_TimeCode\"}";
    }
    json << ",\"time_format\":\"TLType_TimeCode\""
         << ",\"pagination_request\":{\"limit\":200,\"offset\":0}"
         << '}';
    return json.str();
  }

  static std::string BuildRenameTrackRequestJson(const TrackRename& rename) {
    if (rename.track_id.empty()) {
      throw std::runtime_error("Cannot rename track without track_id");
    }

    std::ostringstream json;
    json << '{'
         << "\"track_id\":";
    if (LooksLikeJsonNumber(rename.track_id)) {
      json << rename.track_id;
    } else {
      json << '"' << JsonEscape(rename.track_id) << '"';
    }
    json << ','
         << "\"current_name\":\"" << JsonEscape(rename.current_name) << "\","
         << "\"new_name\":\"" << JsonEscape(rename.new_name) << "\""
         << '}';
    return json.str();
  }

  static std::string BuildSetTrackMuteStateRequestJson(const std::vector<std::string>& track_names, bool enabled) {
    if (track_names.empty()) {
      throw std::runtime_error("Cannot update track mute state without track names");
    }

    std::ostringstream json;
    json << '{'
         << "\"track_names\":[";
    for (std::size_t index = 0; index < track_names.size(); index += 1) {
      if (index > 0) {
        json << ',';
      }
      json << '"' << JsonEscape(track_names[index]) << '"';
    }
    json << "],"
         << "\"enabled\":" << (enabled ? "true" : "false")
         << '}';
    return json.str();
  }

  static std::string BuildCreateNewTracksRequestJson(std::string_view track_name,
                                                     int number_of_tracks,
                                                     std::string_view insertion_point_position,
                                                     std::string_view insertion_point_track_name) {
    const auto trimmed_track_name = Trimmed(std::string(track_name));
    if (trimmed_track_name.empty()) {
      throw std::runtime_error("Cannot create Pro Tools tracks without a name");
    }
    if (number_of_tracks <= 0) {
      throw std::runtime_error("Cannot create zero Pro Tools tracks");
    }

    const auto trimmed_insertion_point_position = Trimmed(std::string(insertion_point_position));
    const auto trimmed_insertion_point_track_name = Trimmed(std::string(insertion_point_track_name));

    std::ostringstream json;
    json << '{'
         << "\"number_of_tracks\":" << number_of_tracks << ','
         << "\"track_name\":\"" << JsonEscape(trimmed_track_name) << "\","
         << "\"track_format\":\"TF_Mono\","
         << "\"track_type\":\"TT_Audio\","
         << "\"track_timebase\":\"TTB_Samples\","
         << "\"pagination_request\":{\"limit\":0,\"offset\":0}";
    if (!trimmed_insertion_point_position.empty()) {
      json << ",\"insertion_point_position\":\"" << JsonEscape(trimmed_insertion_point_position) << '"';
      if (!trimmed_insertion_point_track_name.empty()) {
        json << ",\"insertion_point_track_name\":\"" << JsonEscape(trimmed_insertion_point_track_name) << '"';
      }
    }
    json << '}';
    return json.str();
  }

  static std::string BuildRenameSelectedClipRequestJson(std::string_view new_name, bool rename_file) {
    if (Trimmed(new_name).empty()) {
      throw std::runtime_error("Cannot rename selected clip without a new name");
    }
    const auto limited_new_name = LimitRenameSelectedClipName(std::string(new_name));

    std::ostringstream json;
    json << '{'
         << "\"clip_location\":\"CL_Timeline\","
         << "\"new_name\":\"" << JsonEscape(limited_new_name) << "\","
         << "\"rename_file\":" << (rename_file ? "true" : "false")
         << '}';
    return json.str();
  }

  static std::string BuildRenameTargetClipRequestJson(std::string_view clip_name,
                                                      std::string_view new_name,
                                                      bool rename_file) {
    if (Trimmed(clip_name).empty()) {
      throw std::runtime_error("Cannot rename target clip without a current clip name");
    }
    if (Trimmed(new_name).empty()) {
      throw std::runtime_error("Cannot rename target clip without a new name");
    }

    std::ostringstream json;
    json << '{'
         << "\"clip_name\":\"" << JsonEscape(clip_name) << "\","
         << "\"new_name\":\"" << JsonEscape(new_name) << "\","
         << "\"rename_file\":" << (rename_file ? "true" : "false")
         << '}';
    return json.str();
  }

  PtslSchema schema_;
  std::vector<PtslProtocolVersion> protocol_candidates_;
  PtslProtocolVersion active_protocol_version_;
  std::string last_host_ready_error_;
  std::shared_ptr<grpc::Channel> channel_;
  grpc::GenericStub stub_;
  mutable std::mutex session_id_mutex_;
  std::string session_id_;
};

bool TrackNamesMatch(std::string_view left, std::string_view right) {
  const auto normalized_left = LowercaseAscii(Trimmed(left));
  const auto normalized_right = LowercaseAscii(Trimmed(right));
  return !normalized_left.empty() && normalized_left == normalized_right;
}

bool SelectedTracksContainName(const std::vector<TrackInfo>& tracks, std::string_view wanted_track_name) {
  return std::any_of(tracks.begin(), tracks.end(), [&](const TrackInfo& track) {
    return TrackNamesMatch(track.name, wanted_track_name);
  });
}

bool SelectedTracksContainAllNames(
    const std::vector<TrackInfo>& tracks,
    const std::vector<std::string>& wanted_track_names) {
  return std::all_of(wanted_track_names.begin(), wanted_track_names.end(), [&](const std::string& track_name) {
    return SelectedTracksContainName(tracks, track_name);
  });
}

std::string JoinQuotedNames(const std::vector<std::string>& names) {
  std::ostringstream joined;
  for (std::size_t index = 0; index < names.size(); index += 1) {
    if (index > 0) {
      joined << ", ";
    }
    joined << '"' << names[index] << '"';
  }
  return joined.str();
}

std::string BuildJsonStringArray(const std::vector<std::string>& values) {
  std::ostringstream json;
  json << '[';
  for (std::size_t index = 0; index < values.size(); index += 1) {
    if (index > 0) {
      json << ',';
    }
    json << '"' << JsonEscape(values[index]) << '"';
  }
  json << ']';
  return json.str();
}

const TrackInfo* FindTrackByExactName(const std::vector<TrackInfo>& tracks, std::string_view wanted_track_name) {
  const TrackInfo* match = nullptr;
  for (const auto& track : tracks) {
    if (!TrackNamesMatch(track.name, wanted_track_name)) {
      continue;
    }
    if (match != nullptr) {
      throw std::runtime_error(
          std::string("Found multiple Pro Tools tracks named \"")
          + std::string(wanted_track_name)
          + "\". Rename them so exactly one track matches.");
    }
    match = &track;
  }
  return match;
}

std::string NormalizeTrackSearchText(std::string_view value) {
  std::string normalized;
  normalized.reserve(value.size());
  bool last_was_space = false;

  for (const unsigned char raw_ch : std::string(value)) {
    if (std::isalnum(raw_ch) != 0) {
      normalized.push_back(static_cast<char>(std::tolower(raw_ch)));
      last_was_space = false;
      continue;
    }

    if (!last_was_space) {
      normalized.push_back(' ');
      last_was_space = true;
    }
  }

  return Trimmed(normalized);
}

bool TrackNameContainsCueWord(std::string_view track_name) {
  std::istringstream input(NormalizeTrackSearchText(track_name));
  std::string token;
  while (input >> token) {
    if (token == "cue" || token == "cues") {
      return true;
    }
  }
  return false;
}

bool TrackNameContainsCharacter(std::string_view track_name, std::string_view character_name) {
  const auto normalized_track_name = NormalizeTrackSearchText(track_name);
  const auto normalized_character_name = NormalizeTrackSearchText(character_name);
  return !normalized_track_name.empty()
      && !normalized_character_name.empty()
      && normalized_track_name.find(normalized_character_name) != std::string::npos;
}

bool IsGenericCueTrackSuffixToken(std::string_view token) {
  const auto trimmed_token = Trimmed(std::string(token));
  if (trimmed_token.empty()) {
    return false;
  }
  if (std::all_of(trimmed_token.begin(), trimmed_token.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
      })) {
    return true;
  }

  const auto lowered_token = LowercaseAscii(trimmed_token);
  if (lowered_token == "dup") {
    return true;
  }
  if (lowered_token.size() > 3 && lowered_token.rfind("dup", 0) == 0) {
    return std::all_of(lowered_token.begin() + 3, lowered_token.end(), [](unsigned char ch) {
      return std::isdigit(ch) != 0;
    });
  }
  return false;
}

bool IsGenericCueTrackName(std::string_view track_name) {
  std::istringstream input(NormalizeTrackSearchText(track_name));
  std::vector<std::string> tokens;
  std::string token;
  while (input >> token) {
    tokens.push_back(token);
  }

  if (tokens.empty() || (tokens.front() != "cue" && tokens.front() != "cues")) {
    return false;
  }
  if (tokens.size() == 1) {
    return true;
  }

  return std::all_of(tokens.begin() + 1, tokens.end(), [](const std::string& value) {
    return IsGenericCueTrackSuffixToken(value);
  });
}

std::vector<const TrackInfo*> FindTracksContainingCue(const std::vector<TrackInfo>& tracks,
                                                      std::string_view character_name = "") {
  std::vector<const TrackInfo*> matches;
  matches.reserve(tracks.size());
  const bool needs_character_match = !Trimmed(std::string(character_name)).empty();
  for (const auto& track : tracks) {
    if (needs_character_match) {
      if (!TrackNameContainsCharacter(track.name, character_name)) {
        continue;
      }
    } else {
      if (!TrackNameContainsCueWord(track.name)) {
        continue;
      }
      if (!IsGenericCueTrackName(track.name)) {
        continue;
      }
    }
    matches.push_back(&track);
  }
  return matches;
}

std::string BuildCueTrackPoolKey(std::string_view character_name) {
  const auto normalized_character_name = NormalizeTrackSearchText(character_name);
  return normalized_character_name.empty()
      ? std::string("__generic_cue__")
      : std::string("character:") + normalized_character_name;
}

std::string BuildCueTrackPoolLabel(std::string_view character_name) {
  const auto trimmed_character_name = Trimmed(std::string(character_name));
  if (trimmed_character_name.empty()) {
    return "\"Cue\"/\"Cues\" tracks (numbered or dup-suffixed)";
  }
  return std::string("tracks containing \"")
      + trimmed_character_name
      + "\"";
}

std::string BuildCueTrackBaseName(std::string_view character_name) {
  const auto trimmed_character_name = Trimmed(std::string(character_name));
  return trimmed_character_name.empty() ? std::string("Cue") : trimmed_character_name;
}

MakePtClipTracksMode ParseMakePtClipTracksMode(std::string_view raw_mode) {
  const auto normalized_mode = LowercaseAscii(Trimmed(raw_mode));
  if (normalized_mode.empty()
      || normalized_mode == "per-character"
      || normalized_mode == "per_character") {
    return MakePtClipTracksMode::kPerCharacter;
  }
  if (normalized_mode == "generic-no-overlaps"
      || normalized_mode == "generic_no_overlaps"
      || normalized_mode == "generic-avoid-overlaps") {
    return MakePtClipTracksMode::kGenericNoOverlaps;
  }
  if (normalized_mode == "single-generic"
      || normalized_mode == "single_generic") {
    return MakePtClipTracksMode::kSingleGeneric;
  }

  throw std::runtime_error(
      std::string("Unknown PT clip track mode \"")
      + Trimmed(std::string(raw_mode))
      + "\".");
}

void ApplyMakePtClipTracksMode(std::vector<PreparedClipGroupCue>* cues,
                               MakePtClipTracksMode mode) {
  if (cues == nullptr || mode == MakePtClipTracksMode::kPerCharacter) {
    return;
  }

  for (auto& cue : *cues) {
    cue.character_name.clear();
    cue.track_pool_key = BuildCueTrackPoolKey("");
    cue.track_pool_label = BuildCueTrackPoolLabel("");
  }
}

std::string BuildCueTrackNameFromOrdinal(std::string_view base_name, std::size_t ordinal) {
  const auto trimmed_base_name = Trimmed(std::string(base_name));
  if (trimmed_base_name.empty()) {
    return std::string();
  }
  if (ordinal <= 1) {
    return trimmed_base_name;
  }
  return trimmed_base_name + " " + std::to_string(ordinal);
}

std::unordered_set<std::string> BuildExactTrackNameSet(const std::vector<TrackInfo>& tracks) {
  std::unordered_set<std::string> names;
  names.reserve(tracks.size());
  for (const auto& track : tracks) {
    const auto normalized_name = LowercaseAscii(Trimmed(track.name));
    if (!normalized_name.empty()) {
      names.insert(normalized_name);
    }
  }
  return names;
}

std::string AllocateCueTrackName(std::string_view character_name,
                                 std::unordered_set<std::string>* taken_track_names) {
  if (taken_track_names == nullptr) {
    throw std::runtime_error("Cannot allocate Cue track name without a name set");
  }

  const auto base_name = BuildCueTrackBaseName(character_name);
  if (base_name.empty()) {
    throw std::runtime_error("Cannot allocate Cue track name without a usable base name");
  }

  for (std::size_t ordinal = 1; ordinal < 10000; ordinal += 1) {
    const auto candidate = BuildCueTrackNameFromOrdinal(base_name, ordinal);
    const auto normalized_candidate = LowercaseAscii(Trimmed(candidate));
    if (normalized_candidate.empty()) {
      continue;
    }
    if (taken_track_names->emplace(normalized_candidate).second) {
      return candidate;
    }
  }

  throw std::runtime_error(
      std::string("Could not allocate a unique track name for \"")
      + base_name
      + "\".");
}

std::string JoinTrackNames(const std::vector<const TrackInfo*>& tracks) {
  std::ostringstream names;
  for (std::size_t index = 0; index < tracks.size(); index += 1) {
    if (index > 0) {
      names << ", ";
    }
    names << '"' << tracks[index]->name << '"';
  }
  return names.str();
}

void EnsureUniqueTrackNames(const std::vector<const TrackInfo*>& tracks,
                            std::string_view matching_label) {
  std::unordered_set<std::string> seen_names;
  for (const auto* track : tracks) {
    const auto normalized_name = LowercaseAscii(Trimmed(track ? track->name : ""));
    if (normalized_name.empty()) {
      continue;
    }
    if (!seen_names.emplace(normalized_name).second) {
      throw std::runtime_error(
          std::string("Found multiple Pro Tools tracks named \"")
          + (track ? track->name : std::string())
          + "\" among tracks "
          + std::string(matching_label)
          + ". Rename them so each Cue track name is unique.");
    }
  }
}

std::string NormalizeTrackNameKey(std::string_view name) {
  return LowercaseAscii(Trimmed(std::string(name)));
}

const TrackInfo* FindLiveTrackByIdOrUniqueSavedName(
    const std::vector<TrackInfo>& live_tracks,
    const std::unordered_map<std::string, TrackInfo>& live_track_map,
    std::string_view saved_track_id,
    std::string_view saved_track_name,
    bool* used_name_fallback = nullptr) {
  if (used_name_fallback != nullptr) {
    *used_name_fallback = false;
  }

  const auto track_id = Trimmed(std::string(saved_track_id));
  if (!track_id.empty()) {
    const auto id_it = live_track_map.find(track_id);
    if (id_it != live_track_map.end()) {
      return &id_it->second;
    }
  }

  const auto saved_name_key = NormalizeTrackNameKey(saved_track_name);
  if (saved_name_key.empty()) {
    return nullptr;
  }

  const TrackInfo* match = nullptr;
  for (const auto& live_track : live_tracks) {
    if (NormalizeTrackNameKey(live_track.name) != saved_name_key) {
      continue;
    }
    if (match != nullptr) {
      return nullptr;
    }
    match = &live_track;
  }

  if (match != nullptr && used_name_fallback != nullptr) {
    *used_name_fallback = true;
  }
  return match;
}

struct ResolvedDropToTakeRecordTracks {
  std::unordered_set<std::string> ids;
  std::size_t max_record_index = 0;
  bool found_any_record = false;
};

ResolvedDropToTakeRecordTracks ResolveDropToTakeRecordTracks(
    const std::vector<TrackInfo>& live_tracks,
    const std::unordered_map<std::string, TrackInfo>& live_track_map,
    const std::vector<RenamePlanTrack>& saved_tracks) {
  ResolvedDropToTakeRecordTracks resolved;
  for (const auto& saved_track : saved_tracks) {
    bool used_name_fallback = false;
    const auto* live_track = FindLiveTrackByIdOrUniqueSavedName(
        live_tracks,
        live_track_map,
        saved_track.track_id,
        saved_track.saved_name,
        &used_name_fallback);
    if (live_track == nullptr || live_track->id.empty()) {
      continue;
    }
    resolved.ids.insert(live_track->id);
    if (used_name_fallback) {
      std::cerr << "[drop-to-take] recovered captured record track by name saved_name=\""
                << saved_track.saved_name
                << "\" live_id=\"" << live_track->id
                << "\"\n";
    }
  }

  for (std::size_t index = 0; index < live_tracks.size(); index += 1) {
    if (resolved.ids.count(live_tracks[index].id)) {
      resolved.max_record_index = index;
      resolved.found_any_record = true;
    }
  }

  return resolved;
}

void UsePrimaryTrackAsDropToTakeRecordFallback(
    ResolvedDropToTakeRecordTracks& resolved,
    const std::vector<TrackInfo>& live_tracks,
    const TrackInfo& primary_track) {
  if (resolved.found_any_record || primary_track.id.empty()) {
    return;
  }

  resolved.ids.insert(primary_track.id);
  for (std::size_t index = 0; index < live_tracks.size(); index += 1) {
    if (live_tracks[index].id == primary_track.id) {
      resolved.max_record_index = index;
      resolved.found_any_record = true;
      break;
    }
  }

  if (resolved.found_any_record) {
    std::cerr << "[drop-to-take] recovered captured record track from primary track name=\""
              << primary_track.name
              << "\" live_id=\"" << primary_track.id
              << "\"\n";
  }
}

void SelectTrackByNameReplaceAndWait(PtslClient& client,
                                     const std::string& track_name,
                                     std::string_view context_label) {
  constexpr int kAttempts = 5;
  constexpr int kSettleDelayMs = 75;

  for (int attempt = 0; attempt < kAttempts; attempt += 1) {
    client.SelectTracksByNameReplace(track_name);
    try {
      const auto selected_tracks = client.GetSelectedTracks();
      if (SelectedTracksContainName(selected_tracks, track_name)) {
        return;
      }
    } catch (const std::exception&) {
    }

    if (attempt + 1 < kAttempts) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kSettleDelayMs));
    }
  }

  throw std::runtime_error(
      std::string("Could not select ")
      + std::string(context_label)
      + " track \""
      + track_name
      + "\" in Pro Tools. Make sure the track exists, is visible in the Edit window, and is not hidden, then try again.");
}

void SelectTracksByNameReplaceAndWait(PtslClient& client,
                                      const std::vector<std::string>& track_names,
                                      std::string_view context_label) {
  if (track_names.size() == 1) {
    SelectTrackByNameReplaceAndWait(client, track_names.front(), context_label);
    return;
  }

  constexpr int kAttempts = 5;
  constexpr int kSettleDelayMs = 75;

  for (int attempt = 0; attempt < kAttempts; attempt += 1) {
    client.SelectTracksByNameReplace(track_names);
    try {
      const auto selected_tracks = client.GetSelectedTracks();
      if (SelectedTracksContainAllNames(selected_tracks, track_names)) {
        return;
      }
    } catch (const std::exception&) {
    }

    if (attempt + 1 < kAttempts) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kSettleDelayMs));
    }
  }

  throw std::runtime_error(
      std::string("Could not select ")
      + std::string(context_label)
      + " tracks "
      + JoinQuotedNames(track_names)
      + " in Pro Tools. Make sure the tracks exist, are visible in the Edit window, and are not hidden, then try again.");
}

bool TrackRangeOverlapsSelectionFromSessionExport(
    std::string_view session_export_text,
    std::string_view track_name,
    const std::optional<std::pair<BarsBeatsPosition, BarsBeatsPosition>>& bars_beats_range,
    long long range_start_sf,
    long long range_end_sf,
    const TimeCodeRateInfo& rate_info) {
  if (bars_beats_range.has_value()) {
    const bool bars_beats_overlap = SessionExportTrackOverlapsBarsBeatsRange(
        session_export_text,
        track_name,
        bars_beats_range->first,
        bars_beats_range->second);
    if (bars_beats_overlap) {
      return true;
    }
  }

  return SessionExportTrackOverlapsSubframeRange(
      session_export_text,
      track_name,
      range_start_sf,
      range_end_sf,
      rate_info);
}

std::vector<TrackInfo> ResolveDropToTakeSourceTracksForRange(
    const std::vector<TrackInfo>& live_tracks,
    const std::unordered_set<std::string>& record_ids,
    const TrackInfo& primary_track,
    std::string_view session_export_text,
    const std::optional<std::pair<BarsBeatsPosition, BarsBeatsPosition>>& bars_beats_range,
    long long range_start_sf,
    long long range_end_sf,
    const TimeCodeRateInfo& rate_info) {
  std::vector<TrackInfo> source_tracks;
  source_tracks.reserve(record_ids.size());
  for (const auto& track : live_tracks) {
    if (!record_ids.count(track.id)) {
      continue;
    }
    if (TrackRangeOverlapsSelectionFromSessionExport(
            session_export_text,
            track.name,
            bars_beats_range,
            range_start_sf,
            range_end_sf,
            rate_info)) {
      source_tracks.push_back(track);
    }
  }

  if (source_tracks.empty()) {
    source_tracks.push_back(primary_track);
  }
  return source_tracks;
}

std::vector<std::string> TrackNamesFromInfos(const std::vector<TrackInfo>& tracks) {
  std::vector<std::string> names;
  names.reserve(tracks.size());
  for (const auto& track : tracks) {
    names.push_back(track.name);
  }
  return names;
}

std::vector<std::string> ResolveDropToTakeSourceTrackNames(
    const std::vector<TrackInfo>& source_tracks,
    const std::string& primary_track_name,
    bool use_channel_aware_selection) {
  if (use_channel_aware_selection) {
    return TrackNamesFromInfos(source_tracks);
  }
  if (!source_tracks.empty()) {
    const auto resolved_name = Trimmed(source_tracks.front().name);
    if (!resolved_name.empty()) {
      return {source_tracks.front().name};
    }
  }
  return {primary_track_name};
}

int SumTrackChannelCounts(const std::vector<TrackInfo>& tracks) {
  int channel_count = 0;
  for (const auto& track : tracks) {
    channel_count += std::max(0, TrackChannelCount(track));
  }
  return std::max(1, channel_count);
}

bool CanUseDropToTakeChannelAwareSelection(const std::vector<TrackInfo>& source_tracks) {
  return !source_tracks.empty()
      && std::all_of(source_tracks.begin(), source_tracks.end(), [](const TrackInfo& track) {
           return TrackHasExplicitChannelFormat(track);
         });
}

std::size_t FindDropToTakeSearchStart(
    const std::vector<TrackInfo>& live_tracks,
    std::size_t max_record_index,
    const std::unordered_set<std::string>& record_ids,
    std::string_view placement_mode,
    std::string_view take_track_keyword,
    std::string_view primary_live_type,
    std::string_view session_export_text,
    const std::optional<std::pair<BarsBeatsPosition, BarsBeatsPosition>>& bars_beats_range,
    long long range_start_sf,
    long long range_end_sf,
    const TimeCodeRateInfo& rate_info) {
  std::size_t search_start = max_record_index + 1;
  if (Trimmed(std::string(placement_mode)) == "next-open-track") {
    return search_start;
  }
  for (std::size_t index = max_record_index + 1; index < live_tracks.size(); index += 1) {
    const auto& candidate = live_tracks[index];
    if (record_ids.count(candidate.id)) {
      continue;
    }
    if (!TrackMatchesDropToTakeKeyword(candidate, take_track_keyword)) {
      continue;
    }
    if (!AreTrackTypesCompatible(primary_live_type, candidate.type)) {
      continue;
    }
    const bool overlaps = TrackRangeOverlapsSelectionFromSessionExport(
        session_export_text,
        candidate.name,
        bars_beats_range,
        range_start_sf,
        range_end_sf,
        rate_info);
    if (overlaps) {
      search_start = index + 1;
    }
  }
  return search_start;
}

struct DropToTakeTargetSearchResult {
  std::vector<TrackInfo> targets;
  std::size_t search_start = 0;
  bool used_gap_fallback = false;
  int required_channel_count = 1;
  int selected_channel_count = 0;
};

bool IsDropToTakeNextOpenTrackMode(std::string_view placement_mode) {
  return Trimmed(std::string(placement_mode)) == "next-open-track";
}

std::vector<TrackInfo> ScanDropToTakeTargetCandidates(
    const std::vector<TrackInfo>& live_tracks,
    std::size_t begin_index,
    std::size_t end_index,
    const std::unordered_set<std::string>& record_ids,
    std::string_view take_track_keyword,
    std::string_view primary_live_type,
    std::string_view session_export_text,
    const std::optional<std::pair<BarsBeatsPosition, BarsBeatsPosition>>& bars_beats_range,
    long long range_start_sf,
    long long range_end_sf,
    const TimeCodeRateInfo& rate_info,
    int required_channel_count,
    int* selected_channel_count,
    bool fallback_pass) {
  std::vector<TrackInfo> selected_targets;
  int selected_channels = selected_channel_count != nullptr ? *selected_channel_count : 0;
  const std::size_t bounded_begin = std::min(begin_index, live_tracks.size());
  const std::size_t bounded_end = std::min(end_index, live_tracks.size());
  for (std::size_t index = bounded_begin; index < bounded_end; index += 1) {
    const auto& candidate = live_tracks[index];
    std::string skip_reason;
    const int candidate_channels = TrackChannelCount(candidate);
    if (record_ids.count(candidate.id)) {
      skip_reason = "record-track";
    } else if (!TrackMatchesDropToTakeKeyword(candidate, take_track_keyword)) {
      skip_reason = "keyword";
    } else if (!AreTrackTypesCompatible(primary_live_type, candidate.type)) {
      skip_reason = "format";
    } else if (candidate_channels <= 0) {
      skip_reason = "unknown-channels";
    } else if (TrackRangeOverlapsSelectionFromSessionExport(
                   session_export_text,
                   candidate.name,
                   bars_beats_range,
                   range_start_sf,
                   range_end_sf,
                   rate_info)) {
      skip_reason = "occupied";
    }

    if (!skip_reason.empty()) {
      std::cerr << "[drop-to-take] target-candidate"
                << " index=\"" << index << "\""
                << " track=\"" << JsonEscape(candidate.name) << "\""
                << " type=\"" << JsonEscape(candidate.type) << "\""
                << " format=\"" << JsonEscape(candidate.format) << "\""
                << " channels=\"" << candidate_channels << "\""
                << " selected_channels=\"" << selected_channels << "\""
                << " required_channels=\"" << required_channel_count << "\""
                << " result=\"skip\""
                << " reason=\"" << skip_reason << "\""
                << " fallback=\"" << (fallback_pass ? "true" : "false") << "\"\n";
      continue;
    }

    std::cerr << "[drop-to-take] target-candidate"
              << " index=\"" << index << "\""
              << " track=\"" << JsonEscape(candidate.name) << "\""
              << " type=\"" << JsonEscape(candidate.type) << "\""
              << " format=\"" << JsonEscape(candidate.format) << "\""
              << " channels=\"" << candidate_channels << "\""
              << " selected_channels=\"" << (selected_channels + candidate_channels) << "\""
              << " required_channels=\"" << required_channel_count << "\""
              << " result=\"select\""
              << " fallback=\"" << (fallback_pass ? "true" : "false") << "\"\n";
    selected_targets.push_back(candidate);
    selected_channels += candidate_channels;
    if (selected_channels >= required_channel_count) {
      break;
    }
  }
  if (selected_channel_count != nullptr) {
    *selected_channel_count = selected_channels;
  }
  return selected_targets;
}

DropToTakeTargetSearchResult FindDropToTakeTargetTracks(
    const std::vector<TrackInfo>& live_tracks,
    std::size_t max_record_index,
    const std::unordered_set<std::string>& record_ids,
    std::string_view placement_mode,
    std::string_view take_track_keyword,
    std::string_view primary_live_type,
    std::string_view session_export_text,
    const std::optional<std::pair<BarsBeatsPosition, BarsBeatsPosition>>& bars_beats_range,
    long long range_start_sf,
    long long range_end_sf,
    const TimeCodeRateInfo& rate_info,
    int required_channel_count) {
  DropToTakeTargetSearchResult result;
  result.required_channel_count = std::max(1, required_channel_count);
  result.search_start = FindDropToTakeSearchStart(
      live_tracks,
      max_record_index,
      record_ids,
      placement_mode,
      take_track_keyword,
      primary_live_type,
      session_export_text,
      bars_beats_range,
      range_start_sf,
      range_end_sf,
      rate_info);

  std::cerr << "[drop-to-take] target-search"
            << " mode=\"" << JsonEscape(Trimmed(std::string(placement_mode))) << "\""
            << " start_index=\"" << result.search_start << "\""
            << " record_max_index=\"" << max_record_index << "\""
            << " required_channels=\"" << result.required_channel_count << "\""
            << " track_count=\"" << live_tracks.size() << "\"\n";

  result.targets = ScanDropToTakeTargetCandidates(
      live_tracks,
      result.search_start,
      live_tracks.size(),
      record_ids,
      take_track_keyword,
      primary_live_type,
      session_export_text,
      bars_beats_range,
      range_start_sf,
      range_end_sf,
      rate_info,
      result.required_channel_count,
      &result.selected_channel_count,
      false);
  if (result.selected_channel_count >= result.required_channel_count) {
    return result;
  }

  const std::size_t stack_begin = std::min(max_record_index + 1, live_tracks.size());
  if (!IsDropToTakeNextOpenTrackMode(placement_mode) && result.search_start > stack_begin) {
    std::cerr << "[drop-to-take] target-search fallback=\"earlier-open-take\""
              << " start_index=\"" << stack_begin << "\""
              << " end_index=\"" << result.search_start << "\"\n";
    auto fallback_targets = ScanDropToTakeTargetCandidates(
        live_tracks,
        stack_begin,
        result.search_start,
        record_ids,
        take_track_keyword,
        primary_live_type,
        session_export_text,
        bars_beats_range,
        range_start_sf,
        range_end_sf,
        rate_info,
        result.required_channel_count,
        &result.selected_channel_count,
        true);
    result.targets.insert(result.targets.end(), fallback_targets.begin(), fallback_targets.end());
    result.used_gap_fallback = result.selected_channel_count >= result.required_channel_count;
  }

  if (result.selected_channel_count < result.required_channel_count) {
    result.targets.clear();
  }
  return result;
}

void CutSelectedClipsOnTrackWithRetry(
    PtslClient& client,
    const std::string& source_track_name,
    const std::optional<std::pair<BarsBeatsPosition, BarsBeatsPosition>>& bars_beats_range,
    long long range_start_sf,
    long long range_end_sf,
    const TimeCodeRateInfo& rate_info) {
  constexpr int kCutAttempts = 3;
  constexpr int kSelectionSettleDelayMs = 75;
  constexpr int kRetryDelayMs = 125;

  for (int attempt = 0; attempt < kCutAttempts; attempt += 1) {
    SelectTrackByNameReplaceAndWait(client, source_track_name, "source");
    client.SelectAllClipsOnTrack(source_track_name);
    std::this_thread::sleep_for(std::chrono::milliseconds(kSelectionSettleDelayMs));
    client.Cut();

    const std::string post_cut_session_edl = client.ExportSessionInfoTextForTrackEdls();
    const bool source_still_overlaps = TrackRangeOverlapsSelectionFromSessionExport(
        post_cut_session_edl,
        source_track_name,
        bars_beats_range,
        range_start_sf,
        range_end_sf,
        rate_info);
    if (!source_still_overlaps) {
      return;
    }

    if (attempt + 1 < kCutAttempts) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kRetryDelayMs));
    }
  }

  throw std::runtime_error(
      std::string("Drop to Take could not move the source clip from record track \"")
      + source_track_name
      + "\". Make sure the clip is selected and try again.");
}

void CutSelectedClipsOnTracksWithRetry(
    PtslClient& client,
    const std::vector<std::string>& source_track_names,
    const std::optional<std::pair<BarsBeatsPosition, BarsBeatsPosition>>& bars_beats_range,
    long long range_start_sf,
    long long range_end_sf,
    const TimeCodeRateInfo& rate_info) {
  if (source_track_names.size() == 1) {
    CutSelectedClipsOnTrackWithRetry(
        client,
        source_track_names.front(),
        bars_beats_range,
        range_start_sf,
        range_end_sf,
        rate_info);
    return;
  }

  constexpr int kCutAttempts = 3;
  constexpr int kSelectionSettleDelayMs = 75;
  constexpr int kRetryDelayMs = 125;

  for (int attempt = 0; attempt < kCutAttempts; attempt += 1) {
    SelectTracksByNameReplaceAndWait(client, source_track_names, "source");
    std::this_thread::sleep_for(std::chrono::milliseconds(kSelectionSettleDelayMs));
    client.Cut();

    const std::string post_cut_session_edl = client.ExportSessionInfoTextForTrackEdls();
    bool any_source_still_overlaps = false;
    for (const auto& source_track_name : source_track_names) {
      if (TrackRangeOverlapsSelectionFromSessionExport(
              post_cut_session_edl,
              source_track_name,
              bars_beats_range,
              range_start_sf,
              range_end_sf,
              rate_info)) {
        any_source_still_overlaps = true;
        break;
      }
    }
    if (!any_source_still_overlaps) {
      return;
    }

    if (attempt + 1 < kCutAttempts) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kRetryDelayMs));
    }
  }

  throw std::runtime_error(
      std::string("Drop to Take could not move the source clips from record tracks ")
      + JoinQuotedNames(source_track_names)
      + ". Make sure the clips are selected and try again.");
}

std::optional<std::string> FirstTrackOverlappingRange(
    std::string_view session_export_text,
    const std::vector<std::string>& track_names,
    const std::optional<std::pair<BarsBeatsPosition, BarsBeatsPosition>>& bars_beats_range,
    long long range_start_sf,
    long long range_end_sf,
    const TimeCodeRateInfo& rate_info) {
  for (const auto& track_name : track_names) {
    if (TrackRangeOverlapsSelectionFromSessionExport(
            session_export_text,
            track_name,
            bars_beats_range,
            range_start_sf,
            range_end_sf,
            rate_info)) {
      return track_name;
    }
  }
  return std::nullopt;
}

bool TryResolveDropToTakeUsableSelectionRange(
    PtslClient& client,
    const TimeCodeRateInfo& rate_info,
    std::string* start_timecode_out = nullptr,
    std::string* end_timecode_out = nullptr) {
  auto assign_if_usable = [&](std::string start_time, std::string end_time) -> bool {
    start_time = NormalizeComparableTimecode(start_time);
    end_time = NormalizeComparableTimecode(end_time);
    if (start_time.empty() || end_time.empty() || start_time == end_time) {
      return false;
    }
    if (start_timecode_out) {
      *start_timecode_out = start_time;
    }
    if (end_timecode_out) {
      *end_timecode_out = end_time;
    }
    return true;
  };

  try {
    const auto selection = ResolveEffectiveTimelineSelection(client);
    if (assign_if_usable(selection.in_time, selection.out_time)) {
      return true;
    }
  } catch (const std::exception&) {
  }

  try {
    const auto edit_bounds = client.GetEditSelectionBounds();
    if (assign_if_usable(edit_bounds.in_time, edit_bounds.out_time)) {
      return true;
    }
  } catch (const std::exception&) {
  }

  return false;
}

void PrimeDropToTakeSourceSelection(
    PtslClient& client,
    const std::string& source_track_name,
    const TimeCodeRateInfo& rate_info) {
  constexpr int kSelectionAttempts = 6;
  constexpr int kSelectionSettleDelayMs = 90;

  SelectTrackByNameReplaceAndWait(client, source_track_name, "source-prime");
  if (ShouldAvoidTimelineSelectionReadsForDropToTake(client)) {
    client.SelectAllClipsOnTrack(source_track_name);
    std::this_thread::sleep_for(std::chrono::milliseconds(kSelectionSettleDelayMs));
    std::cerr << "[drop-to-take] step=source-selection-ready"
              << " track=\"" << source_track_name << "\""
              << " result=\"selected-without-timeline-read\"\n";
    return;
  }

  for (int attempt = 0; attempt < kSelectionAttempts; attempt += 1) {
    client.SelectAllClipsOnTrack(source_track_name);
    std::this_thread::sleep_for(std::chrono::milliseconds(kSelectionSettleDelayMs));

    std::string selection_start;
    std::string selection_end;
    if (TryResolveDropToTakeUsableSelectionRange(
            client,
            rate_info,
            &selection_start,
            &selection_end)) {
      std::cerr << "[drop-to-take] step=source-selection-ready"
                << " track=\"" << source_track_name << "\""
                << " start=\"" << selection_start << "\""
                << " end=\"" << selection_end << "\"\n";
      return;
    }
  }

  std::cerr << "[drop-to-take] step=source-selection-ready"
            << " track=\"" << source_track_name << "\""
            << " result=\"unavailable\"\n";
}

TimelineSelection ResolveEffectiveTimelineSelection(PtslClient& client) {
  auto selection = client.GetCurrentTimelineSelection();
  const auto active_protocol = ParsePtslProtocolVersion(client.ActiveProtocolVersionString());
  const auto release_major = NormalizedPtslReleaseMajor(active_protocol);
  if (release_major && *release_major < 25) {
    selection.play_start_marker_time = NormalizeComparableTimecode(selection.play_start_marker_time);
    selection.in_time = NormalizeComparableTimecode(selection.in_time);
    selection.out_time = NormalizeComparableTimecode(selection.out_time);
    selection.pre_roll_start_time = NormalizeComparableTimecode(selection.pre_roll_start_time);
    selection.post_roll_stop_time = NormalizeComparableTimecode(selection.post_roll_stop_time);
    return selection;
  }
  try {
    const auto edit_bounds = client.GetEditSelectionBounds();
    const auto edit_in = NormalizeComparableTimecode(edit_bounds.in_time);
    const auto edit_out = NormalizeComparableTimecode(edit_bounds.out_time);
    if (!Trimmed(edit_in).empty() && !Trimmed(edit_out).empty() && edit_in != edit_out) {
      selection.in_time = edit_in;
      selection.out_time = edit_out;
    }
  } catch (const std::exception&) {
  }

  selection.play_start_marker_time = NormalizeComparableTimecode(selection.play_start_marker_time);
  selection.in_time = NormalizeComparableTimecode(selection.in_time);
  selection.out_time = NormalizeComparableTimecode(selection.out_time);
  selection.pre_roll_start_time = NormalizeComparableTimecode(selection.pre_roll_start_time);
  selection.post_roll_stop_time = NormalizeComparableTimecode(selection.post_roll_stop_time);
  return selection;
}

bool HasDetectedTimelineOrEditRange(PtslClient& client) {
  const auto has_range = [](std::string start_time, std::string end_time) {
    start_time = NormalizeComparableTimecode(start_time);
    end_time = NormalizeComparableTimecode(end_time);
    return !Trimmed(start_time).empty() && !Trimmed(end_time).empty() && start_time != end_time;
  };

  try {
    const auto selection = ResolveEffectiveTimelineSelection(client);
    if (has_range(SelectTimelineReferenceTime(selection), selection.out_time)) {
      return true;
    }
  } catch (const std::exception&) {
  }

  try {
    const auto edit_bounds = client.GetEditSelectionBounds();
    return has_range(edit_bounds.in_time, edit_bounds.out_time);
  } catch (const std::exception&) {
  }

  return false;
}

std::string BuildSelectedRangeWithoutClipMessage(std::string_view detail) {
  std::string message(detail);
  message += " Make sure Link Track and Edit Selection is on in Pro Tools, then select the clip/range again.";
  return message;
}

std::optional<std::pair<long long, long long>> ResolveDropToTakeGuardSelectionRangeSubframes(
    PtslClient& client,
    const TimeCodeRateInfo& rate_info) {
  auto parse_range = [&](std::string start_time, std::string end_time)
      -> std::optional<std::pair<long long, long long>> {
    start_time = NormalizeComparableTimecode(start_time);
    end_time = NormalizeComparableTimecode(end_time);
    if (start_time.empty() || end_time.empty() || start_time == end_time) {
      return std::nullopt;
    }
    try {
      const long long start_sf = TimecodeStringToSubframes(start_time, rate_info);
      const long long end_sf = TimecodeStringToSubframes(end_time, rate_info);
      return std::make_pair(std::min(start_sf, end_sf), std::max(start_sf, end_sf));
    } catch (const std::exception&) {
      return std::nullopt;
    }
  };

  try {
    const auto edit_bounds = client.GetEditSelectionBounds();
    if (const auto edit_range = parse_range(edit_bounds.in_time, edit_bounds.out_time)) {
      return edit_range;
    }
  } catch (const std::exception&) {
  }

  const auto selection = ResolveEffectiveTimelineSelection(client);
  return parse_range(selection.in_time, selection.out_time);
}

std::optional<std::pair<long long, long long>> ResolveDropToTakeSelectionRangeSubframes(
    const TimelineSelection& selection,
    const TimeCodeRateInfo& rate_info) {
  auto start_time = NormalizeComparableTimecode(selection.in_time);
  auto end_time = NormalizeComparableTimecode(selection.out_time);
  if (start_time.empty() || end_time.empty() || start_time == end_time) {
    return std::nullopt;
  }
  try {
    const long long start_sf = TimecodeStringToSubframes(start_time, rate_info);
    const long long end_sf = TimecodeStringToSubframes(end_time, rate_info);
    return std::make_pair(std::min(start_sf, end_sf), std::max(start_sf, end_sf));
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

double MediaTimePositionToSeconds(long long position,
                                  const std::string& time_type,
                                  int sample_rate_hz,
                                  const TimeCodeRateInfo& rate_info) {
  const std::string lower = LowercaseAscii(Trimmed(time_type));
  if (lower.find("sample") != std::string::npos) {
    if (sample_rate_hz <= 0) {
      return 0.0;
    }
    return static_cast<double>(position) / static_cast<double>(sample_rate_hz);
  }
  if (lower.find("frame") != std::string::npos) {
    const double fps = static_cast<double>(rate_info.actual_fps_numerator) /
                       static_cast<double>(rate_info.actual_fps_denominator);
    if (fps <= 0) {
      return 0.0;
    }
    return static_cast<double>(position) / fps;
  }
  return 0.0;
}

std::optional<ClipInfo> ResolveClipInfoForSelectedClip(PtslClient& client,
                                                       const FileLocationEntry& file_location,
                                                       std::string_view display_clip_name,
                                                       const std::vector<ClipInfo>& clips,
                                                       const std::optional<TimelineSelection>& current_selection,
                                                       const TimeCodeRateInfo& rate_info) {
  std::vector<const ClipInfo*> candidates;
  if (!file_location.file_id.empty()) {
    for (const auto& clip : clips) {
      if (clip.file_id == file_location.file_id) {
        candidates.push_back(&clip);
      }
    }
  }

  const std::string normalized_target = NormalizeSelectedClipName(std::string(display_clip_name));
  if (candidates.empty() && !normalized_target.empty()) {
    for (const auto& clip : clips) {
      const auto primary_name =
          !Trimmed(clip.clip_full_name).empty() ? clip.clip_full_name : clip.clip_root_name;
      if (NormalizeSelectedClipName(primary_name) == normalized_target) {
        candidates.push_back(&clip);
      }
    }
  }

  if (candidates.empty()) {
    return std::nullopt;
  }
  if (candidates.size() == 1) {
    return *candidates.front();
  }

  std::vector<const ClipInfo*> prioritized_candidates = candidates;
  if (!normalized_target.empty()) {
    std::vector<const ClipInfo*> matching_name_candidates;
    for (const auto* candidate : candidates) {
      const auto primary_name =
          !Trimmed(candidate->clip_full_name).empty() ? candidate->clip_full_name : candidate->clip_root_name;
      if (NormalizeSelectedClipName(primary_name) == normalized_target) {
        matching_name_candidates.push_back(candidate);
      }
    }
    if (matching_name_candidates.size() == 1) {
      return *matching_name_candidates.front();
    }
    if (!matching_name_candidates.empty()) {
      prioritized_candidates = matching_name_candidates;
    }
  }

  if (current_selection.has_value()) {
    const auto reference_time = NormalizeComparableTimecode(SelectTimelineReferenceTime(*current_selection));
    const auto reference_subframes = TimecodeStringToSubframes(reference_time, rate_info);
    if (!Trimmed(reference_time).empty() && reference_subframes >= 0) {
      const auto resolve_best_candidate = [&](const std::vector<const ClipInfo*>& resolution_candidates) -> const ClipInfo* {
        const ClipInfo* best_candidate = nullptr;
        long long best_distance_subframes = std::numeric_limits<long long>::max();

        for (const auto* candidate : resolution_candidates) {
          if (!candidate || Trimmed(candidate->clip_id).empty()) {
            continue;
          }

          std::optional<std::string> candidate_play_time;
          try {
            candidate_play_time =
                FindClipTimelinePlayTimeFromSelection(client, candidate->clip_id, *current_selection, rate_info);
            if (!candidate_play_time.has_value()) {
              candidate_play_time =
                  FindClipTimelinePlayTimeFromClipId(client, candidate->clip_id, reference_time, rate_info);
            }
          } catch (const std::exception&) {
            candidate_play_time = std::nullopt;
          }
          if (!candidate_play_time.has_value()) {
            continue;
          }

          const auto normalized_candidate_time = NormalizeComparableTimecode(*candidate_play_time);
          const auto candidate_subframes = TimecodeStringToSubframes(normalized_candidate_time, rate_info);
          if (Trimmed(normalized_candidate_time).empty() || candidate_subframes < 0) {
            continue;
          }

          const auto distance_subframes = std::llabs(candidate_subframes - reference_subframes);
          if (distance_subframes < best_distance_subframes) {
            best_distance_subframes = distance_subframes;
            best_candidate = candidate;
            if (best_distance_subframes == 0) {
              break;
            }
          }
        }

        return best_candidate;
      };

      if (const auto* best_candidate = resolve_best_candidate(prioritized_candidates)) {
        return *best_candidate;
      }
      if (prioritized_candidates.size() != candidates.size()) {
        if (const auto* best_candidate = resolve_best_candidate(candidates)) {
          return *best_candidate;
        }
      }
    }
  }

  return *prioritized_candidates.front();
}

std::optional<std::string> FindClipTimelinePlayTimeFromSelection(PtslClient& client,
                                                                 const std::string& clip_id,
                                                                 const TimelineSelection& selection,
                                                                 const TimeCodeRateInfo& rate_info) {
  if (Trimmed(clip_id).empty()) {
    return std::nullopt;
  }

  const auto select_start = SelectTimelineReferenceTime(selection);
  if (Trimmed(select_start).empty()) {
    return std::nullopt;
  }

  const auto select_end = [&]() {
    const auto normalized_out = NormalizeComparableTimecode(selection.out_time);
    if (!normalized_out.empty()) {
      return normalized_out;
    }
    return SubframesToTimecodeString(TimecodeStringToSubframes(select_start, rate_info) + 100, rate_info);
  }();

  try {
    for (const auto& track : client.GetSelectedTracks()) {
      const auto playlists = client.GetTrackPlaylists(track);
      const auto playlist_it = std::find_if(playlists.begin(), playlists.end(), [](const PlaylistInfo& playlist) {
        return playlist.is_target;
      });
      if (playlist_it == playlists.end()) {
        continue;
      }
      const auto elements = client.GetPlaylistElements(playlist_it->playlist_id, select_start, select_end);
      for (const auto& element : elements) {
        for (const auto& channel_clip : element.channel_clips) {
          if (channel_clip.is_null || channel_clip.clip_id.empty()) {
            continue;
          }
          if (channel_clip.clip_id == clip_id) {
            const auto play_time = !NormalizeComparableTimecode(element.play_time).empty()
                ? element.play_time
                : element.start_time;
            if (!NormalizeComparableTimecode(play_time).empty()) {
              return play_time;
            }
          }
        }
      }
    }
  } catch (const std::exception&) {
    return std::nullopt;
  }

  return std::nullopt;
}

std::optional<SelectedClipSegmentInfo> FindSelectedClipAtTimelineReference(PtslClient& client,
                                                                           const TimelineSelection& selection,
                                                                           const std::vector<ClipInfo>& clips,
                                                                           int sample_rate_hz,
                                                                           const TimeCodeRateInfo& rate_info) {
  const auto reference_time = NormalizeComparableTimecode(SelectTimelineReferenceTime(selection));
  if (Trimmed(reference_time).empty()) {
    return std::nullopt;
  }

  const auto reference_subframes = TimecodeStringToSubframes(reference_time, rate_info);
  if (reference_subframes < 0) {
    return std::nullopt;
  }

  const auto selected_tracks = client.GetSelectedTracks();
  if (selected_tracks.size() != 1) {
    return std::nullopt;
  }

  const auto& selected_track = selected_tracks.front();
  const auto playlists = client.GetTrackPlaylists(selected_track);
  const auto playlist_it = std::find_if(playlists.begin(), playlists.end(), [](const PlaylistInfo& playlist) {
    return playlist.is_target;
  });
  if (playlist_it == playlists.end()) {
    return std::nullopt;
  }

  const long long scan_radius_subframes = 100LL * static_cast<long long>(rate_info.nominal_fps);
  const auto start_tc = SubframesToTimecodeString(std::max(0LL, reference_subframes - scan_radius_subframes), rate_info);
  const auto end_tc = SubframesToTimecodeString(reference_subframes + scan_radius_subframes, rate_info);
  const auto elements = client.GetPlaylistElements(playlist_it->playlist_id, start_tc, end_tc);

  std::unordered_map<std::string, FileLocationEntry> file_locations_by_id;
  try {
    for (const auto& entry : client.GetFileLocations({})) {
      if (!Trimmed(entry.file_id).empty()) {
        file_locations_by_id[entry.file_id] = entry;
      }
    }
  } catch (const std::exception&) {
  }

  std::optional<SelectedClipSegmentInfo> best;
  long long best_distance_subframes = std::numeric_limits<long long>::max();

  for (const auto& element : elements) {
    const auto play_time = !NormalizeComparableTimecode(element.play_time).empty()
        ? NormalizeComparableTimecode(element.play_time)
        : NormalizeComparableTimecode(element.start_time);
    const auto stop_time = !NormalizeComparableTimecode(element.stop_time).empty()
        ? NormalizeComparableTimecode(element.stop_time)
        : NormalizeComparableTimecode(element.end_time);
    if (play_time.empty() || stop_time.empty()) {
      continue;
    }

    const auto element_start_subframes = TimecodeStringToSubframes(play_time, rate_info);
    const auto element_stop_subframes = TimecodeStringToSubframes(stop_time, rate_info);
    if (element_start_subframes < 0 || element_stop_subframes < 0) {
      continue;
    }

    long long distance_subframes = 0;
    if (reference_subframes < element_start_subframes) {
      distance_subframes = element_start_subframes - reference_subframes;
    } else if (reference_subframes > element_stop_subframes) {
      distance_subframes = reference_subframes - element_stop_subframes;
    }

    std::unordered_set<std::string> seen_clip_ids;
    for (const auto& channel_clip : element.channel_clips) {
      if (channel_clip.is_null || Trimmed(channel_clip.clip_id).empty()) {
        continue;
      }
      if (!seen_clip_ids.insert(channel_clip.clip_id).second) {
        continue;
      }

      const auto* clip_info = ResolveClipInfoById(clips, channel_clip.clip_id);
      if (!clip_info) {
        continue;
      }

      FileLocationEntry resolved_location;
      if (!Trimmed(clip_info->file_id).empty()) {
        const auto location_it = file_locations_by_id.find(clip_info->file_id);
        if (location_it != file_locations_by_id.end()) {
          resolved_location = location_it->second;
        }
      }
      if (Trimmed(resolved_location.path).empty() && !Trimmed(clip_info->file_path).empty()) {
        resolved_location.path = clip_info->file_path;
        resolved_location.file_id = clip_info->file_id;
        resolved_location.is_online = true;
      }
      if (Trimmed(resolved_location.path).empty()) {
        continue;
      }

      std::optional<double> src_start_seconds;
      if (clip_info->src_start_position && clip_info->src_start_time_type) {
        src_start_seconds = MediaTimePositionToSeconds(
            *clip_info->src_start_position,
            *clip_info->src_start_time_type,
            sample_rate_hz,
            rate_info);
      }

      if (!best.has_value() || distance_subframes < best_distance_subframes) {
        best_distance_subframes = distance_subframes;
        best = SelectedClipSegmentInfo{
            resolved_location,
            channel_clip.clip_id,
            ResolveClipNameFromClipId(clips, channel_clip.clip_id).value_or(""),
            "playlist_reference",
            play_time,
            reference_time,
            reference_time,
            src_start_seconds,
            std::nullopt,
            std::nullopt,
            reference_subframes,
        };
        if (distance_subframes == 0) {
          return best;
        }
      }
    }
  }

  return best;
}

std::optional<std::string> FindClipTimelinePlayTimeFromClipId(PtslClient& client,
                                                                std::string_view clip_id,
                                                                std::string_view reference_timecode,
                                                                const TimeCodeRateInfo& rate_info) {
  if (Trimmed(clip_id).empty()) {
    return std::nullopt;
  }

  const auto normalized_reference = NormalizeComparableTimecode(reference_timecode);
  if (Trimmed(normalized_reference).empty()) {
    return std::nullopt;
  }

  const auto reference_subframes = TimecodeStringToSubframes(normalized_reference, rate_info);
  if (reference_subframes < 0) {
    return std::nullopt;
  }

  struct Window {
    long long seconds;
  };

  // Try small ranges first to keep GetPlaylistElements calls cheap; expand if not found.
  static constexpr Window kWindows[] = {
      {10}, {60}, {240}, {900} // up to 15 minutes
  };

  std::optional<std::string> best;
  long long best_distance_subframes = std::numeric_limits<long long>::max();

  try {
    for (const auto& window : kWindows) {
      const long long window_subframes = window.seconds * 100LL * static_cast<long long>(rate_info.nominal_fps);
      const long long start_subframes = std::max(0LL, reference_subframes - window_subframes);
      const long long end_subframes = std::max(start_subframes, reference_subframes + window_subframes);

      const auto start_tc = SubframesToTimecodeString(start_subframes, rate_info);
      const auto end_tc = SubframesToTimecodeString(end_subframes, rate_info);

      for (const auto& track : client.GetAllTracks()) {
        const auto playlists = client.GetTrackPlaylists(track);
        // Scan across all playlists for robustness; the moved clip can land in
        // a non-target comp playlist depending on PT lane/selection state.
        for (const auto& playlist : playlists) {
          const auto elements = client.GetPlaylistElements(playlist.playlist_id, start_tc, end_tc);
          for (const auto& element : elements) {
            for (const auto& channel_clip : element.channel_clips) {
              if (channel_clip.is_null || channel_clip.clip_id.empty()) {
                continue;
              }
              if (channel_clip.clip_id != clip_id) {
                continue;
              }

              const auto play_time = !NormalizeComparableTimecode(element.play_time).empty()
                  ? element.play_time
                  : element.start_time;
              const auto normalized_candidate = NormalizeComparableTimecode(play_time);
              if (Trimmed(normalized_candidate).empty()) {
                continue;
              }

              const auto candidate_subframes = TimecodeStringToSubframes(normalized_candidate, rate_info);
              const auto distance = std::llabs(candidate_subframes - reference_subframes);
              if (distance < best_distance_subframes) {
                best_distance_subframes = distance;
                best = normalized_candidate;
                if (best_distance_subframes == 0) {
                  return best;
                }
              }
            }
          }
        }
      }

      if (best.has_value()) {
        return best;
      }
    }
  } catch (const std::exception&) {
    return best;
  }

  return best;
}

std::string BuildResolvedClipStartTimeByIdJson(std::string_view clip_id,
                                                std::string_view reference_timecode,
                                                const std::optional<std::string>& resolved_clip_start_time,
                                                std::optional<double> session_fps) {
  std::ostringstream json;
  json << '{'
       << "\"clip_id\":\"" << JsonEscape(clip_id) << "\","
       << "\"reference_timecode\":\"" << JsonEscape(reference_timecode) << '"';
  if (resolved_clip_start_time && !Trimmed(*resolved_clip_start_time).empty()) {
    json << ",\"resolved_clip_start_time\":\"" << JsonEscape(*resolved_clip_start_time) << '\"';
  } else {
    json << ",\"resolved_clip_start_time\":\"\"";
  }
  if (session_fps.has_value() && std::isfinite(*session_fps) && *session_fps > 0) {
    std::ostringstream value;
    value << std::fixed << std::setprecision(6) << *session_fps;
    json << ",\"session_fps\":" << value.str();
  }
  json << '}';
  return json.str();
}

int RunResolveClipStartTimeById(const std::string& clip_id, const std::string& reference_timecode) {
  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();

  const auto rate_info = client.GetSessionTimeCodeRateInfo();

  std::optional<double> session_fps;
  if (rate_info.actual_fps_denominator > 0) {
    const double fps = static_cast<double>(rate_info.actual_fps_numerator)
        / static_cast<double>(rate_info.actual_fps_denominator);
    if (std::isfinite(fps) && fps > 0) {
      session_fps = fps;
    }
  }

  auto resolved = FindClipTimelinePlayTimeFromClipId(client, clip_id, reference_timecode, rate_info);
  if (!resolved.has_value()) {
    std::vector<ClipInfo> clips;
    try {
      clips = client.GetClipList();
    } catch (const std::exception&) {
      clips.clear();
    }

    const auto clip_name = ResolveClipNameFromClipId(clips, clip_id).value_or("");
    if (!clip_name.empty()) {
      try {
        const auto session_edl = client.ExportSessionInfoTextForTrackEdls();
        resolved = FindClipTimelineStartFromSessionExportByName(
            session_edl,
            clip_name,
            reference_timecode,
            rate_info);
      } catch (const std::exception&) {
        resolved = std::nullopt;
      }
    }
  }

  std::cout << BuildResolvedClipStartTimeByIdJson(clip_id, reference_timecode, resolved, session_fps) << '\n';
  return 0;
}

int RunResolveClipStartTimeByIdOrName(const std::string& clip_id,
                                        const std::string& clip_name,
                                        const std::string& reference_timecode) {
  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();

  const auto rate_info = client.GetSessionTimeCodeRateInfo();

  std::optional<double> session_fps;
  if (rate_info.actual_fps_denominator > 0) {
    const double fps = static_cast<double>(rate_info.actual_fps_numerator)
        / static_cast<double>(rate_info.actual_fps_denominator);
    if (std::isfinite(fps) && fps > 0) {
      session_fps = fps;
    }
  }

  if (!Trimmed(clip_id).empty()) {
    const auto resolved = FindClipTimelinePlayTimeFromClipId(client, clip_id, reference_timecode, rate_info);
    if (resolved.has_value()) {
      std::cout << BuildResolvedClipStartTimeByIdJson(clip_id, reference_timecode, resolved, session_fps) << '\n';
      return 0;
    }
  }

  if (Trimmed(clip_name).empty()) {
    std::cout << BuildResolvedClipStartTimeByIdJson(clip_id, reference_timecode, std::nullopt, session_fps) << '\n';
    return 0;
  }

  const auto normalized_target = NormalizeSelectedClipName(clip_name);
  if (normalized_target.empty()) {
    std::cout << BuildResolvedClipStartTimeByIdJson(clip_id, reference_timecode, std::nullopt, session_fps) << '\n';
    return 0;
  }

  std::vector<ClipInfo> clips;
  try {
    clips = client.GetClipList();
  } catch (...) {
    clips.clear();
  }

  std::vector<std::string> candidate_clip_ids;
  for (const auto& clip : clips) {
    const auto primary_name = !Trimmed(clip.clip_full_name).empty() ? clip.clip_full_name : clip.clip_root_name;
    if (NormalizeSelectedClipName(primary_name) == normalized_target && !Trimmed(clip.clip_id).empty()) {
      candidate_clip_ids.push_back(clip.clip_id);
    }
  }

  // If multiple clip IDs share the same display name, prefer the one closest in timeline.
  const double actual_fps = rate_info.actual_fps_denominator > 0
      ? static_cast<double>(rate_info.actual_fps_numerator) / static_cast<double>(rate_info.actual_fps_denominator)
      : static_cast<double>(rate_info.nominal_fps);

  auto timecodeToFrameCount = [&](std::string_view tc) -> long long {
    int hh = 0, mm = 0, ss = 0, ff = 0;
    const auto s(tc.size() ? std::string(tc) : std::string());
    const auto pos1 = s.find(':');
    const auto pos2 = s.find(':', pos1 + 1);
    const auto pos3 = s.find(':', pos2 + 1);
    if (pos1 == std::string::npos || pos2 == std::string::npos || pos3 == std::string::npos) {
      return -1;
    }
    try {
      hh = std::stoi(s.substr(0, pos1));
      mm = std::stoi(s.substr(pos1 + 1, pos2 - (pos1 + 1)));
      ss = std::stoi(s.substr(pos2 + 1, pos3 - (pos2 + 1)));
      ff = std::stoi(s.substr(pos3 + 1));
    } catch (...) {
      return -1;
    }
    const double seconds_total = static_cast<double>(hh) * 3600.0 + static_cast<double>(mm) * 60.0 + static_cast<double>(ss);
    return static_cast<long long>(std::llround(static_cast<double>(ff) + seconds_total * actual_fps));
  };

  const long long reference_frames = timecodeToFrameCount(NormalizeComparableTimecode(reference_timecode));
  std::optional<std::string> best;
  long long best_distance = std::numeric_limits<long long>::max();
  for (const auto& candidate_id : candidate_clip_ids) {
    const auto resolved = FindClipTimelinePlayTimeFromClipId(client, candidate_id, reference_timecode, rate_info);
    if (!resolved.has_value()) {
      continue;
    }
    const long long candidate_frames = timecodeToFrameCount(*resolved);
    if (candidate_frames < 0 || reference_frames < 0) {
      continue;
    }
    const long long distance = std::llabs(candidate_frames - reference_frames);
    if (distance < best_distance) {
      best_distance = distance;
      best = *resolved;
    }
    if (best_distance == 0) {
      break;
    }
  }

  if (!best.has_value()) {
    try {
      const auto session_edl = client.ExportSessionInfoTextForTrackEdls();
      best = FindClipTimelineStartFromSessionExportByName(
          session_edl,
          clip_name,
          reference_timecode,
          rate_info);
    } catch (const std::exception&) {
      best = std::nullopt;
    }
  }

  std::cout << BuildResolvedClipStartTimeByIdJson(
                candidate_clip_ids.empty() ? clip_id : candidate_clip_ids.front(),
                reference_timecode,
                best,
                session_fps)
            << '\n';
  return 0;
}

void PrintUsage() {
  std::cerr << "Usage: ptsl_markers_helper <markers.txt> | --create-pt-clip-groups <markers.txt> [--avoid-overlaps] | --delete-pt-cue-clips <markers.txt> | --create-clip-group-from-selection <name> | --make-pt-clip-tracks <markers.txt> [single-generic|generic-no-overlaps|per-character] | --create-character-track <track-name> | --select-track-by-name <track-name> | --server | --ping | --get-active-protocol | --dump-session-edl | --dump-session-info-full | --dump-raw-clip-list | --dump-selected-playlist-elements | --probe-command <command-id> [request-body-json] | --jump-timecode HH:MM:SS:FF | --set-timeline-selection-range HH:MM:SS:FF HH:MM:SS:FF | --toggle-play-state | --toggle-record-enable | --consolidate-clip | --get-transport-status | --get-transport-armed | --get-session-path | --get-timeline-selection | --set-timeline-rolls <rolls-json> | --get-selected-clip-file | --get-selected-clip-segments | --write-selected-transcription-to-json-file | --list-markers | --clear-pt-markers | --edit-matching-marker <marker-edit-plan.txt> | --delete-matching-marker <marker-edit-plan.txt> | --list-tracks | --list-selected-tracks | --rename-tracks <renames.txt> | --rename-tracks-from-plan <rename-plan.txt> | --set-track-mute-state <track-mute-plan.txt> | --drop-to-take <drop-to-take-plan.txt> | --rename-selected-clip-from-current-marker-comment | --rename-selected-clip <new-name> [replace-suffix|append|prepend|replace] [separator] [--rename-file]\n";
}

int RunFromFile(const std::string& input_path) {
  const auto markers = LoadMarkers(input_path);

  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();

  int next_number = client.GetHighestMemoryLocationNumber();
  for (const auto& marker : markers) {
    next_number += 1;
    client.CreateMarker(next_number, marker);
  }

  return 0;
}

int RunCreatePtClipGroups(const std::string& input_path, bool avoid_overlaps) {
  PtslClient client;
  EnsureClientConnected(client);
  std::cout << ExecuteCreatePtClipGroupsFromFile(client, input_path, avoid_overlaps) << '\n';
  return 0;
}

int RunDeletePtCueClips(const std::string& input_path) {
  PtslClient client;
  EnsureClientConnected(client);
  std::cout << ExecuteDeletePtCueClipsFromFile(client, input_path) << '\n';
  return 0;
}

int RunCreateClipGroupFromSelection(const std::string& group_name) {
  PtslClient client;
  EnsureClientConnected(client);
  std::cout << ExecuteCreateClipGroupFromSelection(client, group_name) << '\n';
  return 0;
}

int RunMakePtClipTracks(const std::string& input_path, MakePtClipTracksMode mode) {
  PtslClient client;
  EnsureClientConnected(client);
  std::cout << ExecuteMakePtClipTracksFromFile(client, input_path, mode) << '\n';
  return 0;
}

int RunCreateCharacterTrack(const std::string& track_name) {
  PtslClient client;
  EnsureClientConnected(client);
  std::cout << ExecuteCreateCharacterTrack(client, track_name) << '\n';
  return 0;
}

int RunSelectTrackByName(const std::string& track_name) {
  PtslClient client;
  EnsureClientConnected(client);
  std::cout << ExecuteSelectTrackByName(client, track_name) << '\n';
  return 0;
}

int RunPing() {
  PtslClient client;
  EnsureClientConnected(client);
  return 0;
}

int RunGetActiveProtocol() {
  PtslClient client;
  EnsureClientConnected(client);
  std::cout << BuildStringValueJson("protocol", client.ActiveProtocolVersionString()) << '\n';
  return 0;
}

int RunDumpSessionEdl() {
  PtslClient client;
  EnsureClientConnected(client);
  std::cout << client.ExportSessionInfoTextForTrackEdls();
  return 0;
}

int RunDumpSessionInfoFull() {
  PtslClient client;
  EnsureClientConnected(client);

  const std::string tmp_path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp")
                               + "/ptsl_session_info_full.txt";
  const std::string requested_offset = std::getenv("PTSL_DUMP_LOCATION_TYPE")
      ? std::string(std::getenv("PTSL_DUMP_LOCATION_TYPE"))
      : std::string("TimeCode");
  const bool use_legacy_location_type = requested_offset.rfind("TLType_", 0) == 0;
  const char* request_field_name = use_legacy_location_type ? "location_type" : "track_offset_options";
  struct OutputTypeAttempt { const char* value; bool is_int; };
  static const OutputTypeAttempt kAttempts[] = {
    {"ESIOType_File", false},
    {"ESI_File", false},
    {"1", true},
  };

  std::string last_error;
  for (const auto& attempt : kAttempts) {
    std::ostringstream json;
    json << '{'
         << "\"include_file_list\":true,"
         << "\"include_clip_list\":true,"
         << "\"include_markers\":false,"
         << "\"include_plugin_list\":false,"
         << "\"include_track_edls\":true,"
         << "\"show_sub_frames\":true,"
         << "\"include_user_timestamps\":true,"
         << "\"track_list_type\":\"AllTracks\","
         << "\"fade_handling_type\":\"DontShowCrossfades\","
         << '"' << request_field_name << "\":\"" << JsonEscape(requested_offset) << "\","
         << "\"text_as_file_format\":\"UTF8\",";
    if (attempt.is_int) {
      json << "\"output_type\":" << attempt.value << ",";
    } else {
      json << "\"output_type\":\"" << attempt.value << "\",";
    }
    json << "\"output_path\":\"" << JsonEscape(tmp_path) << "\""
         << '}';

    const auto response = client.SendCommand("ExportSessionInfoAsText", json.str());
    try {
      client.EnsureCompleted("ExportSessionInfoAsText", response);
    } catch (const std::exception&) {
      if (!response.response_error_json.empty()) {
        if (auto msg = ExtractJsonStringField(response.response_error_json, "command_error_message")) {
          last_error = *msg;
        }
      }
      continue;
    }

    std::ifstream file(tmp_path);
    if (!file) {
      std::cerr << "[ptsl] ExportSessionInfoAsText wrote no file at " << tmp_path << "\n";
      throw std::runtime_error("Could not read the Pro Tools session tracks.");
    }
    std::string contents((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
    std::remove(tmp_path.c_str());
    std::cout << contents;
    return 0;
  }

  std::cerr << "[ptsl] ExportSessionInfoAsText failed with all output_type attempts. Last error: "
            << last_error << "\n";
  throw std::runtime_error("Could not read the Pro Tools session tracks.");
}

int RunDumpRawClipList() {
  PtslClient client;
  EnsureClientConnected(client);
  const auto response = client.SendCommand("GetClipList", "{\"pagination_request\":{\"limit\":1000,\"offset\":0}}");
  client.EnsureCompleted("GetClipList", response);
  std::cout << response.response_body_json << '\n';
  return 0;
}

int RunDumpSelectedPlaylistElements() {
  PtslClient client;
  EnsureClientConnected(client);

  const auto selected_tracks = client.GetSelectedTracks();
  if (selected_tracks.size() != 1) {
    throw std::runtime_error("Select exactly one Pro Tools track first.");
  }

  const auto selection = ResolveEffectiveTimelineSelection(client);
  auto selection_start = SelectTimelineReferenceTime(selection);
  auto selection_end = NormalizeComparableTimecode(selection.out_time);
  if (Trimmed(selection_start).empty()) {
    throw std::runtime_error("Could not resolve the current timeline selection.");
  }
  if (Trimmed(selection_end).empty()) {
    selection_end = selection_start;
  }

  const auto playlists = client.GetTrackPlaylists(selected_tracks.front());
  const auto playlist_it = std::find_if(playlists.begin(), playlists.end(), [](const PlaylistInfo& playlist) {
    return playlist.is_target;
  });
  if (playlist_it == playlists.end()) {
    throw std::runtime_error("Could not resolve the target playlist for the selected track.");
  }

  const auto elements = client.GetPlaylistElements(playlist_it->playlist_id, selection_start, selection_end);
  std::cout << "{"
            << "\"track_name\":\"" << JsonEscape(selected_tracks.front().name) << "\","
            << "\"playlist_id\":\"" << JsonEscape(playlist_it->playlist_id) << "\","
            << "\"selection_start\":\"" << JsonEscape(selection_start) << "\","
            << "\"selection_end\":\"" << JsonEscape(selection_end) << "\","
            << "\"elements\":[";
  for (std::size_t index = 0; index < elements.size(); index += 1) {
    const auto& element = elements[index];
    if (index > 0) {
      std::cout << ',';
    }
    std::cout << "{"
              << "\"start_time\":\"" << JsonEscape(element.start_time) << "\","
              << "\"play_time\":\"" << JsonEscape(element.play_time) << "\","
              << "\"stop_time\":\"" << JsonEscape(element.stop_time) << "\","
              << "\"end_time\":\"" << JsonEscape(element.end_time) << "\","
              << "\"channel_clips\":[";
    for (std::size_t clip_index = 0; clip_index < element.channel_clips.size(); clip_index += 1) {
      const auto& channel_clip = element.channel_clips[clip_index];
      if (clip_index > 0) {
        std::cout << ',';
      }
      std::cout << "{"
                << "\"is_null\":" << (channel_clip.is_null ? "true" : "false") << ","
                << "\"clip_id\":\"" << JsonEscape(channel_clip.clip_id) << "\""
                << "}";
    }
    std::cout << "]"
              << "}";
  }
  std::cout << "]}\n";
  return 0;
}

int RunProbeCommand(const std::string& command_id_raw, const std::optional<std::string>& request_body_json) {
  int command_id = 0;
  try {
    command_id = std::stoi(command_id_raw);
  } catch (...) {
    throw std::runtime_error("Invalid probe command id");
  }

  PtslClient client;
  EnsureClientConnected(client);
  const auto response = client.ProbeCommandId(
      command_id,
      request_body_json.value_or(""),
      std::chrono::milliseconds(3000));

  std::cout << "{"
            << "\"protocol\":\"" << JsonEscape(client.ActiveProtocolVersionString()) << "\","
            << "\"command_id\":" << command_id << ","
            << "\"task_status\":" << response.task_status << ","
            << "\"response_body_json\":\"" << JsonEscape(response.response_body_json) << "\","
            << "\"response_error_json\":\"" << JsonEscape(response.response_error_json) << "\""
            << "}\n";
  return 0;
}

int RunJumpTimecode(const std::string& timecode) {
  static const std::regex pattern(R"(^\d{2}:\d{2}:\d{2}[:;]\d{2}$)");
  if (!std::regex_match(timecode, pattern)) {
    throw std::runtime_error("Invalid timecode format. Expected HH:MM:SS:FF");
  }

  auto normalized_timecode = timecode;
  normalized_timecode[8] = ':';

  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();
  client.JumpToTimecode(normalized_timecode);
  return 0;
}

int RunTogglePlayState() {
  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();
  client.TogglePlayState();
  return 0;
}

int RunToggleRecordEnable() {
  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();
  client.ToggleRecordEnable();
  return 0;
}

int RunConsolidateClip() {
  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();
  client.ConsolidateClip();
  return 0;
}

int RunGetSessionPath() {
  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();
  std::cout << BuildStringValueJson("session_path", client.GetSessionPath()) << '\n';
  return 0;
}

int RunGetTransportStatus() {
  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();
  std::cout << BuildTransportStatusJson(client.GetTransportStatus()) << '\n';
  return 0;
}

int RunGetTransportArmed() {
  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();
  std::cout << BuildTransportArmedJson(client.GetTransportArmed()) << '\n';
  return 0;
}

std::optional<double> ReadSessionTimecodeFpsForTimelineSelection(PtslClient& client) {
  try {
    return ResolveSessionFps(client.GetSessionTimeCodeRateInfo());
  } catch (const std::exception& error) {
    std::cerr << "[ptsl-helper] warning: could not read session timecode rate: "
              << error.what() << "\n";
    return std::nullopt;
  }
}

std::optional<double> ReadSessionFeetFramesFpsForTimelineSelection(PtslClient& client) {
  try {
    return ResolveSessionFps(client.GetSessionFeetFramesRateInfo());
  } catch (const std::exception& error) {
    std::cerr << "[ptsl-helper] warning: could not read session feet+frames rate: "
              << error.what() << "\n";
    return std::nullopt;
  }
}

int RunGetTimelineSelection() {
  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();
  std::cout << BuildTimelineSelectionJson(
      ResolveEffectiveTimelineSelection(client),
      ReadSessionTimecodeFpsForTimelineSelection(client),
      ReadSessionFeetFramesFpsForTimelineSelection(client)) << '\n';
  return 0;
}

int RunSetTimelineRolls(const std::string& payload_json) {
  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();
  const auto pre_roll_frames = ExtractPayloadOptionalLongLongValue(
      payload_json,
      "pre_roll_frames",
      "preRollFrames",
      "pre_roll_frames");
  const auto post_roll_frames = ExtractPayloadOptionalLongLongValue(
      payload_json,
      "post_roll_frames",
      "postRollFrames",
      "post_roll_frames");
  const auto pre_roll_milliseconds = ExtractPayloadOptionalLongLongValue(
      payload_json,
      "pre_roll_milliseconds",
      "preRollMilliseconds",
      "pre_roll_milliseconds");
  const auto post_roll_milliseconds = ExtractPayloadOptionalLongLongValue(
      payload_json,
      "post_roll_milliseconds",
      "postRollMilliseconds",
      "post_roll_milliseconds");
  const auto pre_roll_enabled = ExtractPayloadOptionalBoolValue(
      payload_json,
      "pre_roll_enabled",
      "preRollEnabled");
  const auto post_roll_enabled = ExtractPayloadOptionalBoolValue(
      payload_json,
      "post_roll_enabled",
      "postRollEnabled");
  std::cout << ExecuteSetTimelineRolls(
      client,
      pre_roll_frames,
      post_roll_frames,
      pre_roll_milliseconds,
      post_roll_milliseconds,
      pre_roll_enabled,
      post_roll_enabled) << '\n';
  return 0;
}

int RunGetSelectedClipFile() {
  PtslClient client;
  EnsureClientConnected(client);
  std::cout << ExecuteGetSelectedClipFile(client) << '\n';
  return 0;
}

int RunWriteSelectedTranscriptionToJsonFile() {
  PtslClient client;
  EnsureClientConnected(client);
  std::cout << ExecuteWriteSelectedTranscriptionToJsonFile(client) << '\n';
  return 0;
}

int RunGetSelectedClipSegments() {
  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();

  const auto selected_tracks = client.GetSelectedTracks();
  if (selected_tracks.empty()) {
    if (HasDetectedTimelineOrEditRange(client)) {
      throw std::runtime_error(BuildSelectedRangeWithoutClipMessage(
          "A Pro Tools range is selected, but OverCue does not see a selected track/clip."));
    }
    throw std::runtime_error("Select one Pro Tools track containing the clips you want to analyze.");
  }
  if (selected_tracks.size() != 1) {
    throw std::runtime_error("Select exactly one Pro Tools track for multi-clip transcription.");
  }

  const auto selection = ResolveEffectiveTimelineSelection(client);
  auto selection_start = SelectTimelineReferenceTime(selection);
  auto selection_end = [&]() {
    const auto normalized_out = NormalizeComparableTimecode(selection.out_time);
    if (!normalized_out.empty()) {
      return normalized_out;
    }
    return selection_start;
  }();

  if (Trimmed(selection_start).empty() || Trimmed(selection_end).empty() || selection_start == selection_end) {
    try {
      const auto edit_bounds = client.GetEditSelectionBounds();
      const auto edit_in = NormalizeComparableTimecode(edit_bounds.in_time);
      const auto edit_out = NormalizeComparableTimecode(edit_bounds.out_time);
      if (!Trimmed(edit_in).empty() && !Trimmed(edit_out).empty() && edit_in != edit_out) {
        selection_start = edit_in;
        selection_end = edit_out;
      }
    } catch (const std::exception&) {
    }
  }

  if (Trimmed(selection_start).empty() || Trimmed(selection_end).empty() || selection_start == selection_end) {
    throw std::runtime_error("Set or select a clip range spanning the clips you want to analyze.");
  }

  int sample_rate_hz = 0;
  try {
    sample_rate_hz = client.GetSessionSampleRateHz();
  } catch (const std::exception&) {
    sample_rate_hz = 0;
  }

  TimeCodeRateInfo rate_info = {30, false, 30, 1};
  try {
    rate_info = client.GetSessionTimeCodeRateInfo();
  } catch (const std::exception&) {
    rate_info = {30, false, 30, 1};
  }

  std::optional<double> session_fps;
  if (rate_info.actual_fps_denominator > 0) {
    const double fps = static_cast<double>(rate_info.actual_fps_numerator)
        / static_cast<double>(rate_info.actual_fps_denominator);
    if (std::isfinite(fps) && fps > 0) {
      session_fps = fps;
    }
  }

  const auto timeline_locations = client.GetFileLocations({"FLTFilter_SelectedClipsTimeline"});
  const auto clips_list_locations = client.GetFileLocations({"FLTFilter_SelectedClipsClipsList"});
  if (timeline_locations.empty() && clips_list_locations.empty()) {
    throw std::runtime_error(BuildSelectedRangeWithoutClipMessage(
        "A Pro Tools range is selected, but OverCue does not see selected clip file data."));
  }

  std::unordered_map<std::string, FileLocationEntry> file_locations_by_id;
  const auto remember_location = [&](const FileLocationEntry& entry) {
    if (Trimmed(entry.file_id).empty() || Trimmed(entry.path).empty()) {
      return;
    }
    const auto existing = file_locations_by_id.find(entry.file_id);
    const int score = (entry.is_online ? 100 : 0) + 10 + (NormalizeSelectedClipName(entry.path).empty() ? 0 : 1);
    if (existing == file_locations_by_id.end()) {
      file_locations_by_id.emplace(entry.file_id, entry);
      return;
    }
    const auto& current = existing->second;
    const int current_score = (current.is_online ? 100 : 0) + 10 + (NormalizeSelectedClipName(current.path).empty() ? 0 : 1);
    if (score > current_score) {
      existing->second = entry;
    }
  };
  for (const auto& entry : timeline_locations) {
    remember_location(entry);
  }
  for (const auto& entry : clips_list_locations) {
    remember_location(entry);
  }

  std::vector<ClipInfo> clips;
  try {
    clips = client.GetClipList();
  } catch (const std::exception&) {
    clips.clear();
  }

  const auto selection_start_subframes = TimecodeStringToSubframes(selection_start, rate_info);
  const auto selection_end_subframes = std::max(
      selection_start_subframes,
      TimecodeStringToSubframes(selection_end, rate_info));
  const double session_seconds_per_subframe = session_fps.has_value() && *session_fps > 0
      ? 1.0 / (*session_fps * 100.0)
      : 0.0;
  const auto selected_clip_names = ResolveSelectedClipCurrentNames(clips_list_locations, timeline_locations, clips);
  std::vector<FileLocationEntry> preferred_locations;
  preferred_locations.reserve(timeline_locations.size() + clips_list_locations.size());
  preferred_locations.insert(preferred_locations.end(), timeline_locations.begin(), timeline_locations.end());
  preferred_locations.insert(preferred_locations.end(), clips_list_locations.begin(), clips_list_locations.end());

  const auto& selected_track = selected_tracks.front();
  std::vector<SelectedClipSegmentInfo> segments;
  try {
    const auto all_locations = client.GetFileLocations({});
    for (const auto& entry : all_locations) {
      remember_location(entry);
    }
  } catch (const std::exception&) {
  }

  // For a single selected clip, prefer the same track-specific session export path used by
  // drop-to-take. That path keys off the selected track and timeline range instead of the
  // playlist element clip id, which has proven ambiguous for trimmed clips in some sessions.
  try {
    const auto session_edl = client.ExportSessionInfoTextForTrackEdls();
    auto export_segments = BuildSelectedClipSegmentsFromSessionExport(
        session_edl,
        selected_track.name,
        selection_start_subframes,
        selection_end_subframes,
        {},
        preferred_locations,
        clips,
        file_locations_by_id,
        sample_rate_hz,
        session_fps,
        rate_info);
    MaybeAssignPreferredSessionExportSegments(segments, std::move(export_segments));
  } catch (const std::exception&) {
  }

  if (segments.empty()) {
  try {
    const auto playlists = client.GetTrackPlaylists(selected_track);
    const auto playlist_it = std::find_if(playlists.begin(), playlists.end(), [](const PlaylistInfo& playlist) {
      return playlist.is_target;
    });
    if (playlist_it == playlists.end()) {
      throw std::runtime_error("Could not resolve the target playlist for the selected track.");
    }

    const auto elements = client.GetPlaylistElements(playlist_it->playlist_id, selection_start, selection_end);
    std::unordered_set<std::string> needed_file_ids;
    for (const auto& element : elements) {
      for (const auto& channel_clip : element.channel_clips) {
        if (channel_clip.is_null || Trimmed(channel_clip.clip_id).empty()) {
          continue;
        }
        const auto* clip_info = ResolveClipInfoById(clips, channel_clip.clip_id);
        if (!clip_info || Trimmed(clip_info->file_id).empty()) {
          continue;
        }
        needed_file_ids.insert(clip_info->file_id);
      }
    }
    bool has_missing_file_locations = false;
    for (const auto& file_id : needed_file_ids) {
      if (file_locations_by_id.find(file_id) == file_locations_by_id.end()) {
        has_missing_file_locations = true;
        break;
      }
    }
    if (has_missing_file_locations) {
      try {
        const auto all_locations = client.GetFileLocations({});
        for (const auto& entry : all_locations) {
          remember_location(entry);
        }
      } catch (const std::exception&) {
      }
    }

    for (const auto& element : elements) {
      const auto play_time = !NormalizeComparableTimecode(element.play_time).empty()
          ? NormalizeComparableTimecode(element.play_time)
          : NormalizeComparableTimecode(element.start_time);
      const auto stop_time = !NormalizeComparableTimecode(element.stop_time).empty()
          ? NormalizeComparableTimecode(element.stop_time)
          : NormalizeComparableTimecode(element.end_time);
      if (play_time.empty() || stop_time.empty()) {
        continue;
      }

      const auto element_start_subframes = TimecodeStringToSubframes(play_time, rate_info);
      const auto element_stop_subframes = TimecodeStringToSubframes(stop_time, rate_info);
      if (selection_end_subframes <= element_start_subframes || selection_start_subframes >= element_stop_subframes) {
        continue;
      }

      const auto overlap_start_subframes = std::max(selection_start_subframes, element_start_subframes);
      const auto overlap_end_subframes = std::min(selection_end_subframes, element_stop_subframes);
      if (overlap_end_subframes <= overlap_start_subframes) {
        continue;
      }

      std::unordered_set<std::string> seen_clip_ids;
      for (const auto& channel_clip : element.channel_clips) {
        if (channel_clip.is_null || Trimmed(channel_clip.clip_id).empty()) {
          continue;
        }
        if (!seen_clip_ids.insert(channel_clip.clip_id).second) {
          continue;
        }

        const auto* clip_info = ResolveClipInfoById(clips, channel_clip.clip_id);
        if (!clip_info || Trimmed(clip_info->file_id).empty()) {
          continue;
        }

        FileLocationEntry resolved_location;
        const auto location_it = file_locations_by_id.find(clip_info->file_id);
        if (location_it != file_locations_by_id.end()) {
          resolved_location = location_it->second;
        } else if (!Trimmed(clip_info->file_path).empty()) {
          resolved_location.path = clip_info->file_path;
          resolved_location.file_id = clip_info->file_id;
          resolved_location.is_online = true;
        }
        if (Trimmed(resolved_location.path).empty()) {
          continue;
        }

        std::optional<double> src_start_seconds;
        if (clip_info->src_start_position && clip_info->src_start_time_type) {
          src_start_seconds = MediaTimePositionToSeconds(
              *clip_info->src_start_position,
              *clip_info->src_start_time_type,
              sample_rate_hz,
              rate_info);
        }

        std::optional<double> source_start_seconds;
        std::optional<double> source_end_seconds;
        if (src_start_seconds.has_value() && session_seconds_per_subframe > 0) {
          source_start_seconds =
              *src_start_seconds + static_cast<double>(overlap_start_subframes - element_start_subframes) * session_seconds_per_subframe;
          source_end_seconds =
              *source_start_seconds + static_cast<double>(overlap_end_subframes - overlap_start_subframes) * session_seconds_per_subframe;
        }

        segments.push_back({
            resolved_location,
            channel_clip.clip_id,
            ResolveClipNameFromClipId(clips, channel_clip.clip_id).value_or(""),
            "playlist_elements",
            play_time,
            SubframesToTimecodeString(overlap_start_subframes, rate_info),
            SubframesToTimecodeString(overlap_end_subframes, rate_info),
            src_start_seconds,
            source_start_seconds,
            source_end_seconds,
            overlap_start_subframes,
        });
      }
    }
  } catch (const std::exception&) {
    segments.clear();
  }
  }

  const bool should_try_session_export_fallback =
      segments.empty()
      || segments.size() == 1
      || CountSelectedClipSegmentsWithSourceWindows(segments) == 0
      || (!selected_clip_names.empty() && segments.size() < selected_clip_names.size());
  if (should_try_session_export_fallback) {
    try {
      const auto all_locations = client.GetFileLocations({});
      for (const auto& entry : all_locations) {
        remember_location(entry);
      }
    } catch (const std::exception&) {
    }

    try {
      const auto session_edl = client.ExportSessionInfoTextForTrackEdls();
      const auto export_clip_names = segments.size() == 1
          ? std::vector<std::string>{}
          : selected_clip_names;
      auto export_segments = BuildSelectedClipSegmentsFromSessionExport(
          session_edl,
          selected_track.name,
          selection_start_subframes,
          selection_end_subframes,
          export_clip_names,
          preferred_locations,
          clips,
          file_locations_by_id,
          sample_rate_hz,
          session_fps,
          rate_info);
      MaybeAssignPreferredSessionExportSegments(segments, std::move(export_segments));
    } catch (const std::exception&) {
    }
  }

  if (segments.empty()) {
    throw std::runtime_error(BuildSelectedRangeWithoutClipMessage(
        "A Pro Tools range is selected, but no selected clips were found on the selected track."));
  }

  std::stable_sort(segments.begin(), segments.end(), [](const SelectedClipSegmentInfo& left, const SelectedClipSegmentInfo& right) {
    if (left.segment_start_subframes != right.segment_start_subframes) {
      return left.segment_start_subframes < right.segment_start_subframes;
    }
    return left.clip_name < right.clip_name;
  });

  std::cout << BuildSelectedClipSegmentsJson(selected_track.name, segments, sample_rate_hz, session_fps) << '\n';
  return 0;
}

int RunClearPtMarkers() {
  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();

  const auto marker_numbers = client.GetMarkerMemoryLocationNumbers();
  const auto active_protocol = ParsePtslProtocolVersion(client.ActiveProtocolVersionString());
  if (SupportsClearAllMemoryLocations(active_protocol)) {
    client.ClearAllMemoryLocations();
    std::cout << "{\"cleared\":" << marker_numbers.size() << ",\"used_clear_all_memory_locations\":true}\n";
  } else {
    client.ClearMemoryLocations(marker_numbers);
    std::cout << "{\"cleared\":" << marker_numbers.size() << "}\n";
  }
  return 0;
}

int RunListMarkers() {
  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();

  std::vector<MemoryLocationInfo> markers;
  for (const auto& memory_location : client.GetMemoryLocations()) {
    if (memory_location.time_properties == "TP_Marker") {
      markers.push_back(memory_location);
    }
  }

  std::cout << BuildMarkersJson("markers", markers) << '\n';
  return 0;
}

bool ShouldUseLegacySelectedClipRename(const PtslClient& client);
std::vector<FileLocationEntry> GetSelectedClipFileLocationsForRename(
    PtslClient& client,
    const std::vector<std::string>& filters);
std::optional<std::string> ResolveSelectedClipCurrentNameFromSessionExport(
    PtslClient& client,
    const std::vector<FileLocationEntry>& clips_list_locations,
    const std::vector<FileLocationEntry>& timeline_locations,
    std::string_view fallback_name = "",
    bool allow_any_track_fallback = true);

int RunRenameSelectedClipFromCurrentMarkerComment() {
  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();

  const auto marker = ResolveCurrentMarkerForTimelineSelection(
      client.GetCurrentTimelineSelection(),
      client.GetMemoryLocations());
  if (!marker) {
    throw std::runtime_error("Could not find a Pro Tools marker at the current timeline location.");
  }

  const auto new_name = SanitizeMarkerCommentForClipName(marker->comments);
  client.RenameSelectedClip(new_name, false);
  std::cout << BuildClipRenameFromMarkerJson(*marker, new_name) << '\n';
  return 0;
}

int RunRenameSelectedClip(const std::string& raw_name,
                         std::string_view mode = "replace-suffix",
                         std::string_view separator = "-",
                         bool rename_file = false) {
  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();

  const auto clips_list_locations = GetSelectedClipFileLocationsForRename(
      client,
      {"FLTFilter_SelectedClipsClipsList"});
  const auto timeline_locations = GetSelectedClipFileLocationsForRename(
      client,
      {"FLTFilter_SelectedClipsTimeline"});
  const bool has_selected_clip_locations = !clips_list_locations.empty() || !timeline_locations.empty();

  const bool use_legacy_selected_clip_rename = ShouldUseLegacySelectedClipRename(client);
  std::vector<ClipInfo> clips;
  if (!use_legacy_selected_clip_rename) {
    try {
      clips = client.GetClipList();
    } catch (const std::exception&) {
      clips.clear();
    }
  }

  auto previous_names = ResolveSelectedClipCurrentNames(clips_list_locations, timeline_locations, clips);
  auto previous_name = !previous_names.empty()
      ? previous_names.front()
      : ResolveSelectedClipCurrentName(clips_list_locations, timeline_locations, clips);
  bool use_target_clip_rename = clips_list_locations.empty();

  if (previous_name.empty() && !use_legacy_selected_clip_rename) {
    try {
      const auto selection = ResolveEffectiveTimelineSelection(client);
      const auto rate_info = client.GetCurrentSessionTimeCodeRateInfo();
      const auto selection_start = SelectTimelineReferenceTime(selection);
      const auto selection_end = [&]() {
        const auto normalized_out = NormalizeComparableTimecode(selection.out_time);
        if (!normalized_out.empty()) {
          return normalized_out;
        }
        return selection_start;
      }();

      for (const auto& track : client.GetSelectedTracks()) {
        const auto playlists = client.GetTrackPlaylists(track);
        const auto playlist_it = std::find_if(playlists.begin(), playlists.end(), [](const PlaylistInfo& playlist) {
          return playlist.is_target;
        });
        if (playlist_it == playlists.end()) {
          continue;
        }

        const auto elements = client.GetPlaylistElements(
            playlist_it->playlist_id,
            selection_start,
            selection_end);
        const auto resolved_name = ResolveTimelineClipNameFromSelection(selection, elements, clips, rate_info);
        if (resolved_name && !resolved_name->empty()) {
          previous_name = *resolved_name;
          break;
        }
      }
    } catch (const std::exception&) {
      previous_name.clear();
    }
  }

  if (use_legacy_selected_clip_rename) {
    if (const auto session_export_name = ResolveSelectedClipCurrentNameFromSessionExport(
            client,
            clips_list_locations,
            timeline_locations,
            previous_name,
            has_selected_clip_locations)) {
      previous_name = NormalizeSelectedClipName(*session_export_name);
    }
    previous_names.assign(1, previous_name);
  } else if (previous_name.empty()) {
    if (const auto session_export_name = ResolveSelectedClipCurrentNameFromSessionExport(
            client,
            clips_list_locations,
            timeline_locations,
            previous_name,
            has_selected_clip_locations)) {
      previous_name = NormalizeSelectedClipName(*session_export_name);
    }
  }

  if (previous_name.empty()) {
    throw std::runtime_error(
        "Could not resolve a Pro Tools clip from the current clip selection or timeline selection.");
  }

  if (previous_names.empty()) {
    previous_names.push_back(previous_name);
  }

  std::vector<std::string> renamed_previous_names;
  std::vector<std::string> renamed_target_names;
  renamed_previous_names.reserve(previous_names.size());
  renamed_target_names.reserve(previous_names.size());

  for (const auto& current_name : previous_names) {
    const auto target_name = BuildClipRenameTargetName(
        current_name,
        raw_name,
        ParseClipRenameBehaviorMode(mode),
        separator);
    if (use_legacy_selected_clip_rename) {
      client.RenameSelectedClip(target_name, rename_file);
    } else if (use_target_clip_rename || previous_names.size() > 1) {
      client.RenameTargetClip(current_name, target_name, rename_file);
    } else {
      try {
        client.RenameSelectedClip(target_name, rename_file);
      } catch (const std::exception& error) {
        const std::string message = error.what();
        if (message.find("No clip is selected") == std::string::npos &&
            message.find("PT_InvalidParameter") == std::string::npos) {
          throw;
        }
        client.RenameTargetClip(current_name, target_name, rename_file);
      }
    }
    renamed_previous_names.push_back(current_name);
    renamed_target_names.push_back(target_name);
  }

  if (renamed_previous_names.size() == 1) {
    std::cout << BuildClipRenameJson(renamed_previous_names.front(), renamed_target_names.front()) << '\n';
  } else {
    std::ostringstream json;
    json << '{'
         << "\"renamed_count\":" << renamed_previous_names.size() << ','
         << "\"renames\":[";
    for (std::size_t index = 0; index < renamed_previous_names.size(); index += 1) {
      if (index > 0) {
        json << ',';
      }
      json << "{"
           << "\"previous_name\":\"" << JsonEscape(renamed_previous_names[index]) << "\","
           << "\"clip_name\":\"" << JsonEscape(renamed_target_names[index]) << "\""
           << "}";
    }
    json << "]}";
    std::cout << json.str() << '\n';
  }
  return 0;
}

int RunEditMatchingMarker(const std::string& input_path) {
  const auto plan = LoadMarkerEditPlan(input_path);

  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();

  const auto previous_name = Trimmed(plan.previous_marker.name);
  const auto previous_start = NormalizeComparableTimecode(plan.previous_marker.start_time);
  const auto previous_end = NormalizeComparableTimecode(plan.previous_marker.end_time);
  const auto previous_comments = Trimmed(plan.previous_marker.comments);

  std::vector<MemoryLocationInfo> candidates;
  for (const auto& memory_location : client.GetMemoryLocations()) {
    if (memory_location.time_properties != "TP_Marker") {
      continue;
    }
    if (Trimmed(memory_location.name) != previous_name) {
      continue;
    }
    if (NormalizeComparableTimecode(memory_location.start_time) != previous_start) {
      continue;
    }
    if (!MemoryLocationMatchesMarkerScope(memory_location, plan.previous_marker)) {
      continue;
    }
    candidates.push_back(memory_location);
  }

  auto filter_candidates = [](const std::vector<MemoryLocationInfo>& source,
                              auto&& predicate) -> std::vector<MemoryLocationInfo> {
    std::vector<MemoryLocationInfo> filtered;
    for (const auto& item : source) {
      if (predicate(item)) {
        filtered.push_back(item);
      }
    }
    return filtered;
  };

  const auto edit_matches = [&](const std::vector<MemoryLocationInfo>& matches) -> bool {
    if (matches.empty()) {
      return false;
    }

    std::vector<int> edited_numbers;
    edited_numbers.reserve(matches.size());
    for (const auto& item : matches) {
      client.EditMarker(item.number, plan.next_marker);
      edited_numbers.push_back(item.number);
    }

    std::sort(edited_numbers.begin(), edited_numbers.end());
    std::cout << "{\"edited_count\":" << edited_numbers.size() << ",\"edited_numbers\":[";
    for (std::size_t i = 0; i < edited_numbers.size(); i += 1) {
      if (i > 0) {
        std::cout << ',';
      }
      std::cout << edited_numbers[i];
    }
    std::cout << "]}\n";
    return true;
  };

  const auto exact_matches = filter_candidates(candidates, [&](const MemoryLocationInfo& item) {
    return NormalizeComparableTimecode(item.end_time) == previous_end
           && Trimmed(item.comments) == previous_comments;
  });
  if (edit_matches(exact_matches)) {
    return 0;
  }

  const auto end_matches = filter_candidates(candidates, [&](const MemoryLocationInfo& item) {
    return NormalizeComparableTimecode(item.end_time) == previous_end;
  });
  if (edit_matches(end_matches)) {
    return 0;
  }

  const auto comment_matches = filter_candidates(candidates, [&](const MemoryLocationInfo& item) {
    return Trimmed(item.comments) == previous_comments;
  });
  if (edit_matches(comment_matches)) {
    return 0;
  }

  if (edit_matches(candidates)) {
    return 0;
  }

  const auto name_matches = filter_candidates(client.GetMemoryLocations(), [&](const MemoryLocationInfo& item) {
    return item.time_properties == "TP_Marker"
           && Trimmed(item.name) == previous_name
           && MemoryLocationMatchesMarkerScope(item, plan.previous_marker);
  });

  std::optional<MemoryLocationInfo> target;
  if (name_matches.size() == 1) {
    target = name_matches.front();
  }
  if (!target) {
    throw std::runtime_error("Could not find a unique matching Pro Tools marker to edit.");
  }

  client.EditMarker(target->number, plan.next_marker);
  std::cout << "{\"edited\":" << target->number << "}\n";
  return 0;
}

int RunDeleteMatchingMarker(const std::string& input_path) {
  const auto plan = LoadMarkerEditPlan(input_path, false);

  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();

  const auto previous_name = Trimmed(plan.previous_marker.name);
  const auto previous_start = NormalizeComparableTimecode(plan.previous_marker.start_time);
  const auto previous_end = NormalizeComparableTimecode(plan.previous_marker.end_time);
  const auto previous_comments = Trimmed(plan.previous_marker.comments);

  std::vector<MemoryLocationInfo> candidates;
  for (const auto& memory_location : client.GetMemoryLocations()) {
    if (memory_location.time_properties != "TP_Marker") {
      continue;
    }
    if (Trimmed(memory_location.name) != previous_name) {
      continue;
    }
    if (NormalizeComparableTimecode(memory_location.start_time) != previous_start) {
      continue;
    }
    if (!MemoryLocationMatchesMarkerScope(memory_location, plan.previous_marker)) {
      continue;
    }
    candidates.push_back(memory_location);
  }

  auto filter_candidates = [](const std::vector<MemoryLocationInfo>& source,
                              auto&& predicate) -> std::vector<MemoryLocationInfo> {
    std::vector<MemoryLocationInfo> filtered;
    for (const auto& item : source) {
      if (predicate(item)) {
        filtered.push_back(item);
      }
    }
    return filtered;
  };

  const auto delete_matches = [&](const std::vector<MemoryLocationInfo>& matches) -> bool {
    if (matches.empty()) {
      return false;
    }

    std::vector<int> deleted_numbers;
    deleted_numbers.reserve(matches.size());
    for (const auto& item : matches) {
      deleted_numbers.push_back(item.number);
    }

    client.ClearMemoryLocations(deleted_numbers);
    std::sort(deleted_numbers.begin(), deleted_numbers.end());
    std::cout << "{\"deleted_count\":" << deleted_numbers.size() << ",\"deleted_numbers\":[";
    for (std::size_t i = 0; i < deleted_numbers.size(); i += 1) {
      if (i > 0) {
        std::cout << ',';
      }
      std::cout << deleted_numbers[i];
    }
    std::cout << "]}\n";
    return true;
  };

  const auto exact_matches = filter_candidates(candidates, [&](const MemoryLocationInfo& item) {
    return NormalizeComparableTimecode(item.end_time) == previous_end
           && Trimmed(item.comments) == previous_comments;
  });
  if (delete_matches(exact_matches)) {
    return 0;
  }

  const auto end_matches = filter_candidates(candidates, [&](const MemoryLocationInfo& item) {
    return NormalizeComparableTimecode(item.end_time) == previous_end;
  });
  if (delete_matches(end_matches)) {
    return 0;
  }

  const auto comment_matches = filter_candidates(candidates, [&](const MemoryLocationInfo& item) {
    return Trimmed(item.comments) == previous_comments;
  });
  if (delete_matches(comment_matches)) {
    return 0;
  }

  if (delete_matches(candidates)) {
    return 0;
  }

  const auto name_matches = filter_candidates(client.GetMemoryLocations(), [&](const MemoryLocationInfo& item) {
    return item.time_properties == "TP_Marker"
           && Trimmed(item.name) == previous_name
           && MemoryLocationMatchesMarkerScope(item, plan.previous_marker);
  });

  if (name_matches.size() == 1) {
    client.ClearMemoryLocations({name_matches.front().number});
    std::cout << "{\"deleted_count\":1,\"deleted_numbers\":[" << name_matches.front().number << "]}\n";
    return 0;
  }

  std::cout << "{\"deleted_count\":0,\"deleted_numbers\":[]}\n";
  return 0;
}

int RunListTracks(bool selected_only) {
  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();

  const auto tracks = selected_only ? client.GetSelectedTracks() : client.GetAllTracks();
  std::cout << BuildTracksJson("tracks", tracks) << '\n';
  return 0;
}

int RunRenameTracks(const std::string& input_path) {
  const auto renames = LoadTrackRenames(input_path);

  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();

  int renamed_count = 0;
  for (const auto& rename : renames) {
    if (rename.new_name == rename.current_name) {
      continue;
    }
    client.RenameTrack(rename);
    renamed_count += 1;
  }

  std::cout << "{\"renamed\":" << renamed_count << "}\n";
  return 0;
}

int RunRenameTracksFromPlan(const std::string& input_path) {
  const auto plan = LoadRenamePlan(input_path);

  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();

  const auto live_tracks = client.GetAllTracks();
  std::unordered_map<std::string, TrackInfo> live_track_map;
  live_track_map.reserve(live_tracks.size());
  for (const auto& track : live_tracks) {
    live_track_map.emplace(track.id, track);
  }

  bool used_primary_name_fallback = false;
  const auto* primary_track = FindLiveTrackByIdOrUniqueSavedName(
      live_tracks,
      live_track_map,
      plan.primary_track_id,
      plan.primary_track_name,
      &used_primary_name_fallback);
  if (primary_track == nullptr) {
    throw std::runtime_error(
        std::string("The saved main record track \"") + plan.primary_track_name +
        "\" was not found in the current Pro Tools session.");
  }
  if (used_primary_name_fallback) {
    std::cerr << "[track-rename] recovered primary record track by name saved_name=\""
              << plan.primary_track_name
              << "\" live_id=\"" << primary_track->id
              << "\"\n";
  }

  const auto& live_primary_track = *primary_track;
  std::vector<RenameTrackResult> results;
  results.reserve(plan.tracks.size());

  for (const auto& track : plan.tracks) {
    RenameTrackResult result;
    result.track_id = track.track_id;
    result.saved_name = track.saved_name;

    bool used_track_name_fallback = false;
    const auto* live_track = FindLiveTrackByIdOrUniqueSavedName(
        live_tracks,
        live_track_map,
        track.track_id,
        track.saved_name,
        &used_track_name_fallback);
    if (live_track == nullptr) {
      result.status = "missing";
      results.push_back(std::move(result));
      continue;
    }
    if (used_track_name_fallback) {
      std::cerr << "[track-rename] recovered captured track by name saved_name=\""
                << track.saved_name
                << "\" live_id=\"" << live_track->id
                << "\"\n";
    }

    result.live_track_id = live_track->id;
    result.current_name = live_track->name;
    result.new_name = DeriveRenamedTrackName(
        live_track->name,
        live_primary_track.name,
        plan.marker_name);

    if (result.new_name.empty()) {
      result.status = "unmatched";
      results.push_back(std::move(result));
      continue;
    }

    if (result.new_name == result.current_name) {
      result.status = "unchanged";
      results.push_back(std::move(result));
      continue;
    }

    client.RenameTrack({
        live_track->id,
        result.current_name,
        result.new_name,
    });
    result.status = "renamed";
    results.push_back(std::move(result));
  }

  std::cout << BuildRenamePlanResultJson(
      plan.marker_name,
      live_primary_track.id,
      live_primary_track.name,
      results) << '\n';
  return 0;
}

int RunSetTrackMuteState(const std::string& input_path) {
  const auto update = LoadTrackMuteStateUpdate(input_path);

  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();
  client.SetTrackMuteState(update.track_names, update.enabled);
  std::cout << "{\"updated\":" << update.track_names.size()
            << ",\"enabled\":" << (update.enabled ? "true" : "false") << "}\n";
  return 0;
}

int RunDropToTake(const std::string& input_path) {
  const auto plan = LoadDropToTakePlan(input_path);

  PtslClient client;
  if (!client.HostReadyCheck()) {
    throw std::runtime_error("Pro Tools Not Ready");
  }
  client.RegisterConnection();

  const auto run_drop_to_take = [&client](const DropToTakePlan& loaded_plan) {
    struct DropToTakeResult {
      std::vector<std::string> source_track_names;
      std::vector<std::string> target_track_names;
    };

    std::cerr << "[drop-to-take] step=get-all-tracks\n";
    const auto live_tracks = client.GetAllTracks();
    std::unordered_map<std::string, TrackInfo> live_track_map;
    live_track_map.reserve(live_tracks.size());
    for (const auto& track : live_tracks) {
      live_track_map.emplace(track.id, track);
    }

    auto resolved_record_tracks = ResolveDropToTakeRecordTracks(
        live_tracks,
        live_track_map,
        loaded_plan.tracks);

    bool used_primary_name_fallback = false;
    const auto* primary_track = FindLiveTrackByIdOrUniqueSavedName(
        live_tracks,
        live_track_map,
        loaded_plan.primary_track_id,
        loaded_plan.primary_track_name,
        &used_primary_name_fallback);
    if (primary_track == nullptr) {
      throw std::runtime_error(
          std::string("The saved main record track \"") + loaded_plan.primary_track_name +
          "\" was not found in the current Pro Tools session.");
    }
    if (used_primary_name_fallback) {
      std::cerr << "[drop-to-take] recovered primary record track by name saved_name=\""
                << loaded_plan.primary_track_name
                << "\" live_id=\"" << primary_track->id
                << "\"\n";
    }
    UsePrimaryTrackAsDropToTakeRecordFallback(
        resolved_record_tracks,
        live_tracks,
        *primary_track);
    if (!resolved_record_tracks.found_any_record) {
      throw std::runtime_error(
          "None of the captured record tracks were found in the current Pro Tools session.");
    }

    const auto& record_ids = resolved_record_tracks.ids;
    const std::size_t max_record_index = resolved_record_tracks.max_record_index;
    const std::string primary_live_name = primary_track->name;
    const std::string primary_live_type = primary_track->type;
    const std::string take_track_keyword = NormalizeDropToTakeTrackKeyword(loaded_plan.take_track_keyword);
    std::cerr << "[drop-to-take] step=get-timecode-rate primary_track=\"" << primary_live_name << "\"\n";
    const auto rate_info = client.GetCurrentSessionTimeCodeRateInfo();
    const bool use_legacy_drop_to_take_resolution = ShouldUseLegacyDropToTakeSessionExportResolution(client);
    const bool avoid_timeline_selection_reads = ShouldAvoidTimelineSelectionReadsForDropToTake(client);
    std::vector<std::string> selected_clip_names = ResolveSelectedClipNamesForDropToTake(
        client,
        use_legacy_drop_to_take_resolution);
    if (ShouldPrimeDropToTakeSourceSelection(client, rate_info, selected_clip_names)) {
      std::cerr << "[drop-to-take] step=prime-source-selection primary_track=\"" << primary_live_name << "\"\n";
      PrimeDropToTakeSourceSelection(client, primary_live_name, rate_info);
      selected_clip_names = ResolveSelectedClipNamesForDropToTake(
          client,
          use_legacy_drop_to_take_resolution);
    } else {
      std::cerr << "[drop-to-take] step=prime-source-selection primary_track=\"" << primary_live_name
                << "\" skipped=\"true\"\n";
    }

    std::cerr << "[drop-to-take] step=export-session-edl initial\n";
    const std::string session_edl = client.ExportSessionInfoTextForTrackEdls();
    if (Trimmed(session_edl).empty()) {
      throw std::runtime_error(
          "Could not read the Pro Tools session tracks. OverCue needs that to find an open Take track.");
    }

    long long overlap_start_sf = 0;
    long long overlap_end_sf = 0;
    std::optional<TimelineSelection> effective_selection_for_range;
    std::optional<std::pair<BarsBeatsPosition, BarsBeatsPosition>> selected_clip_bars_beats_range;
    std::cerr << "[drop-to-take] step=find-source-placements legacy="
              << (use_legacy_drop_to_take_resolution ? "true" : "false") << "\n";
    const bool use_selected_clip_bounds = ShouldPreferEditSelectionForDropToTake(client);
    const bool use_session_export_clip_bounds = avoid_timeline_selection_reads || use_selected_clip_bounds;
    auto source_placements = FindTrackClipPlacementsFromSessionExport(
        session_edl,
        primary_live_name,
        use_session_export_clip_bounds ? std::numeric_limits<long long>::lowest() : 0LL,
        use_session_export_clip_bounds ? std::numeric_limits<long long>::max() : 0LL,
        selected_clip_names,
        rate_info);
    if (use_session_export_clip_bounds && source_placements.empty()) {
      std::cerr << "[drop-to-take] step=find-source-placements fallback=all-clips-on-track\n";
      selected_clip_names.clear();
      source_placements = FindTrackClipPlacementsFromSessionExport(
          session_edl,
          primary_live_name,
          std::numeric_limits<long long>::lowest(),
          std::numeric_limits<long long>::max(),
          selected_clip_names,
          rate_info);
    }
    if (use_legacy_drop_to_take_resolution && !source_placements.empty()) {
      // Pro Tools 24.3 can report noisy selected-clip context. Anchor drop-to-take to the
      // primary record track's session-export overlap instead of helper-selected clip metadata.
      selected_clip_names = CollectUniqueClipNamesFromPlacements(source_placements);
    }
    if (!source_placements.empty()) {
      overlap_start_sf = std::min_element(
          source_placements.begin(),
          source_placements.end(),
          [](const SessionExportClipPlacement& left, const SessionExportClipPlacement& right) {
            return left.start_subframes < right.start_subframes;
          })->start_subframes;
      const auto end_it = std::max_element(
          source_placements.begin(),
          source_placements.end(),
          [](const SessionExportClipPlacement& left, const SessionExportClipPlacement& right) {
            return left.end_subframes < right.end_subframes;
          });
      overlap_end_sf = end_it->end_subframes;
      LogDropToTakePrimaryTrackEndTime(primary_live_name, "session_export", end_it->end_time);
    } else if (avoid_timeline_selection_reads) {
      throw std::runtime_error(
          "Could not find the recorded clip range. Select the recorded clip on the captured record track and try again.");
    } else if (!use_selected_clip_bounds) {
      std::cerr << "[drop-to-take] step=resolve-effective-selection\n";
      const auto current_selection = ResolveEffectiveTimelineSelection(client);
      effective_selection_for_range = current_selection;
      std::string rec_in = NormalizeComparableTimecode(current_selection.in_time);
      std::string rec_out = NormalizeComparableTimecode(current_selection.out_time);
      if (rec_in.empty() || rec_out.empty() || rec_in == rec_out) {
        const auto edit_bounds = client.GetEditSelectionBounds();
        rec_in = Trimmed(edit_bounds.in_time);
        rec_out = Trimmed(edit_bounds.out_time);
      }
      if (rec_in.empty() || rec_out.empty()) {
        throw std::runtime_error(
            "Could not read the edit selection time range for the recorded clips. Ensure the primary "
            "record track shows a valid clip selection (try clicking the track or re-selecting clips).");
      }
      LogDropToTakePrimaryTrackEndTime(primary_live_name, "effective_selection", rec_out);
      try {
        overlap_start_sf = TimecodeStringToSubframes(NormalizeComparableTimecode(rec_in), rate_info);
        overlap_end_sf = TimecodeStringToSubframes(NormalizeComparableTimecode(rec_out), rate_info);
      } catch (const std::exception& error) {
        throw std::runtime_error(std::string("Could not parse edit selection timecode: ") + error.what());
      }
      if (overlap_start_sf == overlap_end_sf) {
        overlap_end_sf = overlap_start_sf + 100;
      }
    } else {
      throw std::runtime_error(
          "Could not find the recorded clip range. Select the recorded clip on the captured record track and try again.");
    }
    if (!avoid_timeline_selection_reads) {
      const auto selection_guard_range = effective_selection_for_range.has_value()
          ? ResolveDropToTakeSelectionRangeSubframes(*effective_selection_for_range, rate_info)
          : ResolveDropToTakeGuardSelectionRangeSubframes(client, rate_info);
      if (selection_guard_range) {
        overlap_start_sf = std::min(overlap_start_sf, selection_guard_range->first);
        overlap_end_sf = std::max(overlap_end_sf, selection_guard_range->second);
      }
    }
    const auto derived_start_timecode = SubframesToTimecodeString(
        std::min(overlap_start_sf, overlap_end_sf),
        rate_info);
    const auto derived_end_timecode = SubframesToTimecodeString(
        std::max(overlap_start_sf, overlap_end_sf),
        rate_info);
    std::cerr << "[drop-to-take] step=set-derived-selection start=\"" << derived_start_timecode
              << "\" end=\"" << derived_end_timecode << "\"\n";
    if (effective_selection_for_range.has_value()) {
      client.SetTimelineSelectionRange(
          derived_start_timecode,
          derived_end_timecode,
          *effective_selection_for_range);
    } else {
      client.SetTimelineSelectionRange(
          derived_start_timecode,
          derived_end_timecode,
          avoid_timeline_selection_reads);
    }

    if (!avoid_timeline_selection_reads
        && !(use_selected_clip_bounds && !source_placements.empty())) {
      try {
        const auto bars_beats_selection = client.GetTimelineSelectionBarsBeats();
        const auto start = ParseBarsBeatsPosition(bars_beats_selection.in_time);
        const auto end = ParseBarsBeatsPosition(bars_beats_selection.out_time);
        if (start && end) {
          selected_clip_bars_beats_range = std::make_pair(*start, *end);
        }
      } catch (const std::exception&) {
        selected_clip_bars_beats_range = std::nullopt;
      }
    }
    if (!selected_clip_bars_beats_range.has_value() && source_placements.empty()) {
      for (const auto& selected_clip_name : selected_clip_names) {
        selected_clip_bars_beats_range = FindClipBarsBeatsRangeOnTrackFromSessionExport(
            session_edl,
            primary_live_name,
            selected_clip_name);
        if (!selected_clip_bars_beats_range.has_value()) {
          selected_clip_bars_beats_range = FindClipBarsBeatsRangeFromSessionExport(session_edl, selected_clip_name);
        }
        if (selected_clip_bars_beats_range.has_value()) {
          break;
        }
      }
    }

    const auto source_tracks = ResolveDropToTakeSourceTracksForRange(
        live_tracks,
        record_ids,
        *primary_track,
        session_edl,
        selected_clip_bars_beats_range,
        overlap_start_sf,
        overlap_end_sf,
        rate_info);
    const bool use_channel_aware_selection = CanUseDropToTakeChannelAwareSelection(source_tracks);
    const auto source_track_names = ResolveDropToTakeSourceTrackNames(
        source_tracks,
        primary_live_name,
        use_channel_aware_selection);
    const int source_channel_count = use_channel_aware_selection
        ? SumTrackChannelCounts(source_tracks)
        : 1;
    std::cerr << "[drop-to-take] step=resolve-source-tracks"
              << " tracks=" << BuildJsonStringArray(source_track_names)
              << " recorded_channels=\"" << source_channel_count << "\""
              << " channel_aware=\"" << (use_channel_aware_selection ? "true" : "false") << "\"\n";

    std::cerr << "[drop-to-take] step=find-target-tracks\n";
    const auto target_search = FindDropToTakeTargetTracks(
        live_tracks,
        max_record_index,
        record_ids,
        loaded_plan.placement_mode,
        take_track_keyword,
        primary_live_type,
        session_edl,
        selected_clip_bars_beats_range,
        overlap_start_sf,
        overlap_end_sf,
        rate_info,
        source_channel_count);
    const auto target_track_names = TrackNamesFromInfos(target_search.targets);
    if (target_track_names.empty()) {
      throw std::runtime_error(BuildDropToTakeNoTargetTrackMessage(
          take_track_keyword,
          primary_live_type,
          source_channel_count));
    }

    std::cerr << "[drop-to-take] step=cut-selected-clips source_tracks="
              << BuildJsonStringArray(source_track_names) << "\n";
    CutSelectedClipsOnTracksWithRetry(
        client,
        source_track_names,
        selected_clip_bars_beats_range,
        overlap_start_sf,
        overlap_end_sf,
        rate_info);

    std::cerr << "[drop-to-take] step=export-session-edl pre-paste\n";
    const std::string pre_paste_session_edl = client.ExportSessionInfoTextForTrackEdls();
    const auto overlapping_target_track = FirstTrackOverlappingRange(
        pre_paste_session_edl,
        target_track_names,
        selected_clip_bars_beats_range,
        overlap_start_sf,
        overlap_end_sf,
        rate_info);
    if (overlapping_target_track) {
      throw std::runtime_error(
          std::string("Take track \"")
          + *overlapping_target_track
          + "\" already has a clip in this time range. Choose an open Take track range and try again.");
    }

    std::cerr << "[drop-to-take] step=paste target_tracks="
              << BuildJsonStringArray(target_track_names) << "\n";
    SelectTracksByNameReplaceAndWait(client, target_track_names, "target");
    client.Paste();

    std::cerr << "[drop-to-take] step=done source_tracks=" << BuildJsonStringArray(source_track_names)
              << " target_tracks=" << BuildJsonStringArray(target_track_names) << "\n";
    return DropToTakeResult{
        source_track_names,
        target_track_names,
    };
  };

  const auto result = [&]() {
    try {
      return run_drop_to_take(plan);
    } catch (const std::exception& error) {
      RecoverPtslSessionAfterError(client, "drop-to-take", error.what());
      throw;
    }
  }();
  const auto primary_source = result.source_track_names.empty() ? std::string() : result.source_track_names.front();
  const auto primary_target = result.target_track_names.empty() ? std::string() : result.target_track_names.front();
  std::cout << "{\"status\":\"ok\",\"source_track\":\"" << JsonEscape(primary_source)
            << "\",\"source_tracks\":" << BuildJsonStringArray(result.source_track_names)
            << ",\"target_track\":\"" << JsonEscape(primary_target)
            << "\",\"target_tracks\":" << BuildJsonStringArray(result.target_track_names)
            << "}\n";
  return 0;
}

void EnsureClientConnected(PtslClient& client) {
  if (!client.HostReadyCheck()) {
    client.ClearSession();
    throw std::runtime_error(client.LastHostReadyError());
  }
  if (!client.HasSession()) {
    client.RegisterConnection();
  }
}

bool ShouldResetPtslSessionForError(std::string_view message) {
  const auto normalized = LowercaseAscii(Trimmed(message));
  static const char* const kPatterns[] = {
      "failed to connect to remote host",
      "failed to connect to all addresses",
      "connection refused",
      "grpc status: 14",
      "grpc_status:14",
      "grpc_status: 14",
      "pro tools not ready",
      "pro tools is not available",
      "there is no open session",
      "no open session",
      "pt_noopenedsession",
      "registerconnection failed",
      "poll events start call failed",
      "deadline exceeded",
      "timed out",
  };
  for (const auto* pattern : kPatterns) {
    if (normalized.find(pattern) != std::string::npos) {
      return true;
    }
  }
  return false;
}

void RecoverPtslSessionAfterError(PtslClient& client,
                                  std::string_view context_label,
                                  std::string_view message) {
  if (!ShouldResetPtslSessionForError(message)) {
    return;
  }

  client.ClearSession();

  constexpr int kRecoveryAttempts = 12;
  constexpr auto kRecoveryDelay = std::chrono::milliseconds(250);
  for (int attempt = 0; attempt < kRecoveryAttempts; attempt += 1) {
    if (client.HostReadyCheck()) {
      std::cerr << "[ptsl-recovery] Recovered Pro Tools host readiness after "
                << context_label
                << " failure.\n";
      client.ClearSession();
      return;
    }
    if (attempt + 1 < kRecoveryAttempts) {
      std::this_thread::sleep_for(kRecoveryDelay);
    }
  }

  std::cerr << "[ptsl-recovery] Pro Tools did not become ready again after "
            << context_label
            << " failure.\n";
}

std::string ExtractPayloadStringValue(const std::string& payload_json,
                                      const char* snake_key,
                                      const char* camel_key = nullptr) {
  auto value = ExtractJsonStringField(payload_json, snake_key);
  if ((!value || value->empty()) && camel_key) {
    value = ExtractJsonStringField(payload_json, camel_key);
  }
  return value ? *value : "";
}

std::string RequirePayloadStringValue(const std::string& payload_json,
                                      const char* snake_key,
                                      const char* camel_key,
                                      const char* label) {
  const auto value = Trimmed(ExtractPayloadStringValue(payload_json, snake_key, camel_key));
  if (value.empty()) {
    throw std::runtime_error(std::string("Missing request field: ") + label);
  }
  return value;
}

bool ExtractPayloadBoolValue(const std::string& payload_json,
                             const char* snake_key,
                             const char* camel_key,
                             bool default_value = false) {
  auto value = ExtractJsonBoolField(payload_json, snake_key);
  if (!value && camel_key) {
    value = ExtractJsonBoolField(payload_json, camel_key);
  }
  return value.value_or(default_value);
}

std::optional<bool> ExtractPayloadOptionalBoolValue(const std::string& payload_json,
                                                    const char* snake_key,
                                                    const char* camel_key) {
  auto value = ExtractJsonBoolField(payload_json, snake_key);
  if (!value && camel_key) {
    value = ExtractJsonBoolField(payload_json, camel_key);
  }
  return value;
}

std::optional<long long> ExtractPayloadOptionalLongLongValue(const std::string& payload_json,
                                                             const char* snake_key,
                                                             const char* camel_key,
                                                             const char* label) {
  auto value = ExtractJsonScalarField(payload_json, snake_key);
  if (!value && camel_key) {
    value = ExtractJsonScalarField(payload_json, camel_key);
  }
  if (!value) {
    return std::nullopt;
  }

  const auto trimmed = Trimmed(*value);
  if (trimmed.empty() || LowercaseAscii(trimmed) == "null") {
    return std::nullopt;
  }

  std::size_t parsed_length = 0;
  long long parsed_value = 0;
  try {
    parsed_value = std::stoll(trimmed, &parsed_length, 10);
  } catch (const std::exception&) {
    throw std::runtime_error(std::string("Invalid request field: ") + label);
  }
  if (parsed_length != trimmed.size()) {
    throw std::runtime_error(std::string("Invalid request field: ") + label);
  }
  return parsed_value;
}

std::string EventSubscriptionKey(const EventSubscription& subscription) {
  return subscription.event_id + '\x1f' + subscription.event_data_json;
}

std::vector<EventSubscription> ParseEventSubscriptionsPayload(const std::string& payload_json) {
  auto subscriptions_json = ExtractJsonArrayField(payload_json, "subscriptions");
  if (!subscriptions_json) {
    subscriptions_json = ExtractJsonArrayField(payload_json, "events");
  }
  if (!subscriptions_json) {
    return {};
  }

  std::vector<EventSubscription> subscriptions;
  std::unordered_set<std::string> seen_keys;
  for (const auto& object_json : ExtractTopLevelJsonObjects(*subscriptions_json)) {
    const auto event_id = Trimmed(ExtractPayloadStringValue(object_json, "event_id", "eventId"));
    if (event_id.empty()) {
      continue;
    }
    EventSubscription subscription;
    subscription.event_id = event_id;
    subscription.event_data_json = ExtractPayloadStringValue(object_json, "event_data_json", "eventDataJson");
    const auto key = EventSubscriptionKey(subscription);
    if (!seen_keys.insert(key).second) {
      continue;
    }
    subscriptions.push_back(std::move(subscription));
  }
  return subscriptions;
}

std::string BuildEventSubscriptionsSnapshotJson(const std::vector<EventSubscription>& subscriptions) {
  std::ostringstream json;
  json << "{\"subscriptions\":[";
  for (std::size_t index = 0; index < subscriptions.size(); index += 1) {
    if (index > 0) {
      json << ',';
    }
    json << '{'
         << "\"event_id\":\"" << JsonEscape(subscriptions[index].event_id) << "\","
         << "\"event_data_json\":\"" << JsonEscape(subscriptions[index].event_data_json) << "\""
         << '}';
  }
  json << "]}";
  return json.str();
}

std::string ExecuteCreateMarkersFromFile(PtslClient& client, const std::string& input_path) {
  const auto markers = LoadMarkers(input_path);
  int next_number = client.GetHighestMemoryLocationNumber();
  for (const auto& marker : markers) {
    next_number += 1;
    client.CreateMarker(next_number, marker);
  }
  return "";
}

std::string ExecutePing(PtslClient&) {
  return "";
}

std::string ExecuteJumpTimecode(PtslClient& client, const std::string& timecode) {
  static const std::regex pattern(R"(^\d{2}:\d{2}:\d{2}[:;]\d{2}$)");
  if (!std::regex_match(timecode, pattern)) {
    throw std::runtime_error("Invalid timecode format. Expected HH:MM:SS:FF");
  }

  auto normalized_timecode = timecode;
  normalized_timecode[8] = ':';
  client.JumpToTimecode(normalized_timecode);
  return "";
}

std::string ExecuteSetTimelineSelectionRange(PtslClient& client,
                                             const std::string& start_timecode,
                                             const std::string& end_timecode) {
  static const std::regex pattern(R"(^\d{2}:\d{2}:\d{2}[:;]\d{2}$)");
  if (!std::regex_match(start_timecode, pattern) || !std::regex_match(end_timecode, pattern)) {
    throw std::runtime_error("Invalid timecode format. Expected HH:MM:SS:FF");
  }

  auto normalized_start_timecode = start_timecode;
  auto normalized_end_timecode = end_timecode;
  normalized_start_timecode[8] = ':';
  normalized_end_timecode[8] = ':';
  client.SetTimelineSelectionRange(normalized_start_timecode, normalized_end_timecode);
  return BuildTimelineSelectionJson(ResolveEffectiveTimelineSelection(client));
}

std::string ExecuteTogglePlayState(PtslClient& client) {
  client.TogglePlayState();
  return "";
}

std::string ExecuteToggleRecordEnable(PtslClient& client) {
  client.ToggleRecordEnable();
  return "";
}

std::string ExecuteConsolidateClip(PtslClient& client) {
  client.ConsolidateClip();
  return "";
}

std::string ExecuteGetSessionPath(PtslClient& client) {
  return BuildStringValueJson("session_path", client.GetSessionPath());
}

std::string ExecuteGetTransportStatus(PtslClient& client) {
  return BuildTransportStatusJson(client.GetTransportStatus());
}

std::string ExecuteGetTransportArmed(PtslClient& client) {
  return BuildTransportArmedJson(client.GetTransportArmed());
}

std::string ExecuteGetTimelineSelection(PtslClient& client) {
  return BuildTimelineSelectionJson(
      ResolveEffectiveTimelineSelection(client),
      ReadSessionTimecodeFpsForTimelineSelection(client),
      ReadSessionFeetFramesFpsForTimelineSelection(client));
}

std::string ExecuteSetTimelineRolls(PtslClient& client,
                                    const std::optional<long long>& pre_roll_frames,
                                    const std::optional<long long>& post_roll_frames,
                                    const std::optional<long long>& pre_roll_milliseconds,
                                    const std::optional<long long>& post_roll_milliseconds,
                                    const std::optional<bool>& pre_roll_enabled,
                                    const std::optional<bool>& post_roll_enabled) {
  if (!pre_roll_frames.has_value()
      && !post_roll_frames.has_value()
      && !pre_roll_milliseconds.has_value()
      && !post_roll_milliseconds.has_value()
      && !pre_roll_enabled.has_value()
      && !post_roll_enabled.has_value()) {
    return BuildTimelineSelectionJson(ResolveEffectiveTimelineSelection(client));
  }

  const auto current_selection = ResolveEffectiveTimelineSelection(client);
  const auto rate_info = client.GetSessionTimeCodeRateInfo();
  client.SetTimelineRolls(
      current_selection,
      rate_info,
      pre_roll_frames,
      post_roll_frames,
      pre_roll_milliseconds,
      post_roll_milliseconds,
      pre_roll_enabled,
      post_roll_enabled);
  return BuildTimelineSelectionJson(ResolveEffectiveTimelineSelection(client));
}

std::string ExecuteResolveClipStartTimeById(PtslClient& client,
                                            const std::string& clip_id,
                                            const std::string& reference_timecode) {
  const auto rate_info = client.GetSessionTimeCodeRateInfo();

  std::optional<double> session_fps;
  if (rate_info.actual_fps_denominator > 0) {
    const double fps = static_cast<double>(rate_info.actual_fps_numerator)
        / static_cast<double>(rate_info.actual_fps_denominator);
    if (std::isfinite(fps) && fps > 0) {
      session_fps = fps;
    }
  }

  auto resolved = FindClipTimelinePlayTimeFromClipId(client, clip_id, reference_timecode, rate_info);
  if (!resolved.has_value()) {
    std::vector<ClipInfo> clips;
    try {
      clips = client.GetClipList();
    } catch (const std::exception&) {
      clips.clear();
    }

    const auto clip_name = ResolveClipNameFromClipId(clips, clip_id).value_or("");
    if (!clip_name.empty()) {
      try {
        const auto session_edl = client.ExportSessionInfoTextForTrackEdls();
        resolved = FindClipTimelineStartFromSessionExportByName(
            session_edl,
            clip_name,
            reference_timecode,
            rate_info);
      } catch (const std::exception&) {
        resolved = std::nullopt;
      }
    }
  }

  return BuildResolvedClipStartTimeByIdJson(clip_id, reference_timecode, resolved, session_fps);
}

std::string ExecuteResolveClipStartTimeByIdOrName(PtslClient& client,
                                                  const std::string& clip_id,
                                                  const std::string& clip_name,
                                                  const std::string& reference_timecode) {
  const auto rate_info = client.GetSessionTimeCodeRateInfo();

  std::optional<double> session_fps;
  if (rate_info.actual_fps_denominator > 0) {
    const double fps = static_cast<double>(rate_info.actual_fps_numerator)
        / static_cast<double>(rate_info.actual_fps_denominator);
    if (std::isfinite(fps) && fps > 0) {
      session_fps = fps;
    }
  }

  if (!Trimmed(clip_id).empty()) {
    const auto resolved = FindClipTimelinePlayTimeFromClipId(client, clip_id, reference_timecode, rate_info);
    if (resolved.has_value()) {
      return BuildResolvedClipStartTimeByIdJson(clip_id, reference_timecode, resolved, session_fps);
    }
  }

  if (Trimmed(clip_name).empty()) {
    return BuildResolvedClipStartTimeByIdJson(clip_id, reference_timecode, std::nullopt, session_fps);
  }

  const auto normalized_target = NormalizeSelectedClipName(clip_name);
  if (normalized_target.empty()) {
    return BuildResolvedClipStartTimeByIdJson(clip_id, reference_timecode, std::nullopt, session_fps);
  }

  std::vector<ClipInfo> clips;
  try {
    clips = client.GetClipList();
  } catch (...) {
    clips.clear();
  }

  std::vector<std::string> candidate_clip_ids;
  for (const auto& clip : clips) {
    const auto primary_name = !Trimmed(clip.clip_full_name).empty() ? clip.clip_full_name : clip.clip_root_name;
    if (NormalizeSelectedClipName(primary_name) == normalized_target && !Trimmed(clip.clip_id).empty()) {
      candidate_clip_ids.push_back(clip.clip_id);
    }
  }

  const double actual_fps = rate_info.actual_fps_denominator > 0
      ? static_cast<double>(rate_info.actual_fps_numerator) / static_cast<double>(rate_info.actual_fps_denominator)
      : static_cast<double>(rate_info.nominal_fps);

  auto timecode_to_frame_count = [&](std::string_view tc) -> long long {
    int hh = 0;
    int mm = 0;
    int ss = 0;
    int ff = 0;
    const auto s(tc.size() ? std::string(tc) : std::string());
    const auto pos1 = s.find(':');
    const auto pos2 = s.find(':', pos1 + 1);
    const auto pos3 = s.find(':', pos2 + 1);
    if (pos1 == std::string::npos || pos2 == std::string::npos || pos3 == std::string::npos) {
      return -1;
    }
    try {
      hh = std::stoi(s.substr(0, pos1));
      mm = std::stoi(s.substr(pos1 + 1, pos2 - (pos1 + 1)));
      ss = std::stoi(s.substr(pos2 + 1, pos3 - (pos2 + 1)));
      ff = std::stoi(s.substr(pos3 + 1));
    } catch (...) {
      return -1;
    }
    const double seconds_total =
        static_cast<double>(hh) * 3600.0 + static_cast<double>(mm) * 60.0 + static_cast<double>(ss);
    return static_cast<long long>(std::llround(static_cast<double>(ff) + seconds_total * actual_fps));
  };

  const long long reference_frames = timecode_to_frame_count(NormalizeComparableTimecode(reference_timecode));
  std::optional<std::string> best;
  long long best_distance = std::numeric_limits<long long>::max();
  for (const auto& candidate_id : candidate_clip_ids) {
    const auto resolved = FindClipTimelinePlayTimeFromClipId(client, candidate_id, reference_timecode, rate_info);
    if (!resolved.has_value()) {
      continue;
    }
    const long long candidate_frames = timecode_to_frame_count(*resolved);
    if (candidate_frames < 0 || reference_frames < 0) {
      continue;
    }
    const long long distance = std::llabs(candidate_frames - reference_frames);
    if (distance < best_distance) {
      best_distance = distance;
      best = *resolved;
    }
    if (best_distance == 0) {
      break;
    }
  }

  if (!best.has_value()) {
    try {
      const auto session_edl = client.ExportSessionInfoTextForTrackEdls();
      best = FindClipTimelineStartFromSessionExportByName(
          session_edl,
          clip_name,
          reference_timecode,
          rate_info);
    } catch (const std::exception&) {
      best = std::nullopt;
    }
  }

  return BuildResolvedClipStartTimeByIdJson(
      candidate_clip_ids.empty() ? clip_id : candidate_clip_ids.front(),
      reference_timecode,
      best,
      session_fps);
}

std::string ExecuteGetSelectedClipFile(PtslClient& client) {
  const auto clips_list_locations = client.GetFileLocations({"FLTFilter_SelectedClipsClipsList"});
  const auto timeline_locations = client.GetFileLocations({"FLTFilter_SelectedClipsTimeline"});
  const auto active_protocol = ParsePtslProtocolVersion(client.ActiveProtocolVersionString());
  const bool use_legacy_24_3_resolution = active_protocol.has_value()
      && NormalizedPtslReleaseMajor(*active_protocol) == 24
      && active_protocol->minor == 3;
  auto selected_file_location = use_legacy_24_3_resolution
      ? ResolvePreferredSelectedClipLocation(timeline_locations, clips_list_locations)
      : ResolvePreferredSelectedClipLocation(clips_list_locations, timeline_locations);
  if (!selected_file_location && !use_legacy_24_3_resolution) {
    throw std::runtime_error("Could not resolve a file for the selected Pro Tools clip.");
  }

  std::vector<ClipInfo> clips;
  if (!use_legacy_24_3_resolution) {
    try {
      clips = client.GetClipList();
    } catch (const std::exception&) {
      clips.clear();
    }
  }

  int sample_rate_hz = 0;
  try {
    sample_rate_hz = client.GetSessionSampleRateHz();
  } catch (const std::exception&) {
    sample_rate_hz = 0;
  }

  TimeCodeRateInfo rate_info = {30, false, 30, 1};
  try {
    rate_info = client.GetSessionTimeCodeRateInfo();
  } catch (const std::exception&) {
    rate_info = {30, false, 30, 1};
  }

  std::optional<std::string> clip_id;
  std::optional<std::string> clip_start_time;
  std::optional<double> src_start_seconds;
  std::optional<double> session_fps;
  TimelineSelection current_selection;
  bool has_current_selection = false;

  if (rate_info.actual_fps_denominator > 0) {
    const double fps = static_cast<double>(rate_info.actual_fps_numerator)
        / static_cast<double>(rate_info.actual_fps_denominator);
    if (std::isfinite(fps) && fps > 0) {
      session_fps = fps;
    }
  }

  try {
    current_selection = ResolveEffectiveTimelineSelection(client);
    has_current_selection = true;
  } catch (const std::exception&) {
    has_current_selection = false;
  }

  const auto current_selection_opt = has_current_selection
      ? std::make_optional(current_selection)
      : std::optional<TimelineSelection>{};

  std::string clip_name = selected_file_location
      ? ResolveSelectedClipDisplayName(*selected_file_location, clips)
      : std::string();
  if (use_legacy_24_3_resolution && has_current_selection) {
    bool resolved_from_session_export = false;
    try {
      const auto selected_tracks = client.GetSelectedTracks();
      if (!selected_tracks.empty()) {
        const auto selection_start = SelectTimelineReferenceTime(current_selection);
        const auto selection_end = [&]() {
          const auto normalized_out = NormalizeComparableTimecode(current_selection.out_time);
          if (!normalized_out.empty()) {
            return normalized_out;
          }
          return selection_start;
        }();
        if (!Trimmed(selection_start).empty() && !Trimmed(selection_end).empty()) {
          const auto selection_start_subframes = TimecodeStringToSubframes(selection_start, rate_info);
          const auto selection_end_subframes = std::max(
              selection_start_subframes,
              TimecodeStringToSubframes(selection_end, rate_info));
          const auto session_edl = client.ExportSessionInfoTextForTrackEdls();
          std::vector<FileLocationEntry> preferred_locations;
          preferred_locations.reserve(timeline_locations.size() + clips_list_locations.size());
          preferred_locations.insert(preferred_locations.end(), timeline_locations.begin(), timeline_locations.end());
          preferred_locations.insert(preferred_locations.end(), clips_list_locations.begin(), clips_list_locations.end());

          std::unordered_map<std::string, FileLocationEntry> file_locations_by_id;
          const auto remember_location = [&](const FileLocationEntry& entry) {
            if (Trimmed(entry.file_id).empty() || Trimmed(entry.path).empty()) {
              return;
            }
            const auto existing = file_locations_by_id.find(entry.file_id);
            const int score = (entry.is_online ? 100 : 0) + 10 + (NormalizeSelectedClipName(entry.path).empty() ? 0 : 1);
            if (existing == file_locations_by_id.end()) {
              file_locations_by_id.emplace(entry.file_id, entry);
              return;
            }
            const auto& current = existing->second;
            const int current_score =
                (current.is_online ? 100 : 0) + 10 + (NormalizeSelectedClipName(current.path).empty() ? 0 : 1);
            if (score > current_score) {
              existing->second = entry;
            }
          };
          for (const auto& entry : preferred_locations) {
            remember_location(entry);
          }
          try {
            const auto all_locations = client.GetFileLocations({});
            preferred_locations.insert(preferred_locations.end(), all_locations.begin(), all_locations.end());
            for (const auto& entry : all_locations) {
              remember_location(entry);
            }
          } catch (const std::exception&) {
          }

          auto export_segments = BuildSelectedClipSegmentsFromSessionExport(
              session_edl,
              selected_tracks.front().name,
              selection_start_subframes,
              selection_end_subframes,
              {},
              preferred_locations,
              clips,
              file_locations_by_id,
              sample_rate_hz,
              session_fps,
              rate_info);
          if (export_segments.size() == 1) {
            selected_file_location = export_segments.front().file_location;
            clip_name = ChooseMoreSpecificClipName(clip_name, export_segments.front().clip_name);
            if (!Trimmed(export_segments.front().clip_start_time).empty()) {
              clip_start_time = export_segments.front().clip_start_time;
            }
            if (export_segments.front().src_start_seconds.has_value()) {
              src_start_seconds = export_segments.front().src_start_seconds;
            }
            resolved_from_session_export = true;
          }
        }
      }
    } catch (const std::exception&) {
    }
    if (!resolved_from_session_export) {
      selected_file_location = std::nullopt;
      clip_name.clear();
      clip_start_time = std::nullopt;
      src_start_seconds = std::nullopt;
    }
  }

  if (!selected_file_location) {
    throw std::runtime_error("Could not resolve a file for the selected Pro Tools clip.");
  }

  if (!use_legacy_24_3_resolution) {
    if (const auto clip_info =
            ResolveClipInfoForSelectedClip(client, *selected_file_location, clip_name, clips, current_selection_opt, rate_info)) {
      if (!clip_info->clip_id.empty()) {
        clip_id = clip_info->clip_id;
      }
      if (clip_info->src_start_position && clip_info->src_start_time_type) {
        src_start_seconds =
            MediaTimePositionToSeconds(*clip_info->src_start_position, *clip_info->src_start_time_type, sample_rate_hz, rate_info);
      }
      if (!clip_info->clip_id.empty()) {
        try {
          if (has_current_selection) {
            clip_start_time =
                FindClipTimelinePlayTimeFromSelection(client, clip_info->clip_id, current_selection, rate_info);
            if (!clip_start_time.has_value()) {
              const auto reference_time = SelectTimelineReferenceTime(current_selection);
              if (!Trimmed(reference_time).empty()) {
                clip_start_time =
                    FindClipTimelinePlayTimeFromClipId(client, clip_info->clip_id, reference_time, rate_info);
              }
            }
          }
        } catch (const std::exception&) {
          clip_start_time = std::nullopt;
        }
      }
    }
  }

  if (!clip_start_time.has_value() && has_current_selection && !Trimmed(clip_name).empty()) {
    const auto reference_time = SelectTimelineReferenceTime(current_selection);
    if (!Trimmed(reference_time).empty()) {
      try {
        const auto session_edl = client.ExportSessionInfoTextForTrackEdls();
        clip_start_time = FindClipTimelineStartFromSessionExportByName(
            session_edl,
            clip_name,
            reference_time,
            rate_info);
      } catch (const std::exception&) {
        clip_start_time = std::nullopt;
      }
    }
  }

  std::optional<double> source_start_seconds;
  std::optional<double> source_end_seconds;
  if (has_current_selection) {
    try {
      const auto selected_tracks = client.GetSelectedTracks();
      if (!selected_tracks.empty()) {
        const auto selection_start = SelectTimelineReferenceTime(current_selection);
        const auto selection_end = [&]() {
          const auto normalized_out = NormalizeComparableTimecode(current_selection.out_time);
          if (!normalized_out.empty()) {
            return normalized_out;
          }
          return selection_start;
        }();
        if (!Trimmed(selection_start).empty() && !Trimmed(selection_end).empty() && selection_start != selection_end) {
          const auto selection_start_subframes = TimecodeStringToSubframes(selection_start, rate_info);
          const auto selection_end_subframes = std::max(
              selection_start_subframes,
              TimecodeStringToSubframes(selection_end, rate_info));
          const auto session_edl = client.ExportSessionInfoTextForTrackEdls();

          std::vector<FileLocationEntry> preferred_locations;
          preferred_locations.reserve(timeline_locations.size() + clips_list_locations.size());
          preferred_locations.insert(preferred_locations.end(), timeline_locations.begin(), timeline_locations.end());
          preferred_locations.insert(preferred_locations.end(), clips_list_locations.begin(), clips_list_locations.end());

          std::unordered_map<std::string, FileLocationEntry> file_locations_by_id;
          const auto remember_location = [&](const FileLocationEntry& entry) {
            if (Trimmed(entry.file_id).empty() || Trimmed(entry.path).empty()) {
              return;
            }
            const auto existing = file_locations_by_id.find(entry.file_id);
            const int score = (entry.is_online ? 100 : 0) + 10 + (NormalizeSelectedClipName(entry.path).empty() ? 0 : 1);
            if (existing == file_locations_by_id.end()) {
              file_locations_by_id.emplace(entry.file_id, entry);
              return;
            }
            const auto& current = existing->second;
            const int current_score =
                (current.is_online ? 100 : 0) + 10 + (NormalizeSelectedClipName(current.path).empty() ? 0 : 1);
            if (score > current_score) {
              existing->second = entry;
            }
          };
          for (const auto& entry : preferred_locations) {
            remember_location(entry);
          }
          try {
            const auto all_locations = client.GetFileLocations({});
            preferred_locations.insert(preferred_locations.end(), all_locations.begin(), all_locations.end());
            for (const auto& entry : all_locations) {
              remember_location(entry);
            }
          } catch (const std::exception&) {
          }

          auto export_segments = BuildSelectedClipSegmentsFromSessionExport(
              session_edl,
              selected_tracks.front().name,
              selection_start_subframes,
              selection_end_subframes,
              {},
              preferred_locations,
              clips,
              file_locations_by_id,
              sample_rate_hz,
              session_fps,
              rate_info);
          if (!export_segments.empty()) {
            const auto& lead = export_segments.front();
            if (lead.source_start_seconds.has_value()) {
              source_start_seconds = lead.source_start_seconds;
            }
            if (lead.source_end_seconds.has_value()) {
              source_end_seconds = lead.source_end_seconds;
            }
            if (!clip_start_time.has_value() && !Trimmed(lead.clip_start_time).empty()) {
              clip_start_time = lead.clip_start_time;
            }
            if (!src_start_seconds.has_value() && lead.src_start_seconds.has_value()) {
              src_start_seconds = lead.src_start_seconds;
            }
            if (!clip_id.has_value() && !Trimmed(lead.clip_id).empty()) {
              clip_id = lead.clip_id;
            }
            if (!Trimmed(lead.clip_name).empty()) {
              clip_name = ChooseMoreSpecificClipName(clip_name, lead.clip_name);
            }
          }
        }
      }
    } catch (const std::exception&) {
    }
  }

  return BuildSelectedClipFileJson(
      *selected_file_location,
      clip_name,
      clip_id,
      clip_start_time,
      src_start_seconds,
      sample_rate_hz,
      session_fps,
      source_start_seconds,
      source_end_seconds);
}

std::string ExecuteGetSelectedClipSegments(PtslClient& client) {
  const auto selected_tracks = client.GetSelectedTracks();
  if (selected_tracks.empty()) {
    if (HasDetectedTimelineOrEditRange(client)) {
      throw std::runtime_error(BuildSelectedRangeWithoutClipMessage(
          "A Pro Tools range is selected, but OverCue does not see a selected track/clip."));
    }
    throw std::runtime_error("Select one Pro Tools track containing the clips you want to analyze.");
  }
  if (selected_tracks.size() != 1) {
    throw std::runtime_error("Select exactly one Pro Tools track for multi-clip transcription.");
  }

  const auto selection = ResolveEffectiveTimelineSelection(client);
  auto selection_start = SelectTimelineReferenceTime(selection);
  auto selection_end = [&]() {
    const auto normalized_out = NormalizeComparableTimecode(selection.out_time);
    if (!normalized_out.empty()) {
      return normalized_out;
    }
    return selection_start;
  }();

  if (Trimmed(selection_start).empty() || Trimmed(selection_end).empty() || selection_start == selection_end) {
    try {
      const auto edit_bounds = client.GetEditSelectionBounds();
      const auto edit_in = NormalizeComparableTimecode(edit_bounds.in_time);
      const auto edit_out = NormalizeComparableTimecode(edit_bounds.out_time);
      if (!Trimmed(edit_in).empty() && !Trimmed(edit_out).empty() && edit_in != edit_out) {
        selection_start = edit_in;
        selection_end = edit_out;
      }
    } catch (const std::exception&) {
    }
  }

  if (Trimmed(selection_start).empty() || Trimmed(selection_end).empty() || selection_start == selection_end) {
    throw std::runtime_error("Set or select a clip range spanning the clips you want to analyze.");
  }

  int sample_rate_hz = 0;
  try {
    sample_rate_hz = client.GetSessionSampleRateHz();
  } catch (const std::exception&) {
    sample_rate_hz = 0;
  }

  TimeCodeRateInfo rate_info = {30, false, 30, 1};
  try {
    rate_info = client.GetSessionTimeCodeRateInfo();
  } catch (const std::exception&) {
    rate_info = {30, false, 30, 1};
  }

  std::optional<double> session_fps;
  if (rate_info.actual_fps_denominator > 0) {
    const double fps = static_cast<double>(rate_info.actual_fps_numerator)
        / static_cast<double>(rate_info.actual_fps_denominator);
    if (std::isfinite(fps) && fps > 0) {
      session_fps = fps;
    }
  }

  const auto timeline_locations = client.GetFileLocations({"FLTFilter_SelectedClipsTimeline"});
  const auto clips_list_locations = client.GetFileLocations({"FLTFilter_SelectedClipsClipsList"});
  if (timeline_locations.empty() && clips_list_locations.empty()) {
    throw std::runtime_error(BuildSelectedRangeWithoutClipMessage(
        "A Pro Tools range is selected, but OverCue does not see selected clip file data."));
  }

  std::unordered_map<std::string, FileLocationEntry> file_locations_by_id;
  const auto remember_location = [&](const FileLocationEntry& entry) {
    if (Trimmed(entry.file_id).empty() || Trimmed(entry.path).empty()) {
      return;
    }
    const auto existing = file_locations_by_id.find(entry.file_id);
    const int score = (entry.is_online ? 100 : 0) + 10 + (NormalizeSelectedClipName(entry.path).empty() ? 0 : 1);
    if (existing == file_locations_by_id.end()) {
      file_locations_by_id.emplace(entry.file_id, entry);
      return;
    }
    const auto& current = existing->second;
    const int current_score = (current.is_online ? 100 : 0) + 10 + (NormalizeSelectedClipName(current.path).empty() ? 0 : 1);
    if (score > current_score) {
      existing->second = entry;
    }
  };
  for (const auto& entry : timeline_locations) {
    remember_location(entry);
  }
  for (const auto& entry : clips_list_locations) {
    remember_location(entry);
  }

  std::vector<ClipInfo> clips;
  try {
    clips = client.GetClipList();
  } catch (const std::exception&) {
    clips.clear();
  }

  const auto selection_start_subframes = TimecodeStringToSubframes(selection_start, rate_info);
  const auto selection_end_subframes = std::max(
      selection_start_subframes,
      TimecodeStringToSubframes(selection_end, rate_info));
  const double session_seconds_per_subframe = session_fps.has_value() && *session_fps > 0
      ? 1.0 / (*session_fps * 100.0)
      : 0.0;
  const auto selected_clip_names = ResolveSelectedClipCurrentNames(clips_list_locations, timeline_locations, clips);
  std::vector<FileLocationEntry> preferred_locations;
  preferred_locations.reserve(timeline_locations.size() + clips_list_locations.size());
  preferred_locations.insert(preferred_locations.end(), timeline_locations.begin(), timeline_locations.end());
  preferred_locations.insert(preferred_locations.end(), clips_list_locations.begin(), clips_list_locations.end());

  const auto& selected_track = selected_tracks.front();
  std::vector<SelectedClipSegmentInfo> segments;
  try {
    const auto all_locations = client.GetFileLocations({});
    for (const auto& entry : all_locations) {
      remember_location(entry);
    }
  } catch (const std::exception&) {
  }

  // For a single selected clip, prefer the same track-specific session export path used by
  // drop-to-take. That path keys off the selected track and timeline range instead of the
  // playlist element clip id, which has proven ambiguous for trimmed clips in some sessions.
  try {
    const auto session_edl = client.ExportSessionInfoTextForTrackEdls();
    auto export_segments = BuildSelectedClipSegmentsFromSessionExport(
        session_edl,
        selected_track.name,
        selection_start_subframes,
        selection_end_subframes,
        {},
        preferred_locations,
        clips,
        file_locations_by_id,
        sample_rate_hz,
        session_fps,
        rate_info);
    MaybeAssignPreferredSessionExportSegments(segments, std::move(export_segments));
  } catch (const std::exception&) {
  }

  if (segments.empty()) {
  try {
    const auto playlists = client.GetTrackPlaylists(selected_track);
    const auto playlist_it = std::find_if(playlists.begin(), playlists.end(), [](const PlaylistInfo& playlist) {
      return playlist.is_target;
    });
    if (playlist_it == playlists.end()) {
      throw std::runtime_error("Could not resolve the target playlist for the selected track.");
    }

    const auto elements = client.GetPlaylistElements(playlist_it->playlist_id, selection_start, selection_end);
    std::unordered_set<std::string> needed_file_ids;
    for (const auto& element : elements) {
      for (const auto& channel_clip : element.channel_clips) {
        if (channel_clip.is_null || Trimmed(channel_clip.clip_id).empty()) {
          continue;
        }
        const auto* clip_info = ResolveClipInfoById(clips, channel_clip.clip_id);
        if (!clip_info || Trimmed(clip_info->file_id).empty()) {
          continue;
        }
        needed_file_ids.insert(clip_info->file_id);
      }
    }
    bool has_missing_file_locations = false;
    for (const auto& file_id : needed_file_ids) {
      if (file_locations_by_id.find(file_id) == file_locations_by_id.end()) {
        has_missing_file_locations = true;
        break;
      }
    }
    if (has_missing_file_locations) {
      try {
        const auto all_locations = client.GetFileLocations({});
        for (const auto& entry : all_locations) {
          remember_location(entry);
        }
      } catch (const std::exception&) {
      }
    }

    for (const auto& element : elements) {
      const auto play_time = !NormalizeComparableTimecode(element.play_time).empty()
          ? NormalizeComparableTimecode(element.play_time)
          : NormalizeComparableTimecode(element.start_time);
      const auto stop_time = !NormalizeComparableTimecode(element.stop_time).empty()
          ? NormalizeComparableTimecode(element.stop_time)
          : NormalizeComparableTimecode(element.end_time);
      if (play_time.empty() || stop_time.empty()) {
        continue;
      }

      const auto element_start_subframes = TimecodeStringToSubframes(play_time, rate_info);
      const auto element_stop_subframes = TimecodeStringToSubframes(stop_time, rate_info);
      if (selection_end_subframes <= element_start_subframes || selection_start_subframes >= element_stop_subframes) {
        continue;
      }

      const auto overlap_start_subframes = std::max(selection_start_subframes, element_start_subframes);
      const auto overlap_end_subframes = std::min(selection_end_subframes, element_stop_subframes);
      if (overlap_end_subframes <= overlap_start_subframes) {
        continue;
      }

      std::unordered_set<std::string> seen_clip_ids;
      for (const auto& channel_clip : element.channel_clips) {
        if (channel_clip.is_null || Trimmed(channel_clip.clip_id).empty()) {
          continue;
        }
        if (!seen_clip_ids.insert(channel_clip.clip_id).second) {
          continue;
        }

        const auto* clip_info = ResolveClipInfoById(clips, channel_clip.clip_id);
        if (!clip_info || Trimmed(clip_info->file_id).empty()) {
          continue;
        }

        FileLocationEntry resolved_location;
        const auto location_it = file_locations_by_id.find(clip_info->file_id);
        if (location_it != file_locations_by_id.end()) {
          resolved_location = location_it->second;
        } else if (!Trimmed(clip_info->file_path).empty()) {
          resolved_location.path = clip_info->file_path;
          resolved_location.file_id = clip_info->file_id;
          resolved_location.is_online = true;
        }
        if (Trimmed(resolved_location.path).empty()) {
          continue;
        }

        std::optional<double> src_start_seconds;
        if (clip_info->src_start_position && clip_info->src_start_time_type) {
          src_start_seconds = MediaTimePositionToSeconds(
              *clip_info->src_start_position,
              *clip_info->src_start_time_type,
              sample_rate_hz,
              rate_info);
        }

        std::optional<double> source_start_seconds;
        std::optional<double> source_end_seconds;
        if (src_start_seconds.has_value() && session_seconds_per_subframe > 0) {
          source_start_seconds =
              *src_start_seconds + static_cast<double>(overlap_start_subframes - element_start_subframes) * session_seconds_per_subframe;
          source_end_seconds =
              *source_start_seconds + static_cast<double>(overlap_end_subframes - overlap_start_subframes) * session_seconds_per_subframe;
        }

        segments.push_back({
            resolved_location,
            channel_clip.clip_id,
            ResolveClipNameFromClipId(clips, channel_clip.clip_id).value_or(""),
            "playlist_elements",
            play_time,
            SubframesToTimecodeString(overlap_start_subframes, rate_info),
            SubframesToTimecodeString(overlap_end_subframes, rate_info),
            src_start_seconds,
            source_start_seconds,
            source_end_seconds,
            overlap_start_subframes,
        });
      }
    }
  } catch (const std::exception&) {
    segments.clear();
  }
  }

  const bool should_try_session_export_fallback =
      segments.empty()
      || segments.size() == 1
      || CountSelectedClipSegmentsWithSourceWindows(segments) == 0
      || (!selected_clip_names.empty() && segments.size() < selected_clip_names.size());
  if (should_try_session_export_fallback) {
    try {
      const auto all_locations = client.GetFileLocations({});
      for (const auto& entry : all_locations) {
        remember_location(entry);
      }
    } catch (const std::exception&) {
    }

    try {
      const auto session_edl = client.ExportSessionInfoTextForTrackEdls();
      const auto export_clip_names = segments.size() == 1
          ? std::vector<std::string>{}
          : selected_clip_names;
      auto export_segments = BuildSelectedClipSegmentsFromSessionExport(
          session_edl,
          selected_track.name,
          selection_start_subframes,
          selection_end_subframes,
          export_clip_names,
          preferred_locations,
          clips,
          file_locations_by_id,
          sample_rate_hz,
          session_fps,
          rate_info);
      MaybeAssignPreferredSessionExportSegments(segments, std::move(export_segments));
    } catch (const std::exception&) {
    }
  }

  if (segments.empty()) {
    throw std::runtime_error(BuildSelectedRangeWithoutClipMessage(
        "A Pro Tools range is selected, but no selected clips were found on the selected track."));
  }

  std::stable_sort(segments.begin(), segments.end(), [](const SelectedClipSegmentInfo& left, const SelectedClipSegmentInfo& right) {
    if (left.segment_start_subframes != right.segment_start_subframes) {
      return left.segment_start_subframes < right.segment_start_subframes;
    }
    return left.clip_name < right.clip_name;
  });

  return BuildSelectedClipSegmentsJson(selected_track.name, segments, sample_rate_hz, session_fps);
}

std::string ExecuteWriteSelectedTranscriptionToJsonFile(PtslClient& client) {
  const auto path_to_json_file = client.WriteSelectedTranscriptionToJSONFile();
  std::ifstream file(path_to_json_file);
  if (!file) {
    throw std::runtime_error("Could not read the selected Pro Tools transcription JSON file.");
  }

  std::string transcription_json((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
  if (Trimmed(transcription_json).empty()) {
    throw std::runtime_error("The selected Pro Tools transcription JSON file was empty.");
  }

  std::ostringstream json;
  json << '{'
       << "\"path_to_json_file\":\"" << JsonEscape(path_to_json_file) << "\","
       << "\"transcription_json\":\"" << JsonEscape(transcription_json) << "\""
       << '}';
  return json.str();
}

std::string ExecuteClearPtMarkers(PtslClient& client) {
  const auto marker_numbers = client.GetMarkerMemoryLocationNumbers();
  const auto active_protocol = ParsePtslProtocolVersion(client.ActiveProtocolVersionString());
  const bool use_clear_all_memory_locations = SupportsClearAllMemoryLocations(active_protocol);
  if (use_clear_all_memory_locations) {
    client.ClearAllMemoryLocations();
  } else {
    client.ClearMemoryLocations(marker_numbers);
  }
  std::ostringstream json;
  json << "{\"cleared\":" << marker_numbers.size();
  if (use_clear_all_memory_locations) {
    json << ",\"used_clear_all_memory_locations\":true";
  }
  json << "}";
  return json.str();
}

std::string ExecuteListMarkers(PtslClient& client) {
  std::vector<MemoryLocationInfo> markers;
  for (const auto& memory_location : client.GetMemoryLocations()) {
    if (memory_location.time_properties == "TP_Marker") {
      markers.push_back(memory_location);
    }
  }
  return BuildMarkersJson("markers", markers);
}

std::string ExecuteListTracks(PtslClient& client, bool selected_only) {
  const auto tracks = selected_only ? client.GetSelectedTracks() : client.GetAllTracks();
  return BuildTracksJson("tracks", tracks);
}

std::string BuildClipGroupCreationResultJson(const std::vector<std::string>& track_names,
                                             std::size_t total_count,
                                             std::size_t grouped_count,
                                             std::size_t skipped_count,
                                             const std::vector<ClipGroupFailure>& failures) {
  const std::string primary_track_name = track_names.empty()
      ? std::string()
      : (track_names.size() == 1 ? track_names.front() : std::string("Cue tracks"));
  std::ostringstream json;
  json << '{'
       << "\"track_name\":\"" << JsonEscape(primary_track_name) << "\","
       << "\"track_names\":[";

  for (std::size_t index = 0; index < track_names.size(); index += 1) {
    if (index > 0) {
      json << ',';
    }
    json << '"' << JsonEscape(track_names[index]) << '"';
  }

  json << "],"
       << "\"total_count\":" << total_count << ','
       << "\"grouped_count\":" << grouped_count << ','
       << "\"skipped_count\":" << skipped_count << ','
       << "\"failures\":[";

  for (std::size_t index = 0; index < failures.size(); index += 1) {
    if (index > 0) {
      json << ',';
    }
    const auto& failure = failures[index];
    json << '{'
         << "\"name\":\"" << JsonEscape(failure.name) << "\","
         << "\"start_time\":\"" << JsonEscape(failure.start_time) << "\","
         << "\"end_time\":\"" << JsonEscape(failure.end_time) << "\","
         << "\"error\":\"" << JsonEscape(failure.error) << "\""
         << '}';
  }

  json << "]"
       << '}';
  return json.str();
}

std::string BuildClipGroupDeletionResultJson(const std::vector<std::string>& track_names,
                                             std::size_t total_count,
                                             std::size_t deleted_count,
                                             const std::vector<ClipGroupFailure>& failures) {
  std::ostringstream json;
  json << '{'
       << "\"track_names\":[";

  for (std::size_t index = 0; index < track_names.size(); index += 1) {
    if (index > 0) {
      json << ',';
    }
    json << '"' << JsonEscape(track_names[index]) << '"';
  }

  json << "],"
       << "\"total_count\":" << total_count << ','
       << "\"deleted_count\":" << deleted_count << ','
       << "\"failures\":[";

  for (std::size_t index = 0; index < failures.size(); index += 1) {
    if (index > 0) {
      json << ',';
    }
    const auto& failure = failures[index];
    json << '{'
         << "\"name\":\"" << JsonEscape(failure.name) << "\","
         << "\"start_time\":\"" << JsonEscape(failure.start_time) << "\","
         << "\"end_time\":\"" << JsonEscape(failure.end_time) << "\","
         << "\"error\":\"" << JsonEscape(failure.error) << "\""
         << '}';
  }

  json << "]"
       << '}';
  return json.str();
}

std::string BuildSelectionClipGroupResultJson(std::string_view group_name,
                                              bool used_menu_fallback) {
  std::ostringstream json;
  json << '{'
       << "\"grouped\":true,"
       << "\"clip_group_name\":\"" << JsonEscape(group_name) << "\","
       << "\"used_menu_fallback\":" << (used_menu_fallback ? "true" : "false")
       << '}';
  return json.str();
}

std::string BuildClipTrackCreationResultJson(const std::vector<std::string>& created_track_names,
                                             std::size_t required_track_count,
                                             std::size_t existing_track_count,
                                             const std::vector<ClipGroupFailure>& failures,
                                             std::string_view warning_message = "") {
  std::ostringstream json;
  json << '{'
       << "\"created_track_names\":[";

  for (std::size_t index = 0; index < created_track_names.size(); index += 1) {
    if (index > 0) {
      json << ',';
    }
    json << '"' << JsonEscape(created_track_names[index]) << '"';
  }

  json << "],"
       << "\"created_count\":" << created_track_names.size() << ','
       << "\"required_track_count\":" << required_track_count << ','
       << "\"existing_track_count\":" << existing_track_count;
  if (!Trimmed(std::string(warning_message)).empty()) {
    json << ",\"warning\":\"" << JsonEscape(std::string(warning_message)) << '"';
  }
  json << ','
       << "\"failures\":[";

  for (std::size_t index = 0; index < failures.size(); index += 1) {
    if (index > 0) {
      json << ',';
    }
    const auto& failure = failures[index];
    json << '{'
         << "\"name\":\"" << JsonEscape(failure.name) << "\","
         << "\"start_time\":\"" << JsonEscape(failure.start_time) << "\","
         << "\"end_time\":\"" << JsonEscape(failure.end_time) << "\","
         << "\"error\":\"" << JsonEscape(failure.error) << "\""
         << '}';
  }

  json << "]"
       << '}';
  return json.str();
}

std::string BuildCharacterTrackCreationResultJson(std::string_view track_name,
                                                  const std::vector<std::string>& created_track_names,
                                                  bool already_exists) {
  std::ostringstream json;
  json << '{'
       << "\"track_name\":\"" << JsonEscape(std::string(track_name)) << "\","
       << "\"created_track_names\":" << BuildJsonStringArray(created_track_names) << ','
       << "\"created_count\":" << created_track_names.size() << ','
       << "\"already_exists\":" << (already_exists ? "true" : "false")
       << '}';
  return json.str();
}

std::vector<PreparedClipGroupCue> PrepareClipGroupCues(const std::vector<Marker>& markers,
                                                       const TimeCodeRateInfo& rate_info,
                                                       std::vector<ClipGroupFailure>* failures,
                                                       bool require_group_name = true) {
  std::vector<PreparedClipGroupCue> prepared;
  prepared.reserve(markers.size());

  for (const auto& marker : markers) {
    const auto parsed_comments = ParseStoredMarkerComments(marker.comments);
    const auto group_name = BuildClipGroupName(marker.name, parsed_comments.comment_text);
    const auto track_identity = !Trimmed(marker.track_name).empty()
        ? Trimmed(marker.track_name)
        : parsed_comments.character_name;
    if (require_group_name && Trimmed(group_name).empty()) {
      if (failures != nullptr) {
        failures->push_back({
            marker.name,
            marker.start_time,
            marker.end_time,
            "Cue has no usable name. Enter one in OverCue and try again."
        });
      }
      continue;
    }

    const auto start_time = NormalizeComparableTimecode(marker.start_time);
    auto end_time = NormalizeComparableTimecode(marker.end_time);
    if (Trimmed(start_time).empty()) {
      if (failures != nullptr) {
        failures->push_back({
            marker.name,
            marker.start_time,
            marker.end_time,
            "Cue has no usable in point. Enter one in OverCue and try again."
        });
      }
      continue;
    }
    if (Trimmed(end_time).empty()) {
      end_time = start_time;
    }

    try {
      const auto start_subframes = TimecodeStringToSubframes(start_time, rate_info);
      const auto raw_end_subframes = TimecodeStringToSubframes(end_time, rate_info);
      if (raw_end_subframes <= start_subframes) {
        if (failures != nullptr) {
          failures->push_back({
              marker.name,
              start_time,
              end_time,
              BuildClipGroupNoEditSelectionFailureMessage()
          });
        }
        continue;
      }
      prepared.push_back({
          marker.name,
          group_name,
          track_identity,
          BuildCueTrackPoolKey(track_identity),
          BuildCueTrackPoolLabel(track_identity),
          start_time,
          end_time,
          start_subframes,
          raw_end_subframes,
          0
      });
    } catch (const std::exception& error) {
      if (failures != nullptr) {
        failures->push_back({
            marker.name,
            start_time,
            end_time,
            error.what()
        });
      }
    }
  }

  return prepared;
}

std::size_t AssignClipGroupCueSlots(std::vector<PreparedClipGroupCue>* cues,
                                    const std::vector<std::size_t>& cue_indices) {
  if (cues == nullptr || cue_indices.empty()) {
    return 0;
  }

  std::vector<std::size_t> sorted_indices;
  sorted_indices.reserve(cue_indices.size());
  sorted_indices.insert(sorted_indices.end(), cue_indices.begin(), cue_indices.end());

  std::stable_sort(
      sorted_indices.begin(),
      sorted_indices.end(),
      [&](std::size_t left_index, std::size_t right_index) {
        const auto& left = (*cues)[left_index];
        const auto& right = (*cues)[right_index];
        if (left.start_subframes != right.start_subframes) {
          return left.start_subframes < right.start_subframes;
        }
        return left.end_subframes < right.end_subframes;
      });

  std::vector<long long> slot_end_subframes;
  slot_end_subframes.reserve(cues->size());

  for (const auto cue_index : sorted_indices) {
    auto& cue = (*cues)[cue_index];
    std::size_t chosen_slot = slot_end_subframes.size();
    for (std::size_t slot_index = 0; slot_index < slot_end_subframes.size(); slot_index += 1) {
      if (slot_end_subframes[slot_index] <= cue.start_subframes) {
        chosen_slot = slot_index;
        break;
      }
    }
    if (chosen_slot == slot_end_subframes.size()) {
      slot_end_subframes.push_back(cue.end_subframes);
    } else {
      slot_end_subframes[chosen_slot] = cue.end_subframes;
    }
    cue.slot_index = chosen_slot;
  }

  return slot_end_subframes.size();
}

std::string BuildInsufficientCueTracksMessage(std::size_t required_count,
                                              const std::vector<const TrackInfo*>& cue_tracks,
                                              std::string_view track_pool_label) {
  std::ostringstream message;
  message << "Avoid overlaps needs "
          << required_count
          << " track"
          << (required_count == 1 ? "" : "s")
          << " in "
          << track_pool_label
          << "; found "
          << cue_tracks.size();
  if (cue_tracks.empty()) {
    message << '.';
  } else {
    message << " (" << JoinTrackNames(cue_tracks) << ").";
  }
  message << " Add tracks or turn off Avoid overlaps.";
  return message.str();
}

std::string ExecuteCreateCharacterTrack(PtslClient& client, const std::string& track_name) {
  const auto requested_track_name = Trimmed(track_name);
  if (requested_track_name.empty()) {
    throw std::runtime_error("Cannot create a Pro Tools track without a character name.");
  }

  const auto live_tracks = client.GetAllTracks();
  const auto existing_track = FindTrackByExactName(live_tracks, requested_track_name);
  if (existing_track != nullptr) {
    return BuildCharacterTrackCreationResultJson(existing_track->name, {}, true);
  }

  auto created_track_names = client.CreateNewTracks(requested_track_name, 1);
  for (auto& created_track_name : created_track_names) {
    created_track_name = Trimmed(created_track_name);
  }
  created_track_names.erase(
      std::remove_if(created_track_names.begin(), created_track_names.end(), [](const std::string& value) {
        return value.empty();
      }),
      created_track_names.end());
  if (created_track_names.empty()) {
    created_track_names.push_back(requested_track_name);
  }

  return BuildCharacterTrackCreationResultJson(created_track_names.front(), created_track_names, false);
}

std::string ExecuteSelectTrackByName(PtslClient& client, const std::string& track_name) {
  const auto requested_track_name = Trimmed(track_name);
  if (requested_track_name.empty()) {
    throw std::runtime_error("Cannot select a Pro Tools track without a track name.");
  }

  SelectTrackByNameReplaceAndWait(client, requested_track_name, "character");

  std::ostringstream json;
  json << "{\"track_name\":\"" << JsonEscape(requested_track_name) << "\",\"selected\":true}";
  return json.str();
}

std::string ExecuteMakePtClipTracksFromFile(PtslClient& client,
                                            const std::string& input_path,
                                            MakePtClipTracksMode mode) {
  const auto markers = LoadMarkers(input_path);
  std::vector<ClipGroupFailure> failures;
  failures.reserve(markers.size());

  const auto rate_info = client.GetSessionTimeCodeRateInfo();
  auto prepared_cues = PrepareClipGroupCues(markers, rate_info, &failures, false);
  ApplyMakePtClipTracksMode(&prepared_cues, mode);
  const auto live_tracks = client.GetAllTracks();
  auto taken_track_names = BuildExactTrackNameSet(live_tracks);
  std::vector<std::string> created_track_names;
  std::unordered_map<std::string, std::vector<const TrackInfo*>> cue_tracks_by_pool;
  std::unordered_map<std::string, std::vector<std::size_t>> cue_indices_by_pool;
  std::vector<std::string> ordered_pool_keys;
  std::size_t required_track_count = 0;
  std::size_t existing_track_count = 0;
  std::string selected_track_anchor;
  std::string session_tail_anchor = live_tracks.empty() ? std::string() : live_tracks.back().name;
  std::string warning_message;

  try {
    const auto selected_tracks = client.GetSelectedTracks();
    if (!selected_tracks.empty()) {
      selected_track_anchor = Trimmed(selected_tracks.front().name);
    }
  } catch (const std::exception&) {
    selected_track_anchor.clear();
  }

  std::string created_track_anchor = selected_track_anchor;

  for (std::size_t cue_index = 0; cue_index < prepared_cues.size(); cue_index += 1) {
    const auto& cue = prepared_cues[cue_index];
    auto pool_it = cue_tracks_by_pool.find(cue.track_pool_key);
    if (pool_it == cue_tracks_by_pool.end()) {
      auto cue_tracks = FindTracksContainingCue(live_tracks, cue.character_name);
      EnsureUniqueTrackNames(cue_tracks, cue.track_pool_label);
      pool_it = cue_tracks_by_pool.emplace(cue.track_pool_key, std::move(cue_tracks)).first;
      ordered_pool_keys.push_back(cue.track_pool_key);
    }
    cue_indices_by_pool[cue.track_pool_key].push_back(cue_index);
  }

  for (const auto& pool_key : ordered_pool_keys) {
    const auto pool_it = cue_tracks_by_pool.find(pool_key);
    const auto cue_indices_it = cue_indices_by_pool.find(pool_key);
    if (pool_it == cue_tracks_by_pool.end() || cue_indices_it == cue_indices_by_pool.end()) {
      continue;
    }

    auto& cue_tracks = pool_it->second;
    const auto& cue_indices = cue_indices_it->second;
    if (cue_indices.empty()) {
      continue;
    }

    const auto required_count = mode == MakePtClipTracksMode::kSingleGeneric
        ? std::size_t{1}
        : AssignClipGroupCueSlots(&prepared_cues, cue_indices);
    required_track_count += required_count;
    existing_track_count += cue_tracks.size();
    if (cue_tracks.size() >= required_count) {
      continue;
    }

    const auto& first_cue = prepared_cues[cue_indices.front()];
    const auto pool_base_name = BuildCueTrackBaseName(first_cue.character_name);
    std::string insertion_anchor = !created_track_anchor.empty()
        ? created_track_anchor
        : (!cue_tracks.empty() ? cue_tracks.back()->name : session_tail_anchor);

    for (std::size_t missing_index = cue_tracks.size(); missing_index < required_count; missing_index += 1) {
      const auto requested_track_name = AllocateCueTrackName(first_cue.character_name, &taken_track_names);
      const auto created_names = client.CreateNewTracks(
          requested_track_name,
          1,
          insertion_anchor.empty() ? std::string() : std::string("TIPoint_After"),
          insertion_anchor);
      const auto created_track_name = created_names.empty()
          ? requested_track_name
          : Trimmed(created_names.front());
      if (created_track_name.empty()) {
        throw std::runtime_error(
            std::string("CreateNewTracks returned no usable name for \"")
            + pool_base_name
            + "\".");
      }

      created_track_names.push_back(created_track_name);
      insertion_anchor = created_track_name;
      created_track_anchor = created_track_name;
      taken_track_names.insert(LowercaseAscii(Trimmed(created_track_name)));
      session_tail_anchor = created_track_name;
    }
  }

  if (!created_track_names.empty()) {
    try {
      client.SetTrackMuteState(created_track_names, true);
    } catch (const std::exception&) {
      warning_message = "Created tracks but couldn't mute them automatically. Mute them manually.";
    }
  }

  return BuildClipTrackCreationResultJson(
      created_track_names,
      required_track_count,
      existing_track_count,
      failures,
      warning_message);
}

bool ShouldUseProToolsMenuClipGroupFallback(const PtslClient& client) {
  const auto active_protocol = ParsePtslProtocolVersion(client.ActiveProtocolVersionString());
  const auto release_major = NormalizedPtslReleaseMajor(active_protocol);
  if (!release_major.has_value()) {
    return false;
  }
  if (*release_major < 24) {
    return true;
  }
  return *release_major == 24 && active_protocol->minor < 6;
}

bool GroupClipsForActiveProtocol(PtslClient& client) {
  if (ShouldUseProToolsMenuClipGroupFallback(client)) {
    LogPtslHandshakeDebug(
        std::string("Using Clip > Group menu fallback for PTSL protocol ")
        + client.ActiveProtocolVersionString());
    InvokeProToolsClipGroupMenuItem();
    return true;
  }
  client.GroupClips();
  return false;
}

std::optional<std::string> ResolveClipGroupNameFromSessionExportRange(PtslClient& client,
                                                                       const TrackInfo& target_track,
                                                                       const std::string& start_time,
                                                                       const std::string& end_time,
                                                                       const TimeCodeRateInfo& rate_info,
                                                                       const std::unordered_set<std::string>* existing_signatures_to_skip) {
  const auto start_subframes = TimecodeStringToSubframes(start_time, rate_info);
  const auto end_subframes = TimecodeStringToSubframes(end_time, rate_info);
  if (start_subframes < 0 || end_subframes <= start_subframes) {
    return std::nullopt;
  }

  const auto session_export_text = client.ExportSessionInfoTextForTrackEdls();
  const auto placements = FindTrackClipPlacementsFromSessionExport(
      session_export_text,
      target_track.name,
      start_subframes,
      end_subframes,
      {},
      rate_info);
  if (placements.empty()) {
    return std::nullopt;
  }

  const SessionExportClipPlacement* best = nullptr;
  long long best_overlap = 0;
  long long best_distance = std::numeric_limits<long long>::max();
  for (const auto& placement : placements) {
    const auto placement_start = std::min(placement.start_subframes, placement.end_subframes);
    const auto placement_end = std::max(placement.start_subframes, placement.end_subframes);
    const auto overlap_start = std::max(placement_start, start_subframes);
    const auto overlap_end = std::min(placement_end, end_subframes);
    const auto overlap = overlap_end - overlap_start;
    if (overlap <= 0 || Trimmed(placement.clip_name).empty()) {
      continue;
    }

    const auto distance = std::llabs(placement_start - start_subframes)
        + std::llabs(placement_end - end_subframes);
    if (existing_signatures_to_skip != nullptr
        && existing_signatures_to_skip->find(BuildClipPlacementSignature(
            placement.clip_name,
            placement.start_subframes,
            placement.end_subframes)) != existing_signatures_to_skip->end()) {
      continue;
    }
    if (!best || overlap > best_overlap || (overlap == best_overlap && distance < best_distance)) {
      best = &placement;
      best_overlap = overlap;
      best_distance = distance;
    }
  }

  if (!best) {
    return std::nullopt;
  }
  return best->clip_name;
}

void RenameClipGroupByTargetRange(PtslClient& client,
                                  const TrackInfo& target_track,
                                  const std::string& start_time,
                                  const std::string& end_time,
                                  const TimeCodeRateInfo& rate_info,
                                  const std::unordered_set<std::string>* existing_signatures_to_skip,
                                  const std::string& group_name) {
  constexpr int kAttempts = 4;
  constexpr int kRetryDelayMs = 125;

  for (int attempt = 0; attempt < kAttempts; attempt += 1) {
    if (const auto current_name = ResolveClipGroupNameFromSessionExportRange(
            client,
            target_track,
            start_time,
            end_time,
            rate_info,
            existing_signatures_to_skip)) {
      if (LowercaseAscii(NormalizeSelectedClipName(*current_name))
          == LowercaseAscii(NormalizeSelectedClipName(group_name))) {
        return;
      }
      client.RenameTargetClip(*current_name, group_name, false);
      return;
    }

    if (attempt + 1 < kAttempts) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kRetryDelayMs));
    }
  }

  throw std::runtime_error(BuildClipGroupRenameSelectionFailureMessage());
}

void CreateAndRenameClipGroupForActiveProtocol(PtslClient& client,
                                               const TrackInfo& target_track,
                                               const std::string& start_time,
                                               const std::string& end_time,
                                               const TimeCodeRateInfo& rate_info,
                                               const std::unordered_set<std::string>* existing_signatures_to_skip,
                                               const std::string& group_name) {
  GroupClipsForActiveProtocol(client);
  try {
    client.RenameSelectedClip(group_name, false);
    return;
  } catch (const std::exception& error) {
    if (!IsClipRenameSelectionFailure(error.what())) {
      throw;
    }
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(125));
  try {
    client.RenameSelectedClip(group_name, false);
    return;
  } catch (const std::exception& error) {
    if (!IsClipRenameSelectionFailure(error.what())) {
      throw;
    }
  }

  RenameClipGroupByTargetRange(
      client,
      target_track,
      start_time,
      end_time,
      rate_info,
      existing_signatures_to_skip,
      group_name);
}

void SetTimelineSelectionRangeForClipGroup(PtslClient& client,
                                           const std::string& start_timecode,
                                           const std::string& end_timecode) {
  if (ShouldUseProToolsMenuClipGroupFallback(client)) {
    client.SetTimelineSelectionRange(start_timecode, end_timecode, true);
    return;
  }
  client.SetTimelineSelectionRange(start_timecode, end_timecode);
}

std::string ExecuteCreateClipGroupFromSelection(PtslClient& client,
                                                std::string_view requested_group_name) {
  const auto group_name = BuildClipGroupName(requested_group_name, "");
  if (group_name.empty()) {
    throw std::runtime_error("Clip group name is required.");
  }

  const bool used_menu_fallback = GroupClipsForActiveProtocol(client);
  try {
    client.RenameSelectedClip(group_name, false);
  } catch (const std::exception& error) {
    if (IsClipRenameSelectionFailure(error.what())) {
      throw std::runtime_error(BuildClipGroupRenameSelectionFailureMessage());
    }
    throw;
  }

  return BuildSelectionClipGroupResultJson(group_name, used_menu_fallback);
}

std::string ExecuteCreatePtClipGroupsFromFile(PtslClient& client,
                                              const std::string& input_path,
                                              bool avoid_overlaps) {
  const auto markers = LoadMarkers(input_path);
  std::vector<ClipGroupFailure> failures;
  failures.reserve(markers.size());

  const auto rate_info = client.GetSessionTimeCodeRateInfo();
  auto prepared_cues = PrepareClipGroupCues(markers, rate_info, &failures);
  const auto live_tracks = client.GetAllTracks();
  std::size_t grouped_count = 0;
  std::size_t skipped_count = 0;
  std::vector<std::string> result_track_names;
  std::unordered_map<std::string, std::vector<const TrackInfo*>> cue_tracks_by_pool;
  std::unordered_map<std::string, std::vector<std::size_t>> cue_indices_by_pool;
  std::string session_export_text;
  try {
    session_export_text = client.ExportSessionInfoTextForTrackEdls();
  } catch (const std::exception&) {
    session_export_text.clear();
  }
  std::unordered_map<std::string, std::unordered_set<std::string>> existing_clip_signatures_by_track;

  for (std::size_t cue_index = 0; cue_index < prepared_cues.size(); cue_index += 1) {
    const auto& cue = prepared_cues[cue_index];
    auto pool_it = cue_tracks_by_pool.find(cue.track_pool_key);
    if (pool_it == cue_tracks_by_pool.end()) {
      auto cue_tracks = FindTracksContainingCue(live_tracks, cue.character_name);
      EnsureUniqueTrackNames(cue_tracks, cue.track_pool_label);
      pool_it = cue_tracks_by_pool.emplace(cue.track_pool_key, std::move(cue_tracks)).first;
    }

    if (pool_it->second.empty()) {
      failures.push_back({
          cue.name,
          cue.start_time,
          cue.end_time,
          BuildMissingCueTrackInstructionMessage(cue.track_pool_label)
      });
      continue;
    }

    cue_indices_by_pool[cue.track_pool_key].push_back(cue_index);
  }

  auto append_result_track_name = [&](const std::string& track_name) {
    const auto normalized_name = LowercaseAscii(Trimmed(track_name));
    if (normalized_name.empty()) {
      return;
    }
    const bool already_present = std::any_of(result_track_names.begin(), result_track_names.end(), [&](const std::string& existing) {
      return LowercaseAscii(Trimmed(existing)) == normalized_name;
    });
    if (!already_present) {
      result_track_names.push_back(track_name);
    }
  };

  auto ensure_existing_clip_signatures = [&](const std::string& track_name) -> std::unordered_set<std::string>& {
    const auto normalized_track_name = LowercaseAscii(Trimmed(track_name));
    auto signatures_it = existing_clip_signatures_by_track.find(normalized_track_name);
    if (signatures_it == existing_clip_signatures_by_track.end()) {
      signatures_it = existing_clip_signatures_by_track.emplace(
          normalized_track_name,
          CollectTrackClipPlacementSignaturesFromSessionExport(
              session_export_text,
              track_name,
              rate_info)).first;
    }
    return signatures_it->second;
  };

  if (!avoid_overlaps) {
    for (const auto& [pool_key, cue_indices] : cue_indices_by_pool) {
      const auto pool_it = cue_tracks_by_pool.find(pool_key);
      if (pool_it == cue_tracks_by_pool.end() || pool_it->second.empty()) {
        continue;
      }
      const auto* assigned_track = pool_it->second.front();
      append_result_track_name(assigned_track->name);

      for (const auto cue_index : cue_indices) {
        const auto& cue = prepared_cues[cue_index];
        auto& existing_signatures = ensure_existing_clip_signatures(assigned_track->name);
        const auto duplicate_signature = BuildClipPlacementSignature(
            cue.group_name,
            cue.start_subframes,
            cue.end_subframes);
        if (existing_signatures.find(duplicate_signature) != existing_signatures.end()) {
          skipped_count += 1;
          continue;
        }
        try {
          SelectTrackByNameReplaceAndWait(client, assigned_track->name, "target");
          SetTimelineSelectionRangeForClipGroup(client, cue.start_time, cue.end_time);
          CreateAndRenameClipGroupForActiveProtocol(
              client,
              *assigned_track,
              cue.start_time,
              cue.end_time,
              rate_info,
              &existing_signatures,
              cue.group_name);
          existing_signatures.insert(std::move(duplicate_signature));
          grouped_count += 1;
        } catch (const std::exception& error) {
          const std::string raw_message = error.what();
          failures.push_back({
              cue.name,
              cue.start_time,
              cue.end_time,
              IsClipRenameSelectionFailure(raw_message)
                  ? BuildClipGroupRenameSelectionFailureMessage()
                  : (raw_message.find("no Edit selection was made") != std::string::npos
                      || raw_message.find("No Edit selection was made") != std::string::npos)
                    ? BuildClipGroupNoEditSelectionFailureMessage()
                  : raw_message
          });
        }
      }
    }
  } else {
    for (const auto& [pool_key, cue_indices] : cue_indices_by_pool) {
      const auto pool_it = cue_tracks_by_pool.find(pool_key);
      if (pool_it == cue_tracks_by_pool.end()) {
        continue;
      }
      const auto& cue_tracks = pool_it->second;
      const auto required_track_count = AssignClipGroupCueSlots(&prepared_cues, cue_indices);
      if (cue_tracks.size() < required_track_count) {
        const auto& first_cue = prepared_cues[cue_indices.front()];
        throw std::runtime_error(BuildInsufficientCueTracksMessage(
            required_track_count,
            cue_tracks,
            first_cue.track_pool_label));
      }

      std::vector<const TrackInfo*> assigned_tracks;
      assigned_tracks.reserve(required_track_count);
      for (std::size_t index = 0; index < required_track_count; index += 1) {
        assigned_tracks.push_back(cue_tracks[index]);
        append_result_track_name(cue_tracks[index]->name);
      }

      for (const auto cue_index : cue_indices) {
        const auto& cue = prepared_cues[cue_index];
        const auto* assigned_track = assigned_tracks[cue.slot_index];
        auto& existing_signatures = ensure_existing_clip_signatures(assigned_track->name);
        const auto duplicate_signature = BuildClipPlacementSignature(
            cue.group_name,
            cue.start_subframes,
            cue.end_subframes);
        if (existing_signatures.find(duplicate_signature) != existing_signatures.end()) {
          skipped_count += 1;
          continue;
        }
        try {
          SelectTrackByNameReplaceAndWait(client, assigned_track->name, "target");
          SetTimelineSelectionRangeForClipGroup(client, cue.start_time, cue.end_time);
          CreateAndRenameClipGroupForActiveProtocol(
              client,
              *assigned_track,
              cue.start_time,
              cue.end_time,
              rate_info,
              &existing_signatures,
              cue.group_name);
          existing_signatures.insert(std::move(duplicate_signature));
          grouped_count += 1;
        } catch (const std::exception& error) {
          const std::string raw_message = error.what();
          failures.push_back({
              cue.name,
              cue.start_time,
              cue.end_time,
              IsClipRenameSelectionFailure(raw_message)
                  ? BuildClipGroupRenameSelectionFailureMessage()
                  : (raw_message.find("no Edit selection was made") != std::string::npos
                      || raw_message.find("No Edit selection was made") != std::string::npos)
                    ? BuildClipGroupNoEditSelectionFailureMessage()
                  : raw_message
          });
        }
      }
    }
  }

  return BuildClipGroupCreationResultJson(
      result_track_names,
      markers.size(),
      grouped_count,
      skipped_count,
      failures);
}

std::vector<SessionExportClipPlacement> FindCueClipPlacementsOnTrack(
    std::string_view session_export_text,
    const TrackInfo& track,
    const PreparedClipGroupCue& cue,
    const TimeCodeRateInfo& rate_info) {
  auto placements = FindTrackClipPlacementsFromSessionExport(
      session_export_text,
      track.name,
      cue.start_subframes,
      cue.end_subframes,
      {cue.group_name},
      rate_info);

  if (placements.empty()) {
    const auto cue_name_prefix = LowercaseAscii(NormalizeSelectedClipName(
        SanitizeMarkerCommentForClipName(cue.name)));
    if (!cue_name_prefix.empty()) {
      const auto range_placements = FindTrackClipPlacementsFromSessionExport(
          session_export_text,
          track.name,
          cue.start_subframes,
          cue.end_subframes,
          {},
          rate_info);
      for (const auto& placement : range_placements) {
        const auto normalized_clip_name = LowercaseAscii(NormalizeSelectedClipName(placement.clip_name));
        if (normalized_clip_name == cue_name_prefix
            || normalized_clip_name.rfind(cue_name_prefix + " -", 0) == 0
            || normalized_clip_name.rfind(cue_name_prefix + " ", 0) == 0) {
          placements.push_back(placement);
        }
      }
    }
  }

  std::vector<SessionExportClipPlacement> unique_placements;
  std::unordered_set<std::string> seen_signatures;
  unique_placements.reserve(placements.size());
  for (const auto& placement : placements) {
    const auto signature = BuildClipPlacementSignature(
        placement.clip_name,
        placement.start_subframes,
        placement.end_subframes);
    if (seen_signatures.insert(signature).second) {
      unique_placements.push_back(placement);
    }
  }
  return unique_placements;
}

bool TrackStillHasCueClipPlacement(std::string_view session_export_text,
                                   const TrackInfo& track,
                                   const PreparedClipGroupCue& cue,
                                   const TimeCodeRateInfo& rate_info,
                                   std::string_view placement_signature) {
  const auto placements = FindTrackClipPlacementsFromSessionExport(
      session_export_text,
      track.name,
      cue.start_subframes,
      cue.end_subframes,
      {},
      rate_info);
  return std::any_of(placements.begin(), placements.end(), [&](const SessionExportClipPlacement& placement) {
    return BuildClipPlacementSignature(
        placement.clip_name,
        placement.start_subframes,
        placement.end_subframes) == placement_signature;
  });
}

std::string BuildMissingCueClipMessage(std::string_view track_pool_label) {
  std::ostringstream message;
  message << "No matching OverCue clip group found on "
          << track_pool_label
          << ". Create clips first, or keep cue names/dialogue matching the created clip groups.";
  return message.str();
}

std::string ExecuteDeletePtCueClipsFromFile(PtslClient& client,
                                            const std::string& input_path) {
  const auto markers = LoadMarkers(input_path);
  std::vector<ClipGroupFailure> failures;
  failures.reserve(markers.size());

  const auto rate_info = client.GetSessionTimeCodeRateInfo();
  auto prepared_cues = PrepareClipGroupCues(markers, rate_info, &failures);
  const auto live_tracks = client.GetAllTracks();
  std::string session_export_text;
  try {
    session_export_text = client.ExportSessionInfoTextForTrackEdls();
  } catch (const std::exception&) {
    session_export_text.clear();
  }

  std::size_t deleted_count = 0;
  std::vector<std::string> result_track_names;
  std::unordered_map<std::string, std::vector<const TrackInfo*>> cue_tracks_by_pool;

  auto append_result_track_name = [&](const std::string& track_name) {
    const auto normalized_name = LowercaseAscii(Trimmed(track_name));
    if (normalized_name.empty()) {
      return;
    }
    const bool already_present = std::any_of(result_track_names.begin(), result_track_names.end(), [&](const std::string& existing) {
      return LowercaseAscii(Trimmed(existing)) == normalized_name;
    });
    if (!already_present) {
      result_track_names.push_back(track_name);
    }
  };

  for (const auto& cue : prepared_cues) {
    auto pool_it = cue_tracks_by_pool.find(cue.track_pool_key);
    if (pool_it == cue_tracks_by_pool.end()) {
      auto cue_tracks = FindTracksContainingCue(live_tracks, cue.character_name);
      EnsureUniqueTrackNames(cue_tracks, cue.track_pool_label);
      pool_it = cue_tracks_by_pool.emplace(cue.track_pool_key, std::move(cue_tracks)).first;
    }

    if (pool_it->second.empty()) {
      failures.push_back({
          cue.name,
          cue.start_time,
          cue.end_time,
          BuildMissingCueTrackInstructionMessage(cue.track_pool_label)
      });
      continue;
    }

    bool found_matching_clip = false;
    bool deleted_for_cue = false;
    for (const auto* track : pool_it->second) {
      if (track == nullptr) {
        continue;
      }
      const auto placements = FindCueClipPlacementsOnTrack(
          session_export_text,
          *track,
          cue,
          rate_info);
      if (placements.empty()) {
        continue;
      }

      found_matching_clip = true;
      for (const auto& placement : placements) {
        const auto placement_signature = BuildClipPlacementSignature(
            placement.clip_name,
            placement.start_subframes,
            placement.end_subframes);
        try {
          SelectTrackByNameReplaceAndWait(client, track->name, "target");
          SetTimelineSelectionRangeForClipGroup(client, placement.start_time, placement.end_time);
          client.Clear();
          session_export_text = client.ExportSessionInfoTextForTrackEdls();
          if (TrackStillHasCueClipPlacement(
                  session_export_text,
                  *track,
                  cue,
                  rate_info,
                  placement_signature)) {
            throw std::runtime_error("Pro Tools reported success, but the matching clip still appears on the target track.");
          }
          append_result_track_name(track->name);
          deleted_count += 1;
          deleted_for_cue = true;
        } catch (const std::exception& error) {
          failures.push_back({
              cue.name,
              cue.start_time,
              cue.end_time,
              error.what()
          });
        }
      }
    }

    if (!found_matching_clip && !deleted_for_cue) {
      failures.push_back({
          cue.name,
          cue.start_time,
          cue.end_time,
          BuildMissingCueClipMessage(cue.track_pool_label)
      });
    }
  }

  return BuildClipGroupDeletionResultJson(
      result_track_names,
      markers.size(),
      deleted_count,
      failures);
}

std::string ExecuteRenameTracksFromPlan(PtslClient& client, const std::string& input_path) {
  const auto plan = LoadRenamePlan(input_path);

  const auto live_tracks = client.GetAllTracks();
  std::unordered_map<std::string, TrackInfo> live_track_map;
  live_track_map.reserve(live_tracks.size());
  for (const auto& track : live_tracks) {
    live_track_map.emplace(track.id, track);
  }

  bool used_primary_name_fallback = false;
  const auto* primary_track = FindLiveTrackByIdOrUniqueSavedName(
      live_tracks,
      live_track_map,
      plan.primary_track_id,
      plan.primary_track_name,
      &used_primary_name_fallback);
  if (primary_track == nullptr) {
    throw std::runtime_error(
        std::string("The saved main record track \"") + plan.primary_track_name +
        "\" was not found in the current Pro Tools session.");
  }
  if (used_primary_name_fallback) {
    std::cerr << "[track-rename] recovered primary record track by name saved_name=\""
              << plan.primary_track_name
              << "\" live_id=\"" << primary_track->id
              << "\"\n";
  }

  const auto& live_primary_track = *primary_track;
  std::vector<RenameTrackResult> results;
  results.reserve(plan.tracks.size());

  for (const auto& track : plan.tracks) {
    RenameTrackResult result;
    result.track_id = track.track_id;
    result.saved_name = track.saved_name;

    bool used_track_name_fallback = false;
    const auto* live_track = FindLiveTrackByIdOrUniqueSavedName(
        live_tracks,
        live_track_map,
        track.track_id,
        track.saved_name,
        &used_track_name_fallback);
    if (live_track == nullptr) {
      result.status = "missing";
      results.push_back(std::move(result));
      continue;
    }
    if (used_track_name_fallback) {
      std::cerr << "[track-rename] recovered captured track by name saved_name=\""
                << track.saved_name
                << "\" live_id=\"" << live_track->id
                << "\"\n";
    }

    result.live_track_id = live_track->id;
    result.current_name = live_track->name;
    result.new_name = DeriveRenamedTrackName(
        live_track->name,
        live_primary_track.name,
        plan.marker_name);

    if (result.new_name.empty()) {
      result.status = "unmatched";
      results.push_back(std::move(result));
      continue;
    }

    if (result.new_name == result.current_name) {
      result.status = "unchanged";
      results.push_back(std::move(result));
      continue;
    }

    client.RenameTrack({
        live_track->id,
        result.current_name,
        result.new_name,
    });
    result.status = "renamed";
    results.push_back(std::move(result));
  }

  return BuildRenamePlanResultJson(
      plan.marker_name,
      live_primary_track.id,
      live_primary_track.name,
      results);
}

std::string ExecuteSetTrackMuteState(PtslClient& client, const std::string& input_path) {
  const auto update = LoadTrackMuteStateUpdate(input_path);
  client.SetTrackMuteState(update.track_names, update.enabled);
  std::ostringstream json;
  json << "{\"updated\":" << update.track_names.size()
       << ",\"enabled\":" << (update.enabled ? "true" : "false") << "}";
  return json.str();
}

std::string ExecuteEditMatchingMarker(PtslClient& client, const std::string& input_path) {
  const auto plan = LoadMarkerEditPlan(input_path);

  const auto previous_name = Trimmed(plan.previous_marker.name);
  const auto previous_start = NormalizeComparableTimecode(plan.previous_marker.start_time);
  const auto previous_end = NormalizeComparableTimecode(plan.previous_marker.end_time);
  const auto previous_comments = Trimmed(plan.previous_marker.comments);

  std::vector<MemoryLocationInfo> candidates;
  for (const auto& memory_location : client.GetMemoryLocations()) {
    if (memory_location.time_properties != "TP_Marker") {
      continue;
    }
    if (Trimmed(memory_location.name) != previous_name) {
      continue;
    }
    if (NormalizeComparableTimecode(memory_location.start_time) != previous_start) {
      continue;
    }
    if (!MemoryLocationMatchesMarkerScope(memory_location, plan.previous_marker)) {
      continue;
    }
    candidates.push_back(memory_location);
  }

  auto filter_candidates = [](const std::vector<MemoryLocationInfo>& source,
                              auto&& predicate) -> std::vector<MemoryLocationInfo> {
    std::vector<MemoryLocationInfo> filtered;
    for (const auto& item : source) {
      if (predicate(item)) {
        filtered.push_back(item);
      }
    }
    return filtered;
  };

  auto edit_matches = [&](const std::vector<MemoryLocationInfo>& matches) -> std::optional<std::string> {
    if (matches.empty()) {
      return std::nullopt;
    }

    std::vector<int> edited_numbers;
    edited_numbers.reserve(matches.size());
    for (const auto& item : matches) {
      client.EditMarker(item.number, plan.next_marker);
      edited_numbers.push_back(item.number);
    }

    std::sort(edited_numbers.begin(), edited_numbers.end());
    std::ostringstream json;
    json << "{\"edited_count\":" << edited_numbers.size() << ",\"edited_numbers\":[";
    for (std::size_t i = 0; i < edited_numbers.size(); i += 1) {
      if (i > 0) {
        json << ',';
      }
      json << edited_numbers[i];
    }
    json << "]}";
    return json.str();
  };

  if (const auto exact = edit_matches(filter_candidates(candidates, [&](const MemoryLocationInfo& item) {
        return NormalizeComparableTimecode(item.end_time) == previous_end
               && Trimmed(item.comments) == previous_comments;
      }))) {
    return *exact;
  }

  if (const auto end_matches = edit_matches(filter_candidates(candidates, [&](const MemoryLocationInfo& item) {
        return NormalizeComparableTimecode(item.end_time) == previous_end;
      }))) {
    return *end_matches;
  }

  if (const auto comment_matches = edit_matches(filter_candidates(candidates, [&](const MemoryLocationInfo& item) {
        return Trimmed(item.comments) == previous_comments;
      }))) {
    return *comment_matches;
  }

  if (const auto direct_matches = edit_matches(candidates)) {
    return *direct_matches;
  }

  const auto name_matches = filter_candidates(client.GetMemoryLocations(), [&](const MemoryLocationInfo& item) {
    return item.time_properties == "TP_Marker"
           && Trimmed(item.name) == previous_name
           && MemoryLocationMatchesMarkerScope(item, plan.previous_marker);
  });

  std::optional<MemoryLocationInfo> target;
  if (name_matches.size() == 1) {
    target = name_matches.front();
  }
  if (!target) {
    throw std::runtime_error("Could not find a unique matching Pro Tools marker to edit.");
  }

  client.EditMarker(target->number, plan.next_marker);
  std::ostringstream json;
  json << "{\"edited\":" << target->number << "}";
  return json.str();
}

std::string ExecuteDeleteMatchingMarker(PtslClient& client, const std::string& input_path) {
  const auto plan = LoadMarkerEditPlan(input_path, false);

  const auto previous_name = Trimmed(plan.previous_marker.name);
  const auto previous_start = NormalizeComparableTimecode(plan.previous_marker.start_time);
  const auto previous_end = NormalizeComparableTimecode(plan.previous_marker.end_time);
  const auto previous_comments = Trimmed(plan.previous_marker.comments);

  std::vector<MemoryLocationInfo> candidates;
  for (const auto& memory_location : client.GetMemoryLocations()) {
    if (memory_location.time_properties != "TP_Marker") {
      continue;
    }
    if (Trimmed(memory_location.name) != previous_name) {
      continue;
    }
    if (NormalizeComparableTimecode(memory_location.start_time) != previous_start) {
      continue;
    }
    if (!MemoryLocationMatchesMarkerScope(memory_location, plan.previous_marker)) {
      continue;
    }
    candidates.push_back(memory_location);
  }

  auto filter_candidates = [](const std::vector<MemoryLocationInfo>& source,
                              auto&& predicate) -> std::vector<MemoryLocationInfo> {
    std::vector<MemoryLocationInfo> filtered;
    for (const auto& item : source) {
      if (predicate(item)) {
        filtered.push_back(item);
      }
    }
    return filtered;
  };

  auto delete_matches = [&](const std::vector<MemoryLocationInfo>& matches) -> std::optional<std::string> {
    if (matches.empty()) {
      return std::nullopt;
    }

    std::vector<int> deleted_numbers;
    deleted_numbers.reserve(matches.size());
    for (const auto& item : matches) {
      deleted_numbers.push_back(item.number);
    }

    client.ClearMemoryLocations(deleted_numbers);
    std::sort(deleted_numbers.begin(), deleted_numbers.end());
    std::ostringstream json;
    json << "{\"deleted_count\":" << deleted_numbers.size() << ",\"deleted_numbers\":[";
    for (std::size_t i = 0; i < deleted_numbers.size(); i += 1) {
      if (i > 0) {
        json << ',';
      }
      json << deleted_numbers[i];
    }
    json << "]}";
    return json.str();
  };

  if (const auto exact = delete_matches(filter_candidates(candidates, [&](const MemoryLocationInfo& item) {
        return NormalizeComparableTimecode(item.end_time) == previous_end
               && Trimmed(item.comments) == previous_comments;
      }))) {
    return *exact;
  }

  if (const auto end_matches = delete_matches(filter_candidates(candidates, [&](const MemoryLocationInfo& item) {
        return NormalizeComparableTimecode(item.end_time) == previous_end;
      }))) {
    return *end_matches;
  }

  if (const auto comment_matches = delete_matches(filter_candidates(candidates, [&](const MemoryLocationInfo& item) {
        return Trimmed(item.comments) == previous_comments;
      }))) {
    return *comment_matches;
  }

  if (const auto direct_matches = delete_matches(candidates)) {
    return *direct_matches;
  }

  const auto name_matches = filter_candidates(client.GetMemoryLocations(), [&](const MemoryLocationInfo& item) {
    return item.time_properties == "TP_Marker"
           && Trimmed(item.name) == previous_name
           && MemoryLocationMatchesMarkerScope(item, plan.previous_marker);
  });

  if (name_matches.size() == 1) {
    client.ClearMemoryLocations({name_matches.front().number});
    std::ostringstream json;
    json << "{\"deleted_count\":1,\"deleted_numbers\":[" << name_matches.front().number << "]}";
    return json.str();
  }

  return "{\"deleted_count\":0,\"deleted_numbers\":[]}";
}

std::string ExecuteRenameSelectedClipFromCurrentMarkerComment(PtslClient& client) {
  const auto marker = ResolveCurrentMarkerForTimelineSelection(
      client.GetCurrentTimelineSelection(),
      client.GetMemoryLocations());
  if (!marker) {
    throw std::runtime_error("Could not find a Pro Tools marker at the current timeline location.");
  }

  const auto new_name = SanitizeMarkerCommentForClipName(marker->comments);
  client.RenameSelectedClip(new_name, false);
  return BuildClipRenameFromMarkerJson(*marker, new_name);
}

bool ShouldUseLegacySelectedClipRename(const PtslClient& client) {
  const auto active_protocol = ParsePtslProtocolVersion(client.ActiveProtocolVersionString());
  const auto release_major = NormalizedPtslReleaseMajor(active_protocol);
  return release_major.has_value()
      && *release_major < 25;
}

bool ShouldUseLegacyDropToTakeSessionExportResolution(const PtslClient& client) {
  const auto active_protocol = ParsePtslProtocolVersion(client.ActiveProtocolVersionString());
  const auto release_major = NormalizedPtslReleaseMajor(active_protocol);
  return release_major.has_value()
      && *release_major == 24;
}

bool ShouldPreferEditSelectionForDropToTake(const PtslClient& client) {
  const auto active_protocol = ParsePtslProtocolVersion(client.ActiveProtocolVersionString());
  const auto release_major = NormalizedPtslReleaseMajor(active_protocol);
  return release_major.has_value()
      && *release_major == 24
      && active_protocol->minor >= 10;
}

bool ShouldAvoidTimelineSelectionReadsForDropToTake(const PtslClient& client) {
  const auto active_protocol = ParsePtslProtocolVersion(client.ActiveProtocolVersionString());
  const auto release_major = NormalizedPtslReleaseMajor(active_protocol);
  return release_major.has_value()
      && *release_major == 24
      && active_protocol->minor >= 10;
}

bool IsPtsl23_12(const PtslClient& client) {
  const auto active_protocol = ParsePtslProtocolVersion(client.ActiveProtocolVersionString());
  const auto release_major = NormalizedPtslReleaseMajor(active_protocol);
  return release_major.has_value()
      && *release_major == 23
      && active_protocol->minor == 12;
}

std::vector<FileLocationEntry> GetSelectedClipFileLocationsForRename(
    PtslClient& client,
    const std::vector<std::string>& filters) {
  try {
    return client.GetFileLocations(filters);
  } catch (const std::exception& error) {
    if (IsClipRenameSelectionFailure(error.what())) {
      std::cerr << "[clip-rename] selected-clip-location unavailable"
                << " reason=\"selection-metadata-missing\""
                << " detail=\"" << JsonEscape(error.what()) << "\"\n";
      return {};
    }
    throw;
  }
}

std::vector<std::string> ResolveLegacySelectedClipNamesForDropToTake(PtslClient& client) {
  const auto clips_list_locations = client.GetFileLocations({"FLTFilter_SelectedClipsClipsList"});
  const auto timeline_locations = client.GetFileLocations({"FLTFilter_SelectedClipsTimeline"});
  return ResolveSelectedClipCurrentNames(clips_list_locations, timeline_locations, {});
}

std::vector<std::string> ResolveSelectedClipNamesForDropToTake(
    PtslClient& client,
    bool use_legacy_drop_to_take_resolution) {
  if (use_legacy_drop_to_take_resolution) {
    return ResolveLegacySelectedClipNamesForDropToTake(client);
  }

  std::vector<ClipInfo> selected_clips;
  try {
    selected_clips = client.GetClipList();
  } catch (const std::exception&) {
    selected_clips.clear();
  }
  const auto clips_list_locations = client.GetFileLocations({"FLTFilter_SelectedClipsClipsList"});
  const auto timeline_locations = client.GetFileLocations({"FLTFilter_SelectedClipsTimeline"});
  return ResolveSelectedClipCurrentNames(
      clips_list_locations,
      timeline_locations,
      selected_clips);
}

bool ShouldPrimeDropToTakeSourceSelection(
    PtslClient& client,
    const TimeCodeRateInfo& rate_info,
    const std::vector<std::string>& selected_clip_names) {
  if (ShouldAvoidTimelineSelectionReadsForDropToTake(client)) {
    std::cerr << "[drop-to-take] step=source-selection-prime"
              << " skipped=\"" << (!selected_clip_names.empty() ? "true" : "false") << "\""
              << " reason=\"pt24-no-timeline-selection-read\""
              << " selected_clip_count=\"" << selected_clip_names.size() << "\"\n";
    return selected_clip_names.empty();
  }

  std::string selection_start;
  std::string selection_end;
  const bool has_usable_selection_range = TryResolveDropToTakeUsableSelectionRange(
      client,
      rate_info,
      &selection_start,
      &selection_end);
  if (has_usable_selection_range && !selected_clip_names.empty()) {
    std::cerr << "[drop-to-take] step=source-selection-prime skipped=\"true\""
              << " reason=\"existing-selection-context\""
              << " start=\"" << selection_start << "\""
              << " end=\"" << selection_end << "\""
              << " selected_clip_count=\"" << selected_clip_names.size() << "\"\n";
    return false;
  }

  std::cerr << "[drop-to-take] step=source-selection-prime skipped=\"false\""
            << " reason=\""
            << (has_usable_selection_range ? "missing-selected-clips" : "missing-selection-range")
            << "\""
            << " selected_clip_count=\"" << selected_clip_names.size() << "\"\n";
  return true;
}

std::optional<std::string> ResolveSelectedClipCurrentNameFromSessionExport(
    PtslClient& client,
    const std::vector<FileLocationEntry>& clips_list_locations,
    const std::vector<FileLocationEntry>& timeline_locations,
    std::string_view fallback_name,
    bool allow_any_track_fallback) {
  const auto selected_tracks = client.GetSelectedTracks();
  if (selected_tracks.empty()) {
    std::cerr << "[clip-rename] session-export skipped reason=no-selected-tracks\n";
    return std::nullopt;
  }
  std::vector<TrackInfo> session_export_tracks = selected_tracks;
  const bool use_strict_23_12_track_resolution = IsPtsl23_12(client);
  if (use_strict_23_12_track_resolution) {
    std::vector<TrackInfo> explicitly_selected_tracks;
    for (const auto& track : selected_tracks) {
      const auto selected_state = LowercaseAscii(Trimmed(track.is_selected_state.value_or("")));
      if (selected_state == "setexplicitly" || selected_state == "setexplicitlyandimplicitly") {
        explicitly_selected_tracks.push_back(track);
      }
    }
    if (!explicitly_selected_tracks.empty()) {
      session_export_tracks = explicitly_selected_tracks;
    }
  }

  const auto selection = ResolveEffectiveTimelineSelection(client);
  const auto selection_start = SelectTimelineReferenceTime(selection);
  const auto selection_end = [&]() {
    const auto normalized_out = NormalizeComparableTimecode(selection.out_time);
    if (!normalized_out.empty()) {
      return normalized_out;
    }
    return selection_start;
  }();

  if (Trimmed(selection_start).empty() || Trimmed(selection_end).empty()) {
    std::cerr << "[clip-rename] session-export skipped reason=empty-selection"
              << " track=\"" << session_export_tracks.front().name << "\""
              << " selection_start=\"" << selection_start << "\""
              << " selection_end=\"" << selection_end << "\"\n";
    return std::nullopt;
  }

  const auto rate_info = client.GetCurrentSessionTimeCodeRateInfo();
  const auto selection_start_subframes = TimecodeStringToSubframes(selection_start, rate_info);
  const auto selection_end_subframes = std::max(
      selection_start_subframes,
      TimecodeStringToSubframes(selection_end, rate_info));

  const auto session_edl = client.ExportSessionInfoTextForTrackEdls();
  std::vector<SessionExportClipPlacement> placements;
  std::string resolved_track_name;
  for (const auto& track : session_export_tracks) {
    placements = FindTrackClipPlacementsFromSessionExport(
        session_edl,
        track.name,
        selection_start_subframes,
        selection_end_subframes,
        {},
        rate_info);
    if (!placements.empty()) {
      resolved_track_name = track.name;
      break;
    }
  }
  auto candidate_placements = placements;
  if (candidate_placements.empty()) {
    std::cerr << "[clip-rename] session-export selected-track-miss"
              << " track=\"" << session_export_tracks.front().name << "\""
              << " selection_start=\"" << selection_start << "\""
              << " selection_end=\"" << selection_end << "\""
              << " fallback=\"" << NormalizeSelectedClipName(std::string(fallback_name)) << "\"\n";
    if (use_strict_23_12_track_resolution || !allow_any_track_fallback) {
      return std::nullopt;
    }
    candidate_placements = FindAnyClipPlacementsFromSessionExport(
        session_edl,
        selection_start_subframes,
        selection_end_subframes,
        {},
        rate_info);
  }
  if (candidate_placements.empty()) {
    std::cerr << "[clip-rename] session-export skipped reason=no-placements"
              << " track=\"" << session_export_tracks.front().name << "\""
              << " selection_start=\"" << selection_start << "\""
              << " selection_end=\"" << selection_end << "\""
              << " fallback=\"" << NormalizeSelectedClipName(std::string(fallback_name)) << "\"\n";
    return std::nullopt;
  }

  std::string best_name = NormalizeSelectedClipName(std::string(fallback_name));
  long long best_distance_subframes = std::numeric_limits<long long>::max();
  const long long reference_subframes = selection_start_subframes;
  for (const auto& placement : candidate_placements) {
    const auto candidate_name = NormalizeSelectedClipName(placement.clip_name);
    if (candidate_name.empty()) {
      continue;
    }

    const auto placement_start_subframes = std::min(placement.start_subframes, placement.end_subframes);
    const auto placement_end_subframes = std::max(placement.start_subframes, placement.end_subframes);
    long long distance_subframes = 0;
    if (reference_subframes < placement_start_subframes) {
      distance_subframes = placement_start_subframes - reference_subframes;
    } else if (reference_subframes > placement_end_subframes) {
      distance_subframes = reference_subframes - placement_end_subframes;
    }

    if (best_name.empty() || distance_subframes < best_distance_subframes) {
      best_name = candidate_name;
      best_distance_subframes = distance_subframes;
      continue;
    }
    if (distance_subframes == best_distance_subframes) {
      best_name = ChooseMoreSpecificClipName(best_name, candidate_name);
    }
  }

  if (best_name.empty()) {
    std::cerr << "[clip-rename] session-export skipped reason=no-best-name"
              << " track=\"" << (resolved_track_name.empty() ? session_export_tracks.front().name : resolved_track_name) << "\""
              << " selection_start=\"" << selection_start << "\""
              << " selection_end=\"" << selection_end << "\"\n";
    return std::nullopt;
  }
  std::cerr << "[clip-rename] session-export resolved"
            << " track=\"" << (resolved_track_name.empty() ? session_export_tracks.front().name : resolved_track_name) << "\""
            << " selection_start=\"" << selection_start << "\""
            << " selection_end=\"" << selection_end << "\""
            << " resolved_name=\"" << best_name << "\""
            << " fallback=\"" << NormalizeSelectedClipName(std::string(fallback_name)) << "\"\n";
  return best_name;
}

std::string ExecuteRenameSelectedClip(PtslClient& client,
                                      const std::string& raw_name,
                                      std::string_view mode = "replace-suffix",
                                      std::string_view separator = "-",
                                      bool rename_file = false) {
  const auto clips_list_locations = GetSelectedClipFileLocationsForRename(
      client,
      {"FLTFilter_SelectedClipsClipsList"});
  const auto timeline_locations = GetSelectedClipFileLocationsForRename(
      client,
      {"FLTFilter_SelectedClipsTimeline"});
  const bool has_selected_clip_locations = !clips_list_locations.empty() || !timeline_locations.empty();

  const bool use_legacy_selected_clip_rename = ShouldUseLegacySelectedClipRename(client);
  std::vector<ClipInfo> clips;
  if (!use_legacy_selected_clip_rename) {
    try {
      clips = client.GetClipList();
    } catch (const std::exception&) {
      clips.clear();
    }
  }

  auto previous_names = ResolveSelectedClipCurrentNames(clips_list_locations, timeline_locations, clips);
  auto previous_name = !previous_names.empty()
      ? previous_names.front()
      : ResolveSelectedClipCurrentName(clips_list_locations, timeline_locations, clips);
  bool use_target_clip_rename = clips_list_locations.empty();

  if (previous_name.empty() && !use_legacy_selected_clip_rename) {
    try {
      const auto selection = ResolveEffectiveTimelineSelection(client);
      const auto rate_info = client.GetCurrentSessionTimeCodeRateInfo();
      const auto selection_start = SelectTimelineReferenceTime(selection);
      const auto selection_end = [&]() {
        const auto normalized_out = NormalizeComparableTimecode(selection.out_time);
        if (!normalized_out.empty()) {
          return normalized_out;
        }
        return selection_start;
      }();

      for (const auto& track : client.GetSelectedTracks()) {
        const auto playlists = client.GetTrackPlaylists(track);
        const auto playlist_it = std::find_if(playlists.begin(), playlists.end(), [](const PlaylistInfo& playlist) {
          return playlist.is_target;
        });
        if (playlist_it == playlists.end()) {
          continue;
        }

        const auto elements = client.GetPlaylistElements(
            playlist_it->playlist_id,
            selection_start,
            selection_end);
        const auto resolved_name = ResolveTimelineClipNameFromSelection(selection, elements, clips, rate_info);
        if (resolved_name && !resolved_name->empty()) {
          previous_name = *resolved_name;
          break;
        }
      }
    } catch (const std::exception&) {
      previous_name.clear();
    }
  }

  if (use_legacy_selected_clip_rename) {
    if (const auto session_export_name = ResolveSelectedClipCurrentNameFromSessionExport(
            client,
            clips_list_locations,
            timeline_locations,
            previous_name,
            has_selected_clip_locations)) {
      previous_name = NormalizeSelectedClipName(*session_export_name);
    }
    previous_names.assign(1, previous_name);
  } else if (previous_name.empty()) {
    if (const auto session_export_name = ResolveSelectedClipCurrentNameFromSessionExport(
            client,
            clips_list_locations,
            timeline_locations,
            previous_name,
            has_selected_clip_locations)) {
      previous_name = NormalizeSelectedClipName(*session_export_name);
    }
  }

  if (previous_name.empty()) {
    throw std::runtime_error(
        "Could not resolve a Pro Tools clip from the current clip selection or timeline selection.");
  }

  if (previous_names.empty()) {
    previous_names.push_back(previous_name);
  }

  std::vector<std::string> renamed_previous_names;
  std::vector<std::string> renamed_target_names;
  renamed_previous_names.reserve(previous_names.size());
  renamed_target_names.reserve(previous_names.size());

  for (const auto& current_name : previous_names) {
    const auto target_name = BuildClipRenameTargetName(
        current_name,
        raw_name,
        ParseClipRenameBehaviorMode(mode),
        separator);
    if (use_legacy_selected_clip_rename) {
      client.RenameSelectedClip(target_name, rename_file);
    } else if (use_target_clip_rename || previous_names.size() > 1) {
      client.RenameTargetClip(current_name, target_name, rename_file);
    } else {
      try {
        client.RenameSelectedClip(target_name, rename_file);
      } catch (const std::exception& error) {
        const std::string message = error.what();
        if (message.find("No clip is selected") == std::string::npos &&
            message.find("PT_InvalidParameter") == std::string::npos) {
          throw;
        }
        client.RenameTargetClip(current_name, target_name, rename_file);
      }
    }
    renamed_previous_names.push_back(current_name);
    renamed_target_names.push_back(target_name);
  }

  if (renamed_previous_names.size() == 1) {
    return BuildClipRenameJson(renamed_previous_names.front(), renamed_target_names.front());
  }

  std::ostringstream json;
  json << '{'
       << "\"renamed_count\":" << renamed_previous_names.size() << ','
       << "\"renames\":[";
  for (std::size_t index = 0; index < renamed_previous_names.size(); index += 1) {
    if (index > 0) {
      json << ',';
    }
    json << "{"
         << "\"previous_name\":\"" << JsonEscape(renamed_previous_names[index]) << "\","
         << "\"clip_name\":\"" << JsonEscape(renamed_target_names[index]) << "\""
         << "}";
  }
  json << "]}";
  return json.str();
}

std::string ExecuteDropToTake(PtslClient& client, const std::string& input_path) {
  const auto plan = LoadDropToTakePlan(input_path);

  const auto live_tracks = client.GetAllTracks();
  std::unordered_map<std::string, TrackInfo> live_track_map;
  live_track_map.reserve(live_tracks.size());
  for (const auto& track : live_tracks) {
    live_track_map.emplace(track.id, track);
  }

  auto resolved_record_tracks = ResolveDropToTakeRecordTracks(
      live_tracks,
      live_track_map,
      plan.tracks);

  bool used_primary_name_fallback = false;
  const auto* primary_track = FindLiveTrackByIdOrUniqueSavedName(
      live_tracks,
      live_track_map,
      plan.primary_track_id,
      plan.primary_track_name,
      &used_primary_name_fallback);
  if (primary_track == nullptr) {
    throw std::runtime_error(
        std::string("The saved main record track \"") + plan.primary_track_name +
        "\" was not found in the current Pro Tools session.");
  }
  if (used_primary_name_fallback) {
    std::cerr << "[drop-to-take] recovered primary record track by name saved_name=\""
              << plan.primary_track_name
              << "\" live_id=\"" << primary_track->id
              << "\"\n";
  }
  UsePrimaryTrackAsDropToTakeRecordFallback(
      resolved_record_tracks,
      live_tracks,
      *primary_track);
  if (!resolved_record_tracks.found_any_record) {
    throw std::runtime_error(
        "None of the captured record tracks were found in the current Pro Tools session.");
  }

  const auto& record_ids = resolved_record_tracks.ids;
  const std::size_t max_record_index = resolved_record_tracks.max_record_index;
  const std::string primary_live_name = primary_track->name;
  const std::string primary_live_type = primary_track->type;
  const std::string take_track_keyword = NormalizeDropToTakeTrackKeyword(plan.take_track_keyword);
  const auto rate_info = client.GetCurrentSessionTimeCodeRateInfo();
  const bool use_legacy_drop_to_take_resolution = ShouldUseLegacyDropToTakeSessionExportResolution(client);
  const bool avoid_timeline_selection_reads = ShouldAvoidTimelineSelectionReadsForDropToTake(client);
  std::vector<std::string> selected_clip_names = ResolveSelectedClipNamesForDropToTake(
      client,
      use_legacy_drop_to_take_resolution);
  if (ShouldPrimeDropToTakeSourceSelection(client, rate_info, selected_clip_names)) {
    std::cerr << "[drop-to-take] step=prime-source-selection primary_track=\"" << primary_live_name << "\"\n";
    PrimeDropToTakeSourceSelection(client, primary_live_name, rate_info);
    selected_clip_names = ResolveSelectedClipNamesForDropToTake(
        client,
        use_legacy_drop_to_take_resolution);
  } else {
    std::cerr << "[drop-to-take] step=prime-source-selection primary_track=\"" << primary_live_name
              << "\" skipped=\"true\"\n";
  }

  const std::string session_edl = client.ExportSessionInfoTextForTrackEdls();
  if (Trimmed(session_edl).empty()) {
    throw std::runtime_error(
        "Could not read the Pro Tools session tracks. OverCue needs that to find an open Take track.");
  }

  long long overlap_start_sf = 0;
  long long overlap_end_sf = 0;
  std::optional<TimelineSelection> effective_selection_for_range;
  std::optional<std::pair<BarsBeatsPosition, BarsBeatsPosition>> selected_clip_bars_beats_range;
  const bool use_selected_clip_bounds = ShouldPreferEditSelectionForDropToTake(client);
  const bool use_session_export_clip_bounds = avoid_timeline_selection_reads || use_selected_clip_bounds;
  auto source_placements = FindTrackClipPlacementsFromSessionExport(
      session_edl,
      primary_live_name,
      use_session_export_clip_bounds ? std::numeric_limits<long long>::lowest() : 0LL,
      use_session_export_clip_bounds ? std::numeric_limits<long long>::max() : 0LL,
      selected_clip_names,
      rate_info);
  if (use_session_export_clip_bounds && source_placements.empty()) {
    selected_clip_names.clear();
    source_placements = FindTrackClipPlacementsFromSessionExport(
        session_edl,
        primary_live_name,
        std::numeric_limits<long long>::lowest(),
        std::numeric_limits<long long>::max(),
        selected_clip_names,
        rate_info);
  }
  if (use_legacy_drop_to_take_resolution && !source_placements.empty()) {
    // Pro Tools 24.3 can report noisy selected-clip context. Anchor drop-to-take to the
    // primary record track's session-export overlap instead of helper-selected clip metadata.
    selected_clip_names = CollectUniqueClipNamesFromPlacements(source_placements);
  }
  if (!source_placements.empty()) {
    overlap_start_sf = std::min_element(
        source_placements.begin(),
        source_placements.end(),
        [](const SessionExportClipPlacement& left, const SessionExportClipPlacement& right) {
          return left.start_subframes < right.start_subframes;
        })->start_subframes;
    const auto end_it = std::max_element(
        source_placements.begin(),
        source_placements.end(),
        [](const SessionExportClipPlacement& left, const SessionExportClipPlacement& right) {
          return left.end_subframes < right.end_subframes;
        });
    overlap_end_sf = end_it->end_subframes;
    LogDropToTakePrimaryTrackEndTime(primary_live_name, "session_export", end_it->end_time);
  } else if (avoid_timeline_selection_reads) {
    throw std::runtime_error(
        "Could not find the recorded clip range. Select the recorded clip on the captured record track and try again.");
  } else if (!use_selected_clip_bounds) {
    const auto current_selection = ResolveEffectiveTimelineSelection(client);
    effective_selection_for_range = current_selection;
    std::string rec_in = NormalizeComparableTimecode(current_selection.in_time);
    std::string rec_out = NormalizeComparableTimecode(current_selection.out_time);
    if (rec_in.empty() || rec_out.empty() || rec_in == rec_out) {
      const auto edit_bounds = client.GetEditSelectionBounds();
      rec_in = Trimmed(edit_bounds.in_time);
      rec_out = Trimmed(edit_bounds.out_time);
    }
    if (rec_in.empty() || rec_out.empty()) {
      throw std::runtime_error(
          "Could not read the edit selection time range for the recorded clips. Ensure the primary "
          "record track shows a valid clip selection (try clicking the track or re-selecting clips).");
    }
    LogDropToTakePrimaryTrackEndTime(primary_live_name, "effective_selection", rec_out);
    try {
      overlap_start_sf = TimecodeStringToSubframes(NormalizeComparableTimecode(rec_in), rate_info);
      overlap_end_sf = TimecodeStringToSubframes(NormalizeComparableTimecode(rec_out), rate_info);
    } catch (const std::exception& error) {
      throw std::runtime_error(std::string("Could not parse edit selection timecode: ") + error.what());
    }
    if (overlap_start_sf == overlap_end_sf) {
      overlap_end_sf = overlap_start_sf + 100;
    }
  } else {
    throw std::runtime_error(
        "Could not find the recorded clip range. Select the recorded clip on the captured record track and try again.");
  }
  if (!avoid_timeline_selection_reads) {
    const auto selection_guard_range = effective_selection_for_range.has_value()
        ? ResolveDropToTakeSelectionRangeSubframes(*effective_selection_for_range, rate_info)
        : ResolveDropToTakeGuardSelectionRangeSubframes(client, rate_info);
    if (selection_guard_range) {
      overlap_start_sf = std::min(overlap_start_sf, selection_guard_range->first);
      overlap_end_sf = std::max(overlap_end_sf, selection_guard_range->second);
    }
  }
  const auto derived_start_timecode = SubframesToTimecodeString(
      std::min(overlap_start_sf, overlap_end_sf),
      rate_info);
  const auto derived_end_timecode = SubframesToTimecodeString(
      std::max(overlap_start_sf, overlap_end_sf),
      rate_info);
  if (effective_selection_for_range.has_value()) {
    client.SetTimelineSelectionRange(
        derived_start_timecode,
        derived_end_timecode,
        *effective_selection_for_range);
  } else {
    client.SetTimelineSelectionRange(
        derived_start_timecode,
        derived_end_timecode,
        avoid_timeline_selection_reads);
  }

  if (!avoid_timeline_selection_reads
      && !(use_selected_clip_bounds && !source_placements.empty())) {
    try {
      const auto bars_beats_selection = client.GetTimelineSelectionBarsBeats();
      const auto start = ParseBarsBeatsPosition(bars_beats_selection.in_time);
      const auto end = ParseBarsBeatsPosition(bars_beats_selection.out_time);
      if (start && end) {
        selected_clip_bars_beats_range = std::make_pair(*start, *end);
      }
    } catch (const std::exception&) {
      selected_clip_bars_beats_range = std::nullopt;
    }
  }
  if (!selected_clip_bars_beats_range.has_value() && source_placements.empty()) {
    for (const auto& selected_clip_name : selected_clip_names) {
      selected_clip_bars_beats_range = FindClipBarsBeatsRangeOnTrackFromSessionExport(
          session_edl,
          primary_live_name,
          selected_clip_name);
      if (!selected_clip_bars_beats_range.has_value()) {
        selected_clip_bars_beats_range = FindClipBarsBeatsRangeFromSessionExport(session_edl, selected_clip_name);
      }
      if (selected_clip_bars_beats_range.has_value()) {
        break;
      }
    }
  }

  const auto source_tracks = ResolveDropToTakeSourceTracksForRange(
      live_tracks,
      record_ids,
      *primary_track,
      session_edl,
      selected_clip_bars_beats_range,
      overlap_start_sf,
      overlap_end_sf,
      rate_info);
  const bool use_channel_aware_selection = CanUseDropToTakeChannelAwareSelection(source_tracks);
  const auto source_track_names = ResolveDropToTakeSourceTrackNames(
      source_tracks,
      primary_live_name,
      use_channel_aware_selection);
  const int source_channel_count = use_channel_aware_selection
      ? SumTrackChannelCounts(source_tracks)
      : 1;
  std::cerr << "[drop-to-take] step=resolve-source-tracks"
            << " tracks=" << BuildJsonStringArray(source_track_names)
            << " recorded_channels=\"" << source_channel_count << "\""
            << " channel_aware=\"" << (use_channel_aware_selection ? "true" : "false") << "\"\n";

  const auto target_search = FindDropToTakeTargetTracks(
      live_tracks,
      max_record_index,
      record_ids,
      plan.placement_mode,
      take_track_keyword,
      primary_live_type,
      session_edl,
      selected_clip_bars_beats_range,
      overlap_start_sf,
      overlap_end_sf,
      rate_info,
      source_channel_count);
  const auto target_track_names = TrackNamesFromInfos(target_search.targets);
  if (target_track_names.empty()) {
    const auto no_target_message = BuildDropToTakeNoTargetTrackMessage(
        take_track_keyword,
        primary_live_type,
        source_channel_count);
    RecoverPtslSessionAfterError(client, "drop-to-take", no_target_message);
    throw std::runtime_error(no_target_message);
  }

  try {
    CutSelectedClipsOnTracksWithRetry(
        client,
        source_track_names,
        selected_clip_bars_beats_range,
        overlap_start_sf,
        overlap_end_sf,
        rate_info);
  } catch (const std::exception& error) {
    RecoverPtslSessionAfterError(client, "drop-to-take", error.what());
    throw;
  }

  const std::string pre_paste_session_edl = client.ExportSessionInfoTextForTrackEdls();
  const auto overlapping_target_track = FirstTrackOverlappingRange(
      pre_paste_session_edl,
      target_track_names,
      selected_clip_bars_beats_range,
      overlap_start_sf,
      overlap_end_sf,
      rate_info);
  if (overlapping_target_track) {
    throw std::runtime_error(
        std::string("Take track \"")
        + *overlapping_target_track
        + "\" already has a clip in this time range. Choose an open Take track range and try again.");
  }

  try {
    SelectTracksByNameReplaceAndWait(client, target_track_names, "target");
    client.Paste();
  } catch (const std::exception& error) {
    RecoverPtslSessionAfterError(client, "drop-to-take", error.what());
    throw;
  }

  std::ostringstream json;
  const auto primary_source = source_track_names.empty() ? std::string() : source_track_names.front();
  const auto primary_target = target_track_names.empty() ? std::string() : target_track_names.front();
  json << "{\"status\":\"ok\",\"source_track\":\"" << JsonEscape(primary_source)
       << "\",\"source_tracks\":" << BuildJsonStringArray(source_track_names)
       << ",\"target_track\":\"" << JsonEscape(primary_target)
       << "\",\"target_tracks\":" << BuildJsonStringArray(target_track_names)
       << "}";
  return json.str();
}

class HelperServer {
 public:
  ~HelperServer() {
    RequestStop();
  }

  int Run() {
    WriteReady();

    std::string line;
    while (std::getline(std::cin, line)) {
      if (Trimmed(line).empty()) {
        continue;
      }
      HandleLine(line);
      if (stop_requested_.load()) {
        break;
      }
    }

    RequestStop();
    return 0;
  }

 private:
  void HandleLine(const std::string& line) {
    const auto fields = SplitTabs(line);
    if (fields.size() < 4 || fields[0] != "REQ") {
      std::cerr << "[helper-server] Ignoring malformed request line\n";
      return;
    }

    const std::string request_id = fields[1];
    const std::string command = fields[2];
    std::string payload_json;
    try {
      payload_json = DecodeBase64(fields[3]);
    } catch (const std::exception& error) {
      WriteResponse(request_id, false, std::string("Invalid request payload: ") + error.what());
      return;
    }

    try {
      if (command == "shutdown") {
        WriteResponse(request_id, true, "");
        stop_requested_.store(true);
        RequestPollCancel();
        return;
      }

      if (command == "update_event_subscriptions") {
        const auto subscriptions = ParseEventSubscriptionsPayload(payload_json);
        UpdateDesiredSubscriptions(subscriptions);
        WriteResponse(request_id, true, BuildEventSubscriptionsSnapshotJson(subscriptions));
        return;
      }

      const auto output = ExecuteCommand(command, payload_json);
      WriteResponse(request_id, true, output);
    } catch (const std::exception& error) {
      HandleCommandError(error.what());
      WriteResponse(request_id, false, error.what());
    }
  }

  std::string ExecuteCommand(const std::string& command, const std::string& payload_json) {
    EnsureClientConnected(client_);

    if (command == "create_markers_from_file") {
      return ExecuteCreateMarkersFromFile(
          client_,
          RequirePayloadStringValue(payload_json, "path", nullptr, "path"));
    }
    if (command == "create_pt_clip_groups_from_file") {
      return ExecuteCreatePtClipGroupsFromFile(
          client_,
          RequirePayloadStringValue(payload_json, "path", nullptr, "path"),
          ExtractPayloadOptionalBoolValue(payload_json, "avoid_overlaps", "avoidOverlaps").value_or(false));
    }
    if (command == "delete_pt_cue_clips_from_file") {
      return ExecuteDeletePtCueClipsFromFile(
          client_,
          RequirePayloadStringValue(payload_json, "path", nullptr, "path"));
    }
    if (command == "create_clip_group_from_selection") {
      return ExecuteCreateClipGroupFromSelection(
          client_,
          RequirePayloadStringValue(payload_json, "name", "clipGroupName", "name"));
    }
    if (command == "make_pt_clip_tracks_from_file") {
      return ExecuteMakePtClipTracksFromFile(
          client_,
          RequirePayloadStringValue(payload_json, "path", nullptr, "path"),
          ParseMakePtClipTracksMode(ExtractPayloadStringValue(payload_json, "mode", "trackMode")));
    }
    if (command == "create_character_track") {
      return ExecuteCreateCharacterTrack(
          client_,
          RequirePayloadStringValue(payload_json, "track_name", "trackName", "track_name"));
    }
    if (command == "select_track_by_name") {
      return ExecuteSelectTrackByName(
          client_,
          RequirePayloadStringValue(payload_json, "track_name", "trackName", "track_name"));
    }
    if (command == "ping") {
      return ExecutePing(client_);
    }
    if (command == "get_active_protocol") {
      return BuildStringValueJson("protocol", client_.ActiveProtocolVersionString());
    }
    if (command == "jump_timecode") {
      return ExecuteJumpTimecode(
          client_,
          RequirePayloadStringValue(payload_json, "timecode", nullptr, "timecode"));
    }
    if (command == "set_timeline_selection_range") {
      return ExecuteSetTimelineSelectionRange(
          client_,
          RequirePayloadStringValue(payload_json, "start_timecode", "startTimecode", "start_timecode"),
          RequirePayloadStringValue(payload_json, "end_timecode", "endTimecode", "end_timecode"));
    }
    if (command == "toggle_play_state") {
      return ExecuteTogglePlayState(client_);
    }
    if (command == "toggle_record_enable") {
      return ExecuteToggleRecordEnable(client_);
    }
    if (command == "consolidate_clip") {
      return ExecuteConsolidateClip(client_);
    }
    if (command == "get_session_path") {
      return ExecuteGetSessionPath(client_);
    }
    if (command == "get_transport_status") {
      return ExecuteGetTransportStatus(client_);
    }
    if (command == "get_transport_armed") {
      return ExecuteGetTransportArmed(client_);
    }
    if (command == "get_timeline_selection") {
      return ExecuteGetTimelineSelection(client_);
    }
    if (command == "set_timeline_rolls") {
      return ExecuteSetTimelineRolls(
          client_,
          ExtractPayloadOptionalLongLongValue(
              payload_json,
              "pre_roll_frames",
              "preRollFrames",
              "pre_roll_frames"),
          ExtractPayloadOptionalLongLongValue(
              payload_json,
              "post_roll_frames",
              "postRollFrames",
              "post_roll_frames"),
          ExtractPayloadOptionalLongLongValue(
              payload_json,
              "pre_roll_milliseconds",
              "preRollMilliseconds",
              "pre_roll_milliseconds"),
          ExtractPayloadOptionalLongLongValue(
              payload_json,
              "post_roll_milliseconds",
              "postRollMilliseconds",
              "post_roll_milliseconds"),
          ExtractPayloadOptionalBoolValue(
              payload_json,
              "pre_roll_enabled",
              "preRollEnabled"),
          ExtractPayloadOptionalBoolValue(
              payload_json,
              "post_roll_enabled",
              "postRollEnabled"));
    }
    if (command == "get_selected_clip_file") {
      return ExecuteGetSelectedClipFile(client_);
    }
    if (command == "write_selected_transcription_to_json_file") {
      return ExecuteWriteSelectedTranscriptionToJsonFile(client_);
    }
    if (command == "get_selected_clip_segments") {
      return ExecuteGetSelectedClipSegments(client_);
    }
    if (command == "resolve_clip_start_time_by_id") {
      return ExecuteResolveClipStartTimeById(
          client_,
          RequirePayloadStringValue(payload_json, "clip_id", "clipId", "clip_id"),
          RequirePayloadStringValue(payload_json, "reference_timecode", "referenceTimecode", "reference_timecode"));
    }
    if (command == "resolve_clip_start_time_by_id_or_name") {
      return ExecuteResolveClipStartTimeByIdOrName(
          client_,
          ExtractPayloadStringValue(payload_json, "clip_id", "clipId"),
          RequirePayloadStringValue(payload_json, "clip_name", "clipName", "clip_name"),
          RequirePayloadStringValue(payload_json, "reference_timecode", "referenceTimecode", "reference_timecode"));
    }
    if (command == "list_markers") {
      return ExecuteListMarkers(client_);
    }
    if (command == "clear_pt_markers") {
      return ExecuteClearPtMarkers(client_);
    }
    if (command == "edit_matching_marker") {
      return ExecuteEditMatchingMarker(
          client_,
          RequirePayloadStringValue(payload_json, "path", nullptr, "path"));
    }
    if (command == "delete_matching_marker") {
      return ExecuteDeleteMatchingMarker(
          client_,
          RequirePayloadStringValue(payload_json, "path", nullptr, "path"));
    }
    if (command == "list_tracks") {
      return ExecuteListTracks(client_, false);
    }
    if (command == "list_selected_tracks") {
      return ExecuteListTracks(client_, true);
    }
    if (command == "rename_tracks_from_plan") {
      return ExecuteRenameTracksFromPlan(
          client_,
          RequirePayloadStringValue(payload_json, "path", nullptr, "path"));
    }
    if (command == "set_track_mute_state") {
      return ExecuteSetTrackMuteState(
          client_,
          RequirePayloadStringValue(payload_json, "path", nullptr, "path"));
    }
    if (command == "rename_selected_clip") {
      const auto raw_mode = Trimmed(ExtractPayloadStringValue(payload_json, "mode", nullptr));
      const auto raw_separator = ExtractPayloadStringValue(payload_json, "separator", nullptr);
      return ExecuteRenameSelectedClip(
          client_,
          RequirePayloadStringValue(payload_json, "new_name", "newName", "new_name"),
          raw_mode.empty() ? std::string("replace-suffix") : raw_mode,
          raw_separator.empty() ? std::string("-") : raw_separator,
          ExtractPayloadBoolValue(payload_json, "rename_file", "renameFile", false));
    }
    if (command == "rename_selected_clip_from_current_marker_comment") {
      return ExecuteRenameSelectedClipFromCurrentMarkerComment(client_);
    }
    if (command == "drop_to_take") {
      return ExecuteDropToTake(
          client_,
          RequirePayloadStringValue(payload_json, "path", nullptr, "path"));
    }

    throw std::runtime_error(std::string("Unsupported helper server command: ") + command);
  }

  void UpdateDesiredSubscriptions(const std::vector<EventSubscription>& subscriptions) {
    EnsureClientConnected(client_);
    const auto current_session_id = client_.GetSessionId();

    std::unordered_map<std::string, EventSubscription> desired_map;
    desired_map.reserve(subscriptions.size());
    for (const auto& subscription : subscriptions) {
      desired_map.emplace(EventSubscriptionKey(subscription), subscription);
    }

    std::vector<EventSubscription> to_subscribe;
    std::vector<EventSubscription> to_unsubscribe;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (applied_session_id_ != current_session_id) {
        for (const auto& entry : desired_map) {
          to_subscribe.push_back(entry.second);
        }
      } else {
        for (const auto& entry : desired_map) {
          if (!applied_subscriptions_.count(entry.first)) {
            to_subscribe.push_back(entry.second);
          }
        }
        for (const auto& entry : applied_subscriptions_) {
          if (!desired_map.count(entry.first)) {
            to_unsubscribe.push_back(entry.second);
          }
        }
      }
    }

    if (!to_unsubscribe.empty()) {
      client_.UnsubscribeFromEvents(to_unsubscribe);
    }
    if (!to_subscribe.empty()) {
      client_.SubscribeToEvents(to_subscribe);
    }

    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      desired_subscriptions_ = desired_map;
      applied_subscriptions_ = desired_map;
      applied_session_id_ = current_session_id;
    }

    if (desired_map.empty()) {
      RequestPollCancel();
      return;
    }

    EnsurePollThreadStarted();
    poll_wake_cv_.notify_all();
  }

  void EnsurePollThreadStarted() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (poll_thread_.joinable()) {
      return;
    }
    poll_thread_ = std::thread([this]() { PollLoop(); });
  }

  void PollLoop() {
    while (!stop_requested_.load()) {
      {
        std::unique_lock<std::mutex> lock(state_mutex_);
        poll_wake_cv_.wait(lock, [this]() {
          return stop_requested_.load() || !desired_subscriptions_.empty();
        });
        if (stop_requested_.load()) {
          break;
        }
      }

      try {
        EnsureClientConnected(client_);
        const auto current_session_id = client_.GetSessionId();

        std::vector<EventSubscription> resubscribe;
        {
          std::lock_guard<std::mutex> lock(state_mutex_);
          if (applied_session_id_ != current_session_id) {
            for (const auto& entry : desired_subscriptions_) {
              resubscribe.push_back(entry.second);
            }
          }
        }
        if (!resubscribe.empty()) {
          client_.SubscribeToEvents(resubscribe);
          std::lock_guard<std::mutex> lock(state_mutex_);
          applied_subscriptions_ = desired_subscriptions_;
          applied_session_id_ = current_session_id;
        }

        client_.PollEvents(
            [this](const std::string& response_body_json) {
              WriteEvent("ptsl_event", response_body_json);
            },
            [this](grpc::ClientContext* context) {
              std::lock_guard<std::mutex> lock(state_mutex_);
              active_poll_context_ = context;
            });
      } catch (const std::exception& error) {
        HandleCommandError(error.what());
        if (stop_requested_.load()) {
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
    }
  }

  void HandleCommandError(std::string_view message) {
    if (!ShouldResetPtslSessionForError(message)) {
      return;
    }

    client_.ClearSession();
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      applied_session_id_.clear();
      applied_subscriptions_.clear();
    }
    RequestPollCancel();
  }

  void RequestPollCancel() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (active_poll_context_) {
      active_poll_context_->TryCancel();
    }
  }

  void RequestStop() {
    const bool was_stopped = stop_requested_.exchange(true);
    if (!was_stopped) {
      RequestPollCancel();
      poll_wake_cv_.notify_all();
    }
    if (poll_thread_.joinable()) {
      poll_thread_.join();
    }
  }

  void WriteReady() {
    std::lock_guard<std::mutex> lock(output_mutex_);
    std::cout << "READY\t1\n" << std::flush;
  }

  void WriteResponse(const std::string& request_id, bool ok, const std::string& payload) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    std::cout << "RES\t" << request_id << '\t' << (ok ? "OK" : "ERR") << '\t'
              << EncodeBase64(payload) << '\n'
              << std::flush;
  }

  void WriteEvent(const std::string& event_name, const std::string& payload) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    std::cout << "EVT\t" << event_name << '\t' << EncodeBase64(payload) << '\n'
              << std::flush;
  }

  PtslClient client_;
  std::atomic<bool> stop_requested_{false};
  std::mutex output_mutex_;
  std::mutex state_mutex_;
  std::condition_variable poll_wake_cv_;
  std::thread poll_thread_;
  grpc::ClientContext* active_poll_context_ = nullptr;
  std::string applied_session_id_;
  std::unordered_map<std::string, EventSubscription> desired_subscriptions_;
  std::unordered_map<std::string, EventSubscription> applied_subscriptions_;
};

}  // namespace

int main(int argc, char* argv[]) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--server") {
      HelperServer server;
      return server.Run();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--ping") {
      return RunPing();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--get-active-protocol") {
      return RunGetActiveProtocol();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--dump-session-edl") {
      return RunDumpSessionEdl();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--dump-session-info-full") {
      return RunDumpSessionInfoFull();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--dump-raw-clip-list") {
      return RunDumpRawClipList();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--dump-selected-playlist-elements") {
      return RunDumpSelectedPlaylistElements();
    }

    if ((argc == 3 || argc == 4) && std::string_view(argv[1]) == "--probe-command") {
      return RunProbeCommand(argv[2], argc == 4 ? std::optional<std::string>(argv[3]) : std::nullopt);
    }

    if (argc == 3 && std::string_view(argv[1]) == "--jump-timecode") {
      return RunJumpTimecode(argv[2]);
    }

    if (argc == 4 && std::string_view(argv[1]) == "--set-timeline-selection-range") {
      PtslClient client;
      EnsureClientConnected(client);
      std::cout << ExecuteSetTimelineSelectionRange(client, argv[2], argv[3]) << '\n';
      return 0;
    }

    if (argc == 2 && std::string_view(argv[1]) == "--toggle-play-state") {
      return RunTogglePlayState();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--toggle-record-enable") {
      return RunToggleRecordEnable();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--consolidate-clip") {
      return RunConsolidateClip();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--get-session-path") {
      return RunGetSessionPath();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--get-transport-status") {
      return RunGetTransportStatus();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--get-transport-armed") {
      return RunGetTransportArmed();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--get-timeline-selection") {
      return RunGetTimelineSelection();
    }

    if (argc == 3 && std::string_view(argv[1]) == "--set-timeline-rolls") {
      return RunSetTimelineRolls(argv[2]);
    }

    if (argc == 2 && std::string_view(argv[1]) == "--get-selected-clip-file") {
      return RunGetSelectedClipFile();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--write-selected-transcription-to-json-file") {
      return RunWriteSelectedTranscriptionToJsonFile();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--get-selected-clip-segments") {
      return RunGetSelectedClipSegments();
    }

    if (argc == 4 && std::string_view(argv[1]) == "--resolve-clip-start-time-by-id") {
      return RunResolveClipStartTimeById(argv[2], argv[3]);
    }

    if (argc == 5 && std::string_view(argv[1]) == "--resolve-clip-start-time-by-id-or-name") {
      return RunResolveClipStartTimeByIdOrName(argv[2], argv[3], argv[4]);
    }

    if (argc == 2 && std::string_view(argv[1]) == "--list-markers") {
      return RunListMarkers();
    }

    if (argc == 2 && std::string_view(argv[1]) == "--clear-pt-markers") {
      return RunClearPtMarkers();
    }

    if (argc == 3 && std::string_view(argv[1]) == "--edit-matching-marker") {
      return RunEditMatchingMarker(argv[2]);
    }

    if (argc == 3 && std::string_view(argv[1]) == "--delete-matching-marker") {
      return RunDeleteMatchingMarker(argv[2]);
    }

    if (argc == 2 && std::string_view(argv[1]) == "--list-tracks") {
      return RunListTracks(false);
    }

    if (argc == 2 && std::string_view(argv[1]) == "--list-selected-tracks") {
      return RunListTracks(true);
    }

    if (argc == 3 && std::string_view(argv[1]) == "--rename-tracks") {
      return RunRenameTracks(argv[2]);
    }

    if (argc == 3 && std::string_view(argv[1]) == "--rename-tracks-from-plan") {
      return RunRenameTracksFromPlan(argv[2]);
    }

    if (argc == 3 && std::string_view(argv[1]) == "--set-track-mute-state") {
      return RunSetTrackMuteState(argv[2]);
    }

    if (argc == 2 && std::string_view(argv[1]) == "--rename-selected-clip-from-current-marker-comment") {
      return RunRenameSelectedClipFromCurrentMarkerComment();
    }

    if (argc >= 3 && argc <= 6 && std::string_view(argv[1]) == "--rename-selected-clip") {
      const std::string mode =
          argc >= 4 && std::string_view(argv[3]) != "--rename-file"
              ? std::string(argv[3])
              : std::string("replace-suffix");
      const std::string separator =
          argc >= 5 && std::string_view(argv[4]) != "--rename-file"
              ? std::string(argv[4])
              : std::string("-");
      const bool rename_file =
          (argc >= 4 && std::string_view(argv[argc - 1]) == "--rename-file");
      return RunRenameSelectedClip(argv[2], mode, separator, rename_file);
    }

    if (argc == 3 && std::string_view(argv[1]) == "--drop-to-take") {
      return RunDropToTake(argv[2]);
    }

    if (
        (argc == 3 || argc == 4)
        && std::string_view(argv[1]) == "--create-pt-clip-groups") {
      const bool avoid_overlaps = argc == 4 && std::string_view(argv[3]) == "--avoid-overlaps";
      if (argc == 4 && !avoid_overlaps) {
        PrintUsage();
        return 1;
      }
      return RunCreatePtClipGroups(argv[2], avoid_overlaps);
    }

    if (argc == 3 && std::string_view(argv[1]) == "--delete-pt-cue-clips") {
      return RunDeletePtCueClips(argv[2]);
    }

    if (argc == 3 && std::string_view(argv[1]) == "--create-clip-group-from-selection") {
      return RunCreateClipGroupFromSelection(argv[2]);
    }

    if (
        (argc == 3 || argc == 4)
        && std::string_view(argv[1]) == "--make-pt-clip-tracks") {
      return RunMakePtClipTracks(
          argv[2],
          ParseMakePtClipTracksMode(argc == 4 ? std::string_view(argv[3]) : std::string_view("per-character")));
    }

    if (argc == 3 && std::string_view(argv[1]) == "--create-character-track") {
      return RunCreateCharacterTrack(argv[2]);
    }

    if (argc == 3 && std::string_view(argv[1]) == "--select-track-by-name") {
      return RunSelectTrackByName(argv[2]);
    }

    if (argc != 2) {
      PrintUsage();
      return 1;
    }

    return RunFromFile(argv[1]);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
