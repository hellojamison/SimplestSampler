import SwiftUI

enum SamplerTheme {
    static let backgroundTop = Color(red: 0.956, green: 0.984, blue: 1.0)
    static let backgroundBottom = Color(red: 0.890, green: 0.933, blue: 0.953)
    static let panelBackground = Color.white.opacity(0.84)
    static let text = Color(red: 0.098, green: 0.212, blue: 0.275)
    static let muted = Color(red: 0.365, green: 0.463, blue: 0.518)
    static let accent = Color(red: 0.435, green: 0.561, blue: 0.502)
    static let accentSoft = Color(red: 0.780, green: 0.847, blue: 0.812)
    static let captureTop = Color(red: 0.941, green: 0.725, blue: 0.737)
    static let captureBottom = Color(red: 0.847, green: 0.537, blue: 0.561)
    static let captureText = Color(red: 0.373, green: 0.161, blue: 0.188)
    static let slotBackground = Color(red: 0.973, green: 0.988, blue: 0.992).opacity(0.92)
    static let slotSelected = Color(red: 0.435, green: 0.506, blue: 0.478).opacity(0.92)
    static let slotSelectedText = Color(red: 0.965, green: 1.0, blue: 0.984)
    static let border = Color(red: 0.153, green: 0.298, blue: 0.369).opacity(0.14)
    static let error = Color(red: 0.7, green: 0.2, blue: 0.2)

    static var backgroundGradient: LinearGradient {
        LinearGradient(
            colors: [backgroundTop, backgroundBottom],
            startPoint: .top,
            endPoint: .bottom
        )
    }
}

struct SamplerPanelStyle: ViewModifier {
    func body(content: Content) -> some View {
        content
            .padding(10)
            .background(SamplerTheme.panelBackground)
            .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .stroke(SamplerTheme.border, lineWidth: 1)
            )
    }
}

extension View {
    func samplerPanel() -> some View {
        modifier(SamplerPanelStyle())
    }
}
