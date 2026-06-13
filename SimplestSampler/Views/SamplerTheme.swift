import AppKit
import SwiftUI

enum SamplerAppTheme: String, Codable, CaseIterable, Identifiable {
    case `default`

    var id: String { rawValue }
    var label: String { "Default" }
}

enum SamplerThemeMode: String, Codable, CaseIterable, Identifiable {
    case system
    case light
    case dark

    var id: String { rawValue }

    var label: String {
        switch self {
        case .system: return "System"
        case .light: return "Light"
        case .dark: return "Dark"
        }
    }

    func resolvedColorScheme(system: ColorScheme) -> ColorScheme {
        switch self {
        case .system: return system
        case .light: return .light
        case .dark: return .dark
        }
    }

    var preferredColorScheme: ColorScheme? {
        switch self {
        case .system: return nil
        case .light: return .light
        case .dark: return .dark
        }
    }

    func applyAppAppearance() {
        switch self {
        case .system:
            NSApp.appearance = nil
        case .light:
            NSApp.appearance = NSAppearance(named: .aqua)
        case .dark:
            NSApp.appearance = NSAppearance(named: .darkAqua)
        }
    }
}

private extension Color {
    static func sampler(_ hex: UInt32, opacity: Double = 1) -> Color {
        let red = Double((hex >> 16) & 0xFF) / 255
        let green = Double((hex >> 8) & 0xFF) / 255
        let blue = Double(hex & 0xFF) / 255
        return Color(red: red, green: green, blue: blue).opacity(opacity)
    }
}

// MARK: - Design tokens

struct SamplerThemeColors: Equatable {
    var bgTop: Color
    var bgBottom: Color
    var panel: Color
    var panelSoft: Color
    var text: Color
    var muted: Color
    var border: Color
    var borderStrong: Color
    var controlTop: Color
    var controlBottom: Color
    var accentTop: Color
    var accentStrong: Color
    var accentSoft: Color
    var captureTop: Color
    var captureBottom: Color
    var captureBorder: Color
    var selectedRow: Color
    var selectedRowText: Color
    var focusRing: Color
    var shadow: Color
    var sliderTrack: Color
    var sliderThumb: Color

    // Necessary extras
    var textStrong: Color
    var captureText: Color
    var controlBorder: Color
    var controlHoverTop: Color
    var controlHoverBottom: Color
    var controlActiveTop: Color
    var controlActiveBottom: Color
    var slotBackground: Color
    var slotPlaying: Color
    var selectedRowBorder: Color
    var selectedChromeFill: Color
    var selectedChromeBorder: Color
    var selectedMutedText: Color
    var rowPlaybackOverlay: Color
    var shortcutTriggerFill: Color
    var shortcutCapturingFill: Color
    var tabBarBackground: Color
    var tabBarBorder: Color
    var renameFieldFill: Color
    var renameFieldText: Color
    var renameFieldBorder: Color
    var windowGlowTop: Color
    var windowGlowBottom: Color
    var topHighlight: Color
    var error: Color
    var disabledOpacity: Double

    var bgGradient: LinearGradient {
        LinearGradient(colors: [bgTop, bgBottom], startPoint: .top, endPoint: .bottom)
    }

    var controlGradient: LinearGradient {
        LinearGradient(colors: [controlTop, controlBottom], startPoint: .top, endPoint: .bottom)
    }

    var controlHoverGradient: LinearGradient {
        LinearGradient(colors: [controlHoverTop, controlHoverBottom], startPoint: .top, endPoint: .bottom)
    }

    var controlActiveGradient: LinearGradient {
        LinearGradient(colors: [controlActiveTop, controlActiveBottom], startPoint: .top, endPoint: .bottom)
    }

    var accentGradient: LinearGradient {
        LinearGradient(colors: [accentTop, accentStrong], startPoint: .top, endPoint: .bottom)
    }

    var captureGradient: LinearGradient {
        LinearGradient(colors: [captureTop, captureBottom], startPoint: .top, endPoint: .bottom)
    }

    var accent: Color { accentStrong }
    var buttonFill: Color { controlTop }
    var buttonFillPressed: Color { controlActiveBottom }
    var strongText: Color { textStrong }
    var panelShadow: Color { shadow }
    var chipBackground: Color { panelSoft }
    var slotSelected: Color { selectedRow }
    var slotSelectedText: Color { selectedRowText }
    var slotSelectedBorder: Color { selectedRowBorder }
}

enum SamplerTheme {
    enum Layout {
        static let windowPadding: CGFloat = 10
        static let sectionSpacing: CGFloat = 10
        static let rowSpacing: CGFloat = 8
        static let chipPaddingH: CGFloat = 10
        static let chipPaddingV: CGFloat = 6
        static let chipMinHeight: CGFloat = 28
        static let rowPaddingH: CGFloat = 10
        static let rowPaddingV: CGFloat = 8
        static let rowColumnGap: CGFloat = 10
        static let actionGap: CGFloat = 5
        static let labelWidth: CGFloat = 52
        static let playButtonSize: CGFloat = 22
        static let toolbarButtonHeight: CGFloat = 34
        static let toolbarGap: CGFloat = 8
        static let chipCornerRadius: CGFloat = 12
        static let volumeCornerRadius: CGFloat = 16
        static let rowCornerRadius: CGFloat = 14
        static let panelCornerRadius: CGFloat = 18
        static let iconButtonSize: CGFloat = 34
        static let tabBarPadding: CGFloat = 4
        static let tabPaddingH: CGFloat = 12
        static let tabMinHeight: CGFloat = 28
        static let metaLineSpacing: CGFloat = 5
        static let metaNameDurationGap: CGFloat = 6
        static let volumeValueWidth: CGFloat = 52
        static let volumeSliderGap: CGFloat = 8
        static let volumeMeterHeight: CGFloat = 4
        static let volumeMeterSpacing: CGFloat = 6
        static let actionMinHeight: CGFloat = 23
        static let actionPaddingH: CGFloat = 7
        static let shortcutPaddingH: CGFloat = 8
        static let shortcutTriggerMinWidth: CGFloat = 46
        static let shortcutTriggerPaddingH: CGFloat = 10
        static let statusBarTopPadding: CGFloat = 6
        static let renameFieldCornerRadius: CGFloat = 8
        static let renameFieldPaddingH: CGFloat = 7
        static let renameFieldPaddingV: CGFloat = 4
    }

    static let defaultLight = SamplerThemeColors(
        bgTop: .sampler(0xF4FBFF),
        bgBottom: .sampler(0xE3EEF3),
        panel: .sampler(0xFFFFFF, opacity: 0.84),
        panelSoft: .sampler(0xFFFFFF, opacity: 0.72),
        text: .sampler(0x193646),
        muted: .sampler(0x5D7684),
        border: .sampler(0x274C5E, opacity: 0.14),
        borderStrong: .sampler(0x1F4C62, opacity: 0.22),
        controlTop: .sampler(0xECECEC),
        controlBottom: .sampler(0xDADADA),
        accentTop: .sampler(0x91AD9F),
        accentStrong: .sampler(0x6F8F80),
        accentSoft: .sampler(0xC7D8CF),
        captureTop: .sampler(0xF0B9BC),
        captureBottom: .sampler(0xD8898F),
        captureBorder: .sampler(0xB8686E, opacity: 0.32),
        selectedRow: .sampler(0x6F817A, opacity: 0.92),
        selectedRowText: .sampler(0xF6FFFB),
        focusRing: .sampler(0x6F8F80, opacity: 0.18),
        shadow: .sampler(0x173E54, opacity: 0.12),
        sliderTrack: .sampler(0xDADADA, opacity: 0.92),
        sliderThumb: .sampler(0x6F8F80),
        textStrong: .sampler(0x0F2734),
        captureText: .sampler(0x5F2930),
        controlBorder: .sampler(0x585858, opacity: 0.14),
        controlHoverTop: .sampler(0xFFFFFF, opacity: 0.98),
        controlHoverBottom: .sampler(0xEAF0F4, opacity: 0.98),
        controlActiveTop: .sampler(0xD7E0E6),
        controlActiveBottom: .sampler(0xC6D3DB),
        slotBackground: .sampler(0xF8FCFD, opacity: 0.92),
        slotPlaying: .sampler(0xE7F7F2, opacity: 0.98),
        selectedRowBorder: .sampler(0x4C6C5F, opacity: 0.22),
        selectedChromeFill: .sampler(0xFFFFFF, opacity: 0.14),
        selectedChromeBorder: .sampler(0xFFFFFF, opacity: 0.18),
        selectedMutedText: .sampler(0xF6FFFB, opacity: 0.74),
        rowPlaybackOverlay: .sampler(0xFFFFFF, opacity: 0.16),
        shortcutTriggerFill: .sampler(0x193646, opacity: 0.08),
        shortcutCapturingFill: .sampler(0x6F8F80, opacity: 0.14),
        tabBarBackground: .sampler(0xFFFFFF, opacity: 0.62),
        tabBarBorder: .sampler(0x193646, opacity: 0.08),
        renameFieldFill: .sampler(0xFFFFFF),
        renameFieldText: .sampler(0x193646),
        renameFieldBorder: .sampler(0x6F8F80, opacity: 0.58),
        windowGlowTop: .sampler(0x6F8F80, opacity: 0.16),
        windowGlowBottom: .sampler(0x5B9FCE, opacity: 0.16),
        topHighlight: .sampler(0xFFFFFF, opacity: 0.42),
        error: .sampler(0xB33333),
        disabledOpacity: 0.58
    )

    static let defaultDark = SamplerThemeColors(
        bgTop: .sampler(0x3B3835),
        bgBottom: .sampler(0x282624),
        panel: .sampler(0x2E2B29, opacity: 0.92),
        panelSoft: .sampler(0x423E3B, opacity: 0.76),
        text: .sampler(0xDDD7D1),
        muted: .sampler(0xB3ADA7),
        border: .sampler(0xFFFFFF, opacity: 0.11),
        borderStrong: .sampler(0xFFFFFF, opacity: 0.20),
        controlTop: .sampler(0x5C5855),
        controlBottom: .sampler(0x474340),
        accentTop: .sampler(0x2C7A5F),
        accentStrong: .sampler(0x1D6F54),
        accentSoft: .sampler(0x8CAB9B),
        captureTop: .sampler(0x8A4545),
        captureBottom: .sampler(0x6B3434),
        captureBorder: .sampler(0xE29696, opacity: 0.28),
        selectedRow: .sampler(0x346E56, opacity: 0.96),
        selectedRowText: .sampler(0xF6FFFB),
        focusRing: .sampler(0x8CAB9B, opacity: 0.22),
        shadow: Color.black.opacity(0.30),
        sliderTrack: .sampler(0x363330, opacity: 0.90),
        sliderThumb: .sampler(0x8CAB9B),
        textStrong: .sampler(0xE6E0DA),
        captureText: .sampler(0xFFD8D4),
        controlBorder: .sampler(0xFFFFFF, opacity: 0.12),
        controlHoverTop: .sampler(0x696460),
        controlHoverBottom: .sampler(0x514C48),
        controlActiveTop: .sampler(0x4D4945),
        controlActiveBottom: .sampler(0x3C3835),
        slotBackground: .sampler(0x403C39, opacity: 0.90),
        slotPlaying: .sampler(0x534F4B, opacity: 0.96),
        selectedRowBorder: .sampler(0x569276, opacity: 0.30),
        selectedChromeFill: .sampler(0xFFFFFF, opacity: 0.14),
        selectedChromeBorder: .sampler(0xFFFFFF, opacity: 0.18),
        selectedMutedText: .sampler(0xF6FFFB, opacity: 0.74),
        rowPlaybackOverlay: .sampler(0xFFFFFF, opacity: 0.16),
        shortcutTriggerFill: .sampler(0x363330, opacity: 0.90),
        shortcutCapturingFill: .sampler(0x534F4B, opacity: 0.98),
        tabBarBackground: .sampler(0x3A3633, opacity: 0.92),
        tabBarBorder: .sampler(0xFFFFFF, opacity: 0.12),
        renameFieldFill: .sampler(0x302D2B, opacity: 0.96),
        renameFieldText: .sampler(0xDDD7D1),
        renameFieldBorder: .sampler(0x8CAB9B, opacity: 0.64),
        windowGlowTop: .sampler(0x8CAB9B, opacity: 0.12),
        windowGlowBottom: .sampler(0x8A4545, opacity: 0.08),
        topHighlight: .sampler(0xFFFFFF, opacity: 0.08),
        error: .sampler(0xE49C9C),
        disabledOpacity: 0.58
    )

    static func tokens(appTheme: SamplerAppTheme, colorScheme: ColorScheme) -> SamplerThemeColors {
        switch (appTheme, colorScheme) {
        case (.default, .dark):
            return defaultDark
        default:
            return defaultLight
        }
    }
}

// MARK: - Shared surfaces

struct SamplerWindowBackground: View {
    let theme: SamplerThemeColors

    var body: some View {
        ZStack {
            theme.bgGradient

            RadialGradient(
                colors: [theme.windowGlowTop, .clear],
                center: .topLeading,
                startRadius: 0,
                endRadius: 280
            )

            RadialGradient(
                colors: [theme.windowGlowBottom, .clear],
                center: .bottomTrailing,
                startRadius: 0,
                endRadius: 260
            )
        }
        .ignoresSafeArea()
    }
}

struct SamplerFrostedPanelBackground: View {
    let theme: SamplerThemeColors
    var cornerRadius: CGFloat = SamplerTheme.Layout.panelCornerRadius

    var body: some View {
        RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
            .fill(theme.panel)
            .overlay(
                RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
                    .stroke(theme.border, lineWidth: 1)
            )
            .shadow(color: theme.panelShadow, radius: 18, y: 8)
    }
}

struct SamplerControlSurface<Background: View>: View {
    let theme: SamplerThemeColors
    let cornerRadius: CGFloat
    let borderColor: Color
    @ViewBuilder var background: () -> Background

    var body: some View {
        background()
            .overlay(alignment: .top) {
                RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
                    .stroke(theme.topHighlight, lineWidth: 1)
                    .padding(1)
                    .mask(
                        LinearGradient(
                            colors: [theme.topHighlight, .clear],
                            startPoint: .top,
                            endPoint: .center
                        )
                    )
            }
            .clipShape(RoundedRectangle(cornerRadius: cornerRadius, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
                    .stroke(borderColor, lineWidth: 1)
            )
    }
}

enum SamplerControlFill {
    case control(isPressed: Bool)
    case accent
    case capture(isPressed: Bool)
    case custom(LinearGradient)

    func gradient(theme: SamplerThemeColors) -> LinearGradient {
        switch self {
        case .control(let isPressed):
            return isPressed ? theme.controlActiveGradient : theme.controlGradient
        case .accent:
            return theme.accentGradient
        case .capture(let isPressed):
            if isPressed {
                return LinearGradient(
                    colors: [theme.captureTop.opacity(0.82), theme.captureBottom.opacity(0.82)],
                    startPoint: .top,
                    endPoint: .bottom
                )
            }
            return theme.captureGradient
        case .custom(let gradient):
            return gradient
        }
    }
}

// MARK: - Environment

private struct SamplerThemeColorsKey: EnvironmentKey {
    static let defaultValue = SamplerTheme.tokens(appTheme: .default, colorScheme: .light)
}

extension EnvironmentValues {
    var samplerThemeColors: SamplerThemeColors {
        get { self[SamplerThemeColorsKey.self] }
        set { self[SamplerThemeColorsKey.self] = newValue }
    }
}

struct SamplerThemedRoot<Content: View>: View {
    @ObservedObject var viewModel: SamplerViewModel
    @ObservedObject private var preferencesStore: PreferencesStore
    @Environment(\.colorScheme) private var systemColorScheme
    @ViewBuilder var content: () -> Content

    init(viewModel: SamplerViewModel, @ViewBuilder content: @escaping () -> Content) {
        self.viewModel = viewModel
        _preferencesStore = ObservedObject(wrappedValue: viewModel.preferencesStore)
        self.content = content
    }

    private var themeMode: SamplerThemeMode {
        preferencesStore.preferences.themeMode
    }

    private var appTheme: SamplerAppTheme {
        preferencesStore.preferences.appTheme
    }

    private var effectiveColorScheme: ColorScheme {
        themeMode.resolvedColorScheme(system: systemColorScheme)
    }

    var body: some View {
        content()
            .environment(
                \.samplerThemeColors,
                SamplerTheme.tokens(appTheme: appTheme, colorScheme: effectiveColorScheme)
            )
            .preferredColorScheme(themeMode.preferredColorScheme)
            .onAppear {
                viewModel.applyThemeAppearance()
            }
            .onChange(of: themeMode) { _ in
                viewModel.applyThemeAppearance()
            }
    }
}

struct SamplerPanelStyle: ViewModifier {
    @Environment(\.samplerThemeColors) private var theme

    func body(content: Content) -> some View {
        content
            .padding(10)
            .background(
                SamplerFrostedPanelBackground(theme: theme, cornerRadius: SamplerTheme.Layout.panelCornerRadius)
            )
    }
}

struct SamplerChipSurfaceStyle: ViewModifier {
    @Environment(\.samplerThemeColors) private var theme
    var cornerRadius: CGFloat

    func body(content: Content) -> some View {
        content
            .background(
                SamplerControlSurface(theme: theme, cornerRadius: cornerRadius, borderColor: theme.border) {
                    RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
                        .fill(theme.panelSoft)
                }
            )
            .shadow(color: theme.panelShadow.opacity(0.45), radius: 6, y: 2)
    }
}

struct SamplerSectionLabel: View {
    let title: String
    @Environment(\.samplerThemeColors) private var theme

    var body: some View {
        Text(title.uppercased())
            .font(.system(size: 10, weight: .bold))
            .kerning(0.6)
            .foregroundStyle(theme.muted)
    }
}

extension View {
    func samplerPanel() -> some View {
        modifier(SamplerPanelStyle())
    }

    func samplerChipSurface(cornerRadius: CGFloat = SamplerTheme.Layout.chipCornerRadius) -> some View {
        modifier(SamplerChipSurfaceStyle(cornerRadius: cornerRadius))
    }

    func samplerWindowChrome() -> some View {
        modifier(SamplerWindowChromeStyle())
    }

    func samplerDisabledOpacity(_ isDisabled: Bool, theme: SamplerThemeColors) -> some View {
        opacity(isDisabled ? theme.disabledOpacity : 1)
    }
}

private struct SamplerWindowChromeStyle: ViewModifier {
    @Environment(\.samplerThemeColors) private var theme

    func body(content: Content) -> some View {
        content
            .padding(SamplerTheme.Layout.windowPadding)
            .background {
                SamplerFrostedPanelBackground(theme: theme)
            }
            .background {
                SamplerWindowBackground(theme: theme)
            }
    }
}
