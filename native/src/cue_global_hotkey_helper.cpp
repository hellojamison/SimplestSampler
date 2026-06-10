#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>

#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct ShortcutBinding {
  bool valid = false;
  CGKeyCode keyCode = 0;
  UInt32 modifiers = 0;
};

enum HotKeyActionId : UInt32 {
  kHotKeyActionCreateCue = 1,
  kHotKeyActionCreateStreamerCue,
  kHotKeyActionAddApple,
  kHotKeyActionPreviousMarker,
  kHotKeyActionNextMarker,
  kHotKeyActionResetToCurrentCue,
  kHotKeyActionMarkCurrentCueDone,
  kHotKeyActionExtendMarkerSelectionPrevious,
  kHotKeyActionExtendMarkerSelectionNext,
  kHotKeyActionRenameCapturedTracks,
  kHotKeyActionSetRecordTracks,
  kHotKeyActionDropToTake,
  kHotKeyActionMoveSelectedMarkerToPlayhead,
  kHotKeyActionRenameSelectedPtClipFromCueText,
  kHotKeyActionRenameSelectedPtClipFromPromptedText,
  kHotKeyActionRenameSelectedPtClipFromClipboard,
  kHotKeyActionStorePtClipRenameText,
  kHotKeyActionRenameSelectedPtClipFromStoredText,
  kHotKeyActionToggleOverlayDrawTool,
  kHotKeyActionPreviewMode,
  kHotKeyActionPlaybackMode,
  kHotKeyActionRecordMode,
  kHotKeyActionRecordInPlaceWithBeeps,
  kHotKeyActionSamplerCapture,
  kHotKeyActionSamplerCaptureSlot1,
  kHotKeyActionSamplerCaptureSlot2,
  kHotKeyActionSamplerCaptureSlot3,
  kHotKeyActionSamplerCaptureSlot4,
  kHotKeyActionSamplerSlot1,
  kHotKeyActionSamplerSlot2,
  kHotKeyActionSamplerSlot3,
  kHotKeyActionSamplerSlot4,
};

struct HotKeyRegistration {
  const char* optionName;
  const char* message;
  UInt32 actionId;
  ShortcutBinding* binding;
  EventHotKeyRef hotKeyRef = nullptr;
};

struct DynamicTransportModeRegistration {
  std::string message;
  UInt32 actionId = 0;
  ShortcutBinding binding{};
  EventHotKeyRef hotKeyRef = nullptr;
  bool monitorOnly = false;
};

constexpr OSType kHotKeySignature = 0x4F437565;  // 'OCue'
constexpr UInt32 kFirstDynamicTransportModeActionId = 1000;

ShortcutBinding gCreateCueShortcut{};
ShortcutBinding gCreateStreamerCueShortcut{};
ShortcutBinding gAddAppleShortcut{};
ShortcutBinding gPreviousMarkerShortcut{};
ShortcutBinding gNextMarkerShortcut{};
ShortcutBinding gResetToCurrentCueShortcut{};
ShortcutBinding gMarkCurrentCueDoneShortcut{};
ShortcutBinding gExtendMarkerSelectionPreviousShortcut{};
ShortcutBinding gExtendMarkerSelectionNextShortcut{};
ShortcutBinding gRenameCapturedTracksShortcut{};
ShortcutBinding gSetRecordTracksShortcut{};
ShortcutBinding gDropToTakeShortcut{};
ShortcutBinding gMoveSelectedMarkerToPlayheadShortcut{};
ShortcutBinding gRenameSelectedPtClipFromCueTextShortcut{};
ShortcutBinding gRenameSelectedPtClipFromPromptedTextShortcut{};
ShortcutBinding gRenameSelectedPtClipFromClipboardShortcut{};
ShortcutBinding gStorePtClipRenameTextShortcut{};
ShortcutBinding gRenameSelectedPtClipFromStoredTextShortcut{};
ShortcutBinding gToggleOverlayDrawToolShortcut{};
ShortcutBinding gPreviewModeShortcut{};
ShortcutBinding gPlaybackModeShortcut{};
ShortcutBinding gRecordModeShortcut{};
ShortcutBinding gRecordInPlaceWithBeepsShortcut{};
ShortcutBinding gSamplerCaptureShortcut{};
ShortcutBinding gSamplerCaptureSlotShortcuts[4] = {
    {},
    {},
    {},
    {},
};
ShortcutBinding gSamplerSlotShortcuts[4] = {
    {},
    {},
    {},
    {},
};

HotKeyRegistration gHotKeyRegistrations[] = {
    {"--create-cue-shortcut", "create-cue", kHotKeyActionCreateCue, &gCreateCueShortcut},
    {"--create-streamer-shortcut", "create-streamer-cue", kHotKeyActionCreateStreamerCue, &gCreateStreamerCueShortcut},
    {"--add-apple-shortcut", "add-apple", kHotKeyActionAddApple, &gAddAppleShortcut},
    {"--previous-marker-shortcut", "previous-marker", kHotKeyActionPreviousMarker, &gPreviousMarkerShortcut},
    {"--next-marker-shortcut", "next-marker", kHotKeyActionNextMarker, &gNextMarkerShortcut},
    {"--reset-to-current-cue-shortcut", "reset-to-current-cue", kHotKeyActionResetToCurrentCue, &gResetToCurrentCueShortcut},
    {"--mark-current-cue-done-shortcut", "mark-current-cue-done", kHotKeyActionMarkCurrentCueDone, &gMarkCurrentCueDoneShortcut},
    {"--extend-marker-selection-previous-shortcut", "extend-marker-selection-previous", kHotKeyActionExtendMarkerSelectionPrevious, &gExtendMarkerSelectionPreviousShortcut},
    {"--extend-marker-selection-next-shortcut", "extend-marker-selection-next", kHotKeyActionExtendMarkerSelectionNext, &gExtendMarkerSelectionNextShortcut},
    {"--rename-captured-tracks-shortcut", "rename-captured-tracks", kHotKeyActionRenameCapturedTracks, &gRenameCapturedTracksShortcut},
    {"--set-record-tracks-shortcut", "set-record-tracks", kHotKeyActionSetRecordTracks, &gSetRecordTracksShortcut},
    {"--drop-to-take-shortcut", "drop-to-take", kHotKeyActionDropToTake, &gDropToTakeShortcut},
    {"--move-selected-marker-to-playhead-shortcut", "move-selected-marker-to-playhead", kHotKeyActionMoveSelectedMarkerToPlayhead, &gMoveSelectedMarkerToPlayheadShortcut},
    {"--rename-selected-pt-clip-from-cue-text-shortcut", "rename-selected-pt-clip-from-cue-text", kHotKeyActionRenameSelectedPtClipFromCueText, &gRenameSelectedPtClipFromCueTextShortcut},
    {"--rename-selected-pt-clip-from-prompted-text-shortcut", "rename-selected-pt-clip-from-prompted-text", kHotKeyActionRenameSelectedPtClipFromPromptedText, &gRenameSelectedPtClipFromPromptedTextShortcut},
    {"--rename-selected-pt-clip-from-clipboard-shortcut", "rename-selected-pt-clip-from-clipboard", kHotKeyActionRenameSelectedPtClipFromClipboard, &gRenameSelectedPtClipFromClipboardShortcut},
    {"--store-pt-clip-rename-text-shortcut", "store-pt-clip-rename-text", kHotKeyActionStorePtClipRenameText, &gStorePtClipRenameTextShortcut},
    {"--rename-selected-pt-clip-from-stored-text-shortcut", "rename-selected-pt-clip-from-stored-text", kHotKeyActionRenameSelectedPtClipFromStoredText, &gRenameSelectedPtClipFromStoredTextShortcut},
    {"--toggle-overlay-draw-tool-shortcut", "toggle-overlay-draw-tool", kHotKeyActionToggleOverlayDrawTool, &gToggleOverlayDrawToolShortcut},
    {"--preview-mode-shortcut", "preview-mode", kHotKeyActionPreviewMode, &gPreviewModeShortcut},
    {"--playback-mode-shortcut", "playback-mode", kHotKeyActionPlaybackMode, &gPlaybackModeShortcut},
    {"--record-mode-shortcut", "record-mode", kHotKeyActionRecordMode, &gRecordModeShortcut},
    {"--record-in-place-with-beeps-shortcut", "record-in-place-with-beeps", kHotKeyActionRecordInPlaceWithBeeps, &gRecordInPlaceWithBeepsShortcut},
    {"--sampler-capture-shortcut", "capture-sampler-file", kHotKeyActionSamplerCapture, &gSamplerCaptureShortcut},
    {"--sampler-capture-slot-1-shortcut", "capture-sampler-slot-1", kHotKeyActionSamplerCaptureSlot1, &gSamplerCaptureSlotShortcuts[0]},
    {"--sampler-capture-slot-2-shortcut", "capture-sampler-slot-2", kHotKeyActionSamplerCaptureSlot2, &gSamplerCaptureSlotShortcuts[1]},
    {"--sampler-capture-slot-3-shortcut", "capture-sampler-slot-3", kHotKeyActionSamplerCaptureSlot3, &gSamplerCaptureSlotShortcuts[2]},
    {"--sampler-capture-slot-4-shortcut", "capture-sampler-slot-4", kHotKeyActionSamplerCaptureSlot4, &gSamplerCaptureSlotShortcuts[3]},
    {"--sampler-slot-1-shortcut", "play-sampler-slot-1", kHotKeyActionSamplerSlot1, &gSamplerSlotShortcuts[0]},
    {"--sampler-slot-2-shortcut", "play-sampler-slot-2", kHotKeyActionSamplerSlot2, &gSamplerSlotShortcuts[1]},
    {"--sampler-slot-3-shortcut", "play-sampler-slot-3", kHotKeyActionSamplerSlot3, &gSamplerSlotShortcuts[2]},
    {"--sampler-slot-4-shortcut", "play-sampler-slot-4", kHotKeyActionSamplerSlot4, &gSamplerSlotShortcuts[3]},
};

EventHandlerRef gHotKeyEventHandler = nullptr;
EventTargetRef gHotKeyEventTarget = nullptr;
EventHandlerRef gEventMonitorHandler = nullptr;
std::vector<DynamicTransportModeRegistration> gDynamicTransportModeRegistrations;
std::vector<UInt32> gObservedTransportKeysDown;
volatile std::sig_atomic_t gShouldExit = 0;
bool gMonitorShift = false;
bool gShiftPressed = false;

void LogDebug(const std::string& message) {
  const char* enabled = std::getenv("OVERCUE_HOTKEY_HELPER_DEBUG");
  if (enabled == nullptr || std::string_view(enabled) != "1") {
    return;
  }
  std::cerr << "[hotkey-helper-debug] " << message << std::endl;
}

void PrepareProcessForGlobalHotKeys() {
  ProcessSerialNumber processSerialNumber{0, kCurrentProcess};
  const OSStatus transformStatus = TransformProcessType(
      &processSerialNumber,
      kProcessTransformToUIElementApplication);
  LogDebug(std::string("transform-process-type status=") + std::to_string(transformStatus));
}

std::string LowercaseCopy(std::string_view text) {
  std::string lowered(text);
  for (char& value : lowered) {
    value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  }
  return lowered;
}

std::string TrimCopy(std::string_view text) {
  std::size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
    start += 1;
  }

  std::size_t end = text.size();
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    end -= 1;
  }

  return std::string(text.substr(start, end - start));
}

bool TryParseModifierToken(std::string_view token, UInt32* modifierFlags) {
  if (modifierFlags == nullptr) {
    return false;
  }

  const std::string lowered = LowercaseCopy(token);
  if (lowered == "cmd" || lowered == "command" || lowered == "commandorcontrol" || lowered == "cmdorctrl" || lowered == "cmdorcontrol") {
    *modifierFlags |= cmdKey;
    return true;
  }
  if (lowered == "ctrl" || lowered == "control") {
    *modifierFlags |= controlKey;
    return true;
  }
  if (lowered == "alt" || lowered == "opt" || lowered == "option") {
    *modifierFlags |= optionKey;
    return true;
  }
  if (lowered == "shift") {
    *modifierFlags |= shiftKey;
    return true;
  }
  return false;
}

bool TryParseKeyToken(std::string_view token, CGKeyCode* keyCode) {
  if (keyCode == nullptr) {
    return false;
  }

  const std::string lowered = LowercaseCopy(token);
  if (lowered.size() == 1) {
    switch (lowered[0]) {
      case 'a': *keyCode = kVK_ANSI_A; return true;
      case 'b': *keyCode = kVK_ANSI_B; return true;
      case 'c': *keyCode = kVK_ANSI_C; return true;
      case 'd': *keyCode = kVK_ANSI_D; return true;
      case 'e': *keyCode = kVK_ANSI_E; return true;
      case 'f': *keyCode = kVK_ANSI_F; return true;
      case 'g': *keyCode = kVK_ANSI_G; return true;
      case 'h': *keyCode = kVK_ANSI_H; return true;
      case 'i': *keyCode = kVK_ANSI_I; return true;
      case 'j': *keyCode = kVK_ANSI_J; return true;
      case 'k': *keyCode = kVK_ANSI_K; return true;
      case 'l': *keyCode = kVK_ANSI_L; return true;
      case 'm': *keyCode = kVK_ANSI_M; return true;
      case 'n': *keyCode = kVK_ANSI_N; return true;
      case 'o': *keyCode = kVK_ANSI_O; return true;
      case 'p': *keyCode = kVK_ANSI_P; return true;
      case 'q': *keyCode = kVK_ANSI_Q; return true;
      case 'r': *keyCode = kVK_ANSI_R; return true;
      case 's': *keyCode = kVK_ANSI_S; return true;
      case 't': *keyCode = kVK_ANSI_T; return true;
      case 'u': *keyCode = kVK_ANSI_U; return true;
      case 'v': *keyCode = kVK_ANSI_V; return true;
      case 'w': *keyCode = kVK_ANSI_W; return true;
      case 'x': *keyCode = kVK_ANSI_X; return true;
      case 'y': *keyCode = kVK_ANSI_Y; return true;
      case 'z': *keyCode = kVK_ANSI_Z; return true;
      case '0': *keyCode = kVK_ANSI_0; return true;
      case '1': *keyCode = kVK_ANSI_1; return true;
      case '2': *keyCode = kVK_ANSI_2; return true;
      case '3': *keyCode = kVK_ANSI_3; return true;
      case '4': *keyCode = kVK_ANSI_4; return true;
      case '5': *keyCode = kVK_ANSI_5; return true;
      case '6': *keyCode = kVK_ANSI_6; return true;
      case '7': *keyCode = kVK_ANSI_7; return true;
      case '8': *keyCode = kVK_ANSI_8; return true;
      case '9': *keyCode = kVK_ANSI_9; return true;
      case ',': *keyCode = kVK_ANSI_Comma; return true;
      case '.': *keyCode = kVK_ANSI_Period; return true;
      case '/': *keyCode = kVK_ANSI_Slash; return true;
      case ';': *keyCode = kVK_ANSI_Semicolon; return true;
      case '\'': *keyCode = kVK_ANSI_Quote; return true;
      case '[': *keyCode = kVK_ANSI_LeftBracket; return true;
      case ']': *keyCode = kVK_ANSI_RightBracket; return true;
      case '\\': *keyCode = kVK_ANSI_Backslash; return true;
      case '-': *keyCode = kVK_ANSI_Minus; return true;
      case '=': *keyCode = kVK_ANSI_Equal; return true;
      case '`': *keyCode = kVK_ANSI_Grave; return true;
      default: break;
    }
  }

  if (lowered == "delete" || lowered == "backspace") {
    *keyCode = kVK_Delete;
    return true;
  }
  if (lowered == "space" || lowered == "spacebar") {
    *keyCode = kVK_Space;
    return true;
  }
  if (lowered == "home") {
    *keyCode = kVK_Home;
    return true;
  }
  if (lowered == "insert") {
    *keyCode = kVK_Help;
    return true;
  }
  if (lowered == "end") {
    *keyCode = kVK_End;
    return true;
  }
  if (lowered == "pageup") {
    *keyCode = kVK_PageUp;
    return true;
  }
  if (lowered == "pagedown") {
    *keyCode = kVK_PageDown;
    return true;
  }
  if (lowered == "escape" || lowered == "esc") {
    *keyCode = kVK_Escape;
    return true;
  }
  if (lowered == "tab") {
    *keyCode = kVK_Tab;
    return true;
  }
  if (lowered == "enter" || lowered == "return") {
    *keyCode = kVK_Return;
    return true;
  }
  if (lowered == "numpadenter" || lowered == "numenter") {
    *keyCode = kVK_ANSI_KeypadEnter;
    return true;
  }
  if (lowered == "left") {
    *keyCode = kVK_LeftArrow;
    return true;
  }
  if (lowered == "right") {
    *keyCode = kVK_RightArrow;
    return true;
  }
  if (lowered == "up") {
    *keyCode = kVK_UpArrow;
    return true;
  }
  if (lowered == "down") {
    *keyCode = kVK_DownArrow;
    return true;
  }
  if (lowered == "f13") {
    *keyCode = kVK_F13;
    return true;
  }
  if (lowered == "f1") {
    *keyCode = kVK_F1;
    return true;
  }
  if (lowered == "f2") {
    *keyCode = kVK_F2;
    return true;
  }
  if (lowered == "f3") {
    *keyCode = kVK_F3;
    return true;
  }
  if (lowered == "f4") {
    *keyCode = kVK_F4;
    return true;
  }
  if (lowered == "f5") {
    *keyCode = kVK_F5;
    return true;
  }
  if (lowered == "f6") {
    *keyCode = kVK_F6;
    return true;
  }
  if (lowered == "f7") {
    *keyCode = kVK_F7;
    return true;
  }
  if (lowered == "f8") {
    *keyCode = kVK_F8;
    return true;
  }
  if (lowered == "f9") {
    *keyCode = kVK_F9;
    return true;
  }
  if (lowered == "f10") {
    *keyCode = kVK_F10;
    return true;
  }
  if (lowered == "f11") {
    *keyCode = kVK_F11;
    return true;
  }
  if (lowered == "f12") {
    *keyCode = kVK_F12;
    return true;
  }
  if (lowered == "f14") {
    *keyCode = kVK_F14;
    return true;
  }
  if (lowered == "f15") {
    *keyCode = kVK_F15;
    return true;
  }
  if (lowered == "f16") {
    *keyCode = kVK_F16;
    return true;
  }
  if (lowered == "f17") {
    *keyCode = kVK_F17;
    return true;
  }
  if (lowered == "f18") {
    *keyCode = kVK_F18;
    return true;
  }
  if (lowered == "f19") {
    *keyCode = kVK_F19;
    return true;
  }
  if (lowered == "f20") {
    *keyCode = kVK_F20;
    return true;
  }
  if (lowered == "numpad0") {
    *keyCode = kVK_ANSI_Keypad0;
    return true;
  }
  if (lowered == "numpad1") {
    *keyCode = kVK_ANSI_Keypad1;
    return true;
  }
  if (lowered == "numpad2") {
    *keyCode = kVK_ANSI_Keypad2;
    return true;
  }
  if (lowered == "numpad3") {
    *keyCode = kVK_ANSI_Keypad3;
    return true;
  }
  if (lowered == "numpad4") {
    *keyCode = kVK_ANSI_Keypad4;
    return true;
  }
  if (lowered == "numpad5") {
    *keyCode = kVK_ANSI_Keypad5;
    return true;
  }
  if (lowered == "numpad6") {
    *keyCode = kVK_ANSI_Keypad6;
    return true;
  }
  if (lowered == "numpad7") {
    *keyCode = kVK_ANSI_Keypad7;
    return true;
  }
  if (lowered == "numpad8") {
    *keyCode = kVK_ANSI_Keypad8;
    return true;
  }
  if (lowered == "numpad9") {
    *keyCode = kVK_ANSI_Keypad9;
    return true;
  }
  if (lowered == "numpadadd" || lowered == "numadd") {
    *keyCode = kVK_ANSI_KeypadPlus;
    return true;
  }
  if (lowered == "numpadsubtract" || lowered == "numsubtract") {
    *keyCode = kVK_ANSI_KeypadMinus;
    return true;
  }
  if (lowered == "numpadmultiply" || lowered == "nummultiply") {
    *keyCode = kVK_ANSI_KeypadMultiply;
    return true;
  }
  if (lowered == "numpaddivide" || lowered == "numdivide") {
    *keyCode = kVK_ANSI_KeypadDivide;
    return true;
  }
  if (lowered == "numpaddecimal" || lowered == "numdecimal") {
    *keyCode = kVK_ANSI_KeypadDecimal;
    return true;
  }

  return false;
}

bool ParseShortcutBinding(std::string_view accelerator, ShortcutBinding* binding) {
  if (binding == nullptr) {
    return false;
  }

  ShortcutBinding nextBinding{};
  std::string text(accelerator);
  std::size_t start = 0;
  bool hasKey = false;
  while (start <= text.size()) {
    const std::size_t delimiter = text.find('+', start);
    const std::string part = text.substr(start, delimiter == std::string::npos ? std::string::npos : delimiter - start);
    if (part.empty()) {
      return false;
    }

    if (TryParseModifierToken(part, &nextBinding.modifiers)) {
      start = delimiter == std::string::npos ? text.size() + 1 : delimiter + 1;
      continue;
    }

    if (hasKey || !TryParseKeyToken(part, &nextBinding.keyCode)) {
      return false;
    }
    hasKey = true;
    start = delimiter == std::string::npos ? text.size() + 1 : delimiter + 1;
  }

  if (!hasKey) {
    return false;
  }

  nextBinding.valid = true;
  *binding = nextBinding;
  return true;
}

bool ConfigureShortcutBinding(std::string_view optionName, std::string_view accelerator, ShortcutBinding* binding) {
  if (binding == nullptr) {
    return false;
  }

  const std::string trimmedAccelerator = TrimCopy(accelerator);
  if (trimmedAccelerator.empty()) {
    *binding = ShortcutBinding{};
    return true;
  }

  if (ParseShortcutBinding(trimmedAccelerator, binding)) {
    return true;
  }

  std::cerr << "Invalid shortcut for " << optionName << ": " << trimmedAccelerator << std::endl;
  return false;
}

bool ConfigureDynamicTransportModeShortcut(std::string_view optionName, std::string_view spec, bool monitorOnly) {
  const std::string trimmedSpec = TrimCopy(spec);
  const std::size_t delimiter = trimmedSpec.find("::");
  if (delimiter == std::string::npos) {
    std::cerr << "Invalid transport mode shortcut specification: " << trimmedSpec << std::endl;
    return false;
  }

  const std::string modeId = TrimCopy(trimmedSpec.substr(0, delimiter));
  if (modeId.empty()) {
    std::cerr << "Transport mode shortcut is missing a mode id." << std::endl;
    return false;
  }

  DynamicTransportModeRegistration registration{};
  registration.message = std::string("transport-mode:") + modeId;
  registration.actionId = kFirstDynamicTransportModeActionId + static_cast<UInt32>(gDynamicTransportModeRegistrations.size());
  if (!ConfigureShortcutBinding(optionName, trimmedSpec.substr(delimiter + 2), &registration.binding)) {
    return false;
  }
  registration.monitorOnly = monitorOnly && registration.binding.valid;

  gDynamicTransportModeRegistrations.push_back(registration);
  return true;
}

bool ParseShortcutArgs(int argc, char* argv[]) {
  for (int index = 1; index < argc; index += 1) {
    const std::string_view argument = argv[index];
    if (argument == "--check-permission" || argument == "--request-permission") {
      continue;
    }
    if (argument == "--monitor-shift") {
      gMonitorShift = true;
      continue;
    }
    if (argument == "--transport-mode-shortcut") {
      if ((index + 1) >= argc || !ConfigureDynamicTransportModeShortcut(argument, argv[index + 1], false)) {
        return false;
      }
      index += 1;
      continue;
    }

    if (argument == "--observed-transport-mode-shortcut") {
      if ((index + 1) >= argc || !ConfigureDynamicTransportModeShortcut(argument, argv[index + 1], true)) {
        return false;
      }
      index += 1;
      continue;
    }

    bool handled = false;
    for (HotKeyRegistration& registration : gHotKeyRegistrations) {
      if (argument != registration.optionName) {
        continue;
      }

      if ((index + 1) >= argc || !ConfigureShortcutBinding(argument, argv[index + 1], registration.binding)) {
        return false;
      }
      index += 1;
      handled = true;
      break;
    }

    if (!handled) {
      std::cerr << "Unknown argument: " << argument << std::endl;
      return false;
    }
  }
  return true;
}

void CleanupHotKeys() {
  for (HotKeyRegistration& registration : gHotKeyRegistrations) {
    if (registration.hotKeyRef != nullptr) {
      UnregisterEventHotKey(registration.hotKeyRef);
      registration.hotKeyRef = nullptr;
    }
  }

  for (DynamicTransportModeRegistration& registration : gDynamicTransportModeRegistrations) {
    if (registration.hotKeyRef != nullptr) {
      UnregisterEventHotKey(registration.hotKeyRef);
      registration.hotKeyRef = nullptr;
    }
  }
  gDynamicTransportModeRegistrations.clear();

  if (gHotKeyEventHandler != nullptr) {
    RemoveEventHandler(gHotKeyEventHandler);
    gHotKeyEventHandler = nullptr;
  }

  if (gEventMonitorHandler != nullptr) {
    RemoveEventHandler(gEventMonitorHandler);
    gEventMonitorHandler = nullptr;
  }
}

bool IsObservedTransportKeyDown(UInt32 keyCode) {
  return std::find(
      gObservedTransportKeysDown.begin(),
      gObservedTransportKeysDown.end(),
      keyCode) != gObservedTransportKeysDown.end();
}

void MarkObservedTransportKeyDown(UInt32 keyCode) {
  if (IsObservedTransportKeyDown(keyCode)) {
    return;
  }
  gObservedTransportKeysDown.push_back(keyCode);
}

void ClearObservedTransportKeyDown(UInt32 keyCode) {
  gObservedTransportKeysDown.erase(
      std::remove(
          gObservedTransportKeysDown.begin(),
          gObservedTransportKeysDown.end(),
          keyCode),
      gObservedTransportKeysDown.end());
}

void HandleTerminationSignal(int) {
  gShouldExit = 1;
  CleanupHotKeys();
}

OSStatus HandleHotKeyEvent(EventHandlerCallRef, EventRef event, void*) {
  EventHotKeyID hotKeyId{};
  OSStatus status = GetEventParameter(
      event,
      kEventParamDirectObject,
      typeEventHotKeyID,
      nullptr,
      sizeof(hotKeyId),
      nullptr,
      &hotKeyId);
  if (status != noErr || hotKeyId.signature != kHotKeySignature) {
    return noErr;
  }

  for (const HotKeyRegistration& registration : gHotKeyRegistrations) {
    if (registration.actionId != hotKeyId.id) {
      continue;
    }

    std::cout << registration.message << std::endl;
    std::cout.flush();
    return noErr;
  }

  for (const DynamicTransportModeRegistration& registration : gDynamicTransportModeRegistrations) {
    if (registration.actionId != hotKeyId.id) {
      continue;
    }

    std::cout << registration.message << std::endl;
    std::cout.flush();
    return noErr;
  }

  return noErr;
}

OSStatus HandleEventMonitor(EventHandlerCallRef, EventRef event, void*) {
  if (GetEventClass(event) != kEventClassKeyboard) {
    return noErr;
  }

  const UInt32 eventKind = GetEventKind(event);
  UInt32 modifiers = 0;
  OSStatus status = GetEventParameter(
      event,
      kEventParamKeyModifiers,
      typeUInt32,
      nullptr,
      sizeof(modifiers),
      nullptr,
      &modifiers);
  if (status != noErr) {
    return noErr;
  }

  const bool shiftPressed = (modifiers & shiftKey) != 0;
  if (gMonitorShift && eventKind == kEventRawKeyModifiersChanged && shiftPressed != gShiftPressed) {
    gShiftPressed = shiftPressed;
    std::cout << (shiftPressed ? "modifier:shift:down" : "modifier:shift:up") << std::endl;
    std::cout.flush();
  }

  if (eventKind != kEventRawKeyDown && eventKind != kEventRawKeyUp) {
    return noErr;
  }

  UInt32 keyCode = 0;
  status = GetEventParameter(
      event,
      kEventParamKeyCode,
      typeUInt32,
      nullptr,
      sizeof(keyCode),
      nullptr,
      &keyCode);
  if (status != noErr) {
    return noErr;
  }

  if (eventKind == kEventRawKeyUp) {
    ClearObservedTransportKeyDown(keyCode);
    return noErr;
  }

  const UInt32 normalizedModifiers = modifiers & (cmdKey | controlKey | optionKey | shiftKey);
  for (const DynamicTransportModeRegistration& registration : gDynamicTransportModeRegistrations) {
    if (!registration.monitorOnly || !registration.binding.valid) {
      continue;
    }
    if (registration.binding.keyCode != keyCode || registration.binding.modifiers != normalizedModifiers) {
      continue;
    }
    if (IsObservedTransportKeyDown(keyCode)) {
      break;
    }

    MarkObservedTransportKeyDown(keyCode);
    std::cout << "observed:" << registration.message << std::endl;
    std::cout.flush();
    break;
  }

  return noErr;
}

bool StartListening() {
  LogDebug("start-listening-enter");
  const EventTypeSpec eventType = {kEventClassKeyboard, kEventHotKeyPressed};
  EventTargetRef hotKeyEventTarget = GetEventDispatcherTarget();
  if (hotKeyEventTarget == nullptr) {
    hotKeyEventTarget = GetApplicationEventTarget();
    LogDebug("using-application-event-target-fallback");
  } else {
    LogDebug("using-event-dispatcher-target");
  }
  gHotKeyEventTarget = hotKeyEventTarget;

  LogDebug("install-event-handler-begin");
  const OSStatus installStatus = InstallEventHandler(
      hotKeyEventTarget,
      &HandleHotKeyEvent,
      1,
      &eventType,
      nullptr,
      &gHotKeyEventHandler);
  LogDebug(std::string("install-event-handler-end status=") + std::to_string(installStatus));
  if (installStatus != noErr) {
    std::cerr << "failed to install hotkey event handler" << std::endl;
    return false;
  }

  bool needsEventMonitor = false;
  for (const DynamicTransportModeRegistration& registration : gDynamicTransportModeRegistrations) {
    if (registration.monitorOnly) {
      needsEventMonitor = true;
      break;
    }
  }
  needsEventMonitor = needsEventMonitor || gMonitorShift;

  if (needsEventMonitor) {
    if (!AXIsProcessTrusted()) {
      LogDebug("event-monitor-accessibility-untrusted");
    }
    const EventTypeSpec monitorEventTypes[] = {
      {kEventClassKeyboard, kEventRawKeyDown},
      {kEventClassKeyboard, kEventRawKeyUp},
      {kEventClassKeyboard, kEventRawKeyModifiersChanged}
    };
    const EventTargetRef eventMonitorTarget = GetEventMonitorTarget();
    if (eventMonitorTarget == nullptr) {
      LogDebug("event-monitor-missing-target");
    } else {
      const OSStatus monitorInstallStatus = InstallEventHandler(
          eventMonitorTarget,
          &HandleEventMonitor,
          GetEventTypeCount(monitorEventTypes),
          monitorEventTypes,
          nullptr,
          &gEventMonitorHandler);
      LogDebug(std::string("install-event-monitor-end status=") + std::to_string(monitorInstallStatus));
      if (monitorInstallStatus != noErr) {
        std::cerr << "failed to install event monitor" << std::endl;
      }
    }
  }

  for (HotKeyRegistration& registration : gHotKeyRegistrations) {
    if (!registration.binding->valid) {
      LogDebug(std::string("skip-hotkey-registration option=") + registration.optionName);
      continue;
    }

    LogDebug(
        std::string("register-hotkey-begin option=") + registration.optionName +
        " keyCode=" + std::to_string(registration.binding->keyCode) +
        " modifiers=" + std::to_string(registration.binding->modifiers));
    const EventHotKeyID hotKeyId = {kHotKeySignature, registration.actionId};
    const OSStatus registerStatus = RegisterEventHotKey(
        registration.binding->keyCode,
        registration.binding->modifiers,
        hotKeyId,
        hotKeyEventTarget,
        0,
        &registration.hotKeyRef);
    LogDebug(std::string("register-hotkey-end option=") + registration.optionName + " status=" + std::to_string(registerStatus));
    if (registerStatus != noErr) {
      std::cerr << "failed to register hotkey for " << registration.optionName << std::endl;
    }
  }

  for (DynamicTransportModeRegistration& registration : gDynamicTransportModeRegistrations) {
    if (!registration.binding.valid) {
      LogDebug(std::string("skip-dynamic-transport-hotkey-registration message=") + registration.message);
      continue;
    }
    if (registration.monitorOnly) {
      LogDebug(std::string("skip-dynamic-transport-hotkey-registration-monitor-only message=") + registration.message);
      continue;
    }

    LogDebug(
        std::string("register-dynamic-transport-hotkey-begin message=") + registration.message +
        " keyCode=" + std::to_string(registration.binding.keyCode) +
        " modifiers=" + std::to_string(registration.binding.modifiers));
    const EventHotKeyID hotKeyId = {kHotKeySignature, registration.actionId};
    const OSStatus registerStatus = RegisterEventHotKey(
        registration.binding.keyCode,
        registration.binding.modifiers,
        hotKeyId,
        hotKeyEventTarget,
        0,
        &registration.hotKeyRef);
    LogDebug(std::string("register-dynamic-transport-hotkey-end message=") + registration.message + " status=" + std::to_string(registerStatus));
    if (registerStatus != noErr) {
      std::cerr << "failed to register hotkey for " << registration.message << std::endl;
    }
  }

  LogDebug("start-listening-ready");
  std::cout << "ready" << std::endl;
  std::cout.flush();
  return true;
}

void RunHotKeyEventPump() {
  while (!gShouldExit) {
    EventRef event = nullptr;
    const OSStatus receiveStatus = ReceiveNextEvent(
        0,
        nullptr,
        0.25,
        true,
        &event);
    if (receiveStatus == eventLoopTimedOutErr) {
      continue;
    }
    if (receiveStatus == eventLoopQuitErr) {
      break;
    }
    if (receiveStatus != noErr) {
      LogDebug(std::string("receive-next-event status=") + std::to_string(receiveStatus));
      continue;
    }
    if (event == nullptr) {
      continue;
    }

    const EventTargetRef eventTarget = gHotKeyEventTarget != nullptr
      ? gHotKeyEventTarget
      : GetEventDispatcherTarget();
    const OSStatus dispatchStatus = SendEventToEventTarget(event, eventTarget);
    if (dispatchStatus != noErr && dispatchStatus != eventNotHandledErr) {
      LogDebug(std::string("send-event-to-target status=") + std::to_string(dispatchStatus));
    }
    ReleaseEvent(event);
  }
}

int RunPermissionCommand() {
  std::cout << "granted" << std::endl;
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc > 1) {
    const std::string_view command = argv[1];
    if (command == "--check-permission" || command == "--request-permission") {
      return RunPermissionCommand();
    }
  }

  if (!ParseShortcutArgs(argc, argv)) {
    return EXIT_FAILURE;
  }

  std::signal(SIGINT, HandleTerminationSignal);
  std::signal(SIGTERM, HandleTerminationSignal);

  PrepareProcessForGlobalHotKeys();
  LogDebug("main-before-start-listening");
  if (!StartListening()) {
    CleanupHotKeys();
    return EXIT_FAILURE;
  }

  LogDebug("main-before-run-hotkey-event-pump");
  RunHotKeyEventPump();
  LogDebug("main-after-run-hotkey-event-pump");
  CleanupHotKeys();
  return EXIT_SUCCESS;
}
