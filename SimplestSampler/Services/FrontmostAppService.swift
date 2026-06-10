import AppKit
import Foundation

enum FrontmostAppService {
    static func isSimplestSamplerFocused() -> Bool {
        guard let frontmost = NSWorkspace.shared.frontmostApplication else { return false }
        return frontmost.processIdentifier == ProcessInfo.processInfo.processIdentifier
    }

    static func isProToolsFrontmost() -> Bool {
        guard let frontmost = NSWorkspace.shared.frontmostApplication else { return false }
        return isProToolsApplicationName(frontmost.localizedName ?? "")
    }

    static func shouldAllowGlobalCaptureShortcut() -> Bool {
        if isSimplestSamplerFocused() {
            return true
        }
        return isProToolsFrontmost()
    }

    static func isProToolsApplicationName(_ value: String) -> Bool {
        let normalized = value
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .lowercased()
            .replacingOccurrences(of: "[^a-z0-9]+", with: "", options: .regularExpression)
        return normalized == "protools" || normalized.hasPrefix("protools")
    }
}
