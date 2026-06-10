import SwiftUI

struct ShortcutBindingButton: View {
    @ObservedObject var viewModel: SamplerViewModel
    @Environment(\.samplerThemeColors) private var theme
    let shortcutID: String
    var prefix: String? = nil
    var compact = false
    var isTrigger = false

    private var isCapturing: Bool {
        viewModel.capturingShortcutId == shortcutID
    }

    var body: some View {
        Button(action: toggleCapture) {
            if let prefix {
                HStack(spacing: 5) {
                    Text(prefix)
                        .font(.system(size: compact ? 9 : 10, weight: .bold))
                        .foregroundStyle(theme.muted)
                    Text(displayLabel)
                        .font(.system(size: compact ? 9 : 11, weight: .medium))
                }
            } else {
                Text(displayLabel)
                    .font(.system(size: compact ? 9 : 11, weight: .medium))
            }
        }
        .buttonStyle(SlotShortcutButtonStyle(isCapturing: isCapturing, isTrigger: isTrigger))
        .help(isCapturing ? "Press a key combination. Escape cancels." : "Click to change shortcut")
    }

    private var displayLabel: String {
        isCapturing ? "Press Shortcut" : viewModel.shortcutLabel(for: shortcutID)
    }

    private func toggleCapture() {
        if isCapturing {
            viewModel.cancelShortcutCapture()
        } else {
            viewModel.beginShortcutCapture(shortcutID: shortcutID)
        }
    }
}
