import AppKit
import SwiftUI

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

struct SamplerThemeColors: Equatable {
    var backgroundTop: Color
    var backgroundBottom: Color
    var panelBackground: Color
    var text: Color
    var muted: Color
    var accent: Color
    var accentSoft: Color
    var captureTop: Color
    var captureBottom: Color
    var captureText: Color
    var slotBackground: Color
    var slotPlaying: Color
    var slotSelected: Color
    var slotSelectedText: Color
    var border: Color
    var error: Color
    var chipBackground: Color
    var tabBarBackground: Color
    var buttonFill: Color
    var buttonFillPressed: Color
    var playButtonFill: Color
    var playButtonFillPressed: Color
    var shortcutButtonFill: Color
    var shortcutTriggerFill: Color
    var shortcutCapturingFill: Color
    var actionButtonFill: Color
    var renameFieldFill: Color
    var renameFieldText: Color
    var renameFieldBorder: Color

    var backgroundGradient: LinearGradient {
        LinearGradient(
            colors: [backgroundTop, backgroundBottom],
            startPoint: .top,
            endPoint: .bottom
        )
    }
}

enum SamplerTheme {
    enum Layout {
        static let windowPadding: CGFloat = 8
        static let sectionSpacing: CGFloat = 8
        static let rowSpacing: CGFloat = 6
        static let chipPaddingH: CGFloat = 9
        static let chipPaddingV: CGFloat = 5
        static let chipMinHeight: CGFloat = 26
        static let rowPaddingH: CGFloat = 9
        static let rowPaddingV: CGFloat = 6
        static let rowColumnGap: CGFloat = 9
        static let actionGap: CGFloat = 4
        static let labelWidth: CGFloat = 52
        static let playButtonSize: CGFloat = 22
        static let toolbarButtonHeight: CGFloat = 34
        static let toolbarGap: CGFloat = 6
        static let chipCornerRadius: CGFloat = 8
        static let volumeCornerRadius: CGFloat = 12
        static let rowCornerRadius: CGFloat = 11
        static let iconButtonSize: CGFloat = 34
        static let tabBarPadding: CGFloat = 4
        static let tabPaddingH: CGFloat = 12
        static let tabMinHeight: CGFloat = 28
        static let metaLineSpacing: CGFloat = 4
        static let metaNameDurationGap: CGFloat = 5
        static let volumeValueWidth: CGFloat = 52
        static let volumeSliderGap: CGFloat = 7
        static let volumeMeterHeight: CGFloat = 4
        static let volumeMeterSpacing: CGFloat = 5
        static let actionMinHeight: CGFloat = 23
        static let actionPaddingH: CGFloat = 7
        static let shortcutPaddingH: CGFloat = 8
        static let shortcutTriggerMinWidth: CGFloat = 46
        static let shortcutTriggerPaddingH: CGFloat = 10
        static let statusBarTopPadding: CGFloat = 4
        static let renameFieldCornerRadius: CGFloat = 6
        static let renameFieldPaddingH: CGFloat = 7
        static let renameFieldPaddingV: CGFloat = 4
    }

    static let light = SamplerThemeColors(
        backgroundTop: Color(red: 0.956, green: 0.984, blue: 1.0),
        backgroundBottom: Color(red: 0.890, green: 0.933, blue: 0.953),
        panelBackground: Color.white.opacity(0.84),
        text: Color(red: 0.098, green: 0.212, blue: 0.275),
        muted: Color(red: 0.365, green: 0.463, blue: 0.518),
        accent: Color(red: 0.435, green: 0.561, blue: 0.502),
        accentSoft: Color(red: 0.780, green: 0.847, blue: 0.812),
        captureTop: Color(red: 0.941, green: 0.725, blue: 0.737),
        captureBottom: Color(red: 0.847, green: 0.537, blue: 0.561),
        captureText: Color(red: 0.373, green: 0.161, blue: 0.188),
        slotBackground: Color(red: 0.973, green: 0.988, blue: 0.992).opacity(0.92),
        slotPlaying: Color(red: 0.780, green: 0.847, blue: 0.812).opacity(0.55),
        slotSelected: Color(red: 0.435, green: 0.506, blue: 0.478).opacity(0.92),
        slotSelectedText: Color(red: 0.965, green: 1.0, blue: 0.984),
        border: Color(red: 0.153, green: 0.298, blue: 0.369).opacity(0.14),
        error: Color(red: 0.7, green: 0.2, blue: 0.2),
        chipBackground: Color.white.opacity(0.55),
        tabBarBackground: Color.white.opacity(0.45),
        buttonFill: Color.white.opacity(0.92),
        buttonFillPressed: Color.white.opacity(0.7),
        playButtonFill: Color.white.opacity(0.88),
        playButtonFillPressed: Color.white.opacity(0.75),
        shortcutButtonFill: Color.white.opacity(0.86),
        shortcutTriggerFill: Color(red: 0.098, green: 0.212, blue: 0.275).opacity(0.08),
        shortcutCapturingFill: Color(red: 0.941, green: 0.725, blue: 0.737).opacity(0.35),
        actionButtonFill: Color.white.opacity(0.86),
        renameFieldFill: Color.white,
        renameFieldText: Color(red: 0.098, green: 0.212, blue: 0.275),
        renameFieldBorder: Color(red: 0.318, green: 0.494, blue: 0.420)
    )

    // OverCue sampler dark vars from darkTheme.css
    static let dark = SamplerThemeColors(
        backgroundTop: Color(red: 0.231, green: 0.220, blue: 0.208),
        backgroundBottom: Color(red: 0.157, green: 0.149, blue: 0.141),
        panelBackground: Color(red: 0.180, green: 0.169, blue: 0.161).opacity(0.92),
        text: Color(red: 0.867, green: 0.843, blue: 0.820),
        muted: Color(red: 0.702, green: 0.678, blue: 0.655),
        accent: Color(red: 0.173, green: 0.478, blue: 0.373),
        accentSoft: Color(red: 0.549, green: 0.671, blue: 0.608),
        captureTop: Color(red: 0.541, green: 0.271, blue: 0.271),
        captureBottom: Color(red: 0.420, green: 0.204, blue: 0.204),
        captureText: Color(red: 1.0, green: 0.847, blue: 0.831),
        slotBackground: Color(red: 0.251, green: 0.235, blue: 0.224).opacity(0.9),
        slotPlaying: Color(red: 0.302, green: 0.282, blue: 0.267).opacity(0.96),
        slotSelected: Color(red: 0.204, green: 0.431, blue: 0.337).opacity(0.96),
        slotSelectedText: Color(red: 0.965, green: 1.0, blue: 0.984),
        border: Color.white.opacity(0.11),
        error: Color(red: 0.894, green: 0.612, blue: 0.612),
        chipBackground: Color(red: 0.251, green: 0.235, blue: 0.224).opacity(0.9),
        tabBarBackground: Color(red: 0.208, green: 0.196, blue: 0.188).opacity(0.92),
        buttonFill: Color(red: 0.361, green: 0.345, blue: 0.333),
        buttonFillPressed: Color(red: 0.278, green: 0.263, blue: 0.251),
        playButtonFill: Color(red: 0.361, green: 0.345, blue: 0.333),
        playButtonFillPressed: Color(red: 0.278, green: 0.263, blue: 0.251),
        shortcutButtonFill: Color(red: 0.329, green: 0.314, blue: 0.302).opacity(0.95),
        shortcutTriggerFill: Color.white.opacity(0.08),
        shortcutCapturingFill: Color(red: 0.541, green: 0.271, blue: 0.271).opacity(0.35),
        actionButtonFill: Color(red: 0.329, green: 0.314, blue: 0.302).opacity(0.95),
        renameFieldFill: Color.white.opacity(0.97),
        renameFieldText: Color(red: 0.098, green: 0.212, blue: 0.275),
        renameFieldBorder: Color(red: 0.549, green: 0.847, blue: 0.706)
    )

    static func palette(for colorScheme: ColorScheme) -> SamplerThemeColors {
        colorScheme == .dark ? dark : light
    }
}

private struct SamplerThemeColorsKey: EnvironmentKey {
    static let defaultValue = SamplerTheme.light
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

    private var effectiveColorScheme: ColorScheme {
        themeMode.resolvedColorScheme(system: systemColorScheme)
    }

    var body: some View {
        content()
            .environment(\.samplerThemeColors, SamplerTheme.palette(for: effectiveColorScheme))
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
            .background(theme.panelBackground)
            .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .stroke(theme.border, lineWidth: 1)
            )
    }
}

extension View {
    func samplerPanel() -> some View {
        modifier(SamplerPanelStyle())
    }

    func samplerChipSurface(cornerRadius: CGFloat = SamplerTheme.Layout.chipCornerRadius) -> some View {
        modifier(SamplerChipSurfaceStyle(cornerRadius: cornerRadius))
    }
}

private struct SamplerChipSurfaceStyle: ViewModifier {
    @Environment(\.samplerThemeColors) private var theme
    var cornerRadius: CGFloat

    func body(content: Content) -> some View {
        content
            .background(theme.chipBackground)
            .clipShape(RoundedRectangle(cornerRadius: cornerRadius, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
                    .stroke(theme.border, lineWidth: 1)
            )
    }
}
